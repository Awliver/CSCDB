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

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 外表（驱动）
    std::unique_ptr<AbstractExecutor> right_;   // 内表（被驱动，每次外表推进就重扫）
    size_t len_;                                // 连接后每条记录的总长度
    std::vector<ColMeta> cols_;                 // 连接后
    std::vector<Condition> fed_conds_;          // 连接条件
    bool isend;

    // 优化：缓存当前对的左右记录，避免对子 Next() 二次调用
    std::unique_ptr<RmRecord> cur_left_;

    // 优化：内表物化
    std::vector<std::unique_ptr<RmRecord>> right_buf_;
    size_t right_idx_ = 0;

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                            std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        // 右表列在连接结果中的偏移要加左表 tuple 长度
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        isend = false;
        fed_conds_ = std::move(conds);
    }

    void buffer_right() {
        right_buf_.clear();
        right_->beginTuple();
        while (!right_->is_end()) {
            right_buf_.push_back(right_->Next());
            right_->nextTuple();
        }
    }

    /**
     * 按类型/操作符比较两段字节数据
     */
    static bool compare_value(const char *a, const char *b, int len, ColType type, CompOp op) {
        int cmp;
        if (type == TYPE_INT) {
            int ia = *reinterpret_cast<const int *>(a);
            int ib = *reinterpret_cast<const int *>(b);
            cmp = (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
        } else if (type == TYPE_FLOAT) {
            float fa = *reinterpret_cast<const float *>(a);
            float fb = *reinterpret_cast<const float *>(b);
            cmp = (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
        } else {
            cmp = memcmp(a, b, len);
        }
        switch (op) {
            case OP_EQ: return cmp == 0;
            case OP_NE: return cmp != 0;
            case OP_LT: return cmp < 0;
            case OP_GT: return cmp > 0;
            case OP_LE: return cmp <= 0;
            case OP_GE: return cmp >= 0;
        }
        return false;
    }


    /**
     * 评估 JOIN 条件
     * 列可能在 left_ 或 right_ 的 cols() 里，分别用对应的记录数据偏移
     */
    bool eval_join_conds(const RmRecord *left_rec, const RmRecord *right_rec) const {
        const auto &left_cols = left_->cols();
        const auto &right_cols = right_->cols();

        for (const auto &cond : fed_conds_) {
            // 找 lhs 列
            const char *lhs_data = nullptr;
            int lhs_len = 0;
            ColType lhs_type = TYPE_INT;

            auto lhs_in_left = std::find_if(left_cols.begin(), left_cols.end(), [&](const ColMeta &c) {
                return c.tab_name == cond.lhs_col.tab_name && c.name == cond.lhs_col.col_name;
            });
            if (lhs_in_left != left_cols.end()) {
                lhs_data = left_rec->data + lhs_in_left->offset;
                lhs_len = lhs_in_left->len;
                lhs_type = lhs_in_left->type;
            } else {
                auto lhs_in_right = std::find_if(right_cols.begin(), right_cols.end(), [&](const ColMeta &c) {
                    return c.tab_name == cond.lhs_col.tab_name && c.name == cond.lhs_col.col_name;
                });
                if (lhs_in_right == right_cols.end()) return false;
                lhs_data = right_rec->data + lhs_in_right->offset;
                lhs_len = lhs_in_right->len;
                lhs_type = lhs_in_right->type;
            }

            // 找 rhs
            const char *rhs_data = nullptr;
            if (cond.is_rhs_val) {
                rhs_data = cond.rhs_val.raw->data;
            } else {
                auto rhs_in_left = std::find_if(left_cols.begin(), left_cols.end(), [&](const ColMeta &c) {
                    return c.tab_name == cond.rhs_col.tab_name && c.name == cond.rhs_col.col_name;
                });
                if (rhs_in_left != left_cols.end()) {
                    rhs_data = left_rec->data + rhs_in_left->offset;
                } else {
                    auto rhs_in_right = std::find_if(right_cols.begin(), right_cols.end(), [&](const ColMeta &c) {
                        return c.tab_name == cond.rhs_col.tab_name && c.name == cond.rhs_col.col_name;
                    });
                    if (rhs_in_right == right_cols.end()) return false;
                    rhs_data = right_rec->data + rhs_in_right->offset;
                }
            }

            // 比较
            if (!compare_value(lhs_data, rhs_data, lhs_len, lhs_type, cond.op)) {
                return false;
            }
        }
        return true;  // 所有条件满足
    }

    void find_next_match() {
        while (!isend) {
            if (right_idx_ >= right_buf_.size()) {
                left_->nextTuple();
                if (left_->is_end()) {
                    isend = true;
                    return;
                }
                right_idx_ = 0;
                cur_left_ = left_->Next();
                continue;
            }
            if (eval_join_conds(cur_left_.get(), right_buf_[right_idx_].get())) {
                return;
            }
            right_idx_++;
        }
    }

    void beginTuple() override {
        left_->beginTuple();
        if (left_->is_end()) {
            isend = true;
            return;
        }
        buffer_right();                   // 一次性物化内表
        if (right_buf_.empty()) {
            isend = true;
            return;
        }
        right_idx_ = 0;
        isend = false;
        cur_left_ = left_->Next();
        find_next_match();
    }

    void nextTuple() override {
        if (isend) return;
        right_idx_++;
        find_next_match();
    }

    bool is_end() const override { return isend; }

    std::unique_ptr<RmRecord> Next() override {
        auto joined = std::make_unique<RmRecord>(len_);
        memcpy(joined->data, cur_left_->data, left_->tupleLen());
        memcpy(joined->data + left_->tupleLen(), right_buf_[right_idx_]->data, right_->tupleLen());
        return joined;
    }


    const std::vector<ColMeta> &cols() const override { return cols_; }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return _abstract_rid; }

};
