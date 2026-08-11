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
#include "parser/ast.h"
#include "system/sm.h"

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    enum class OutputKind { MATCHED, LEFT_NULL_EXTENDED, RIGHT_NULL_EXTENDED };

    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    JoinType join_type_ = INNER_JOIN;
    size_t len_ = 0;
    std::vector<ColMeta> cols_;
    std::vector<Condition> fed_conds_;
    bool isend_ = true;

    std::unique_ptr<RmRecord> cur_left_;
    std::vector<bool> cur_left_nulls_;
    bool current_left_matched_ = false;
    bool left_null_emitted_ = false;

    // NLJ 的右侧一次性物化。FULL/RIGHT JOIN 还需记录哪些右行曾匹配。
    std::vector<std::unique_ptr<RmRecord>> right_buf_;
    std::vector<std::vector<bool>> right_nulls_;
    std::vector<bool> right_matched_;
    size_t right_idx_ = 0;
    size_t current_right_idx_ = 0;

    bool unmatched_right_phase_ = false;
    size_t unmatched_right_idx_ = 0;
    OutputKind output_kind_ = OutputKind::MATCHED;
    std::vector<bool> current_nulls_;

    static std::vector<bool> copy_null_mask(AbstractExecutor *executor) {
        std::vector<bool> result(executor->cols().size(), false);
        if (const auto *mask = executor->null_mask(); mask != nullptr) {
            for (size_t i = 0; i < result.size() && i < mask->size(); ++i) result[i] = (*mask)[i];
        }
        return result;
    }

    void buffer_right() {
        right_buf_.clear();
        right_nulls_.clear();
        right_->beginTuple();
        while (!right_->is_end()) {
            right_nulls_.push_back(copy_null_mask(right_.get()));
            right_buf_.push_back(right_->Next());
            right_->nextTuple();
        }
        right_matched_.assign(right_buf_.size(), false);
    }

    void load_current_left() {
        cur_left_nulls_ = copy_null_mask(left_.get());
        cur_left_ = left_->Next();
        right_idx_ = 0;
        current_left_matched_ = false;
        left_null_emitted_ = false;
    }

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

    struct Operand {
        const char *data = nullptr;
        int len = 0;
        ColType type = TYPE_INT;
        bool is_null = false;
        bool found = false;
    };

    Operand find_operand(const TabCol &target, const RmRecord *left_rec,
                         const RmRecord *right_rec, size_t right_index) const {
        const auto &left_cols = left_->cols();
        for (size_t i = 0; i < left_cols.size(); ++i) {
            const auto &col = left_cols[i];
            if (col.name == target.col_name &&
                (target.tab_name.empty() || col.tab_name == target.tab_name)) {
                return {left_rec->data + col.offset, col.len, col.type,
                        i < cur_left_nulls_.size() && cur_left_nulls_[i], true};
            }
        }

        const auto &right_cols = right_->cols();
        for (size_t i = 0; i < right_cols.size(); ++i) {
            const auto &col = right_cols[i];
            if (col.name == target.col_name &&
                (target.tab_name.empty() || col.tab_name == target.tab_name)) {
                const auto &mask = right_nulls_[right_index];
                return {right_rec->data + col.offset, col.len, col.type,
                        i < mask.size() && mask[i], true};
            }
        }
        return {};
    }

    // SQL 三值逻辑：ON 中任一操作数为 NULL 时结果是 UNKNOWN，
    // 对 JOIN 匹配而言与 false 相同。
    bool eval_join_conds(const RmRecord *left_rec, const RmRecord *right_rec,
                         size_t right_index) const {
        for (const auto &cond : fed_conds_) {
            const auto lhs = find_operand(cond.lhs_col, left_rec, right_rec, right_index);
            if (!lhs.found || lhs.is_null) return false;

            const char *rhs_data = nullptr;
            if (cond.is_rhs_val) {
                rhs_data = cond.rhs_val.raw->data;
            } else {
                const auto rhs = find_operand(cond.rhs_col, left_rec, right_rec, right_index);
                if (!rhs.found || rhs.is_null) return false;
                rhs_data = rhs.data;
            }
            if (!compare_value(lhs.data, rhs_data, lhs.len, lhs.type, cond.op)) return false;
        }
        return true;
    }

    void set_output_nulls(OutputKind kind, size_t right_index = 0) {
        current_nulls_.clear();
        current_nulls_.reserve(cols_.size());
        if (kind == OutputKind::RIGHT_NULL_EXTENDED) {
            current_nulls_.insert(current_nulls_.end(), left_->cols().size(), true);
        } else {
            current_nulls_.insert(current_nulls_.end(), cur_left_nulls_.begin(), cur_left_nulls_.end());
        }
        if (kind == OutputKind::LEFT_NULL_EXTENDED) {
            current_nulls_.insert(current_nulls_.end(), right_->cols().size(), true);
        } else {
            const auto &right_mask = right_nulls_[right_index];
            current_nulls_.insert(current_nulls_.end(), right_mask.begin(), right_mask.end());
        }
    }

    void seek_next_output() {
        while (true) {
            if (unmatched_right_phase_) {
                while (unmatched_right_idx_ < right_buf_.size() && right_matched_[unmatched_right_idx_]) {
                    ++unmatched_right_idx_;
                }
                if (unmatched_right_idx_ >= right_buf_.size()) {
                    isend_ = true;
                    current_nulls_.clear();
                    return;
                }
                current_right_idx_ = unmatched_right_idx_++;
                output_kind_ = OutputKind::RIGHT_NULL_EXTENDED;
                set_output_nulls(output_kind_, current_right_idx_);
                isend_ = false;
                return;
            }

            if (cur_left_ == nullptr) {
                if (join_type_ == RIGHT_JOIN || join_type_ == FULL_JOIN) {
                    unmatched_right_phase_ = true;
                    unmatched_right_idx_ = 0;
                    continue;
                }
                isend_ = true;
                current_nulls_.clear();
                return;
            }

            while (right_idx_ < right_buf_.size()) {
                const size_t candidate = right_idx_++;
                if (eval_join_conds(cur_left_.get(), right_buf_[candidate].get(), candidate)) {
                    current_left_matched_ = true;
                    right_matched_[candidate] = true;
                    current_right_idx_ = candidate;
                    output_kind_ = OutputKind::MATCHED;
                    set_output_nulls(output_kind_, current_right_idx_);
                    isend_ = false;
                    return;
                }
            }

            if (!current_left_matched_ && !left_null_emitted_ &&
                (join_type_ == LEFT_JOIN || join_type_ == FULL_JOIN)) {
                left_null_emitted_ = true;
                output_kind_ = OutputKind::LEFT_NULL_EXTENDED;
                set_output_nulls(output_kind_);
                isend_ = false;
                return;
            }

            left_->nextTuple();
            if (left_->is_end()) {
                cur_left_.reset();
            } else {
                load_current_left();
            }
        }
    }

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left,
                           std::unique_ptr<AbstractExecutor> right,
                           std::vector<Condition> conds,
                           JoinType join_type = INNER_JOIN)
        : left_(std::move(left)), right_(std::move(right)), join_type_(join_type),
          fed_conds_(std::move(conds)) {
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) col.offset += left_->tupleLen();
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
    }

    void beginTuple() override {
        isend_ = true;
        unmatched_right_phase_ = false;
        unmatched_right_idx_ = 0;
        current_nulls_.clear();
        cur_left_.reset();

        buffer_right();
        left_->beginTuple();
        if (!left_->is_end()) load_current_left();
        seek_next_output();
    }

    void nextTuple() override {
        if (!isend_) seek_next_output();
    }

    bool is_end() const override { return isend_; }

    std::unique_ptr<RmRecord> Next() override {
        if (isend_) return nullptr;
        auto joined = std::make_unique<RmRecord>(len_);
        memset(joined->data, 0, len_);
        if (output_kind_ != OutputKind::RIGHT_NULL_EXTENDED) {
            memcpy(joined->data, cur_left_->data, left_->tupleLen());
        }
        if (output_kind_ != OutputKind::LEFT_NULL_EXTENDED) {
            memcpy(joined->data + left_->tupleLen(), right_buf_[current_right_idx_]->data,
                   right_->tupleLen());
        }
        return joined;
    }

    const std::vector<bool> *null_mask() const override {
        return std::any_of(current_nulls_.begin(), current_nulls_.end(), [](bool value) { return value; })
                   ? &current_nulls_
                   : nullptr;
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return _abstract_rid; }
};
