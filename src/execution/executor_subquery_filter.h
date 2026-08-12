/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "executor_abstract.h"
#include "executor_correlated_filter.h"

// Evaluates ordinary comparison leaves and parameterized EXISTS/IN leaves in
// one BoolExpr.  Each subquery executor is restarted after binding the current
// input tuple, which gives correlated predicates the same execution contract
// already used by JOIN LATERAL.
class SubqueryFilterExecutor : public AbstractExecutor {
   private:
    struct RuntimeSubquery {
        const Condition *condition = nullptr;
        std::unique_ptr<AbstractExecutor> executor;
        std::shared_ptr<CorrelatedTupleContext> correlated;
    };

    std::unique_ptr<AbstractExecutor> child_;
    ConditionExprPtr predicate_;
    std::vector<RuntimeSubquery> subqueries_;
    std::unique_ptr<RmRecord> current_;
    std::vector<bool> current_nulls_;
    bool ended_ = true;

    CorrelatedTupleContext::Operand outer_operand(
        const TabCol &target, const RmRecord &record,
        const std::vector<bool> &nulls) const {
        const auto &columns = child_->cols();
        for (size_t i = 0; i < columns.size(); ++i) {
            const auto &column = columns[i];
            if (column.name != target.col_name ||
                (!target.tab_name.empty() && column.tab_name != target.tab_name)) {
                continue;
            }
            return {record.data + column.offset, column.len, column.type,
                    i < nulls.size() && nulls[i], true};
        }
        throw ColumnNotFoundError(target.col_name);
    }

    RuntimeSubquery &runtime_for(const Condition &condition) {
        auto found = std::find_if(
            subqueries_.begin(), subqueries_.end(),
            [&](const RuntimeSubquery &runtime) {
                return runtime.condition == &condition;
            });
        if (found == subqueries_.end()) {
            throw InternalError("Missing runtime predicate subquery");
        }
        return *found;
    }

    static bool equal_operands(const CorrelatedTupleContext::Operand &lhs,
                               const CorrelatedTupleContext::Operand &rhs) {
        if (lhs.type == rhs.type) {
            return correlated_executor_detail::compare_operands(lhs, rhs, OP_EQ);
        }
        const bool numeric = (lhs.type == TYPE_INT || lhs.type == TYPE_FLOAT) &&
                             (rhs.type == TYPE_INT || rhs.type == TYPE_FLOAT);
        if (!numeric) {
            throw IncompatibleTypeError(coltype2str(lhs.type), coltype2str(rhs.type));
        }
        const double left = lhs.type == TYPE_INT
                                ? static_cast<double>(load_unaligned<int>(lhs.data))
                                : static_cast<double>(load_unaligned<float>(lhs.data));
        const double right = rhs.type == TYPE_INT
                                 ? static_cast<double>(load_unaligned<int>(rhs.data))
                                 : static_cast<double>(load_unaligned<float>(rhs.data));
        return left == right;
    }

    TruthValue evaluate_subquery(const Condition &condition,
                                 const RmRecord &record,
                                 const std::vector<bool> &nulls) {
        auto &runtime = runtime_for(condition);
        runtime.correlated->bind(record, child_->cols(), &nulls);
        runtime.executor->beginTuple();

        if (condition.kind == ConditionKind::EXISTS_SUBQUERY) {
            const bool exists = !runtime.executor->is_end();
            runtime.correlated->clear();
            return exists ? TruthValue::TRUE_VALUE : TruthValue::FALSE_VALUE;
        }

        const auto lhs = outer_operand(condition.lhs_col, record, nulls);
        if (lhs.is_null) {
            runtime.correlated->clear();
            return TruthValue::UNKNOWN_VALUE;
        }
        if (runtime.executor->cols().size() != 1) {
            throw InternalError("IN subquery executor must return one column");
        }

        bool saw_null = false;
        while (!runtime.executor->is_end()) {
            bool rhs_null = false;
            if (const auto *mask = runtime.executor->null_mask();
                mask != nullptr && !mask->empty()) {
                rhs_null = (*mask)[0];
            }
            auto row = runtime.executor->Next();
            if (row == nullptr) {
                throw InternalError("Predicate subquery returned no current tuple");
            }
            if (rhs_null) {
                saw_null = true;
            } else {
                const auto &column = runtime.executor->cols().front();
                CorrelatedTupleContext::Operand rhs{
                    row->data + column.offset, column.len, column.type, false, true};
                if (equal_operands(lhs, rhs)) {
                    runtime.correlated->clear();
                    return TruthValue::TRUE_VALUE;
                }
            }
            runtime.executor->nextTuple();
        }
        runtime.correlated->clear();
        return saw_null ? TruthValue::UNKNOWN_VALUE : TruthValue::FALSE_VALUE;
    }

