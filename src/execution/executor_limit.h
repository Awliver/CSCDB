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
#include "executor_abstract.h"

class LimitExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> prev_;
    size_t limit_;
    size_t count_ = 0;

public:
    LimitExecutor(std::unique_ptr<AbstractExecutor> prev, size_t limit)
        : prev_(std::move(prev)), limit_(limit) {}

    void beginTuple() override {
        prev_->beginTuple();
        count_ = 0;
    }

    void nextTuple() override {
        if (count_ >= limit_) return;
        prev_->nextTuple();
        count_++;
    }

    bool is_end() const override {
        return count_ >= limit_ || prev_->is_end();
    }

    std::unique_ptr<RmRecord> Next() override {
        return prev_->Next();
    }

    const std::vector<ColMeta> &cols() const override {
        return prev_->cols();
    }

    size_t tupleLen() const override { return prev_->tupleLen(); }
    Rid &rid() override { return _abstract_rid; }
};
