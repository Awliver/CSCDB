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
#include "common/common.h"
#include "index/ix.h"
#include "system/sm.h"

class AbstractExecutor {
   public:
    Rid _abstract_rid;

    Context *context_;

    virtual ~AbstractExecutor() = default;

    virtual size_t tupleLen() const { return 0; };

    virtual const std::vector<ColMeta> &cols() const {
        std::vector<ColMeta> *_cols = nullptr;
        return *_cols;
    };

    virtual std::string getType() { return "AbstractExecutor"; };

    /* 当前输出行的 NULL 掩码（按 cols() 顺序）；nullptr = 无 NULL（默认）。
       目前仅空集聚合产生 NULL，经 Projection 转发到 wire 层以 present=0 发出。 */
    virtual const std::vector<bool> *null_mask() const { return nullptr; }

    virtual void beginTuple(){};

    virtual void nextTuple(){};

    virtual bool is_end() const { return true; };

    virtual Rid &rid() = 0;

    virtual std::unique_ptr<RmRecord> Next() = 0;

    virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta();};

    /* 输出行是否保证按 col 升序（序依赖优化的资格询问，如 MIN 索引早停）。
     * 保守默认否；目前仅 IndexScan 在"col 落在 EQ 前缀内或恰为首个非 EQ 索引列"
     * 时应答是。 */
    virtual bool sorted_asc_on(const TabCol &col) const { return false; }

    std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols, const TabCol &target) {
        auto pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
            return col.name == target.col_name &&
                   (target.tab_name.empty() || col.tab_name == target.tab_name);
        });
        if (pos == rec_cols.end()) {
            throw ColumnNotFoundError(target.col_name);
        }
        return pos;
    }
};