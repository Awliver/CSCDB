/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include "executor_abstract.h"
#include "executor_seq_scan.h"

// 对子计划的完整输出行求值。Filter 和 Scan 过滤分开，使 Planner 能在
// 外连接边界上保留 WHERE 的求值位置。
class FilterExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<Condition> conds_;
    std::unique_ptr<RmRecord> current_;
    std::vector<bool> current_nulls_;
    bool ended_ = true;

    bool eval_cond(const Condition &cond, const RmRecord &record) {
        const auto &cols = child_->cols();
        auto lhs = get_col(cols, cond.lhs_col);
        const auto *child_nulls = child_->null_mask();
        const auto lhs_index = static_cast<size_t>(lhs - cols.begin());
        if (child_nulls != nullptr && lhs_index < child_nulls->size() && (*child_nulls)[lhs_index]) {
            return false;
        }

        const char *rhs_data = nullptr;
        if (cond.is_rhs_val) {
            rhs_data = cond.rhs_val.raw->data;
        } else {
            auto rhs = get_col(cols, cond.rhs_col);
            const auto rhs_index = static_cast<size_t>(rhs - cols.begin());
            if (child_nulls != nullptr && rhs_index < child_nulls->size() && (*child_nulls)[rhs_index]) {
                return false;
            }
            rhs_data = record.data + rhs->offset;
        }
        return SeqScanExecutor::compare_value(record.data + lhs->offset, rhs_data,
                                              lhs->len, lhs->type, cond.op);
    }

    bool matches(const RmRecord &record) {
        for (const auto &cond : conds_) {
            if (!eval_cond(cond, record)) return false;
        }
        return true;
    }

    void seek_match() {
        current_.reset();
        current_nulls_.clear();
        while (!child_->is_end()) {
            auto record = child_->Next();
            if (matches(*record)) {
                if (const auto *mask = child_->null_mask(); mask != nullptr) current_nulls_ = *mask;
                current_ = std::move(record);
                ended_ = false;
                return;
            }
            child_->nextTuple();
        }
        ended_ = true;
    }

   public:
    FilterExecutor(std::unique_ptr<AbstractExecutor> child, std::vector<Condition> conds)
        : child_(std::move(child)), conds_(std::move(conds)) {}

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
        return current_nulls_.empty() ? nullptr : &current_nulls_;
    }
};
