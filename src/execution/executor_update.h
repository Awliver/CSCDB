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
#include "common/output_control.h"
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
    // 优化 4：跨 rid 复用 page handle，直接写 slot
    int cached_page_no_ = -1;
    Page *cached_page_ = nullptr;
    char *cached_slots_ = nullptr;
    int record_size_ = 0;

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
        record_size_ = fh_->get_file_hdr().record_size;
    }

    void release_cached_page() {
        if (cached_page_) {
            sm_manager_->get_bpm()->unpin_page(cached_page_->get_page_id(), true);
            cached_page_ = nullptr;
            cached_page_no_ = -1;
            cached_slots_ = nullptr;
        }
    }

    char *get_slot_ptr(const Rid &rid) {
        if (rid.page_no != cached_page_no_) {
            release_cached_page();
            RmPageHandle handle = fh_->fetch_page_handle(rid.page_no);
            cached_page_ = handle.page;
            cached_page_no_ = rid.page_no;
            cached_slots_ = handle.slots;
        }
        return cached_slots_ + rid.slot_no * record_size_;
    }

    // 题9：把一条 SET 子句应写入的值放到 dest_field。算术增量 v=v+字面量 从 base_rec 读取右侧列基值。
    void apply_set_value(const char *base_rec, char *dest_field, const SetClause &set, const ColMeta &col) {
        if (!set.is_arith) {
            memcpy(dest_field, set.rhs.raw->data, col.len);
            return;
        }
        auto rcol = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                                 [&](const ColMeta &c) { return c.name == set.rhs_col; });
        if (rcol == tab_.cols.end()) return;
        if (rcol->type == TYPE_FLOAT) {
            float base = *(const float *)(base_rec + rcol->offset);
            float delta = set.arith_neg ? -set.rhs.float_val : set.rhs.float_val;
            *(float *)dest_field = base + delta;
        } else {
            int base = *(const int *)(base_rec + rcol->offset);
            int delta = set.arith_neg ? -set.rhs.int_val : set.rhs.int_val;
            *(int *)dest_field = base + delta;
        }
    }

    // 识别 col=col±字面量（int/float），可走 mvcc_write_col_delta 避免整行拷贝
    struct ColArithDelta {
        bool hit = false;
        int col_off = -1;
        ColType col_type = TYPE_INT;
        float delta_f = 0.f;
        int delta_i = 0;
    };

    ColArithDelta detect_col_arith_delta() const {
        ColArithDelta r;
        if (set_clauses_.size() != 1) return r;
        const auto &set = set_clauses_[0];
        if (!set.is_arith || set.lhs.col_name != set.rhs_col) return r;
        auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                                   [&](const ColMeta &c) { return c.name == set.lhs.col_name; });
        if (col_it == tab_.cols.end()) return r;
        if (col_it->type == TYPE_FLOAT && col_it->len == (int)sizeof(float)) {
            r.hit = true;
            r.col_off = col_it->offset;
            r.col_type = TYPE_FLOAT;
            r.delta_f = set.arith_neg ? -set.rhs.float_val : set.rhs.float_val;
        } else if (col_it->type == TYPE_INT && col_it->len == (int)sizeof(int)) {
            r.hit = true;
            r.col_off = col_it->offset;
            r.col_type = TYPE_INT;
            r.delta_i = set.arith_neg ? -set.rhs.int_val : set.rhs.int_val;
        }
        return r;
    }

    /**
     * @description: 遍历所有匹配的 rid，对每条记录应用 SET 修改后写回
     */
    std::unique_ptr<RmRecord> Next() override {
        for (const auto &rid : rids_) {
            char *slot = get_slot_ptr(rid);
            std::vector<char> orig_rec(slot, slot + record_size_);
            const bool mvcc_path = context_ && context_->txn_mgr_ && context_->txn_ &&
                                   context_->txn_mgr_->needs_versioning(context_->txn_, tab_name_);
            std::string mvcc_effective;

            // 显式事务 MVCC 写：行锁与 delete 对称，持有到 commit/abort
            if (context_ && context_->lock_mgr_ && context_->txn_ && context_->txn_->get_txn_mode() &&
                mvcc_path) {
                context_->lock_mgr_->lock_exclusive_on_record(context_->txn_, rid, fh_->GetFd());
            }

            // 题9 MVCC：在改动 slot 之前做写写冲突检测 + 登记未提交版本
            if (mvcc_path) {
                std::string visible;
                if (!context_->txn_mgr_->mvcc_read(context_->txn_, tab_name_, rid,
                                                     slot, record_size_, visible)) {
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                    AbortReason::DEADLOCK_PREVENTION);
                }
                ColArithDelta col_delta = detect_col_arith_delta();
                bool write_ok = false;
                if (col_delta.hit) {
                    write_ok = context_->txn_mgr_->mvcc_write_col_delta(
                        context_->txn_, tab_name_, rid, visible.data(), record_size_,
                        col_delta.col_off, col_delta.col_type, col_delta.delta_f, col_delta.delta_i,
                        &mvcc_effective);
                } else {
                    std::vector<char> mv_old(record_size_);
                    memcpy(mv_old.data(), visible.data(), record_size_);
                    std::vector<char> mv_new = mv_old;
                    for (const auto &set : set_clauses_) {
                        auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                                                   [&](const ColMeta &c) { return c.name == set.lhs.col_name; });
                        if (col_it == tab_.cols.end()) continue;
                        apply_set_value(mv_old.data(), mv_new.data() + col_it->offset, set, *col_it);
                    }
                    write_ok = context_->txn_mgr_->mvcc_write(context_->txn_, tab_name_, rid,
                                                              mv_old.data(), mv_new.data(), record_size_, false,
                                                              &mvcc_effective);
                }
                if (!write_ok) {
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                    AbortReason::DEADLOCK_PREVENTION);
                }
                // 未提交版本仅存 MVCC 链/overlay；堆在 commit 时物化，避免 overlay 释放后脏堆暴露
                if (context_->txn_mgr_->is_ser(context_->txn_)) {
                    bool d1 = context_->txn_mgr_->ser_write_check(context_->txn_, tab_name_, rid, visible.data());
                    bool d2 = context_->txn_mgr_->ser_write_check(context_->txn_, tab_name_, rid, mvcc_effective.data());
                    if (d1 || d2)
                        throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                        AbortReason::DEADLOCK_PREVENTION);
                }
            }

            // 题3：先识别 SET 受影响的索引列
            std::vector<char> old_data;
            bool need_index_sync = !tab_.indexes.empty();
            if (need_index_sync) {
                old_data = orig_rec;
            }

            // 题3 测试点 3：先模拟应用 SET 到 new_data，检查唯一索引违反
            if (need_index_sync) {
                std::vector<char> new_data;
                if (mvcc_path) {
                    new_data.assign(mvcc_effective.data(), mvcc_effective.data() + record_size_);
                } else {
                    new_data = old_data;
                    for (const auto &set : set_clauses_) {
                        auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                                                   [&](const ColMeta &c) { return c.name == set.lhs.col_name; });
                        if (col_it == tab_.cols.end()) continue;
                        apply_set_value(old_data.data(), new_data.data() + col_it->offset, set, *col_it);
                    }
                }
                bool violated = false;
                for (auto &index : tab_.indexes) {
                    // 检查新 key 是否会与已有键冲突（且不是自己）
                    bool touches = false;
                    for (auto &idx_col : index.cols) {
                        for (auto &sc : set_clauses_) {
                            if (sc.lhs.col_name == idx_col.name) { touches = true; break; }
                        }
                        if (touches) break;
                    }
                    if (!touches) continue;

                    std::vector<char> new_key(index.col_tot_len);
                    int offset = 0;
                    for (auto &idx_col : index.cols) {
                        memcpy(new_key.data() + offset, new_data.data() + idx_col.offset, idx_col.len);
                        offset += idx_col.len;
                    }
                    auto ih = sm_manager_->ihs_.at(
                        sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                    std::vector<Rid> found;
                    if (ih->get_value(new_key.data(), &found, context_ ? context_->txn_ : nullptr)) {
                        // 已存在——若不是自己，违反唯一性
                        if (found[0].page_no != rid.page_no || found[0].slot_no != rid.slot_no) {
                            violated = true;
                            break;
                        }
                    }
                }
                if (violated) {
                    append_output_file("failure\n");
                    continue;  // 跳过这条 rid 的更新
                }
            }

            // 第一遍：在改 slot 之前，把所有"受影响索引"的旧 key 删掉
            for (auto &index : tab_.indexes) {
                bool touches = false;
                for (auto &idx_col : index.cols) {
                    for (auto &sc : set_clauses_) {
                        if (sc.lhs.col_name == idx_col.name) { touches = true; break; }
                    }
                    if (touches) break;
                }
                if (!touches) continue;
                std::vector<char> old_key(index.col_tot_len);
                int offset = 0;
                for (auto &idx_col : index.cols) {
                    memcpy(old_key.data() + offset, old_data.data() + idx_col.offset, idx_col.len);
                    offset += idx_col.len;
                }
                auto ih = sm_manager_->ihs_.at(
                    sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                ih->delete_entry(old_key.data(), context_ ? context_->txn_ : nullptr);
            }

            // 应用 SET 子句到 slot（非 MVCC 路径）
            if (!mvcc_path) {
            for (const auto &set : set_clauses_) {
                auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                                           [&](const ColMeta &c) { return c.name == set.lhs.col_name; });
                if (col_it == tab_.cols.end()) continue;
                apply_set_value(orig_rec.data(), slot + col_it->offset, set, *col_it);
            }
            }

            // 题10 WAL：记录更新前后镜像
            if (context_ && context_->log_mgr_ && context_->txn_) {
                RmRecord old_rec(record_size_), new_rec(record_size_);
                memcpy(old_rec.data, orig_rec.data(), record_size_);
                if (mvcc_path) memcpy(new_rec.data, mvcc_effective.data(), record_size_);
                else memcpy(new_rec.data, slot, record_size_);
                Rid r = rid;
                UpdateLogRecord lr(context_->txn_->get_transaction_id(), old_rec, new_rec, r, tab_name_);
                context_->txn_->set_prev_lsn(context_->log_mgr_->add_log_to_buffer(&lr));
            }

            if (context_ && context_->txn_ && context_->txn_mgr_ &&
                context_->txn_mgr_->uses_si_fast_path(context_->txn_)) {
                RmRecord wr_old(record_size_);
                memcpy(wr_old.data, orig_rec.data(), record_size_);
                context_->txn_->append_write_record(new WriteRecord(WType::UPDATE_TUPLE, tab_name_, rid, wr_old));
            }

            // 第二遍：把新 key 插回受影响的索引
            for (auto &index : tab_.indexes) {
                bool touches = false;
                for (auto &idx_col : index.cols) {
                    for (auto &sc : set_clauses_) {
                        if (sc.lhs.col_name == idx_col.name) { touches = true; break; }
                    }
                    if (touches) break;
                }
                if (!touches) continue;
                std::vector<char> new_key(index.col_tot_len);
                int offset = 0;
                for (auto &idx_col : index.cols) {
                    const char *src = mvcc_path ? mvcc_effective.data() : slot;
                    memcpy(new_key.data() + offset, src + idx_col.offset, idx_col.len);
                    offset += idx_col.len;
                }
                auto ih = sm_manager_->ihs_.at(
                    sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                ih->insert_entry(new_key.data(), rid, context_ ? context_->txn_ : nullptr);
            }
        }
        release_cached_page();
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }

    ~UpdateExecutor() override { release_cached_page(); }
};
