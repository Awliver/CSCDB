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
#include "common/output_control.h"
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
        // 题9：values_ 可含多行(平铺)，总数须为列数整数倍
        if (tab_.cols.empty() || values.size() % tab_.cols.size() != 0) {
            throw InvalidValueCountError();
        }
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        context_ = context;
    };

    std::unique_ptr<RmRecord> Next() override {
        // 题9：values_ 平铺多行，按表列数分块逐行插入；单行时 nrows_=1，行为不变
        size_t ncols_ = tab_.cols.size();
        size_t nrows_ = ncols_ ? values_.size() / ncols_ : 0;
        for (size_t row_ = 0; row_ < nrows_; ++row_) {
        // Make record buffer
        RmRecord rec(fh_->get_file_hdr().record_size);
        for (size_t i = 0; i < ncols_; i++) {
            auto &col = tab_.cols[i];
            auto &val = values_[row_ * ncols_ + i];
            if (col.type != val.type) {
                throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
            }
            val.init_raw(col.len);
            memcpy(rec.data + col.offset, val.raw->data, col.len);
        }

        // 题9 删-插写写冲突：插入的键正被并发删除(他人未提交删 / 本事务快照后已提交删)
        // → first-updater-wins → abort。检查经被删键索引 O(1) 点查（原全链扫描占 87% CPU）。
        if (context_ && context_->txn_mgr_ && context_->txn_ && context_->txn_mgr_->needs_versioning(context_->txn_, tab_name_) &&
            !tab_.cols.empty()) {
            auto &kcol = tab_.cols[0];
            MvccWriteResult conflict = context_->txn_mgr_->mvcc_insert_key_conflict(
                context_->txn_, tab_name_, rec.data, kcol.offset, kcol.len);
            if (conflict != MvccWriteResult::OK) {
                throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                abort_reason_from(conflict));
            }
        }

        // 题3 唯一索引检查 + 题9 MVCC 感知：索引项可能指向本事务快照下已删(不可见)的旧记录，
        // 此时同事务可重插同键。仅当存在对本事务仍可见的同键记录才算真唯一冲突。
        std::vector<std::vector<char>> index_keys(tab_.indexes.size());
        std::vector<IxLeafHint> index_hints(tab_.indexes.size());
        for (size_t index_no = 0; index_no < tab_.indexes.size(); ++index_no) {
            auto& index = tab_.indexes[index_no];
            auto ih = sm_manager_->ihs_.at(
                sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            auto &key = index_keys[index_no];
            key.resize(index.col_tot_len);
            int offset = 0;
            for (auto& idx_col : index.cols) {
                memcpy(key.data() + offset, rec.data + idx_col.offset, idx_col.len);
                offset += idx_col.len;
            }
            std::vector<Rid> existing;
            if (ih->get_value(key.data(), &existing, context_ ? context_->txn_ : nullptr,
                              &index_hints[index_no])) {
                bool conflict = true;
                bool mvcc = context_ && context_->txn_mgr_ && context_->txn_ &&
                            context_->txn_mgr_->needs_versioning(context_->txn_, tab_name_);
                if (!mvcc) {
                    // 非 MVCC 模式同样可能遇到陈旧索引项（checkpoint 强制清堆 + 崩溃
                    // 恢复后，磁盘索引可能残留指向已释放槽位的项）：同键项全部指向
                    // 死槽 → 不构成唯一冲突，下方清掉陈旧项后正常插入。
                    conflict = false;
                    for (auto &er : existing) {
                        if (fh_->is_record(er)) { conflict = true; break; }
                    }
                }
                if (mvcc) {
                    conflict = false;
                    bool other_writer = false;
                    int rsz = (int)fh_->get_file_hdr().record_size;
                    for (auto& er : existing) {
                        if (!fh_->is_record(er)) continue;
                        // 先判可见性：同键记录在本事务快照下可见 → 真唯一冲突 → failure。
                        // INSERT 无旧版本(spec 1755)，与并发删/写同键不构成写写冲突；可见冲突优先于 abort。
                        auto erec = fh_->get_record(er, context_);
                        std::string vbuf;
                        if (context_->txn_mgr_->mvcc_read(context_->txn_, tab_name_, er,
                                                          erec->data, rsz, vbuf)) {
                            conflict = true; break;   // 同键记录对本事务仍可见 → 真唯一冲突
                        }
                        // 不可见但有其他活跃事务正写同键(未提交插入) → 记下，循环后无可见冲突再 abort
                        if (context_->txn_mgr_->mvcc_other_writer(tab_name_, er,
                                                                  context_->txn_->get_transaction_id())) {
                            other_writer = true;
                        }
                    }
                    if (!conflict && other_writer) {
                        throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                        AbortReason::ACTIVE_WRITE_CONFLICT);
                    }
                }
                if (conflict) {
                    append_output_file("failure\n");
                    return nullptr;
                }
                // 同键记录均已删(不可见)/项已陈旧：清掉旧索引项，下方再插入新项
                ih->delete_entry(key.data(), context_ ? context_->txn_ : nullptr);
            }
        }

        // Insert into record file
        // MVCC：先占槽并登记版本链，再 publish bitmap——避免 dirty 门未开时他事务直读堆看见未提交插入
        bool do_mvcc = context_ && context_->txn_mgr_ && context_->txn_ &&
                       context_->txn_mgr_->needs_versioning(context_->txn_, tab_name_);
        bool reserved = false;
        if (do_mvcc) {
            rid_ = fh_->reserve_insert_slot();
            reserved = true;
            try {
                if (context_->log_mgr_) {
                    InsertLogRecord lr(context_->txn_->get_transaction_id(), rec, rid_, tab_name_);
                    context_->txn_->set_prev_lsn(context_->log_mgr_->add_log_to_buffer(&lr));
                }
                context_->txn_mgr_->mvcc_insert(context_->txn_, tab_name_, rid_, rec.data,
                                                (int)fh_->get_file_hdr().record_size);
                fh_->publish_insert_slot(rid_, rec.data);
                reserved = false;
                if (context_->txn_mgr_->is_ser(context_->txn_) &&
                    context_->txn_mgr_->ser_write_check(context_->txn_, tab_name_, rid_, rec.data)) {
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                    AbortReason::SSI_DANGEROUS_STRUCTURE);
                }
            } catch (...) {
                if (reserved) fh_->cancel_insert_slot(rid_);
                throw;
            }
        } else {
            if (context_ && context_->log_mgr_ && context_->txn_) {
                rid_ = fh_->reserve_insert_slot();
                bool heap_reserved = true;
                try {
                    InsertLogRecord lr(context_->txn_->get_transaction_id(), rec, rid_, tab_name_);
                    context_->txn_->set_prev_lsn(context_->log_mgr_->add_log_to_buffer(&lr));
                    fh_->publish_insert_slot(rid_, rec.data);
                    heap_reserved = false;
                } catch (...) {
                    if (heap_reserved) fh_->cancel_insert_slot(rid_);
                    throw;
                }
            } else {
                rid_ = fh_->insert_record(rec.data, context_);
            }
            if (context_ && context_->txn_ && context_->txn_mgr_ &&
                context_->txn_mgr_->uses_si_fast_path(context_->txn_)) {
                context_->txn_->append_write_record(new WriteRecord(WType::INSERT_TUPLE, tab_name_, rid_, rec));
            }
        }

        // Insert into index
        for (size_t i = 0; i < tab_.indexes.size(); ++i) {
            auto& index = tab_.indexes[i];
            auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            auto &key = index_keys[i];
            ih->insert_entry(key.data(), rid_, context_->txn_, nullptr, &index_hints[i]);
        }
        }   // 题9：多行 insert 行循环结束
        return nullptr;
    }
    Rid &rid() override { return rid_; }
};
