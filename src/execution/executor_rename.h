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
#include <memory>
#include <vector>

#include "executor_abstract.h"

/*
 * A zero-copy schema renamer for derived tables.  Column names/qualifiers and
 * other descriptive metadata come from output_schema.  The child's physical
 * offsets are retained because tuples and NULL masks are forwarded unchanged.
 */
class RenameExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<ColMeta> cols_;

   public:
    RenameExecutor(std::unique_ptr<AbstractExecutor> child,
                   std::vector<ColMeta> output_schema)
        : child_(std::move(child)), cols_(std::move(output_schema)) {
        if (child_ == nullptr) throw InternalError("RenameExecutor requires a child");
        const auto &input_schema = child_->cols();
        if (cols_.size() != input_schema.size()) {
            throw InternalError("RenameExecutor schema width mismatch");
        }
        for (size_t i = 0; i < cols_.size(); ++i) {
            if (cols_[i].type != input_schema[i].type || cols_[i].len != input_schema[i].len) {
                throw InternalError("RenameExecutor schema type mismatch");
            }
            // Forwarded records retain the child's layout.  Callers only need
            // to provide the desired logical names and qualifiers.
            cols_[i].offset = input_schema[i].offset;
        }
    }

    void beginTuple() override { child_->beginTuple(); }
    void nextTuple() override { child_->nextTuple(); }
    bool is_end() const override { return child_->is_end(); }
    std::unique_ptr<RmRecord> Next() override { return child_->Next(); }
    const std::vector<bool> *null_mask() const override { return child_->null_mask(); }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return child_->tupleLen(); }
    Rid &rid() override { return child_->rid(); }

    bool sorted_asc_on(const TabCol &target) const override {
        auto output = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &column) {
            return column.name == target.col_name &&
                   (target.tab_name.empty() || column.tab_name == target.tab_name);
        });
        if (output == cols_.end()) return false;
        const size_t index = static_cast<size_t>(output - cols_.begin());
        const auto &input = child_->cols()[index];
        TabCol input_target;
        input_target.tab_name = input.tab_name;
        input_target.col_name = input.name;
        return child_->sorted_asc_on(input_target);
    }
};
