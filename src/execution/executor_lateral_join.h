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
#include <cstring>
#include <memory>
#include <vector>

#include "executor_correlated_filter.h"
#include "parser/ast.h"

/*
 * Dependent nested-loop executor used by JOIN LATERAL.  Unlike the ordinary
 * NLJ, the right executor is not materialized once: for every left tuple this
 * executor binds CorrelatedTupleContext and restarts the complete right tree.
 * Consequently a correlated Filter below Sort/Aggregation/Limit observes the
 * correct outer value and those operators run independently for each left row.
 *
 * RIGHT/FULL are intentionally unsupported: a right side that depends on a
 * particular left row has no single independent population from which their
 * unmatched-right phase could be derived.
 */
class LateralNestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    ConditionExprPtr on_predicate_;
    JoinType join_type_ = INNER_JOIN;
    std::shared_ptr<CorrelatedTupleContext> correlated_;

    size_t len_ = 0;
    std::vector<ColMeta> cols_;
    std::unique_ptr<RmRecord> current_left_;
    std::unique_ptr<RmRecord> current_right_;
    std::vector<bool> left_nulls_;
    std::vector<bool> right_nulls_;
    std::vector<bool> output_nulls_;

    bool ended_ = true;
    bool left_matched_ = false;
    bool left_null_emitted_ = false;
    bool current_is_left_null_ = false;
    bool advance_right_before_seek_ = false;

    static std::vector<bool> copy_null_mask(const AbstractExecutor &executor) {
        std::vector<bool> result(executor.cols().size(), false);
        if (const auto *mask = executor.null_mask(); mask != nullptr) {
            for (size_t i = 0; i < result.size() && i < mask->size(); ++i) {
                result[i] = (*mask)[i];
            }
        }
        return result;
    }

    bool find_join_operand(const TabCol &target, const RmRecord &right_record,
                           CorrelatedTupleContext::Operand &result) const {
        const auto &left_columns = left_->cols();
        for (size_t i = 0; i < left_columns.size(); ++i) {
            const auto &column = left_columns[i];
            if (column.name != target.col_name ||
                (!target.tab_name.empty() && column.tab_name != target.tab_name)) {
                continue;
            }
            result.data = current_left_->data + column.offset;
            result.len = column.len;
            result.type = column.type;
            result.is_null = i < left_nulls_.size() && left_nulls_[i];
            result.found = true;
            return true;
        }

        const auto &right_columns = right_->cols();
        for (size_t i = 0; i < right_columns.size(); ++i) {
            const auto &column = right_columns[i];
            if (column.name != target.col_name ||
                (!target.tab_name.empty() && column.tab_name != target.tab_name)) {
                continue;
            }
            result.data = right_record.data + column.offset;
            result.len = column.len;
            result.type = column.type;
            result.is_null = i < right_nulls_.size() && right_nulls_[i];
            result.found = true;
            return true;
        }
        return false;
    }

    CorrelatedTupleContext::Operand require_join_operand(const TabCol &target,
                                                          const RmRecord &right_record) const {
        CorrelatedTupleContext::Operand result;
        if (!find_join_operand(target, right_record, result)) {
            throw ColumnNotFoundError(target.col_name);
        }
        return result;
    }

    TruthValue evaluate_on_atom(const Condition &predicate,
                                const RmRecord &right_record) const {
        const auto lhs = require_join_operand(predicate.lhs_col, right_record);
        if (is_null_test_op(predicate.op)) {
            return evaluate_null_test(lhs.is_null, predicate.op);
        }
        if (lhs.is_null) return TruthValue::UNKNOWN_VALUE;

        CorrelatedTupleContext::Operand rhs;
        if (predicate.is_rhs_val) {
            if (predicate.rhs_val.raw == nullptr) {
                throw InternalError("LATERAL JOIN predicate literal has no raw value");
            }
            rhs.data = predicate.rhs_val.raw->data;
            rhs.len = predicate.rhs_val.raw->size;
            rhs.type = predicate.rhs_val.type;
            rhs.found = true;
        } else {
            rhs = require_join_operand(predicate.rhs_col, right_record);
            if (rhs.is_null) return TruthValue::UNKNOWN_VALUE;
        }
        return correlated_executor_detail::compare_operands(lhs, rhs, predicate.op)
                   ? TruthValue::TRUE_VALUE
                   : TruthValue::FALSE_VALUE;
    }

    bool matches_on(const RmRecord &right_record) const {
        return evaluate_bool_expr(on_predicate_, [&](const Condition &predicate) {
                   return evaluate_on_atom(predicate, right_record);
               }) == TruthValue::TRUE_VALUE;
    }

    void set_output_nulls(bool null_extend_right) {
        output_nulls_ = left_nulls_;
        if (null_extend_right) {
            output_nulls_.insert(output_nulls_.end(), right_->cols().size(), true);
        } else {
            output_nulls_.insert(output_nulls_.end(), right_nulls_.begin(), right_nulls_.end());
        }
    }

    void load_current_left() {
        left_nulls_ = copy_null_mask(*left_);
        current_left_ = left_->Next();
        if (current_left_ == nullptr) {
            throw InternalError("LATERAL JOIN left child returned no current tuple");
        }
        correlated_->bind(*current_left_, left_->cols(), &left_nulls_);
        right_->beginTuple();
        current_right_.reset();
        right_nulls_.clear();
        left_matched_ = false;
        left_null_emitted_ = false;
        current_is_left_null_ = false;
        advance_right_before_seek_ = false;
    }

    void advance_left() {
        left_->nextTuple();
        if (left_->is_end()) {
            current_left_.reset();
            correlated_->clear();
            return;
        }
        load_current_left();
    }

    void seek_next_output() {
        current_right_.reset();
        output_nulls_.clear();
        current_is_left_null_ = false;

        while (current_left_ != nullptr) {
            if (advance_right_before_seek_) {
                right_->nextTuple();
                advance_right_before_seek_ = false;
            }

            while (!right_->is_end()) {
                right_nulls_ = copy_null_mask(*right_);
                auto candidate = right_->Next();
                if (candidate == nullptr) {
                    throw InternalError("LATERAL JOIN right child returned no current tuple");
                }
                if (matches_on(*candidate)) {
                    current_right_ = std::move(candidate);
                    left_matched_ = true;
                    current_is_left_null_ = false;
                    advance_right_before_seek_ = true;
                    set_output_nulls(false);
                    ended_ = false;
                    return;
                }
                right_->nextTuple();
            }

            if (join_type_ == LEFT_JOIN && !left_matched_ && !left_null_emitted_) {
                left_null_emitted_ = true;
                current_is_left_null_ = true;
                set_output_nulls(true);
                ended_ = false;
                return;
            }
            advance_left();
        }

        ended_ = true;
        correlated_->clear();
    }

   public:
    LateralNestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left,
                                  std::unique_ptr<AbstractExecutor> right,
                                  ConditionExprPtr on_predicate,
                                  JoinType join_type,
                                  std::shared_ptr<CorrelatedTupleContext> correlated)
        : left_(std::move(left)), right_(std::move(right)),
          on_predicate_(std::move(on_predicate)), join_type_(join_type),
          correlated_(std::move(correlated)) {
        if (left_ == nullptr || right_ == nullptr || correlated_ == nullptr) {
            throw InternalError("LateralNestedLoopJoinExecutor requires children and context");
        }
        if (join_type_ != INNER_JOIN && join_type_ != CROSS_JOIN && join_type_ != LEFT_JOIN) {
            throw InternalError("LATERAL JOIN supports only INNER, CROSS, and LEFT");
        }
        if (join_type_ == CROSS_JOIN && on_predicate_ != nullptr) {
            throw InternalError("CROSS JOIN LATERAL cannot have an ON predicate");
        }

        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_columns = right_->cols();
        for (auto &column : right_columns) column.offset += left_->tupleLen();
        cols_.insert(cols_.end(), right_columns.begin(), right_columns.end());
    }

    LateralNestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left,
                                  std::unique_ptr<AbstractExecutor> right,
                                  std::vector<Condition> on_predicates,
                                  JoinType join_type,
                                  std::shared_ptr<CorrelatedTupleContext> correlated)
        : LateralNestedLoopJoinExecutor(
              std::move(left), std::move(right),
              SeqScanExecutor::conditions_to_expr(std::move(on_predicates)), join_type,
              std::move(correlated)) {}

    ~LateralNestedLoopJoinExecutor() override { correlated_->clear(); }

    void beginTuple() override {
        ended_ = true;
        current_left_.reset();
        current_right_.reset();
        left_nulls_.clear();
        right_nulls_.clear();
        output_nulls_.clear();
        correlated_->clear();

        left_->beginTuple();
        if (left_->is_end()) return;
        load_current_left();
        seek_next_output();
    }

    void nextTuple() override {
        if (!ended_) seek_next_output();
    }

    bool is_end() const override { return ended_; }

    std::unique_ptr<RmRecord> Next() override {
        if (ended_ || current_left_ == nullptr) return nullptr;
        auto result = std::make_unique<RmRecord>(len_);
        std::memset(result->data, 0, len_);
        std::memcpy(result->data, current_left_->data, left_->tupleLen());
        if (!current_is_left_null_ && current_right_ != nullptr) {
            std::memcpy(result->data + left_->tupleLen(), current_right_->data, right_->tupleLen());
        }
        return result;
    }

    const std::vector<bool> *null_mask() const override {
        return std::any_of(output_nulls_.begin(), output_nulls_.end(),
                           [](bool value) { return value; })
                   ? &output_nulls_
                   : nullptr;
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return _abstract_rid; }
};
