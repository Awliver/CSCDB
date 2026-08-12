/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once
#include <cmath>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<std::pair<ColMeta, bool>> sort_cols_;
    std::vector<size_t> sort_col_indexes_;
    struct BufferedRow {
        std::unique_ptr<RmRecord> record;
        std::vector<bool> nulls;
    };
    std::vector<BufferedRow> buffer_;
    size_t idx_ = 0;

    int compare_single(const char *a, const char *b, const ColMeta &col) {
        if (col.type == TYPE_INT) {
            int ia = load_unaligned<int>(a);
            int ib = load_unaligned<int>(b);
            return (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
        } else if (col.type == TYPE_FLOAT) {
            float fa = load_unaligned<float>(a);
            float fb = load_unaligned<float>(b);
            const bool a_nan = std::isnan(fa);
            const bool b_nan = std::isnan(fb);
            if (a_nan || b_nan) {
                if (a_nan && b_nan) return 0;
                // Give NaN a stable position after every non-NaN value.  The
                // DESC inversion in compare_records naturally puts it first.
                return a_nan ? 1 : -1;
            }
            return (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
        } else {
            return memcmp(a, b, col.len);
        }
    }

    int compare_records(const BufferedRow &a, const BufferedRow &b) {
        for (size_t i = 0; i < sort_cols_.size(); ++i) {
            const auto &[col, is_desc] = sort_cols_[i];
            const size_t col_index = sort_col_indexes_[i];
            const bool a_null = col_index < a.nulls.size() && a.nulls[col_index];
            const bool b_null = col_index < b.nulls.size() && b.nulls[col_index];
            if (a_null || b_null) {
                if (a_null && b_null) continue;
                // ASC: NULLS LAST；DESC: NULLS FIRST。
                const int cmp = a_null ? 1 : -1;
                return is_desc ? -cmp : cmp;
            }
            int cmp = compare_single(a.record->data + col.offset,
                                     b.record->data + col.offset, col);
            if (cmp != 0) return is_desc ? -cmp : cmp;
        }
        return 0;
    }

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, TabCol sel_cols, bool is_desc) {
        prev_ = std::move(prev);
        auto &input_cols = prev_->cols();
        auto it = input_cols.end();
        if (sel_cols.output_index >= 0 &&
            static_cast<size_t>(sel_cols.output_index) < input_cols.size()) {
            it = input_cols.begin() + sel_cols.output_index;
        } else {
            it = std::find_if(input_cols.begin(), input_cols.end(),
                [&](const ColMeta &c) {
                    return c.name == sel_cols.col_name &&
                           (sel_cols.tab_name.empty() || c.tab_name == sel_cols.tab_name);
                });
        }
        if (it == input_cols.end()) throw ColumnNotFoundError(sel_cols.col_name);
        sort_cols_.emplace_back(*it, is_desc);
        sort_col_indexes_.push_back(static_cast<size_t>(it - input_cols.begin()));
        idx_ = 0;
    }

    SortExecutor(std::unique_ptr<AbstractExecutor> prev,
                 const std::vector<std::pair<ColMeta, bool>> &sort_cols) {
        prev_ = std::move(prev);
        sort_cols_ = sort_cols;
        const auto &input_cols = prev_->cols();
        for (const auto &[sort_col, _] : sort_cols_) {
            auto it = std::find_if(input_cols.begin(), input_cols.end(), [&](const ColMeta &col) {
                return col.name == sort_col.name && col.tab_name == sort_col.tab_name;
            });
            if (it == input_cols.end()) throw ColumnNotFoundError(sort_col.name);
            sort_col_indexes_.push_back(static_cast<size_t>(it - input_cols.begin()));
        }
        idx_ = 0;
    }

    SortExecutor(std::unique_ptr<AbstractExecutor> prev,
                 const std::vector<std::pair<TabCol, bool>> &sort_cols) {
        prev_ = std::move(prev);
        const auto &input_cols = prev_->cols();
        for (const auto &[target, is_desc] : sort_cols) {
            auto it = input_cols.end();
            if (target.output_index >= 0 &&
                static_cast<size_t>(target.output_index) < input_cols.size()) {
                it = input_cols.begin() + target.output_index;
            } else {
                it = std::find_if(input_cols.begin(), input_cols.end(),
                    [&](const ColMeta &col) {
                        return col.name == target.col_name &&
                               (target.tab_name.empty() || col.tab_name == target.tab_name);
                    });
            }
            if (it == input_cols.end()) throw ColumnNotFoundError(target.col_name);
            sort_cols_.emplace_back(*it, is_desc);
            sort_col_indexes_.push_back(static_cast<size_t>(it - input_cols.begin()));
        }
        idx_ = 0;
    }

    void beginTuple() override {
        buffer_.clear();
        idx_ = 0;
        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            std::vector<bool> nulls(prev_->cols().size(), false);
            if (const auto *mask = prev_->null_mask(); mask != nullptr) {
                for (size_t i = 0; i < nulls.size() && i < mask->size(); ++i) nulls[i] = (*mask)[i];
            }
            buffer_.push_back({prev_->Next(), std::move(nulls)});
        }
        std::stable_sort(buffer_.begin(), buffer_.end(),
            [this](const BufferedRow &a, const BufferedRow &b) {
                return compare_records(a, b) < 0;
            });
    }

    void nextTuple() override { idx_++; }

    bool is_end() const override { return idx_ >= buffer_.size(); }

    std::unique_ptr<RmRecord> Next() override {
        if (idx_ >= buffer_.size()) return nullptr;
        auto rec = std::make_unique<RmRecord>(*buffer_[idx_].record);
        return rec;
    }

    const std::vector<bool> *null_mask() const override {
        if (idx_ >= buffer_.size()) return nullptr;
        const auto &mask = buffer_[idx_].nulls;
        return std::any_of(mask.begin(), mask.end(), [](bool value) { return value; }) ? &mask : nullptr;
    }

    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }
    size_t tupleLen() const override { return prev_->tupleLen(); }

    Rid &rid() override { return _abstract_rid; }
};
