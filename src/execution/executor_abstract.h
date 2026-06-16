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
#include <chrono>
#include <fstream>

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

    virtual void beginTuple(){};

    virtual void nextTuple(){};

    virtual bool is_end() const { return true; };

    virtual Rid &rid() = 0;

    virtual std::unique_ptr<RmRecord> Next() = 0;

    virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta();};

    std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols, const TabCol &target) {
        auto pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
            return col.name == target.col_name &&
                   (target.tab_name.empty() || col.tab_name == target.tab_name);
        });
        if (pos == rec_cols.end()) {
            // #region agent log
            std::ofstream ofs("/home/neo/CSC_DB/db2026/.cursor/debug-b42dcf.log", std::ios::app);
            if (ofs.is_open()) {
                const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
                ofs << "{\"sessionId\":\"b42dcf\",\"runId\":\"run1\",\"hypothesisId\":\"H4\",\"location\":"
                    << "\"executor_abstract.h:54\",\"message\":\"get_col not found\",\"data\":\"target_tab="
                    << target.tab_name << ",target_col=" << target.col_name << "\",\"timestamp\":" << ts
                    << "}\n";
            }
            // #endregion
            throw ColumnNotFoundError(target.col_name);
        }
        return pos;
    }
};