    TruthValue evaluate_atom(const Condition &condition,
                             const RmRecord &record,
                             const std::vector<bool> &nulls) {
        if (condition.kind != ConditionKind::COMPARISON) {
            return evaluate_subquery(condition, record, nulls);
        }

        const auto lhs = outer_operand(condition.lhs_col, record, nulls);
        if (is_null_test_op(condition.op)) {
            return evaluate_null_test(lhs.is_null, condition.op);
        }
        if (lhs.is_null) return TruthValue::UNKNOWN_VALUE;
        CorrelatedTupleContext::Operand rhs;
        if (condition.is_rhs_val) {
            if (condition.rhs_val.raw == nullptr) {
                throw InternalError("Subquery filter literal has no raw value");
            }
            rhs = {condition.rhs_val.raw->data, condition.rhs_val.raw->size,
                   condition.rhs_val.type, false, true};
        } else {
            rhs = outer_operand(condition.rhs_col, record, nulls);
            if (rhs.is_null) return TruthValue::UNKNOWN_VALUE;
        }
        return correlated_executor_detail::compare_operands(lhs, rhs, condition.op)
                   ? TruthValue::TRUE_VALUE
                   : TruthValue::FALSE_VALUE;
    }

    bool matches(const RmRecord &record, const std::vector<bool> &nulls) {
        return evaluate_bool_expr(predicate_, [&](const Condition &condition) {
                   return evaluate_atom(condition, record, nulls);
               }) == TruthValue::TRUE_VALUE;
    }

    void seek_match() {
        current_.reset();
        current_nulls_.clear();
        while (!child_->is_end()) {
            std::vector<bool> nulls(child_->cols().size(), false);
            if (const auto *mask = child_->null_mask(); mask != nullptr) {
                for (size_t i = 0; i < nulls.size() && i < mask->size(); ++i) {
                    nulls[i] = (*mask)[i];
                }
            }
            auto candidate = child_->Next();
            if (candidate == nullptr) {
                throw InternalError("Subquery filter child returned no current tuple");
            }
            if (matches(*candidate, nulls)) {
                current_ = std::move(candidate);
                current_nulls_ = std::move(nulls);
                ended_ = false;
                return;
            }
            child_->nextTuple();
        }
        ended_ = true;
    }

   public:
    SubqueryFilterExecutor(
        std::unique_ptr<AbstractExecutor> child, ConditionExprPtr predicate,
        std::vector<std::unique_ptr<AbstractExecutor>> subquery_executors,
        std::vector<std::shared_ptr<CorrelatedTupleContext>> correlated_contexts)
        : child_(std::move(child)), predicate_(std::move(predicate)) {
        if (child_ == nullptr || subquery_executors.size() != correlated_contexts.size()) {
            throw InternalError("Invalid SubqueryFilterExecutor inputs");
        }
        size_t index = 0;
        visit_bool_atoms(predicate_, [&](Condition &condition) {
            if (!is_subquery_condition(condition)) return;
            if (index >= subquery_executors.size()) {
                throw InternalError("Missing predicate subquery executor");
            }
            subqueries_.push_back({&condition, std::move(subquery_executors[index]),
                                   std::move(correlated_contexts[index])});
            ++index;
        });
        if (index != subquery_executors.size()) {
            throw InternalError("Unexpected predicate subquery executor");
        }
    }

    ~SubqueryFilterExecutor() override {
        for (auto &runtime : subqueries_) runtime.correlated->clear();
    }

    void beginTuple() override {
        child_->beginTuple();
        seek_match();
    }
    void nextTuple() override {
        if (ended_) return;
        child_->nextTuple();
        seek_match();
    }
    bool is_end() const override { return ended_; }
    std::unique_ptr<RmRecord> Next() override { return std::move(current_); }
    const std::vector<ColMeta> &cols() const override { return child_->cols(); }
    size_t tupleLen() const override { return child_->tupleLen(); }
    Rid &rid() override { return child_->rid(); }
    const std::vector<bool> *null_mask() const override {
        return std::any_of(current_nulls_.begin(), current_nulls_.end(),
                           [](bool value) { return value; })
                   ? &current_nulls_
                   : nullptr;
    }
};
