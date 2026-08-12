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

#include "executor_abstract.h"
#include "executor_seq_scan.h"

/*
 * A non-owning view of the current outer tuple of a correlated subquery.
 *
 * LateralNestedLoopJoinExecutor owns the record and NULL mask and keeps both
 * alive while the right executor is running.  Correlated filters in that right
 * executor share this small context object.  Keeping the context independent
 * of Context (the transaction/execution context) also makes it impossible for
 * one statement's correlated row to leak into another statement.
 */
struct CorrelatedTupleContext {
    struct Operand {
        const char *data = nullptr;
        int len = 0;
        ColType type = TYPE_INT;
        bool is_null = false;
        bool found = false;
    };

    const RmRecord *record = nullptr;
    const std::vector<ColMeta> *columns = nullptr;
    const std::vector<bool> *nulls = nullptr;

    void bind(const RmRecord &outer_record, const std::vector<ColMeta> &outer_columns,
              const std::vector<bool> *outer_nulls = nullptr) {
        record = &outer_record;
        columns = &outer_columns;
        nulls = outer_nulls;
    }

    void clear() {
        record = nullptr;
        columns = nullptr;
        nulls = nullptr;
    }

    bool is_bound() const { return record != nullptr && columns != nullptr; }

    bool find(const TabCol &target, Operand &result) const {
        result = {};
        if (!is_bound()) return false;
        for (size_t i = 0; i < columns->size(); ++i) {
            const auto &column = (*columns)[i];
            if (column.name != target.col_name ||
                (!target.tab_name.empty() && column.tab_name != target.tab_name)) {
                continue;
            }
            result.data = record->data + column.offset;
            result.len = column.len;
            result.type = column.type;
            result.is_null = nulls != nullptr && i < nulls->size() && (*nulls)[i];
            result.found = true;
            return true;
        }
        return false;
    }
};

namespace correlated_executor_detail {

inline int compare_bytes(const CorrelatedTupleContext::Operand &lhs,
                         const CorrelatedTupleContext::Operand &rhs) {
    if (lhs.type != rhs.type) {
        throw IncompatibleTypeError(coltype2str(lhs.type), coltype2str(rhs.type));
    }
    if (lhs.type == TYPE_INT) {
        const int a = load_unaligned<int>(lhs.data);
        const int b = load_unaligned<int>(rhs.data);
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    if (lhs.type == TYPE_FLOAT) {
        const float a = load_unaligned<float>(lhs.data);
        const float b = load_unaligned<float>(rhs.data);
        return a < b ? -1 : (a > b ? 1 : 0);
    }

    // CHAR values are zero padded.  Comparing the common prefix and treating
    // bytes beyond the shorter declaration as zero avoids reading past an
    // outer column when two compatible CHAR columns have different lengths.
    const int common = std::min(lhs.len, rhs.len);
    const int prefix_cmp = std::memcmp(lhs.data, rhs.data, common);
    if (prefix_cmp != 0) return prefix_cmp < 0 ? -1 : 1;
    for (int i = common; i < lhs.len; ++i) {
        const unsigned char byte = static_cast<unsigned char>(lhs.data[i]);
        if (byte != 0) return 1;
    }
    for (int i = common; i < rhs.len; ++i) {
        const unsigned char byte = static_cast<unsigned char>(rhs.data[i]);
        if (byte != 0) return -1;
    }
    return 0;
}

inline bool compare_operands(const CorrelatedTupleContext::Operand &lhs,
                             const CorrelatedTupleContext::Operand &rhs, CompOp op) {
    if (op == OP_LIKE) {
        return lhs.type == TYPE_STRING && rhs.type == TYPE_STRING &&
               sql_like_match(lhs.data, lhs.len, rhs.data, rhs.len);
    }
    const int cmp = compare_bytes(lhs, rhs);
    switch (op) {
        case OP_EQ: return cmp == 0;
        case OP_NE: return cmp != 0;
        case OP_LT: return cmp < 0;
        case OP_GT: return cmp > 0;
        case OP_LE: return cmp <= 0;
        case OP_GE: return cmp >= 0;
        case OP_LIKE: return false;
        case OP_IS_NULL:
        case OP_IS_NOT_NULL: return false;
    }
    return false;
}

}  // namespace correlated_executor_detail

/*
 * FilterExecutor variant whose column operands may come from either its child
 * tuple or the currently bound outer tuple.  Analyzer-resolved qualifiers make
 * the normal case unambiguous; for an unqualified target the local child is
 * deliberately searched first, matching SQL's inner-scope shadowing rule.
 *
 * SQL three-valued logic is reduced to the filtering decision required here:
 * if either operand is NULL, the predicate is UNKNOWN and the row is rejected.
 */
class CorrelatedFilterExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> child_;
    ConditionExprPtr predicate_;
    std::shared_ptr<CorrelatedTupleContext> correlated_;
    std::unique_ptr<RmRecord> current_;
    std::vector<bool> current_nulls_;
    bool ended_ = true;

