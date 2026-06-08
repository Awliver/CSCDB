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
    ColMeta cols_;
    size_t tuple_num;
    bool is_desc_;
    std::vector<size_t> used_tuple;
    std::unique_ptr<RmRecord> current_tuple;

    // 实现：一次性收集所有 tuple，排序，逐个返回
    std::vector<std::unique_ptr<RmRecord>> buffer_;
    size_t idx_ = 0;

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, TabCol sel_cols, bool is_desc) {
        prev_ = std::move(prev);
        // 修复：按列名在子算子输出列中解析排序列。原 get_col_offset 走基类默认实现返回空
        // ColMeta(offset=0)，导致所有 ORDER BY 都按首列排序（首列恰为排序列时才"碰巧"正确）。
        const auto &pcols = prev_->cols();
        auto it = std::find_if(pcols.begin(), pcols.end(), [&](const ColMeta &c) {
            return c.name == sel_cols.col_name &&
                   (sel_cols.tab_name.empty() || c.tab_name == sel_cols.tab_name);
        });
        cols_ = (it != pcols.end()) ? *it : prev_->get_col_offset(sel_cols);
        is_desc_ = is_desc;
        tuple_num = 0;
        used_tuple.clear();
    }

    void beginTuple() override {
        buffer_.clear();
        idx_ = 0;
        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            buffer_.push_back(prev_->Next());
        }
        // 按 cols_ 排序
        std::sort(buffer_.begin(), buffer_.end(),
            [this](const std::unique_ptr<RmRecord>& a, const std::unique_ptr<RmRecord>& b) {
                int cmp;
                const char* pa = a->data + cols_.offset;
                const char* pb = b->data + cols_.offset;
                if (cols_.type == TYPE_INT) {
                    int ia = *reinterpret_cast<const int*>(pa);
                    int ib = *reinterpret_cast<const int*>(pb);
                    cmp = (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
                } else if (cols_.type == TYPE_FLOAT) {
                    float fa = *reinterpret_cast<const float*>(pa);
                    float fb = *reinterpret_cast<const float*>(pb);
                    cmp = (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
                } else {
                    cmp = memcmp(pa, pb, cols_.len);
                }
                return is_desc_ ? cmp > 0 : cmp < 0;
            });
    }

    void nextTuple() override { idx_++; }

    bool is_end() const override { return idx_ >= buffer_.size(); }

    std::unique_ptr<RmRecord> Next() override {
        if (idx_ >= buffer_.size()) return nullptr;
        // 复制返回（多次 Next 可能被调）
        auto rec = std::make_unique<RmRecord>(*buffer_[idx_]);
        return rec;
    }

    const std::vector<ColMeta>& cols() const override { return prev_->cols(); }
    size_t tupleLen() const override { return prev_->tupleLen(); }

    Rid &rid() override { return _abstract_rid; }
};