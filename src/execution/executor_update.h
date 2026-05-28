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
#include "system/sm.h"

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }

    /**
     * @description: 遍历所有匹配的 rid，对每条记录应用 SET 修改后写回
     */
    std::unique_ptr<RmRecord> Next() override {
        int record_size = fh_->get_file_hdr().record_size;

        for (const auto &rid : rids_) {
            // 读出旧记录
            auto old_rec = fh_->get_record(rid, context_);

            // 构造新记录
            RmRecord new_rec(record_size);
            memcpy(new_rec.data, old_rec->data, record_size);

            for (const auto &set : set_clauses_) {
                // 查找该列在 tab_ 中的元数据
                auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                                           [&](const ColMeta &c) {
                                               return c.name == set.lhs.col_name;
                                           });
                if (col_it == tab_.cols.end()) continue;  // analyze 应已校验

                // 把 SET 值拷贝到对应字段位置
                memcpy(new_rec.data + col_it->offset,
                       set.rhs.raw->data,
                       col_it->len);
            }

            // 写回磁盘
            fh_->update_record(rid, new_rec.data, context_);
        }

        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};
