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
#include "executor_seq_scan.h"
#include "index/ix.h"
#include "parser/ast.h"
#include "system/sm.h"

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    enum class OutputKind {
        MATCHED,
        LEFT_NULL_EXTENDED,
        RIGHT_NULL_EXTENDED,
        LEFT_ONLY,
        RIGHT_ONLY
    };

    struct CoalescedRuntimeColumn {
        CoalescedJoinColumn spec;
        size_t left_index = 0;
        size_t right_index = 0;
        size_t output_offset = 0;
    };

    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    JoinType join_type_ = INNER_JOIN;
    size_t len_ = 0;
    std::vector<ColMeta> cols_;
    ConditionExprPtr on_predicate_;
    std::vector<CoalescedRuntimeColumn> coalesced_cols_;
    bool isend_ = true;

    std::unique_ptr<RmRecord> cur_left_;
    std::vector<bool> cur_left_nulls_;
    bool current_left_matched_ = false;
    bool current_left_single_emitted_ = false;

    // NLJ 的右侧一次性物化。FULL/RIGHT outer 以及 RIGHT SEMI/ANTI
    // 还需按物化行身份记录是否曾匹配（重复值也必须分别保留）。
    std::vector<std::unique_ptr<RmRecord>> right_buf_;
    std::vector<std::vector<bool>> right_nulls_;
    std::vector<bool> right_matched_;
    size_t right_idx_ = 0;
    size_t current_right_idx_ = 0;

    bool unmatched_right_phase_ = false;
    size_t unmatched_right_idx_ = 0;
    OutputKind output_kind_ = OutputKind::MATCHED;
    std::vector<bool> current_nulls_;

    bool is_left_semi() const { return join_type_ == LEFT_SEMI_JOIN; }
    bool is_right_semi() const { return join_type_ == RIGHT_SEMI_JOIN; }
    bool is_left_anti() const { return join_type_ == LEFT_ANTI_JOIN; }
    bool is_right_anti() const { return join_type_ == RIGHT_ANTI_JOIN; }
    bool outputs_left_only() const { return is_left_semi() || is_left_anti(); }
    bool outputs_right_only() const { return is_right_semi() || is_right_anti(); }

    bool needs_right_final_phase() const {
        return join_type_ == RIGHT_JOIN || join_type_ == FULL_JOIN || outputs_right_only();
    }

    bool should_emit_right_in_final_phase(size_t index) const {
        if (is_right_semi()) return right_matched_[index];
        // RIGHT/FULL outer joins and RIGHT ANTI all emit unmatched right rows.
        return !right_matched_[index];
    }

    static size_t find_col_index(const std::vector<ColMeta> &cols, const TabCol &target) {
        auto it = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) {
            return col.name == target.col_name &&
                   (target.tab_name.empty() || col.tab_name == target.tab_name);
        });
        if (it == cols.end()) throw ColumnNotFoundError(target.col_name);
        return static_cast<size_t>(it - cols.begin());
    }

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
        current_left_single_emitted_ = false;
    }

    static int compare_typed_value(const char *a, int a_len, const char *b, int b_len,
                                   ColType type) {
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
            const int common = std::min(a_len, b_len);
            cmp = memcmp(a, b, common);
            if (cmp == 0) {
                for (int i = common; i < a_len; ++i) {
                    if (static_cast<unsigned char>(a[i]) != 0) { cmp = 1; break; }
                }
            }
            if (cmp == 0) {
                for (int i = common; i < b_len; ++i) {
                    if (static_cast<unsigned char>(b[i]) != 0) { cmp = -1; break; }
                }
            }
        }
        return cmp;
    }

    static bool compare_value(const char *a, int a_len, const char *b, int b_len,
                              ColType type, CompOp op) {
        const int cmp = compare_typed_value(a, a_len, b, b_len, type);
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

    TruthValue eval_join_atom(const Condition &cond, const RmRecord *left_rec,
                              const RmRecord *right_rec, size_t right_index) const {
        const auto lhs = find_operand(cond.lhs_col, left_rec, right_rec, right_index);
        if (!lhs.found) throw ColumnNotFoundError(cond.lhs_col.col_name);
        if (lhs.is_null) return TruthValue::UNKNOWN_VALUE;

        const char *rhs_data = nullptr;
        int rhs_len = lhs.len;
        if (cond.is_rhs_val) {
            if (cond.rhs_val.raw == nullptr) {
                throw InternalError("JOIN predicate literal has no raw value");
            }
            rhs_data = cond.rhs_val.raw->data;
        } else {
            const auto rhs = find_operand(cond.rhs_col, left_rec, right_rec, right_index);
            if (!rhs.found) throw ColumnNotFoundError(cond.rhs_col.col_name);
            if (rhs.is_null) return TruthValue::UNKNOWN_VALUE;
            rhs_data = rhs.data;
            rhs_len = rhs.len;
        }
        return compare_value(lhs.data, lhs.len, rhs_data, rhs_len, lhs.type, cond.op)
                   ? TruthValue::TRUE_VALUE
                   : TruthValue::FALSE_VALUE;
    }

    // ON 只有整棵树计算为 TRUE 才构成匹配；UNKNOWN 不构成匹配，
    // 但必须继续参与 NOT/AND/OR 的 SQL 三值组合。
    bool eval_join_conds(const RmRecord *left_rec, const RmRecord *right_rec,
                         size_t right_index) const {
        return evaluate_bool_expr(on_predicate_, [&](const Condition &cond) {
                   return eval_join_atom(cond, left_rec, right_rec, right_index);
               }) == TruthValue::TRUE_VALUE;
    }

    bool left_source_is_null(const CoalescedRuntimeColumn &column, OutputKind kind) const {
        if (kind == OutputKind::RIGHT_NULL_EXTENDED) return true;
        return column.left_index < cur_left_nulls_.size() &&
               cur_left_nulls_[column.left_index];
    }

    bool right_source_is_null(const CoalescedRuntimeColumn &column, OutputKind kind,
                              size_t right_index) const {
        if (kind == OutputKind::LEFT_NULL_EXTENDED) return true;
        const auto &mask = right_nulls_[right_index];
        return column.right_index < mask.size() && mask[column.right_index];
    }

    void set_output_nulls(OutputKind kind, size_t right_index = 0) {
        current_nulls_.clear();
        current_nulls_.reserve(cols_.size());

        if (kind == OutputKind::LEFT_ONLY) {
            current_nulls_ = cur_left_nulls_;
            return;
        }
        if (kind == OutputKind::RIGHT_ONLY) {
            current_nulls_ = right_nulls_[right_index];
            return;
        }

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

        // The appended COALESCE value is NULL only when both candidate inputs
        // are NULL (including a side supplied as NULL by an outer join).
        for (const auto &column : coalesced_cols_) {
            current_nulls_.push_back(left_source_is_null(column, kind) &&
                                     right_source_is_null(column, kind, right_index));
        }
    }

    void append_coalesced_values(RmRecord *record, OutputKind kind,
                                 size_t right_index) const {
        for (const auto &column : coalesced_cols_) {
            const ColMeta &left_col = left_->cols()[column.left_index];
            const ColMeta &right_col = right_->cols()[column.right_index];
            char *dst = record->data + column.output_offset;

            if (!left_source_is_null(column, kind)) {
                memcpy(dst, cur_left_->data + left_col.offset,
                       std::min(column.spec.len, left_col.len));
            } else if (!right_source_is_null(column, kind, right_index)) {
                memcpy(dst, right_buf_[right_index]->data + right_col.offset,
                       std::min(column.spec.len, right_col.len));
            }
        }
    }

    void seek_next_output() {
        while (true) {
            if (unmatched_right_phase_) {
                while (unmatched_right_idx_ < right_buf_.size() &&
                       !should_emit_right_in_final_phase(unmatched_right_idx_)) {
                    ++unmatched_right_idx_;
                }
                if (unmatched_right_idx_ >= right_buf_.size()) {
                    isend_ = true;
                    current_nulls_.clear();
                    return;
                }
                current_right_idx_ = unmatched_right_idx_++;
                output_kind_ = outputs_right_only() ? OutputKind::RIGHT_ONLY
                                                    : OutputKind::RIGHT_NULL_EXTENDED;
                set_output_nulls(output_kind_, current_right_idx_);
                isend_ = false;
                return;
            }

            if (cur_left_ == nullptr) {
                if (needs_right_final_phase()) {
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

                    if (is_left_semi()) {
                        // EXISTS semantics: a preserved left row is returned
                        // once, irrespective of how many right rows match it.
                        right_idx_ = right_buf_.size();
                        current_left_single_emitted_ = true;
                        output_kind_ = OutputKind::LEFT_ONLY;
                        set_output_nulls(output_kind_);
                        isend_ = false;
                        return;
                    }
                    if (is_left_anti()) {
                        // One TRUE match is enough to reject this left row.
                        right_idx_ = right_buf_.size();
                        break;
                    }
                    if (outputs_right_only()) {
                        // RIGHT SEMI/ANTI cannot decide output until every left
                        // row has been considered.  Track per buffered right row
                        // and emit in right-input order in the final phase.
                        continue;
                    }

                    output_kind_ = OutputKind::MATCHED;
                    set_output_nulls(output_kind_, current_right_idx_);
                    isend_ = false;
                    return;
                }
            }

            if (is_left_anti() && !current_left_matched_ &&
                !current_left_single_emitted_) {
                current_left_single_emitted_ = true;
                output_kind_ = OutputKind::LEFT_ONLY;
                set_output_nulls(output_kind_);
                isend_ = false;
                return;
            }

            if (!current_left_matched_ && !current_left_single_emitted_ &&
                (join_type_ == LEFT_JOIN || join_type_ == FULL_JOIN)) {
                current_left_single_emitted_ = true;
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
                           ConditionExprPtr on_predicate,
                           JoinType join_type = INNER_JOIN,
                           std::vector<CoalescedJoinColumn> coalesced_cols = {})
        : left_(std::move(left)), right_(std::move(right)), join_type_(join_type),
          on_predicate_(std::move(on_predicate)) {
        if (outputs_left_only()) {
            len_ = left_->tupleLen();
            cols_ = left_->cols();
            return;
        }
        if (outputs_right_only()) {
            len_ = right_->tupleLen();
            cols_ = right_->cols();
            return;
        }

        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) col.offset += left_->tupleLen();
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());

        coalesced_cols_.reserve(coalesced_cols.size());
        for (auto &spec : coalesced_cols) {
            CoalescedRuntimeColumn runtime;
            runtime.left_index = find_col_index(left_->cols(), spec.left);
            runtime.right_index = find_col_index(right_->cols(), spec.right);
            const auto &left_source = left_->cols()[runtime.left_index];
            const auto &right_source = right_->cols()[runtime.right_index];
            if (spec.len <= 0 || left_source.type != spec.type ||
                right_source.type != spec.type) {
                throw InternalError("Invalid COALESCE join column metadata");
            }
            runtime.output_offset = len_;
            runtime.spec = std::move(spec);

            ColMeta output_col;
            output_col.tab_name = runtime.spec.output.tab_name;
            output_col.name = runtime.spec.output.col_name;
            output_col.type = runtime.spec.type;
            output_col.len = runtime.spec.len;
            output_col.offset = static_cast<int>(runtime.output_offset);
            output_col.index = false;
            cols_.push_back(std::move(output_col));
            len_ += runtime.spec.len;
            coalesced_cols_.push_back(std::move(runtime));
        }
    }

    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left,
                           std::unique_ptr<AbstractExecutor> right,
                           std::vector<Condition> conds,
                           JoinType join_type = INNER_JOIN,
                           std::vector<CoalescedJoinColumn> coalesced_cols = {})
        : NestedLoopJoinExecutor(std::move(left), std::move(right),
                                 SeqScanExecutor::conditions_to_expr(std::move(conds)),
                                 join_type, std::move(coalesced_cols)) {}

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

        if (output_kind_ == OutputKind::LEFT_ONLY) {
            memcpy(joined->data, cur_left_->data, left_->tupleLen());
            return joined;
        }
        if (output_kind_ == OutputKind::RIGHT_ONLY) {
            memcpy(joined->data, right_buf_[current_right_idx_]->data, right_->tupleLen());
            return joined;
        }

        if (output_kind_ != OutputKind::RIGHT_NULL_EXTENDED) {
            memcpy(joined->data, cur_left_->data, left_->tupleLen());
        }
        if (output_kind_ != OutputKind::LEFT_NULL_EXTENDED) {
            memcpy(joined->data + left_->tupleLen(), right_buf_[current_right_idx_]->data,
                   right_->tupleLen());
        }
        append_coalesced_values(joined.get(), output_kind_, current_right_idx_);
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
