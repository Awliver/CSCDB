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
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<std::pair<ColMeta, bool>> sort_cols_;
    std::vector<std::unique_ptr<RmRecord>> buffer_;
    size_t idx_ = 0;

    int compare_single(const char *a, const char *b, const ColMeta &col) {
        if (col.type == TYPE_INT) {
            int ia = *reinterpret_cast<const int *>(a);
            int ib = *reinterpret_cast<const int *>(b);
            return (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
        } else if (col.type == TYPE_FLOAT) {
            float fa = *reinterpret_cast<const float *>(a);
            float fb = *reinterpret_cast<const float *>(b);
            return (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
        } else {
            return memcmp(a, b, col.len);
        }
    }

    int compare_records(const char *a, const char *b) {
        for (auto &[col, is_desc] : sort_cols_) {
            int cmp = compare_single(a + col.offset, b + col.offset, col);
            if (cmp != 0) return is_desc ? -cmp : cmp;
        }
        return 0;
    }

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, TabCol sel_cols, bool is_desc) {
        prev_ = std::move(prev);
        auto &input_cols = prev_->cols();
        auto it = std::find_if(input_cols.begin(), input_cols.end(),
            [&](const ColMeta &c) {
                return c.name == sel_cols.col_name &&
                       (sel_cols.tab_name.empty() || c.tab_name == sel_cols.tab_name);
            });
        if (it == input_cols.end()) throw ColumnNotFoundError(sel_cols.col_name);
        sort_cols_.emplace_back(*it, is_desc);
        idx_ = 0;
    }

    SortExecutor(std::unique_ptr<AbstractExecutor> prev,
                 const std::vector<std::pair<ColMeta, bool>> &sort_cols) {
        prev_ = std::move(prev);
        sort_cols_ = sort_cols;
        idx_ = 0;
    }

    void beginTuple() override {
        buffer_.clear();
        idx_ = 0;
        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            buffer_.push_back(prev_->Next());
        }
        std::stable_sort(buffer_.begin(), buffer_.end(),
            [this](const std::unique_ptr<RmRecord> &a, const std::unique_ptr<RmRecord> &b) {
                return compare_records(a->data, b->data) < 0;
            });
    }

    void nextTuple() override { idx_++; }

    bool is_end() const override { return idx_ >= buffer_.size(); }

    std::unique_ptr<RmRecord> Next() override {
        if (idx_ >= buffer_.size()) return nullptr;
        auto rec = std::make_unique<RmRecord>(*buffer_[idx_]);
        return rec;
    }

    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }
    size_t tupleLen() const override { return prev_->tupleLen(); }

    Rid &rid() override { return _abstract_rid; }
};
