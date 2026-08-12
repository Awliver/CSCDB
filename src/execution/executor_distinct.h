/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "executor_abstract.h"

// Removes duplicate rows from an already projected SELECT result.  The
// executor owns buffered copies because its child may reuse its current-row
// storage.  NULL participates in SQL DISTINCT equality: two NULLs in the same
// output position compare equal.
class DistinctExecutor : public AbstractExecutor {
   private:
    struct BufferedRow {
        std::unique_ptr<RmRecord> record;
        std::vector<bool> nulls;
    };

    std::unique_ptr<AbstractExecutor> child_;
    std::vector<ColMeta> cols_;
    size_t len_ = 0;
    std::vector<BufferedRow> rows_;
    size_t index_ = 0;

    std::string row_key(const RmRecord &record,
                        const std::vector<bool> &nulls) const {
        std::string key(record.data, len_);
        // SQL numeric equality treats -0 and +0 as equal.  Canonicalize NaN
        // payloads as well so DISTINCT is independent of their bit pattern.
        for (size_t i = 0; i < cols_.size(); ++i) {
            const auto &col = cols_[i];
            if (i < nulls.size() && nulls[i]) {
                // NULL equality must not depend on unspecified payload bytes.
                std::memset(key.data() + col.offset, 0, col.len);
                continue;
            }
            if (col.type != TYPE_FLOAT) continue;
            float value = load_unaligned<float>(record.data + col.offset);
            if (value == 0.0F) {
                value = 0.0F;
            } else if (std::isnan(value)) {
                value = std::numeric_limits<float>::quiet_NaN();
            }
            std::memcpy(key.data() + col.offset, &value, sizeof(value));
        }
        for (bool is_null : nulls) key.push_back(is_null ? '\1' : '\0');
        return key;
    }

   public:
    explicit DistinctExecutor(std::unique_ptr<AbstractExecutor> child)
        : child_(std::move(child)) {
        if (child_ == nullptr) throw InternalError("DistinctExecutor requires a child");
        cols_ = child_->cols();
        len_ = child_->tupleLen();
    }

    void beginTuple() override {
        rows_.clear();
        index_ = 0;
        std::set<std::string> seen;
        for (child_->beginTuple(); !child_->is_end(); child_->nextTuple()) {
            std::vector<bool> nulls(cols_.size(), false);
            if (const auto *mask = child_->null_mask(); mask != nullptr) {
                for (size_t i = 0; i < nulls.size() && i < mask->size(); ++i) {
                    nulls[i] = (*mask)[i];
                }
            }
            auto record = child_->Next();
            if (record == nullptr) {
                throw InternalError("DistinctExecutor child returned no current tuple");
            }
            if (seen.insert(row_key(*record, nulls)).second) {
                rows_.push_back({std::move(record), std::move(nulls)});
            }
        }
    }

    void nextTuple() override {
        if (index_ < rows_.size()) ++index_;
    }

    bool is_end() const override { return index_ >= rows_.size(); }

    std::unique_ptr<RmRecord> Next() override {
        if (is_end()) return nullptr;
        return std::make_unique<RmRecord>(*rows_[index_].record);
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return _abstract_rid; }

    const std::vector<bool> *null_mask() const override {
        if (is_end()) return nullptr;
        const auto &mask = rows_[index_].nulls;
        return std::any_of(mask.begin(), mask.end(), [](bool value) { return value; })
                   ? &mask
                   : nullptr;
    }
};
