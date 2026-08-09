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

#include <algorithm>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class ProjectionExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;        // 投影节点的儿子节点
    std::vector<ColMeta> cols_;                     // 需要投影的字段
    size_t len_;                                    // 字段总长度
    std::vector<size_t> sel_idxs_;

    // 优化：预编译投影步骤
    struct ProjStep {
        int src_offset;
        int dst_offset;
        int len;
    };
    std::vector<ProjStep> steps_;

    // 优化：缓存投影后的当前记录
    std::unique_ptr<RmRecord> cur_rec_;

   public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> prev, const std::vector<TabCol> &sel_cols) {
        prev_ = std::move(prev);

        size_t curr_offset = 0;
        auto &prev_cols = prev_->cols();
        for (auto &sel_col : sel_cols) {
            auto pos = get_col(prev_cols, sel_col);
            sel_idxs_.push_back(pos - prev_cols.begin());
            auto col = *pos;
            if (!sel_col.alias.empty()) col.name = sel_col.alias;   // 决赛：col AS alias
            col.offset = curr_offset;
            curr_offset += col.len;
            cols_.push_back(col);
        }
        len_ = curr_offset;
                steps_.reserve(sel_idxs_.size());
        for (size_t i = 0; i < sel_idxs_.size(); ++i) {
            const auto &src_col = prev_->cols()[sel_idxs_[i]];
            ProjStep s;
            s.src_offset = src_col.offset;
            s.dst_offset = cols_[i].offset;
            s.len = src_col.len;
            steps_.push_back(s);
        }
    }

    void build_cur() {
        if (prev_->is_end()) {
            cur_rec_.reset();
            return;
        }
        auto src_rec = prev_->Next();
        if (!src_rec) {
            cur_rec_.reset();
            return;
        }
        cur_rec_ = std::make_unique<RmRecord>(len_);
        for (const auto &s : steps_) {
            memcpy(cur_rec_->data + s.dst_offset, src_rec->data + s.src_offset, s.len);
        }
        // 转发 NULL 掩码（按投影列重排）
        cur_nulls_.clear();
        if (const auto *nm = prev_->null_mask()) {
            if (!nm->empty()) {
                cur_nulls_.resize(sel_idxs_.size(), false);
                for (size_t i = 0; i < sel_idxs_.size(); i++) {
                    if (sel_idxs_[i] < nm->size()) cur_nulls_[i] = (*nm)[sel_idxs_[i]];
                }
            }
        }
    }

   public:
    const std::vector<bool> *null_mask() const override {
        return cur_nulls_.empty() ? nullptr : &cur_nulls_;
    }

   private:
    std::vector<bool> cur_nulls_;

    void beginTuple() override { prev_->beginTuple(); build_cur(); }

    void nextTuple() override { prev_->nextTuple(); build_cur(); }

    bool is_end() const override { return prev_->is_end(); }

    std::unique_ptr<RmRecord> Next() override {
        return std::move(cur_rec_);
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    size_t tupleLen() const override { return len_; }

    // 投影不会改变输入行序。把有序性向上传递，避免逻辑投影下推后遮蔽
    // IndexScan 的顺序属性（例如 MIN(col) 的索引首行早停）。
    bool sorted_asc_on(const TabCol &col) const override {
        const bool exposed = std::any_of(cols_.begin(), cols_.end(), [&](const ColMeta &meta) {
            return meta.name == col.col_name &&
                   (col.tab_name.empty() || meta.tab_name == col.tab_name);
        });
        return exposed && prev_->sorted_asc_on(col);
    }

    Rid &rid() override { return _abstract_rid; }
};
