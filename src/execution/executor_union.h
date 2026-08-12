/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"

class UnionExecutor : public AbstractExecutor {
   private:
    std::vector<std::unique_ptr<AbstractExecutor>> children_;
    std::vector<ColMeta> cols_;
    size_t len_ = 0;
    struct BufferedRow {
        std::unique_ptr<RmRecord> record;
        std::vector<bool> nulls;
        std::string key;
    };
    std::vector<BufferedRow> rows_;
    size_t idx_ = 0;
    bool all_ = false;
    ast::SetOpType op_ = ast::SetOpType::UNION;

    void write_value(char *dest, const ColMeta &dst_col, const char *src, const ColMeta &src_col) {
        if (dst_col.type == TYPE_FLOAT && src_col.type == TYPE_INT) {
            float v = static_cast<float>(load_unaligned<int>(src));
            memcpy(dest, &v, sizeof(float));
        } else if (dst_col.type == TYPE_INT && src_col.type == TYPE_INT) {
            memcpy(dest, src, sizeof(int));
        } else if (dst_col.type == TYPE_FLOAT && src_col.type == TYPE_FLOAT) {
            memcpy(dest, src, sizeof(float));
        } else if (dst_col.type == TYPE_STRING && src_col.type == TYPE_STRING) {
            memset(dest, 0, dst_col.len);
            memcpy(dest, src, std::min(dst_col.len, src_col.len));
        } else {
            throw InternalError("failure");
        }
    }

   public:
    UnionExecutor(std::vector<std::unique_ptr<AbstractExecutor>> children,
                  std::vector<ColMeta> output_cols,
                  ast::SetOpType op = ast::SetOpType::UNION, bool all = false) {
        children_ = std::move(children);
        cols_ = std::move(output_cols);
        all_ = all;
        op_ = op;
        if (children_.size() != 2 || cols_.empty()) {
            throw InternalError("Set operation requires two non-empty query inputs");
        }
        len_ = cols_.empty() ? 0 : cols_.back().offset + cols_.back().len;
    }

    // Compatibility with the original UNION-only constructor.
    UnionExecutor(std::vector<std::unique_ptr<AbstractExecutor>> children,
                  std::vector<ColMeta> output_cols, bool all)
        : UnionExecutor(std::move(children), std::move(output_cols),
                        ast::SetOpType::UNION, all) {}

    void beginTuple() override {
        rows_.clear();
        idx_ = 0;
        std::vector<std::vector<BufferedRow>> inputs(2);
        size_t child_index = 0;
        for (auto &child : children_) {
            const auto &child_cols = child->cols();
            if (child_cols.size() != cols_.size()) {
                throw InternalError("failure");
            }
            for (child->beginTuple(); !child->is_end(); child->nextTuple()) {
                auto src = child->Next();
                if (src == nullptr) {
                    throw InternalError("failure");
                }
                auto out = std::make_unique<RmRecord>(len_);
                memset(out->data, 0, len_);
                std::vector<bool> out_nulls(cols_.size(), false);
                const auto *child_nulls = child->null_mask();
                for (size_t i = 0; i < cols_.size(); i++) {
                    if (child_nulls != nullptr && i < child_nulls->size() && (*child_nulls)[i]) {
                        out_nulls[i] = true;
                        continue;
                    }
                    write_value(out->data + cols_[i].offset, cols_[i],
                                src->data + child_cols[i].offset, child_cols[i]);
                }
                std::string key(out->data, len_);
                // SQL set equality is numeric rather than bitwise.  Canonical
                // keys make +0/-0 equal and collapse NaN payload variants.
                for (const auto &col : cols_) {
                    if (col.type != TYPE_FLOAT) continue;
                    float value = load_unaligned<float>(out->data + col.offset);
                    if (value == 0.0F) value = 0.0F;
                    else if (std::isnan(value)) value = std::numeric_limits<float>::quiet_NaN();
                    memcpy(key.data() + col.offset, &value, sizeof(value));
                }
                for (size_t i = 0; i < cols_.size(); ++i) {
                    if (out_nulls[i]) {
                        memset(key.data() + cols_[i].offset, 0, cols_[i].len);
                    }
                }
                for (bool is_null : out_nulls) key.push_back(is_null ? '\1' : '\0');
                inputs[child_index].push_back(
                    {std::move(out), std::move(out_nulls), std::move(key)});
            }
            ++child_index;
        }

        auto append_all = [&](std::vector<BufferedRow> &input) {
            for (auto &row : input) rows_.push_back(std::move(row));
        };
        auto append_distinct = [&](std::vector<BufferedRow> &input,
                                   std::set<std::string> &seen) {
            for (auto &row : input) {
                if (seen.insert(row.key).second) rows_.push_back(std::move(row));
            }
        };

        if (op_ == ast::SetOpType::UNION) {
            if (all_) {
                append_all(inputs[0]);
                append_all(inputs[1]);
            } else {
                std::set<std::string> seen;
                append_distinct(inputs[0], seen);
                append_distinct(inputs[1], seen);
            }
            return;
        }

        std::map<std::string, size_t> right_counts;
        for (const auto &row : inputs[1]) ++right_counts[row.key];
        if (all_) {
            for (auto &row : inputs[0]) {
                auto found = right_counts.find(row.key);
                const bool include = op_ == ast::SetOpType::INTERSECT
                                         ? found != right_counts.end() && found->second > 0
                                         : found == right_counts.end() || found->second == 0;
                if (!include) {
                    // EXCEPT ALL consumes one matching row from the right too.
                    if (op_ == ast::SetOpType::EXCEPT && found->second > 0) --found->second;
                    continue;
                }
                if (op_ == ast::SetOpType::INTERSECT) --found->second;
                rows_.push_back(std::move(row));
            }
        } else {
            std::set<std::string> emitted;
            for (auto &row : inputs[0]) {
                const bool in_right = right_counts.find(row.key) != right_counts.end();
                const bool include = op_ == ast::SetOpType::INTERSECT ? in_right : !in_right;
                if (include && emitted.insert(row.key).second) {
                    rows_.push_back(std::move(row));
                }
            }
        }
    }

    void nextTuple() override { idx_++; }

    bool is_end() const override { return idx_ >= rows_.size(); }

    std::unique_ptr<RmRecord> Next() override {
        if (idx_ >= rows_.size()) return nullptr;
        return std::make_unique<RmRecord>(*rows_[idx_].record);
    }

    const std::vector<bool> *null_mask() const override {
        if (idx_ >= rows_.size()) return nullptr;
        const auto &mask = rows_[idx_].nulls;
        return std::any_of(mask.begin(), mask.end(), [](bool value) { return value; }) ? &mask : nullptr;
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return _abstract_rid; }
};

using SetOperationExecutor = UnionExecutor;
