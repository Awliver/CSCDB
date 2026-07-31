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
    bool cached_page_dirty_ = false;
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
            // MVCC UPDATE 只在版本链/overlay 中生成新版本，堆页由 COMMIT 物化时
            // 另行标脏；自赋值连字节都不变。旧代码在这里无条件 dirty=true，
            // 会把每次 Stock 锁定读到的随机堆页都变成脏页，W=50 时造成无意义
            // cleaner/淘汰写盘，并可能把瞬时换页压力升级成语句 ERROR。
            sm_manager_->get_bpm()->unpin_page(cached_page_->get_page_id(), cached_page_dirty_);
            cached_page_ = nullptr;
            cached_page_no_ = -1;
            cached_slots_ = nullptr;
            cached_page_dirty_ = false;
        }
    }

    char *get_slot_ptr(const Rid &rid) {
        // slot 越界硬防线（与 RmFileHandle::check_slot_bounds 同责）：本指针既读又写，
        // 坏 rid 的越界写会踩 BPM 邻帧 id_（陈旧映射的产生源，07-31 canary 实测）
        fh_->check_slot_bounds(rid);
        if (rid.page_no != cached_page_no_) {
            release_cached_page();
            RmPageHandle handle = fh_->fetch_page_handle(rid.page_no);
            cached_page_ = handle.page;
            cached_page_no_ = rid.page_no;
            cached_slots_ = handle.slots;
            cached_page_dirty_ = false;
        }
        return cached_slots_ + rid.slot_no * record_size_;
    }

    // 题9：把一条 SET 子句应写入的值放到 dest_field。算术增量 v=v+字面量 从 base_rec 读取右侧列基值。
    void apply_set_value(const char *base_rec, char *dest_field, const SetClause &set, const ColMeta &col) {
        if (set.self_noop) {
            // 决赛：SET col = col 自赋值——按原值恒等拷贝（任意类型），rhs 不参与
            memcpy(dest_field, base_rec + col.offset, col.len);
            return;
        }
        if (!set.is_arith) {
            memcpy(dest_field, set.rhs.raw->data, col.len);
            return;
        }
        auto rcol = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                                 [&](const ColMeta &c) { return c.name == set.rhs_col; });
        if (rcol == tab_.cols.end()) return;
        if (rcol->type == TYPE_FLOAT) {
            float base = *(const float *)(base_rec + rcol->offset);
            if (!set.chain_f.empty()) {
                // 决赛链式算术：f32 逐步左结合累加（((base±v1)±v2)...，不能折叠）
                for (float t : set.chain_f) base += t;
                *(float *)dest_field = base;
                return;
            }
            float delta = set.arith_neg ? -set.rhs.float_val : set.rhs.float_val;
            *(float *)dest_field = base + delta;
        } else {
            int base = *(const int *)(base_rec + rcol->offset);
            int delta = set.arith_neg ? -set.rhs.int_val : set.rhs.int_val;
            *(int *)dest_field = base + delta;
        }
    }

    // col=col±常数（int/float）→ mvcc_write_col_delta
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
        // float 链式算术无法表示为单增量（左结合逐步 f32），走全记录回退路径
        if (!set.is_arith || set.lhs.col_name != set.rhs_col || !set.chain_f.empty()) return r;
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

    std::vector<MvccColPatch> build_col_patches() const {
        std::vector<MvccColPatch> patches;
        patches.reserve(set_clauses_.size());
        for (const auto &set : set_clauses_) {
            // 自赋值恒等写不产生字节变化，无需 patch（char 列的 arith patch 也无法表达）；
            // 全部子句均为自赋值时 patches 为空，上层回退 mvcc_write 全记录路径，
            // 写冲突/回滚语义保持完整
            if (set.self_noop) continue;
            // float 链式算术 patch 无法表达（MvccColPatch 只有单增量）；一旦出现，整条
            // 语句放弃 patch 路径（返回空），统一回退 mvcc_write 全记录——否则该子句会丢失
            if (!set.chain_f.empty()) return {};
            auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                                       [&](const ColMeta &c) { return c.name == set.lhs.col_name; });
            if (col_it == tab_.cols.end()) continue;
            MvccColPatch p;
            p.offset = col_it->offset;
            p.len = col_it->len;
            p.type = col_it->type;
            p.is_arith = set.is_arith;
            p.arith_neg = set.arith_neg;
            if (set.is_arith) {
                p.rhs_col = set.rhs_col;
                if (col_it->type == TYPE_FLOAT) p.arith_rhs_f = set.rhs.float_val;
                else p.arith_rhs_i = set.rhs.int_val;
            } else {
                p.abs_value.assign(set.rhs.raw->data, col_it->len);
            }
            patches.push_back(std::move(p));
        }
        return patches;
    }

    /**
     * @description: 遍历所有匹配的 rid，对每条记录应用 SET 修改后写回
     */
    std::unique_ptr<RmRecord> Next() override {
        std::vector<Rid> ordered = rids_;
        std::sort(ordered.begin(), ordered.end(), [](const Rid &a, const Rid &b) {
            if (a.page_no != b.page_no) return a.page_no < b.page_no;
            return a.slot_no < b.slot_no;
        });
        for (const auto &rid : ordered) {
            char *slot = get_slot_ptr(rid);
            std::vector<char> orig_rec(slot, slot + record_size_);
            const bool mvcc_path = context_ && context_->txn_mgr_ && context_->txn_ &&
                                   context_->txn_mgr_->needs_versioning(context_->txn_, tab_name_);
            std::string mvcc_effective;
            std::string visible;   // MVCC 快照可见值（含本事务前序语句累计写）——WAL diff 基准

            // 显式事务 MVCC 写：行锁与 delete 对称，持有到 commit/abort
            if (context_ && context_->lock_mgr_ && context_->txn_ && context_->txn_->get_txn_mode() &&
                mvcc_path) {
                if (!context_->lock_mgr_->lock_exclusive_on_record(context_->txn_, rid, fh_->GetFd())) {
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                    AbortReason::DEADLOCK_PREVENTION);
                }
            }

            // 题9 MVCC：在改动 slot 之前做写写冲突检测 + 登记未提交版本
            if (mvcc_path) {
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
                    std::vector<MvccColPatch> patches = build_col_patches();
                    // 单列绝对 int/float（如 d_next_o_id = ?）→ col_delta，rebase 与 hold_writer_to_commit
                    // 与 payment 一致，保证语句后 re-read 可见本事务写入
                    if (patches.size() == 1 && !patches[0].is_arith) {
                        const auto &p = patches[0];
                        if (p.type == TYPE_INT && p.len == (int)sizeof(int) &&
                            (int)p.abs_value.size() >= p.len) {
                            int o = *reinterpret_cast<const int *>(visible.data() + p.offset);
                            int n = *reinterpret_cast<const int *>(p.abs_value.data());
                            write_ok = context_->txn_mgr_->mvcc_write_col_delta(
                                context_->txn_, tab_name_, rid, visible.data(), record_size_,
                                p.offset, TYPE_INT, 0.f, n - o, &mvcc_effective);
                        } else if (p.type == TYPE_FLOAT && p.len == (int)sizeof(float) &&
                                   (int)p.abs_value.size() >= p.len) {
                            float o = *reinterpret_cast<const float *>(visible.data() + p.offset);
                            float n = *reinterpret_cast<const float *>(p.abs_value.data());
                            write_ok = context_->txn_mgr_->mvcc_write_col_delta(
                                context_->txn_, tab_name_, rid, visible.data(), record_size_,
                                p.offset, TYPE_FLOAT, n - o, 0, &mvcc_effective);
                        }
                    }
                    if (!write_ok && set_clauses_.size() > 1 && !patches.empty()) {
                        write_ok = context_->txn_mgr_->mvcc_write_col_patch(
                            context_->txn_, tab_name_, rid, visible.data(), record_size_, patches,
                            &mvcc_effective);
                    } else if (!write_ok) {
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
                            if (fh_->is_record(found[0])) {
                                violated = true;
                                break;
                            }
                            // 陈旧索引项（指向已释放槽位，见 executor_insert 同注释）：
                            // 清除后不构成冲突
                            ih->delete_entry(new_key.data(), context_ ? context_->txn_ : nullptr);
                        }
                    }
                }
                if (violated) {
                    append_output_file("failure\n");
                    continue;  // 跳过这条 rid 的更新
                }
            }

            // 第一遍：在改 slot 之前，把所有"受影响索引"的旧 key 删掉。
            // 仅限非 MVCC（物理）路径——MVCC 路径的旧项必须保留到 commit：其他会话
            // 的快照读要经旧 key 索引项找到 rid、再经版本链取旧版本（语句期就删会让
            // 未提交的 UPDATE 影响他人索引扫描，OJ dirty-read 场景实测丢行）。
            // MVCC 路径的旧项由 commit 物化时删除（有更旧活跃快照则延迟）、abort 时天然保留。
            if (!mvcc_path)
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
                // 非 MVCC 路径确有物理字节变化才标脏；SET col=col 仍完整走锁、冲突
                // 检测、WAL/undo，但不制造一张内容未变的脏页。
                if (memcmp(orig_rec.data(), slot, record_size_) != 0) cached_page_dirty_ = true;
            }

            // 题10 WAL：优先写增量差分；省不到 1/3 或段过多则回退全量镜像。
            // diff/undo 基准必须是【本语句执行前的可见值】——MVCC 路径下即 visible
            //（含本事务前序语句的累计写），绝不能用事务前堆镜像 orig_rec：同一事务
            // 多次触碰同一行时，被前一语句改过又被本语句改回原值的字节会因
            // diff(事务前值, 累计值) "看似未变"而漏出 ranges，恢复按"每字节最后
            // 写者"合成出中间态（OJ 16:47 SUM(s_ytd) 0 ULP 判负实测：float 累加
            // 使 s_ytd 尾数字节 00→80→00 往返，重放停在 80，恢复后 +1.0/行）。
            if (context_ && context_->log_mgr_ && context_->txn_) {
                const bool have_visible = mvcc_path && (int)visible.size() >= record_size_;
                const char* old_ptr = have_visible ? visible.data() : orig_rec.data();
                const char* new_ptr = mvcc_path ? mvcc_effective.data() : slot;
                Rid r = rid;
                UpdateDeltaLogRecord dlr(context_->txn_->get_transaction_id(),
                                        old_ptr, new_ptr, record_size_, r, tab_name_);
                if (dlr.useful()) {
                    context_->txn_->set_prev_lsn(context_->log_mgr_->add_log_to_buffer(&dlr));
                } else {
                    RmRecord old_rec(record_size_), new_rec(record_size_);
                    memcpy(old_rec.data, old_ptr, record_size_);
                    memcpy(new_rec.data, new_ptr, record_size_);
                    UpdateLogRecord lr(context_->txn_->get_transaction_id(), old_rec, new_rec, r, tab_name_);
                    context_->txn_->set_prev_lsn(context_->log_mgr_->add_log_to_buffer(&lr));
                }
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
