/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>
#include <cstring>
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
    };
    std::vector<BufferedRow> rows_;
    size_t idx_ = 0;

    void write_value(char *dest, const ColMeta &dst_col, const char *src, const ColMeta &src_col) {
        if (dst_col.type == TYPE_FLOAT && src_col.type == TYPE_INT) {
            float v = static_cast<float>(*reinterpret_cast<const int *>(src));
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
    UnionExecutor(std::vector<std::unique_ptr<AbstractExecutor>> children, std::vector<ColMeta> output_cols) {
        children_ = std::move(children);
        cols_ = std::move(output_cols);
        len_ = cols_.empty() ? 0 : cols_.back().offset + cols_.back().len;
    }

    void beginTuple() override {
        rows_.clear();
        idx_ = 0;
        std::set<std::string> seen;
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
                for (bool is_null : out_nulls) key.push_back(is_null ? '\1' : '\0');
                if (seen.insert(key).second) {
                    rows_.push_back({std::move(out), std::move(out_nulls)});
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