    bool find_child_operand(const TabCol &target, const RmRecord &record,
                            const std::vector<bool> &nulls,
                            CorrelatedTupleContext::Operand &result) const {
        const auto &columns = child_->cols();
        for (size_t i = 0; i < columns.size(); ++i) {
            const auto &column = columns[i];
            if (column.name != target.col_name ||
                (!target.tab_name.empty() && column.tab_name != target.tab_name)) {
                continue;
            }
            result.data = record.data + column.offset;
            result.len = column.len;
            result.type = column.type;
            result.is_null = i < nulls.size() && nulls[i];
            result.found = true;
            return true;
        }
        return false;
    }

    CorrelatedTupleContext::Operand find_operand(const TabCol &target,
                                                  const RmRecord &record,
                                                  const std::vector<bool> &nulls) const {
        CorrelatedTupleContext::Operand result;
        if (find_child_operand(target, record, nulls, result)) return result;
        if (correlated_ != nullptr && correlated_->find(target, result)) return result;
        throw ColumnNotFoundError(target.col_name);
    }

    TruthValue evaluate(const Condition &predicate, const RmRecord &record,
                        const std::vector<bool> &nulls) const {
        const auto lhs = find_operand(predicate.lhs_col, record, nulls);
        if (is_null_test_op(predicate.op)) {
            return evaluate_null_test(lhs.is_null, predicate.op);
        }
        if (lhs.is_null) return TruthValue::UNKNOWN_VALUE;

        CorrelatedTupleContext::Operand rhs;
        if (predicate.is_rhs_val) {
            if (predicate.rhs_val.raw == nullptr) {
                throw InternalError("Correlated predicate literal has no raw value");
            }
            rhs.data = predicate.rhs_val.raw->data;
            rhs.len = predicate.rhs_val.raw->size;
            rhs.type = predicate.rhs_val.type;
            rhs.found = true;
        } else {
            rhs = find_operand(predicate.rhs_col, record, nulls);
            if (rhs.is_null) return TruthValue::UNKNOWN_VALUE;
        }
        return correlated_executor_detail::compare_operands(lhs, rhs, predicate.op)
                   ? TruthValue::TRUE_VALUE
                   : TruthValue::FALSE_VALUE;
    }

    bool matches(const RmRecord &record, const std::vector<bool> &nulls) const {
        return evaluate_bool_expr(predicate_, [&](const Condition &predicate) {
                   return evaluate(predicate, record, nulls);
               }) == TruthValue::TRUE_VALUE;
    }

    void seek_match() {
        current_.reset();
        current_nulls_.clear();
        while (!child_->is_end()) {
            std::vector<bool> candidate_nulls;
            if (const auto *mask = child_->null_mask(); mask != nullptr) candidate_nulls = *mask;
            auto candidate = child_->Next();
            if (candidate == nullptr) {
                throw InternalError("Correlated filter child returned no current tuple");
            }
            if (matches(*candidate, candidate_nulls)) {
                current_ = std::move(candidate);
                current_nulls_ = std::move(candidate_nulls);
                ended_ = false;
                return;
            }
            child_->nextTuple();
        }
        ended_ = true;
    }

   public:
    CorrelatedFilterExecutor(std::unique_ptr<AbstractExecutor> child,
                             ConditionExprPtr predicate,
                             std::shared_ptr<CorrelatedTupleContext> correlated)
        : child_(std::move(child)), predicate_(std::move(predicate)),
          correlated_(std::move(correlated)) {
        if (child_ == nullptr || correlated_ == nullptr) {
            throw InternalError("CorrelatedFilterExecutor requires child and context");
        }
    }

    CorrelatedFilterExecutor(std::unique_ptr<AbstractExecutor> child,
                             std::vector<Condition> predicates,
                             std::shared_ptr<CorrelatedTupleContext> correlated)
        : CorrelatedFilterExecutor(
              std::move(child), SeqScanExecutor::conditions_to_expr(std::move(predicates)),
              std::move(correlated)) {}

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
