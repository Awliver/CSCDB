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
#include <fstream>
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class InsertExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Value> values_;     // 需要插入的数据
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::string tab_name_;          // 表名称
    Rid rid_;                       // 插入的位置，由于系统默认插入时不指定位置，因此当前rid_在插入后才赋值
    SmManager *sm_manager_;

   public:
    InsertExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Value> values, Context *context) {
        sm_manager_ = sm_manager;
        tab_ = sm_manager_->db_.get_table(tab_name);
        values_ = values;
        tab_name_ = tab_name;
        if (values.size() != tab_.cols.size()) {
            throw InvalidValueCountError();
        }
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        context_ = context;
    };

    std::unique_ptr<RmRecord> Next() override {
        // Make record buffer
        RmRecord rec(fh_->get_file_hdr().record_size);
        for (size_t i = 0; i < values_.size(); i++) {
            auto &col = tab_.cols[i];
            auto &val = values_[i];
            if (col.type != val.type) {
                throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
            }
            val.init_raw(col.len);
            memcpy(rec.data + col.offset, val.raw->data, col.len);
        }

        // 题3 测试点 3：唯一索引检查——若任一索引下 key 已存在，写 failure 并跳过整条 INSERT
        for (auto& index : tab_.indexes) {
            auto ih = sm_manager_->ihs_.at(
                sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            std::vector<char> key(index.col_tot_len);
            int offset = 0;
            for (auto& idx_col : index.cols) {
                memcpy(key.data() + offset, rec.data + idx_col.offset, idx_col.len);
                offset += idx_col.len;
            }
            std::vector<Rid> dummy;
            if (ih->get_value(key.data(), &dummy, context_ ? context_->txn_ : nullptr)) {
                // 唯一性违反
                std::fstream outfile;
                outfile.open("output.txt", std::ios::out | std::ios::app);
                outfile << "failure\n";
                outfile.close();
                return nullptr;
            }
        }

        // Insert into record file
        rid_ = fh_->insert_record(rec.data, context_);

        // 题9：有活跃显式事务时，登记为未提交插入版本（提交后才对他人可见，回滚则物理删除）
        if (context_ && context_->txn_mgr_ && context_->txn_ && context_->txn_mgr_->mvcc_should_version()) {
            context_->txn_mgr_->mvcc_insert(context_->txn_, tab_name_, rid_, rec.data,
                                            (int)fh_->get_file_hdr().record_size);
            // 题9 SER：新插入记录 vs 其他事务谓词读 → rw 反依赖；成 SSI 危险结构则 abort
            if (context_->txn_mgr_->is_ser(context_->txn_) &&
                context_->txn_mgr_->ser_write_check(context_->txn_, tab_name_, rid_, rec.data)) {
                throw TransactionAbortException(context_->txn_->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
            }
        }

        // Insert into index
        for(size_t i = 0; i < tab_.indexes.size(); ++i) {
            auto& index = tab_.indexes[i];
            auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            char* key = new char[index.col_tot_len];
            int offset = 0;
            for(size_t i = 0; i < index.col_num; ++i) {
                memcpy(key + offset, rec.data + index.cols[i].offset, index.cols[i].len);
                offset += index.cols[i].len;
            }
            ih->insert_entry(key, rid_, context_->txn_);
            delete[] key;
        }
        return nullptr;
    }
    Rid &rid() override { return rid_; }
};