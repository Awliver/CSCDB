/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction_manager.h"
#include "recovery/log_manager.h"
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"
#include "index/ix.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <thread>

namespace {

void rollback_index_on_abort(SmManager *sm, const std::string &tab_name, const Rid &rid,
                             WType wtype, const std::string &old_data, const std::string &new_data) {
    if (sm == nullptr) return;
    TabMeta &tab = sm->db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        auto ih = sm->ihs_.at(sm->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        std::vector<char> old_key(index.col_tot_len), new_key(index.col_tot_len);
        int off = 0;
        for (auto &idx_col : index.cols) {
            if (!old_data.empty())
                memcpy(old_key.data() + off, old_data.data() + idx_col.offset, idx_col.len);
            if (!new_data.empty())
                memcpy(new_key.data() + off, new_data.data() + idx_col.offset, idx_col.len);
            off += idx_col.len;
        }
        if (wtype == WType::INSERT_TUPLE) {
            ih->delete_entry(new_key.data(), nullptr);
        } else if (wtype == WType::UPDATE_TUPLE && !old_data.empty() && !new_data.empty() &&
                   memcmp(old_key.data(), new_key.data(), index.col_tot_len) != 0) {
            ih->delete_entry(new_key.data(), nullptr);
            // MVCC 路径的旧项在语句期从未删除（延迟到 commit），此处通常仍在——
            // 仅缺失时补插（物理快路径等遗留场景），避免对已存在 key 重复插入
            std::vector<Rid> found;
            if (!ih->get_value(old_key.data(), &found, nullptr)) {
                ih->insert_entry(old_key.data(), rid, nullptr);
            }
        }
    }
}

/* 已提交删除的索引收尾：按记录字节构造各索引 key 并删除对应索引项。
 * 仅在墓碑链将被整链剪除（无更旧活跃快照）时调用——此后版本链不复存在，
 * 索引项若残留，索引扫描会经它读到已释放槽位的残留字节，令已删行"复活"。
 * 仅当索引项仍指向本 rid 才删：同事务删后重插同 key 时，重插已把索引项
 * 重定向到新 rid（executor_insert 的陈旧项替换），此时绝不能误删新行的项。 */
void remove_index_entries_on_commit(SmManager *sm, const std::string &tab_name, const Rid &rid,
                                    const std::string &data) {
    if (sm == nullptr || data.empty()) return;
    TabMeta &tab = sm->db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        auto ih = sm->ihs_.at(sm->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        std::vector<char> key(index.col_tot_len);
        int off = 0;
        for (auto &idx_col : index.cols) {
            memcpy(key.data() + off, data.data() + idx_col.offset, idx_col.len);
            off += idx_col.len;
        }
        std::vector<Rid> found;
        if (!ih->get_value(key.data(), &found, nullptr)) continue;
        bool points_here = false;
        for (auto &fr : found) {
            if (fr == rid) { points_here = true; break; }
        }
        if (points_here) ih->delete_entry(key.data(), nullptr);
    }
}

/* 已提交 UPDATE 的旧索引项清理：MVCC 路径语句期不删旧项（他人快照读需要），
 * 提交后按"旧 key ≠ 当前 key 且旧项仍指向本 rid"逐索引删除。cur_data 取当前
 * 已提交记录字节：若后续事务把 key 改回旧值，old==cur 判定自然跳过，不会误删。 */
void remove_stale_update_index_entries(SmManager *sm, const std::string &tab_name, const Rid &rid,
                                       const std::string &old_data, const char *cur_data) {
    if (sm == nullptr || old_data.empty() || cur_data == nullptr) return;
    TabMeta &tab = sm->db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        std::vector<char> old_key(index.col_tot_len), cur_key(index.col_tot_len);
        int off = 0;
        for (auto &idx_col : index.cols) {
            memcpy(old_key.data() + off, old_data.data() + idx_col.offset, idx_col.len);
            memcpy(cur_key.data() + off, cur_data + idx_col.offset, idx_col.len);
            off += idx_col.len;
        }
        if (memcmp(old_key.data(), cur_key.data(), index.col_tot_len) == 0) continue;
        auto ih = sm->ihs_.at(sm->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        std::vector<Rid> found;
        if (!ih->get_value(old_key.data(), &found, nullptr)) continue;
        bool points_here = false;
        for (auto &fr : found) {
            if (fr == rid) { points_here = true; break; }
        }
        if (points_here) ih->delete_entry(old_key.data(), nullptr);
    }
}

void restore_index_if_missing(SmManager *sm, const std::string &tab_name, const Rid &rid,
                              const std::string &data) {
    if (sm == nullptr || data.empty()) return;
    TabMeta &tab = sm->db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        auto ih = sm->ihs_.at(sm->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        std::vector<char> key(index.col_tot_len);
        int off = 0;
        for (auto &idx_col : index.cols) {
            memcpy(key.data() + off, data.data() + idx_col.offset, idx_col.len);
            off += idx_col.len;
        }
        std::vector<Rid> found;
        if (!ih->get_value(key.data(), &found, nullptr)) {
            ih->insert_entry(key.data(), rid, nullptr);
        }
    }
}

static const ColMeta *find_col_meta(const TabMeta &meta, const std::string &name) {
    for (const auto &c : meta.cols) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

static void apply_col_patch_abs(const MvccColPatch &p, char *dest) {
    if ((int)p.abs_value.size() >= p.len) {
        memcpy(dest + p.offset, p.abs_value.data(), p.len);
    }
}

static bool apply_col_patch_from_visible(const MvccColPatch &p, const char *visible,
                                         char *dest, int len, const TabMeta &meta) {
    if (p.offset + p.len > len) return false;
    if (!p.is_arith) {
        apply_col_patch_abs(p, dest);
        return true;
    }
    const ColMeta *rcol = find_col_meta(meta, p.rhs_col);
    if (rcol == nullptr) return false;
    if (p.type == TYPE_FLOAT && rcol->type == TYPE_FLOAT &&
        p.len == (int)sizeof(float) && rcol->len == (int)sizeof(float)) {
        float base = *reinterpret_cast<const float *>(visible + rcol->offset);
        float delta = p.arith_neg ? -p.arith_rhs_f : p.arith_rhs_f;
        *reinterpret_cast<float *>(dest + p.offset) = base + delta;
        return true;
    }
    if (p.type == TYPE_INT && rcol->type == TYPE_INT &&
        p.len == (int)sizeof(int) && rcol->len == (int)sizeof(int)) {
        int base = *reinterpret_cast<const int *>(visible + rcol->offset);
        int delta = p.arith_neg ? -p.arith_rhs_i : p.arith_rhs_i;
        *reinterpret_cast<int *>(dest + p.offset) = base + delta;
        return true;
    }
    return false;
}

}  // namespace

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

void TransactionManager::lock_all_mvcc_shards() const {
    for (size_t i = 0; i < MVCC_NSHARDS; ++i) {
        mvcc_shards_[i].lock();
    }
}

void TransactionManager::unlock_all_mvcc_shards() const {
    for (int i = static_cast<int>(MVCC_NSHARDS) - 1; i >= 0; --i) {
        mvcc_shards_[i].unlock();
    }
}

void TransactionManager::release_statement_writes(Transaction *txn) {
    if (txn == nullptr || !txn->get_txn_mode()) return;
    txn_id_t me = txn->get_transaction_id();
    // 仅扫描本事务 write_set；hold_writer_to_commit 的链跳过（跨语句累加增量）
    for (auto *wr : *txn->get_write_set()) {
        const std::string &tab = wr->GetTableName();
        int64_t rkey = mvcc_key(wr->GetRid());
        size_t sh = mvcc_shard_idx(tab, rkey);
        std::scoped_lock<std::mutex> lck(mvcc_shards_[sh]);
        auto &store = mvcc_shard_data_[sh].store;
        auto tit = store.find(tab);
        if (tit == store.end()) continue;
        auto cit = tit->second.find(rkey);
        if (cit == tit->second.end()) continue;
        MvccChain &ch = cit->second;
        if (ch.writer != me) continue;
        if (ch.hold_writer_to_commit) continue;
        Transaction::SiOverlay ov;
        ov.is_deleted = ch.writer_del;
        ov.data = ch.writer_data;
        txn->put_si_overlay(si_overlay_key(tab, rkey), std::move(ov));
        mvcc_shard_data_[sh].pending[tab][rkey] = me;
        ch.writer = INVALID_TXN_ID;
        ch.writer_del = false;
        ch.writer_data.clear();
    }
}

void TransactionManager::clear_pending_si_for_txn(Transaction *txn) {
    if (txn == nullptr) return;
    txn_id_t me = txn->get_transaction_id();
    for (auto &kv : txn->si_overlays()) {
        const std::string &tab = kv.first.tab;
        int64_t rkey = kv.first.rkey;
        size_t sh = mvcc_shard_idx(tab, rkey);
        std::scoped_lock<std::mutex> lck(mvcc_shards_[sh]);
        auto &pending = mvcc_shard_data_[sh].pending;
        auto tit = pending.find(tab);
        if (tit == pending.end()) continue;
        auto it = tit->second.find(rkey);
        if (it != tit->second.end() && it->second == me) tit->second.erase(it);
    }
    for (auto *wr : *txn->get_write_set()) {
        const std::string &tab = wr->GetTableName();
        int64_t rkey = mvcc_key(wr->GetRid());
        size_t sh = mvcc_shard_idx(tab, rkey);
        std::scoped_lock<std::mutex> lck(mvcc_shards_[sh]);
        auto &pending = mvcc_shard_data_[sh].pending;
        auto tit = pending.find(tab);
        if (tit == pending.end()) continue;
        auto it = tit->second.find(rkey);
        if (it != tit->second.end() && it->second == me) tit->second.erase(it);
    }
}

void TransactionManager::restore_writers_from_overlays(Transaction *txn) {
    if (txn == nullptr) return;
    txn_id_t me = txn->get_transaction_id();
    for (auto &kv : txn->si_overlays()) {
        const std::string &tab = kv.first.tab;
        int64_t rkey = kv.first.rkey;
        size_t sh = mvcc_shard_idx(tab, rkey);
        std::scoped_lock<std::mutex> lck(mvcc_shards_[sh]);
        MvccChain &ch = mvcc_shard_data_[sh].store[tab][rkey];
        mvcc_shard_data_[sh].pending[tab].erase(rkey);
        ch.writer = me;
        ch.writer_del = kv.second.is_deleted;
        ch.writer_data = kv.second.data;
    }
}

struct MvccAllShardsGuard {
    const TransactionManager *tm_;
    explicit MvccAllShardsGuard(const TransactionManager *tm) : tm_(tm) { tm_->lock_all_mvcc_shards(); }
    ~MvccAllShardsGuard() { tm_->unlock_all_mvcc_shards(); }
};

/**
 * @description: 事务的开始方法。空指针代表创建新事务。
 * 题9：新事务在此获得事务级快照读时间戳 read_ts = 当前 last_commit_ts。
 */
Transaction * TransactionManager::begin(Transaction* txn, LogManager* log_manager) {
    std::scoped_lock<std::mutex> lock(latch_);
    if (txn == nullptr) {
        txn_id_t new_id = next_txn_id_++;
        txn = new Transaction(new_id);
        txn->set_start_ts(next_timestamp_++);
        // 题9：事务级快照——以当前最后提交序为快照标识
        txn->set_read_ts(last_commit_ts_.load());
    }
    {
        std::scoped_lock<std::mutex> lck(rts_latch_);
        active_rts_.insert(txn->get_read_ts());
    }
    txn_map[txn->get_transaction_id()] = txn;
    return txn;
}

/**
 * @description: 事务提交。题9：为本事务的所有写入分配单调递增 commit_ts，
 * 并将各 (table,rid) 的未提交写定格为一个已提交版本。
 */
void TransactionManager::commit(Transaction* txn, LogManager* log_manager) {
    if (txn == nullptr) return;
    if (txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) return;

    timestamp_t cts = 0;
    timestamp_t prune_wm = 0;
    auto write_set = txn->get_write_set();
    const bool had_writes = !write_set->empty();
    std::unordered_set<std::string> touched_tabs;
    restore_writers_from_overlays(txn);
    // commit_ts【分配】：只从分配器取号并登记未发布集合，发布水位 last_commit_ts_
    // 在版本物化完成后才推进（见 publish_cts）——否则新快照会在"取号后、物化前"
    // 的窗口读到提交前旧状态（已删行瞬态复活，OJ Delivery MIN canary 实测）
    if (!write_set->empty() || txn->get_txn_mode()) {
        cts = next_cts_.fetch_add(1, std::memory_order_acq_rel) + 1;
        txn->set_commit_ts(cts);
        std::scoped_lock<std::mutex> pl(cts_publish_mtx_);
        unpublished_cts_.insert(cts);
    }
    // 发布：从未发布集合摘除本 cts 并把水位推进到"最小未发布 cts - 1"（前缀完成）。
    // RAII 兜底保证异常路径也发布，否则水位永久卡死（所有新快照停在旧时间戳）。
    struct CtsPublishGuard {
        TransactionManager *tm;
        timestamp_t cts;
        bool done = false;
        void publish() {
            if (done || cts == 0) return;
            done = true;
            std::scoped_lock<std::mutex> pl(tm->cts_publish_mtx_);
            tm->unpublished_cts_.erase(cts);
            timestamp_t frontier = tm->unpublished_cts_.empty()
                                       ? tm->next_cts_.load(std::memory_order_acquire)
                                       : (*tm->unpublished_cts_.begin() - 1);
            timestamp_t cur = tm->last_commit_ts_.load(std::memory_order_relaxed);
            while (frontier > cur &&
                   !tm->last_commit_ts_.compare_exchange_weak(cur, frontier)) {
            }
        }
        ~CtsPublishGuard() { publish(); }
    } cts_guard{this, cts};
    {
        std::scoped_lock<std::mutex> lck(rts_latch_);
        auto wit = active_rts_.find(txn->get_read_ts());
        if (wit != active_rts_.end()) active_rts_.erase(wit);
        prune_wm = active_rts_.empty() ? cts : *active_rts_.begin();
    }
    if (is_ser(txn)) {
        std::scoped_lock<std::mutex> lck(ser_latch_);
        ser_finish(txn->get_transaction_id(), true, cts);
    }
    struct HeapFlush {
        std::string tab;
        Rid rid;
        std::string data;
    };
    for (auto *wr : *write_set) {
        const std::string &tab = wr->GetTableName();
        touched_tabs.insert(tab);
        int64_t rkey = mvcc_key(wr->GetRid());
        size_t sh = mvcc_shard_idx(tab, rkey);
        std::scoped_lock<std::mutex> shlk(mvcc_shards_[sh]);
        auto &store = mvcc_shard_data_[sh].store;
        auto tit = store.find(tab);
        if (tit == store.end()) continue;
        auto cit = tit->second.find(rkey);
        if (cit == tit->second.end()) continue;
        MvccChain &ch = cit->second;
        if (ch.writer == txn->get_transaction_id()) {
            MvccVer v;
            v.commit_ts = cts;
            v.is_deleted = ch.writer_del;
            v.writer_txn = txn->get_transaction_id();
            if (!ch.writer_del) v.data = ch.writer_data;
            ch.hist.push_back(std::move(v));
            // 先物化堆再 prune：否则摘链后读者会读到未刷新的堆页
            if (ch.writer_del) {
                if (sm_manager_->fhs_.at(tab)->is_record(wr->GetRid())) {
                    if (prune_wm >= cts) {
                        // 无更旧活跃快照：下方 prune 会把整条链连同墓碑一起摘除，索引项
                        // 将成为指向已释放槽位的悬空引用——必须与堆同步清理，否则索引
                        // 扫描会读到残留字节，已提交删除的行"复活"（OJ Transaction
                        // Commit Index 实测）。
                        auto old_rec = sm_manager_->fhs_.at(tab)->get_record(wr->GetRid(), nullptr);
                        remove_index_entries_on_commit(
                            sm_manager_, tab, wr->GetRid(),
                            std::string(old_rec->data,
                                        (size_t)sm_manager_->fhs_.at(tab)->get_file_hdr().record_size));
                        sm_manager_->fhs_.at(tab)->delete_record(wr->GetRid(), nullptr);
                    } else {
                        // 有更旧活跃快照：堆槽与索引项都必须保留——seq scan 靠 bitmap
                        // 发现行、index scan 靠索引项定位 rid，快照可见性由链上墓碑之下
                        // 的旧版本裁决（此前在此直接删堆导致 pinned reader 的 seq scan
                        // 丢行）。物理清理登记延迟，待水位越过 cts 后由
                        // drain_deferred_deletes 物化。
                        std::scoped_lock<std::mutex> dl(deferred_del_latch_);
                        deferred_dels_.push_back(DeferredDelete{tab, wr->GetRid(), cts});
                    }
                }
            } else if (!ch.writer_data.empty()) {
                sm_manager_->fhs_.at(tab)->update_record(wr->GetRid(),
                                                         (char *)ch.writer_data.data(), nullptr);
                // MVCC UPDATE 语句期只插新索引项、旧项保留（他人快照读经旧 key 找行）。
                // 提交后旧项成为陈旧引用：无更旧活跃快照即刻清理；否则登记延迟，
                // 待水位越过 cts 由 drain_deferred_deletes 清理。
                RmRecord &undo_old = wr->GetRecord();
                if (wr->GetWriteType() == WType::UPDATE_TUPLE && undo_old.size > 0) {
                    std::string old_bytes(undo_old.data, (size_t)undo_old.size);
                    if (prune_wm >= cts) {
                        remove_stale_update_index_entries(sm_manager_, tab, wr->GetRid(),
                                                          old_bytes, ch.writer_data.data());
                    } else {
                        std::scoped_lock<std::mutex> dl(deferred_del_latch_);
                        deferred_dels_.push_back(
                            DeferredDelete{tab, wr->GetRid(), cts, std::move(old_bytes)});
                    }
                }
            }
            ch.writer = INVALID_TXN_ID;
            ch.writer_data.clear();
            ch.hold_writer_to_commit = false;
            prune_mvcc_after_commit(tab, wr->GetRid(), prune_wm, cts);
        }
    }
    // 每表写活动收尾：记录含写提交的 cts、写者计数 -1（与 note_table_write 对称）。
    // 顺序：cts 更新与减计数同锁原子——读者要么见 writers>0 要么见 last_cts>其快照，
    // 两者都触发检查，不存在"计数已减而 cts 未记"的漏检窗口。
    if (!txn->written_tabs().empty()) {
        std::unique_lock<std::shared_mutex> lck(write_activity_mutex_);
        for (const auto &tab : txn->written_tabs()) {
            WriteActivity &wa = write_activity_[tab];
            if (cts > wa.last_cts) wa.last_cts = cts;
            if (wa.writers > 0) wa.writers--;
        }
        txn->written_tabs().clear();
    }
    // 被删键索引收尾：本事务的删除键转为已提交（记 cts、清 writer），并机会式
    // 清理水位以下的陈旧条目（无未提交删除者且 cts <= 水位的键不可能再触发冲突）。
    if (!txn->del_keys().empty()) {
        std::scoped_lock<std::mutex> lck(del_meta_latch_);
        for (const auto &tk : txn->del_keys()) {
            auto &st = del_keys_[tk.first][tk.second];
            st.writers.erase(txn->get_transaction_id());
            if (cts > st.last_del_cts) st.last_del_cts = cts;
            auto &tabmap = del_keys_[tk.first];
            if (tabmap.size() > 8192) {
                for (auto it = tabmap.begin(); it != tabmap.end();) {
                    if (it->second.writers.empty() && it->second.last_del_cts <= prune_wm)
                        it = tabmap.erase(it);
                    else
                        ++it;
                }
            }
        }
        txn->del_keys().clear();
    }
    // 干净链回收：堆已物化后，单版本、非墓碑、低于水位且无 pending 的链与堆等价，
    // 可整链删除——否则 store 随事务数无界增长，插入端删-插冲突全链扫描 O(n²) 恶化。
    for (auto *wr : *write_set) {
        const std::string &tab = wr->GetTableName();
        int64_t rkey = mvcc_key(wr->GetRid());
        size_t sh = mvcc_shard_idx(tab, rkey);
        std::scoped_lock<std::mutex> shlk(mvcc_shards_[sh]);
        auto &sd = mvcc_shard_data_[sh];
        auto tit = sd.store.find(tab);
        if (tit == sd.store.end()) continue;
        auto cit = tit->second.find(rkey);
        if (cit == tit->second.end()) continue;
        MvccChain &ch = cit->second;
        if (ch.writer != INVALID_TXN_ID || ch.hist.size() != 1 ||
            ch.hist[0].is_deleted || ch.hist[0].commit_ts > prune_wm) continue;
        auto pit = sd.pending.find(tab);
        if (pit != sd.pending.end() && pit->second.count(rkey)) continue;   // 他人 overlay 持有
        tit->second.erase(cit);
    }
    if (!touched_tabs.empty()) {
        std::unique_lock<std::shared_mutex> lck(mvcc_dirty_mutex_);
        for (const auto &tab : touched_tabs) mvcc_dirty_.insert(tab);
        any_mvcc_dirty_.store(!mvcc_dirty_.empty());
    }
    for (auto *wr : *write_set) delete wr;   // write_set 为事务私有，无需全局锁
    write_set->clear();

    // 自动提交的 insert 等会直接写 WAL（设置 prev_lsn）但不进 write_set（非 versioning 路径），
    // 此时 had_writes 为假。若仅凭 had_writes/txn_mode 判定，会漏写 commit 记录 → 恢复时该事务
    // 被当作 loser 撤销，已提交数据丢失。凡产生过 redo 日志（prev_lsn 有效）必须落 commit 记录。
    const bool wrote_log = txn->get_prev_lsn() != INVALID_LSN;
    const bool need_commit_log =
        log_manager != nullptr && (had_writes || txn->get_txn_mode() || wrote_log);
    lsn_t commit_lsn = INVALID_LSN;
    if (need_commit_log) {
        CommitLogRecord lr(txn->get_transaction_id());
        commit_lsn = log_manager->add_log_to_buffer(&lr);
    }

    // MVCC 版本与堆已物化后先放锁，再 wait_for_persist：减 district 行锁与组提交 fsync 重叠
    if (lock_manager_ != nullptr) lock_manager_->unlock_all(txn);
    if (need_commit_log) {
        log_manager->wait_for_persist(commit_lsn);
    }

    if (txn->get_txn_mode() && active_explicit_count_.load() > 0) active_explicit_count_--;
    clear_pending_si_for_txn(txn);
    txn->si_overlays().clear();
    txn->set_state(TransactionState::COMMITTED);
    // 发布本 cts，并等待发布水位覆盖它（外部一致性：响应发出后开始的任何新快照
    // 必须看到本事务效果；前缀 committer 的物化都是内存操作，等待常为零）
    cts_guard.publish();
    if (cts != 0) {
        while (last_commit_ts_.load(std::memory_order_acquire) < cts) {
            std::this_thread::yield();
        }
    }
    // 本事务退出后水位可能前移：物化已无快照依赖的延迟删除（空列表时近零开销）
    drain_deferred_deletes();
}

/**
 * @description: 事务回滚。题9：逆序撤销本事务所有未提交写——新插入的记录保留堆槽为不可见
 * 墓碑(不物理删除、不复用槽，保证 select * 行序与标准一致)，update/delete 则将堆恢复为最新已提交版本。
 */
void TransactionManager::abort(Transaction * txn, LogManager *log_manager) {
    if (txn == nullptr) return;
    if (txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) return;

    auto write_set = txn->get_write_set();
    const bool had_writes = !write_set->empty();
    // 与 commit 对称：每条语句后 release_statement_writes 已把未提交写移入 overlay 并清空
    // ch.writer，回滚前必须先恢复，否则下方按 ch.writer==me 的正常 MVCC 撤销路径全部落空，
    // 转入 physical_undo_write_record 用空 WriteRecord 数据覆写堆 → 崩溃/脏数据。
    restore_writers_from_overlays(txn);
    std::unordered_set<std::string> mvcc_undone_keys;
    for (auto it = write_set->rbegin(); it != write_set->rend(); ++it) {
        WriteRecord *wr = *it;
        const std::string &tab = wr->GetTableName();
        int64_t rkey = mvcc_key(wr->GetRid());
        const std::string undo_key = tab + "#" + std::to_string(rkey);
        size_t sh = mvcc_shard_idx(tab, rkey);
        std::scoped_lock<std::mutex> shlk(mvcc_shards_[sh]);
        bool undone = false;
        auto &store = mvcc_shard_data_[sh].store;
        auto tit = store.find(tab);
        if (tit != store.end()) {
            auto cit = tit->second.find(rkey);
            if (cit != tit->second.end()) {
                MvccChain &ch = cit->second;
                if (ch.writer == txn->get_transaction_id()) {
                    undone = true;
                    WType wtype = wr->GetWriteType();
                    RmRecord &wr_rec = wr->GetRecord();
                    std::string old_data, new_data;
                    bool insert_then_deleted = (wtype == WType::INSERT_TUPLE && ch.writer_del);
                    if (wtype == WType::INSERT_TUPLE) {
                        new_data = ch.writer_data;
                    } else if (wtype == WType::UPDATE_TUPLE && !ch.writer_del) {
                        // 复合写（本事务先走快路径直写堆、未建 MVCC 链；后续语句才转入 MVCC
                        // 建链）下 hist 可能始终为空。此时唯一可信的"事务前原值"是 wr 自身
                        // 在本事务首次触碰该行时捕获的数据——本事务独占写期间 hist 不会再
                        // 增长，二者在非复合写场景下取值完全一致，回退到 wr_rec 不改变原语义。
                        if (!ch.hist.empty()) old_data = ch.hist.back().data;
                        else if (wr_rec.size > 0) old_data.assign(wr_rec.data, wr_rec.size);
                        new_data = ch.writer_data;
                    } else if (wtype == WType::DELETE_TUPLE && ch.hist.empty() && wr_rec.size > 0) {
                        old_data.assign(wr_rec.data, wr_rec.size);
                    }
                    ch.writer = INVALID_TXN_ID;
                    ch.writer_data.clear();
                    ch.writer_del = false;
                    ch.hold_writer_to_commit = false;
                    RmFileHandle *fh = sm_manager_->fhs_.at(tab).get();
                    if (ch.hist.empty()) {
                        if (new_data.empty() && insert_then_deleted && fh->is_record(wr->GetRid())) {
                            auto rec = fh->get_record(wr->GetRid(), nullptr);
                            new_data.assign(rec->data, (size_t)fh->get_file_hdr().record_size);
                        }
                        if (wtype == WType::INSERT_TUPLE) {
                            if (!new_data.empty()) {
                                rollback_index_on_abort(sm_manager_, tab, wr->GetRid(),
                                                        WType::INSERT_TUPLE, old_data, new_data);
                            }
                        } else if (wtype == WType::UPDATE_TUPLE && !old_data.empty()) {
                            // 堆在 MVCC 阶段从未物化过（仅早先快路径写落过一次堆)：
                            // 显式恢复堆到事务前原值，否则会停留在快路径写之后的中间态。
                            if (fh->is_record(wr->GetRid())) {
                                fh->update_record(wr->GetRid(), (char *)old_data.data(), nullptr);
                            }
                            if (!new_data.empty()) {
                                rollback_index_on_abort(sm_manager_, tab, wr->GetRid(),
                                                        WType::UPDATE_TUPLE, old_data, new_data);
                            }
                            // 这行在本事务之前就已是真实已提交数据（并非本事务凭空插入），
                            // 必须补一条 commit_ts=0 基版本，否则空 hist 会被 mvcc_read
                            // 判定为"无任何可见版本"，导致行对所有事务错误地变为不可见。
                            MvccVer base;
                            base.data = old_data;
                            base.commit_ts = 0;
                            base.is_deleted = false;
                            ch.hist.push_back(std::move(base));
                        } else if (wtype == WType::DELETE_TUPLE && !old_data.empty()) {
                            // 早先快路径删除已清 bitmap；MVCC 阶段未再复原过堆槽，须显式补回，
                            // 否则该行会在快路径删除后永久消失，无法回滚。
                            restore_index_if_missing(sm_manager_, tab, wr->GetRid(), old_data);
                            if (!fh->is_record(wr->GetRid())) {
                                RmPageHandle ph = fh->fetch_page_handle(wr->GetRid().page_no);
                                Bitmap::set(ph.bitmap, wr->GetRid().slot_no);
                                ph.page_hdr->num_records++;
                                memcpy(ph.get_slot(wr->GetRid().slot_no), old_data.data(),
                                      (int)old_data.size());
                                sm_manager_->get_bpm()->unpin_page(ph.page->get_page_id(), true);
                            } else {
                                fh->update_record(wr->GetRid(), (char *)old_data.data(), nullptr);
                            }
                            // 同上：撤销的是"删除"，该行本就是事务前已提交数据，补 commit_ts=0
                            // 基版本以恢复可见性。
                            MvccVer base;
                            base.data = old_data;
                            base.commit_ts = 0;
                            base.is_deleted = false;
                            ch.hist.push_back(std::move(base));
                        }
                    } else {
                        const MvccVer &last = ch.hist.back();
                        if (!last.is_deleted && !last.data.empty() && fh->is_record(wr->GetRid())) {
                            fh->update_record(wr->GetRid(), (char *)last.data.data(), nullptr);
                        }
                        if (wtype == WType::UPDATE_TUPLE && !old_data.empty() && !new_data.empty()) {
                            rollback_index_on_abort(sm_manager_, tab, wr->GetRid(),
                                                    WType::UPDATE_TUPLE, old_data, new_data);
                        } else if (wtype == WType::DELETE_TUPLE && !last.is_deleted && !last.data.empty()) {
                            restore_index_if_missing(sm_manager_, tab, wr->GetRid(), last.data);
                        }
                    }
                    mvcc_undone_keys.insert(undo_key);
                }
            }
        }
        if (!undone) {
            if (mvcc_undone_keys.count(undo_key)) continue;
            physical_undo_write_record(txn, wr);
        }
    }
    for (auto *wr : *write_set) delete wr;
    write_set->clear();
    {
        std::scoped_lock<std::mutex> lck(rts_latch_);
        auto wit = active_rts_.find(txn->get_read_ts());
        if (wit != active_rts_.end()) active_rts_.erase(wit);
    }
    if (is_ser(txn)) {
        std::scoped_lock<std::mutex> lck(ser_latch_);
        ser_finish(txn->get_transaction_id(), false, 0);
    }

    if (log_manager != nullptr && had_writes) {
        AbortLogRecord lr(txn->get_transaction_id());
        log_manager->add_log_to_buffer(&lr);
        // abort 不写盘：高冲突 SI 下 abort 极频繁，同步刷 WAL 是主要瓶颈之一
    }

    // 每表写活动收尾（abort）：写者计数 -1，不记 cts（回滚的写不产生已提交版本）
    if (!txn->written_tabs().empty()) {
        std::unique_lock<std::shared_mutex> lck(write_activity_mutex_);
        for (const auto &tab : txn->written_tabs()) {
            auto it = write_activity_.find(tab);
            if (it != write_activity_.end() && it->second.writers > 0) it->second.writers--;
        }
        txn->written_tabs().clear();
    }
    // 撤销本事务的被删键登记；条目已无内容则移除
    if (!txn->del_keys().empty()) {
        std::scoped_lock<std::mutex> lck(del_meta_latch_);
        for (const auto &tk : txn->del_keys()) {
            auto tit = del_keys_.find(tk.first);
            if (tit == del_keys_.end()) continue;
            auto kit = tit->second.find(tk.second);
            if (kit == tit->second.end()) continue;
            kit->second.writers.erase(txn->get_transaction_id());
            if (kit->second.writers.empty() && kit->second.last_del_cts == 0)
                tit->second.erase(kit);
        }
        txn->del_keys().clear();
    }
    if (txn->get_txn_mode() && active_explicit_count_.load() > 0) active_explicit_count_--;
    clear_pending_si_for_txn(txn);
    if (lock_manager_ != nullptr) lock_manager_->unlock_all(txn);
    txn->si_overlays().clear();
    txn->set_state(TransactionState::ABORTED);
    // 与 commit 对称：abort 同样使水位前移
    drain_deferred_deletes();
}

/* ------------------------ 题9：MVCC 读 / 插入 / 写 ------------------------ */

bool TransactionManager::mvcc_read(Transaction *txn, const std::string &tab, const Rid &rid,
                                   const char *heap_data, int len, std::string &out, bool heap_live,
                                   bool *from_heap) {
    if (from_heap != nullptr) *from_heap = false;
    if (!any_mvcc_dirty_.load(std::memory_order_acquire)) {
        if (!heap_live) return false;
        out.assign(heap_data, len);
        if (from_heap != nullptr) *from_heap = true;
        return true;
    }
    int64_t rkey = mvcc_key(rid);
    if (const auto *self_ov = txn->get_si_overlay(si_overlay_key(tab, rkey))) {
        if (self_ov->is_deleted) return false;
        out = self_ov->data;
        return true;
    }
    size_t sh = mvcc_shard_idx(tab, rkey);
    std::scoped_lock<std::mutex> lck(mvcc_shards_[sh]);
    auto &store = mvcc_shard_data_[sh].store;
    auto tit = store.find(tab);
    if (tit == store.end()) {
        if (!heap_live) return false;
        out.assign(heap_data, len);
        if (from_heap != nullptr) *from_heap = true;
        return true;
    }
    auto cit = tit->second.find(mvcc_key(rid));
    if (cit == tit->second.end()) {                       // 未跟踪 = 基础数据，对所有事务可见
        if (!heap_live) return false;                     // 但槽位已死（陈旧索引项）则不可见
        out.assign(heap_data, len);
        if (from_heap != nullptr) *from_heap = true;
        return true;
    }
    MvccChain &ch = cit->second;
    if (ch.writer == txn->get_transaction_id()) {        // 自身未提交写：总能读到
        if (ch.writer_del) return false;
        out = ch.writer_data;
        return true;
    }
    // 快照已覆盖链顶：读 hist.back()（hist 为空时不能读堆，abort 插入会误可见）
    if (ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
        txn->get_read_ts() >= ch.hist.back().commit_ts) {
        if (ch.hist.back().is_deleted) return false;
        out = ch.hist.back().data;
        return true;
    }
    // 反向扫 hist：多数读者只碰最新版本；链过长时避免正向线性退化。
    timestamp_t rts = txn->get_read_ts();
    const MvccVer *vis = nullptr;
    for (auto it = ch.hist.rbegin(); it != ch.hist.rend(); ++it) {   // commit_ts 降序
        if (it->commit_ts <= rts) { vis = &*it; break; }
    }
    if (vis == nullptr || vis->is_deleted) return false;  // 快照中不存在或已删
    out = vis->data;
    return true;
}

void TransactionManager::mvcc_insert(Transaction *txn, const std::string &tab, const Rid &rid,
                                     const char *data, int len) {
    note_table_write(txn, tab);        // 先计数后写存储：读者门不得漏看在飞写者
    int64_t rkey = mvcc_key(rid);
    recent_writes_add(tab, rkey);      // 先登记后写链：幻影检测扫描范围不得漏看在飞写
    size_t sh = mvcc_shard_idx(tab, rkey);
    std::scoped_lock<std::mutex> lck(mvcc_shards_[sh]);
    {
        std::unique_lock<std::shared_mutex> meta(mvcc_dirty_mutex_);
        mvcc_dirty_.insert(tab);
        any_mvcc_dirty_.store(true, std::memory_order_release);
    }
    MvccChain &ch = mvcc_shard_data_[sh].store[tab][rkey];
    ch.hist.clear();                          // 新插入：无已提交基础版本
    ch.writer = txn->get_transaction_id();
    ch.writer_del = false;
    ch.writer_data.assign(data, len);
    txn->append_write_record(new WriteRecord(WType::INSERT_TUPLE, tab, rid));
}

bool TransactionManager::mvcc_write(Transaction *txn, const std::string &tab, const Rid &rid,
                                    const char *old_data, const char *new_data, int len, bool is_delete,
                                    std::string *effective_out) {
    note_table_write(txn, tab);        // 先计数后写存储：读者门不得漏看在飞写者
    int64_t rkey = mvcc_key(rid);
    recent_writes_add(tab, rkey);      // 先登记后写链：幻影检测扫描范围不得漏看在飞写
    size_t sh = mvcc_shard_idx(tab, rkey);
    std::unique_lock<std::mutex> lck(mvcc_shards_[sh]);
    {
        std::unique_lock<std::shared_mutex> meta(mvcc_dirty_mutex_);
        mvcc_dirty_.insert(tab);
        any_mvcc_dirty_.store(true, std::memory_order_release);
    }
    auto &pending = mvcc_shard_data_[sh].pending;
    MvccChain *chp = &mvcc_shard_data_[sh].store[tab][rkey];
    MvccChain &ch = *chp;
    auto pit = pending[tab].find(rkey);
    if (pit != pending[tab].end() && pit->second != txn->get_transaction_id()) return false;
    if (ch.writer != INVALID_TXN_ID && ch.writer != txn->get_transaction_id()) return false;
    // 决赛 SI 铁律：快照之后若已有其它事务提交了新版本，本次写基于的是过期快照，
    // 必须直接 abort，不允许把本次写变基（rebase）合并到最新已提交版本上——
    // 否则会拼出一行任何单个事务都未真正提交过的"缝合"数据，且违反
    // "SI 陈旧写必须 TRANSACTION_ABORT" 的赛题规范（决赛赛题整理 §5.3）。
    if (ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
        ch.hist.back().commit_ts > txn->get_read_ts()) {
        return false;
    }
    const char *write_ptr = new_data;
    bool first_touch = (ch.writer != txn->get_transaction_id()) &&
                       (txn->get_si_overlay(si_overlay_key(tab, rkey)) == nullptr);
    if (first_touch) {
        for (auto *wr : *txn->get_write_set()) {
            if (wr->GetTableName() == tab && wr->GetRid() == rid) {
                first_touch = false;
                break;
            }
        }
    }
    // 首次触及预先存在(未跟踪)的记录：以堆当前值作为基础已提交版本(commit_ts=0)。
    // 注意：若该 rid 只是本事务上一条语句释放到 overlay 的未提交写（典型 insert 后再 delete/update），
    // 不能伪造基础已提交版本，否则 abort 时会把该行当成已提交数据保留下来。
    // 同理：若 write_set 里已有本事务对同一 (tab,rid) 的早先写入（典型：单连接 SI 快路径
    // 直接落堆的 INSERT/UPDATE，未建 MVCC 链），此处的 old_data 其实是本事务自己尚未提交
    // 的堆内容，绝非外部已提交版本——first_touch 已在上面通过扫描 write_set 排除了这种
    // 情况，必须同时作为伪造基版本的前提条件，否则 abort 会把这份自写数据当成"已提交历史"
    // 永久保留/复原，造成回滚后行仍可见（compound rollback 场景）。
    const bool had_overlay = txn->get_si_overlay(si_overlay_key(tab, rkey)) != nullptr;
    if (ch.hist.empty() && ch.writer == INVALID_TXN_ID && !had_overlay && first_touch) {
        MvccVer base;
        base.data.assign(old_data, len);
        base.commit_ts = 0;
        base.is_deleted = false;
        ch.hist.push_back(std::move(base));
    }
    ch.writer = txn->get_transaction_id();
    ch.writer_del = is_delete;
    if (is_delete) ch.writer_data.clear();
    else ch.writer_data.assign(write_ptr, len);
    // 被删键登记（首列字节）：供插入端删-插冲突 O(1) 点查。锁序：分片锁 → del_meta_latch_
    if (is_delete && old_data != nullptr) {
        TabMeta &meta = sm_manager_->db_.get_table(tab);
        if (!meta.cols.empty() && meta.cols[0].offset + meta.cols[0].len <= len) {
            std::string kb(old_data + meta.cols[0].offset, meta.cols[0].len);
            std::scoped_lock<std::mutex> dl(del_meta_latch_);
            del_keys_[tab][kb].writers.insert(txn->get_transaction_id());
            txn->add_del_key(tab, kb);
        }
    }
    if (first_touch) {
        RmRecord undo_old(len);
        memcpy(undo_old.data, old_data, len);
        txn->append_write_record(new WriteRecord(
            is_delete ? WType::DELETE_TUPLE : WType::UPDATE_TUPLE, tab, rid, undo_old));
    }
    if (effective_out != nullptr) *effective_out = ch.writer_data;
    return true;
}

bool TransactionManager::mvcc_write_col_delta(Transaction *txn, const std::string &tab, const Rid &rid,
                                              const char *visible_data, int len, int col_off,
                                              ColType col_type, float delta_f, int delta_i,
                                              std::string *effective_out) {
    note_table_write(txn, tab);
    int64_t rkey = mvcc_key(rid);
    recent_writes_add(tab, rkey);
    size_t sh = mvcc_shard_idx(tab, rkey);
    std::unique_lock<std::mutex> lck(mvcc_shards_[sh]);
    {
        std::unique_lock<std::shared_mutex> meta(mvcc_dirty_mutex_);
        mvcc_dirty_.insert(tab);
        any_mvcc_dirty_.store(true, std::memory_order_release);
    }
    auto &pending = mvcc_shard_data_[sh].pending;
    MvccChain &ch = mvcc_shard_data_[sh].store[tab][rkey];
    auto pit = pending[tab].find(rkey);
    if (pit != pending[tab].end() && pit->second != txn->get_transaction_id()) return false;
    if (ch.writer != INVALID_TXN_ID && ch.writer != txn->get_transaction_id()) return false;
    const bool reuse_writer = (ch.writer == txn->get_transaction_id());
    const bool had_overlay = txn->get_si_overlay(si_overlay_key(tab, rkey)) != nullptr;
    // first_touch 须先于伪造基版本判定算出：write_set 内若已有本事务对同一 (tab,rid) 的
    // 早先写入（典型：单连接 SI 快路径直写堆的 INSERT/UPDATE，未建 MVCC 链），visible_data
    // 其实是本事务自己尚未提交的堆内容，绝不能当作外部已提交版本伪造 commit_ts=0 基版本，
    // 否则 abort 时会把这份自写数据当成"已提交历史"保留，造成回滚后行仍可见。
    bool first_touch = !reuse_writer && !had_overlay;
    if (first_touch) {
        for (auto *wr : *txn->get_write_set()) {
            if (wr->GetTableName() == tab && wr->GetRid() == rid) {
                first_touch = false;
                break;
            }
        }
    }
    if (ch.hist.empty() && ch.writer == INVALID_TXN_ID && !had_overlay && first_touch) {
        MvccVer base;
        base.data.assign(visible_data, len);
        base.commit_ts = 0;
        base.is_deleted = false;
        ch.hist.push_back(std::move(base));
    }
    // 决赛 SI 铁律：同 mvcc_write——快照之后已有新提交版本，本次(增量)写也必须 abort，
    // 不得把 delta 变基叠加到最新版本上（哪怕是同列的交换律累加）。
    if (!reuse_writer && !ch.hist.empty() && ch.hist.back().commit_ts > txn->get_read_ts()) {
        return false;
    }
    const char *base_rec = visible_data;
    ch.writer = txn->get_transaction_id();
    ch.writer_del = false;
    ch.hold_writer_to_commit = true;
    if (col_type == TYPE_FLOAT) {
        if (col_off + (int)sizeof(float) > len) return false;
        if (reuse_writer && !ch.writer_data.empty()) {
            *reinterpret_cast<float *>(ch.writer_data.data() + col_off) += delta_f;
        } else {
            ch.writer_data.assign(base_rec, len);
            *reinterpret_cast<float *>(ch.writer_data.data() + col_off) =
                *reinterpret_cast<const float *>(base_rec + col_off) + delta_f;
        }
    } else if (col_type == TYPE_INT) {
        if (col_off + (int)sizeof(int) > len) return false;
        if (reuse_writer && !ch.writer_data.empty()) {
            *reinterpret_cast<int *>(ch.writer_data.data() + col_off) += delta_i;
        } else {
            ch.writer_data.assign(base_rec, len);
            *reinterpret_cast<int *>(ch.writer_data.data() + col_off) =
                *reinterpret_cast<const int *>(base_rec + col_off) + delta_i;
        }
    } else {
        return false;
    }
    if (first_touch) {
        RmRecord undo_old(len);
        memcpy(undo_old.data, visible_data, len);
        txn->append_write_record(new WriteRecord(WType::UPDATE_TUPLE, tab, rid, undo_old));
    }
    if (effective_out != nullptr) *effective_out = ch.writer_data;
    return true;
}

bool TransactionManager::mvcc_write_col_patch(Transaction *txn, const std::string &tab, const Rid &rid,
                                              const char *visible_data, int len,
                                              const std::vector<MvccColPatch> &patches,
                                              std::string *effective_out) {
    if (patches.empty()) return false;
    note_table_write(txn, tab);
    int64_t rkey = mvcc_key(rid);
    recent_writes_add(tab, rkey);
    size_t sh = mvcc_shard_idx(tab, rkey);
    std::unique_lock<std::mutex> lck(mvcc_shards_[sh]);
    {
        std::unique_lock<std::shared_mutex> meta(mvcc_dirty_mutex_);
        mvcc_dirty_.insert(tab);
        any_mvcc_dirty_.store(true, std::memory_order_release);
    }
    auto &pending = mvcc_shard_data_[sh].pending;
    MvccChain &ch = mvcc_shard_data_[sh].store[tab][rkey];
    auto pit = pending[tab].find(rkey);
    if (pit != pending[tab].end() && pit->second != txn->get_transaction_id()) return false;
    if (ch.writer != INVALID_TXN_ID && ch.writer != txn->get_transaction_id()) return false;

    const bool reuse_writer = (ch.writer == txn->get_transaction_id());
    const bool had_overlay = txn->get_si_overlay(si_overlay_key(tab, rkey)) != nullptr;
    // 同 mvcc_write / mvcc_write_col_delta：first_touch 须先于伪造基版本判定算出，避免把
    // 本事务早先快路径直写堆、尚未提交的内容误当外部已提交版本伪造 commit_ts=0 基版本。
    bool first_touch = !reuse_writer && !had_overlay;
    if (first_touch) {
        for (auto *wr : *txn->get_write_set()) {
            if (wr->GetTableName() == tab && wr->GetRid() == rid) {
                first_touch = false;
                break;
            }
        }
    }
    if (ch.hist.empty() && ch.writer == INVALID_TXN_ID && !had_overlay && first_touch) {
        MvccVer base;
        base.data.assign(visible_data, len);
        base.commit_ts = 0;
        base.is_deleted = false;
        ch.hist.push_back(std::move(base));
    }

    // 决赛 SI 铁律：同 mvcc_write——快照之后已有新提交版本，本次写必须 abort，
    // 不得把 patch 变基合并到最新版本上。
    if (!reuse_writer && !ch.hist.empty() && ch.hist.back().commit_ts > txn->get_read_ts()) {
        return false;
    }
    const char *base_rec = visible_data;
    if (reuse_writer && !ch.writer_data.empty()) {
        base_rec = ch.writer_data.data();
    }

    TabMeta &tmeta = sm_manager_->db_.get_table(tab);
    ch.writer = txn->get_transaction_id();
    ch.writer_del = false;
    if (!reuse_writer || ch.writer_data.empty()) {
        ch.writer_data.assign(base_rec, len);
    }
    for (const auto &p : patches) {
        if (!apply_col_patch_from_visible(p, visible_data, ch.writer_data.data(), len, tmeta)) return false;
    }
    if (first_touch) {
        RmRecord undo_old(len);
        memcpy(undo_old.data, visible_data, len);
        txn->append_write_record(new WriteRecord(WType::UPDATE_TUPLE, tab, rid, undo_old));
    }
    if (effective_out != nullptr) *effective_out = ch.writer_data;
    return true;
}

bool TransactionManager::mvcc_insert_key_conflict(Transaction *txn, const std::string &tab,
                                                  const char *rec_data, int key_off, int key_len) {
    // 被删键索引 O(1) 点查（原实现持全部分片锁全表扫版本链，占 87% CPU）。
    // 略保守：不再验证被删旧版本对本快照是否可见，误报仅多一次 abort。
    std::scoped_lock<std::mutex> lck(del_meta_latch_);
    auto tit = del_keys_.find(tab);
    if (tit == del_keys_.end()) return false;
    auto kit = tit->second.find(std::string(rec_data + key_off, key_len));
    if (kit == tit->second.end()) return false;
    const DelKeyState &st = kit->second;
    txn_id_t me = txn->get_transaction_id();
    for (txn_id_t w : st.writers) {
        if (w != me) return true;                       // 他人未提交删除同键 → 冲突
    }
    return st.last_del_cts > txn->get_read_ts();        // 快照之后已提交的删除 → 冲突
}

/* ------------------------ 题9：SER (SSI 风格可串行化) ------------------------
 * 锁约定：ser_record_read/pred、ser_write_check 自持 ser_latch_；
 * ser_read_check 持分片锁 + meta；表扫描持全分片锁。
 * 内部 helper 假定调用方已持对应锁。
 */
bool TransactionManager::is_ser(Transaction *txn) {
    return txn && txn->get_txn_mode() &&
           txn->get_isolation_level() == IsolationLevel::SERIALIZABLE;
}

static bool ser_cmp(const char *a, const char *b, int len, ColType type, CompOp op) {
    int cmp;
    if (type == TYPE_INT) {
        int ia = *reinterpret_cast<const int *>(a), ib = *reinterpret_cast<const int *>(b);
        cmp = (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
    } else if (type == TYPE_FLOAT) {
        float fa = *reinterpret_cast<const float *>(a), fb = *reinterpret_cast<const float *>(b);
        cmp = (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
    } else {
        cmp = memcmp(a, b, len);
    }
    switch (op) {
        case OP_EQ: return cmp == 0;
        case OP_NE: return cmp != 0;
        case OP_LT: return cmp < 0;
        case OP_GT: return cmp > 0;
        case OP_LE: return cmp <= 0;
        case OP_GE: return cmp >= 0;
    }
    return false;
}

bool TransactionManager::ser_record_matches(const std::string &tab, const char *data,
                                            const std::vector<Condition> &conds) {
    if (conds.empty()) return true;   // 空谓词(全表扫描)匹配所有记录
    TabMeta &meta = sm_manager_->db_.get_table(tab);
    for (const auto &cond : conds) {
        auto it = std::find_if(meta.cols.begin(), meta.cols.end(), [&](const ColMeta &c) {
            return c.name == cond.lhs_col.col_name &&
                   (cond.lhs_col.tab_name.empty() || c.tab_name == cond.lhs_col.tab_name);
        });
        if (it == meta.cols.end()) return false;
        const char *lhs = data + it->offset;
        const char *rhs;
        if (cond.is_rhs_val) {
            rhs = cond.rhs_val.raw->data;
        } else {
            auto rit = std::find_if(meta.cols.begin(), meta.cols.end(),
                                    [&](const ColMeta &c) { return c.name == cond.rhs_col.col_name; });
            if (rit == meta.cols.end()) return false;
            rhs = data + rit->offset;
        }
        if (!ser_cmp(lhs, rhs, it->len, it->type, cond.op)) return false;
    }
    return true;
}

void TransactionManager::ser_record_read(Transaction *txn, const std::string &tab, const Rid &rid) {
    std::scoped_lock<std::mutex> lck(ser_latch_);
    txn_id_t id = txn->get_transaction_id();
    SerInfo &info = ser_[id];
    info.read_ts = txn->get_read_ts();
    int64_t rkey = mvcc_key(rid);
    info.read_rids.push_back({tab, rkey});
    ser_rid_readers_[tab][rkey].insert(id);      // 反查索引：写方 O(1) 直查读者
}

void TransactionManager::ser_record_pred(Transaction *txn, const std::string &tab,
                                         const std::vector<Condition> &conds) {
    std::scoped_lock<std::mutex> lck(ser_latch_);
    txn_id_t id = txn->get_transaction_id();
    SerInfo &info = ser_[id];
    info.read_ts = txn->get_read_ts();
    info.read_preds.push_back({tab, conds});
    // 预编译（列偏移解析一次）后进按表分桶的反查索引；编译失败＝谓词引用不存在的列，
    // 旧的 ser_record_matches 对其恒返回 false（永不命中），故直接不登记，语义等价。
    std::vector<SerCompiledCond> cc;
    if (ser_compile_pred(tab, conds, cc)) {
        ser_pred_readers_[tab][id].push_back(std::move(cc));
    }
}

bool TransactionManager::ser_compile_pred(const std::string &tab, const std::vector<Condition> &conds,
                                          std::vector<SerCompiledCond> &out) {
    TabMeta &meta = sm_manager_->db_.get_table(tab);
    for (const auto &cond : conds) {
        auto it = std::find_if(meta.cols.begin(), meta.cols.end(), [&](const ColMeta &c) {
            return c.name == cond.lhs_col.col_name &&
                   (cond.lhs_col.tab_name.empty() || c.tab_name == cond.lhs_col.tab_name);
        });
        if (it == meta.cols.end()) return false;
        SerCompiledCond cc;
        cc.lhs_off = it->offset;
        cc.lhs_len = it->len;
        cc.type = it->type;
        cc.op = cond.op;
        cc.rhs_is_val = cond.is_rhs_val;
        cc.rhs_off = -1;
        if (cond.is_rhs_val) {
            cc.rhs_val.assign(cond.rhs_val.raw->data, it->len);
        } else {
            auto rit = std::find_if(meta.cols.begin(), meta.cols.end(),
                                    [&](const ColMeta &c) { return c.name == cond.rhs_col.col_name; });
            if (rit == meta.cols.end()) return false;
            cc.rhs_off = rit->offset;
        }
        out.push_back(std::move(cc));
    }
    return true;
}

bool TransactionManager::ser_compiled_match(const char *data, const std::vector<SerCompiledCond> &cs) {
    for (const auto &c : cs) {                    // 空谓词(全表扫描)匹配所有记录
        const char *rhs = c.rhs_is_val ? c.rhs_val.data() : data + c.rhs_off;
        if (!ser_cmp(data + c.lhs_off, rhs, c.lhs_len, c.type, c.op)) return false;
    }
    return true;
}

void TransactionManager::ser_unindex(txn_id_t id, const SerInfo &info) {
    for (const auto &rr : info.read_rids) {
        auto t = ser_rid_readers_.find(rr.first);
        if (t == ser_rid_readers_.end()) continue;
        auto r = t->second.find(rr.second);
        if (r == t->second.end()) continue;
        r->second.erase(id);
        if (r->second.empty()) t->second.erase(r);
    }
    for (const auto &pr : info.read_preds) {
        auto t = ser_pred_readers_.find(pr.first);
        if (t != ser_pred_readers_.end()) t->second.erase(id);
    }
}

void TransactionManager::ser_finish(txn_id_t id, bool committed, timestamp_t commit_ts) {
    auto it = ser_.find(id);
    if (it == ser_.end()) return;
    if (committed) {
        it->second.committed = true;
        it->second.commit_ts = commit_ts;        // 保留信息供并发 SER 事务判定危险结构
    } else {
        for (txn_id_t o : it->second.in_rw)  { auto p = ser_.find(o); if (p != ser_.end()) p->second.out_rw.erase(id); }
        for (txn_id_t o : it->second.out_rw) { auto p = ser_.find(o); if (p != ser_.end()) p->second.in_rw.erase(id); }
        ser_unindex(id, it->second);             // 同步移除反查索引
        ser_.erase(it);                          // 回滚视为从未发生
    }
    // 水位 GC：已提交且 commit_ts <= 所有活跃事务最低 read_ts 的条目，不可能再与任何
    // 现役/未来事务重叠（ser_overlap: committed && cts <= rts → 不重叠），可安全清除。
    // 不清则 ser_ 随事务数无界增长——ser_write_check 每次写遍历全表 → 长跑衰减。
    if (ser_.size() > 512) {
        timestamp_t wm = active_rts_.empty() ? last_commit_ts_.load() : *active_rts_.begin();
        for (auto sit = ser_.begin(); sit != ser_.end();) {
            if (sit->second.committed && sit->second.commit_ts <= wm) {
                txn_id_t gone = sit->first;
                for (txn_id_t o : sit->second.in_rw)  { auto p = ser_.find(o); if (p != ser_.end()) p->second.out_rw.erase(gone); }
                for (txn_id_t o : sit->second.out_rw) { auto p = ser_.find(o); if (p != ser_.end()) p->second.in_rw.erase(gone); }
                ser_unindex(gone, sit->second);      // 同步移除反查索引
                sit = ser_.erase(sit);
            } else {
                ++sit;
            }
        }
    }
}

bool TransactionManager::ser_overlap(txn_id_t a, txn_id_t b) {
    auto ia = ser_.find(a), ib = ser_.find(b);
    if (ia == ser_.end() || ib == ser_.end()) return true;          // 信息缺失：保守认为重叠
    const SerInfo &A = ia->second, &B = ib->second;
    if (A.committed && A.commit_ts <= B.read_ts) return false;      // A 在 B 开始前提交
    if (B.committed && B.commit_ts <= A.read_ts) return false;      // B 在 A 开始前提交
    return true;
}

bool TransactionManager::ser_dangerous(txn_id_t tin, txn_id_t tpiv, txn_id_t tout) {
    if (!ser_overlap(tin, tpiv) || !ser_overlap(tpiv, tout)) return false;
    if (tin == tout) return true;                                  // Tin = Tout
    auto io = ser_.find(tout);
    if (io == ser_.end() || !io->second.committed) return false;   // Tout 未提交，谈不上"先提交"
    auto ii = ser_.find(tin);
    if (ii == ser_.end()) return true;
    if (!ii->second.committed) return true;                        // Tin 仍活跃，Tout 已提交 → Tout 先提交
    return io->second.commit_ts < ii->second.commit_ts;
}

bool TransactionManager::ser_add_edge(txn_id_t reader, txn_id_t writer) {
    if (reader == writer) return false;
    SerInfo &R = ser_[reader];
    SerInfo &W = ser_[writer];
    W.in_rw.insert(reader);
    R.out_rw.insert(writer);
    for (txn_id_t y : W.out_rw) if (ser_dangerous(reader, writer, y)) {
        return true;
    }  // writer 为 pivot
    for (txn_id_t x : R.in_rw)  if (ser_dangerous(x, reader, writer)) {
        return true;
    }  // reader 为 pivot
    return false;
}

bool TransactionManager::ser_write_check(Transaction *txn, const std::string &tab,
                                         const Rid &rid, const char *data) {
    txn_id_t me = txn->get_transaction_id();
    int64_t key = mvcc_key(rid);
    std::scoped_lock<std::mutex> lck(ser_latch_);
    SerInfo &my = ser_[me];
    my.read_ts = txn->get_read_ts();
    // 反查索引代替全表遍历（旧实现：遍历全部 ser_ 条目 × 线性扫其读集 × 按列名字符串
    // 解析匹配谓词，perf 占 ~25% CPU）。命中集合与旧实现逐条等价：
    // 读过该 rid 的事务（rid 索引直查）∪ 本表谓词匹配写入值的事务（预编译谓词按表桶匹配）。
    std::unordered_set<txn_id_t> hits;
    auto t1 = ser_rid_readers_.find(tab);
    if (t1 != ser_rid_readers_.end()) {
        auto r = t1->second.find(key);
        if (r != t1->second.end()) {
            for (txn_id_t o : r->second) {
                if (o != me) hits.insert(o);
            }
        }
    }
    if (data != nullptr) {
        auto t2 = ser_pred_readers_.find(tab);
        if (t2 != ser_pred_readers_.end()) {
            for (auto &kv : t2->second) {
                if (kv.first == me || hits.count(kv.first)) continue;
                for (auto &cc : kv.second) {
                    if (ser_compiled_match(data, cc)) { hits.insert(kv.first); break; }
                }
            }
        }
    }
    bool dangerous = false;
    for (txn_id_t other : hits) {
        if (ser_overlap(other, me) && ser_add_edge(other, me)) dangerous = true;   // other ->rw me
    }
    return dangerous;
}

bool TransactionManager::ser_read_check(Transaction *txn, const std::string &tab, const Rid &rid) {
    // 门：表上无在飞写者且最近写提交 <= 本快照 ⇒ 不存在对本事务不可见的写 ⇒ 无边可建。
    // （门后出现的写由写方 ser_write_check 匹配本事务已记录的读集/谓词建边，不漏检。）
    if (!ser_needs_read_check(txn, tab)) return false;
    txn_id_t me = txn->get_transaction_id();
    timestamp_t rts = txn->get_read_ts();
    int64_t key = mvcc_key(rid);
    size_t sh = mvcc_shard_idx(tab, key);
    std::scoped_lock<std::mutex> shlk(mvcc_shards_[sh]);
    std::scoped_lock<std::mutex> lck(ser_latch_);
    SerInfo &my = ser_[me];
    my.read_ts = rts;
    bool dangerous = false;
    auto &store = mvcc_shard_data_[sh].store;
    auto tit = store.find(tab);
    if (tit == store.end()) return false;
    auto cit = tit->second.find(key);
    if (cit == tit->second.end()) return false;
    MvccChain &ch = cit->second;
    // 其他事务对该 rid 的未提交写 → me ->rw writer
    if (ch.writer != INVALID_TXN_ID && ch.writer != me && ser_.count(ch.writer) && ser_overlap(me, ch.writer))
        if (ser_add_edge(me, ch.writer)) dangerous = true;
    // 已提交但对本事务快照不可见的写 → me ->rw writer
    for (auto &v : ch.hist)
        if (v.commit_ts > rts && v.writer_txn != INVALID_TXN_ID && v.writer_txn != me &&
            ser_.count(v.writer_txn) && ser_overlap(me, v.writer_txn))
            if (ser_add_edge(me, v.writer_txn)) dangerous = true;
    return dangerous;
}

// 读时(谓词)：扫描本表版本链，找匹配谓词、但本事务快照不可见的他事务写(尤其幻影插入)，
// 建立 me ->rw writer。补齐 ser_read_check(只查已读 rid) 无法发现的"看不到的新行"。
bool TransactionManager::ser_read_pred_check(Transaction *txn, const std::string &tab,
                                             const std::vector<Condition> &conds) {
    // 门：表无在飞写者且最近写提交 <= 本快照 ⇒ 整跳（item 等只读表）。
    // 调用方保证先 ser_record_pred 再调本函数——门后出现的匹配写由写方 ser_write_check 建边。
    if (!ser_needs_read_check(txn, tab)) return false;

    // 谓词只依赖 schema：编译一次后循环内用 O(1) 偏移比较，避免每条记录重复 find_if。
    // 编译失败＝引用不存在的列，旧 ser_record_matches 对任何记录恒 false，语义等价。
    std::vector<SerCompiledCond> cc;
    if (!ser_compile_pred(tab, conds, cc)) return false;

    txn_id_t me = txn->get_transaction_id();
    timestamp_t rts = txn->get_read_ts();

    // 只扫"近期写链"集合（未提交写 + 水位以上已提交写），O(活跃窗口写数)；
    // 旧实现锁全部 64 分片 + 全表链扫描，占 47% CPU 且随累计事务数恶化。
    std::vector<int64_t> keys;
    {
        std::scoped_lock<std::mutex> rl(recent_writes_latch_);
        auto it = recent_writes_.find(tab);
        if (it == recent_writes_.end() || it->second.empty()) return false;
        keys.assign(it->second.begin(), it->second.end());
    }
    timestamp_t wm;
    {
        std::scoped_lock<std::mutex> ml(rts_latch_);
        wm = active_rts_.empty() ? last_commit_ts_.load() : *active_rts_.begin();
    }

    // 逐链检查（仅持该链的分片锁），收集需建边的写者；冷链顺手剔除
    std::vector<txn_id_t> hit_writers;
    std::vector<int64_t> cold;
    for (int64_t rkey : keys) {
        size_t sh = mvcc_shard_idx(tab, rkey);
        std::scoped_lock<std::mutex> shlk(mvcc_shards_[sh]);
        auto tit = mvcc_shard_data_[sh].store.find(tab);
        if (tit == mvcc_shard_data_[sh].store.end()) { cold.push_back(rkey); continue; }
        auto cit = tit->second.find(rkey);
        if (cit == tit->second.end()) { cold.push_back(rkey); continue; }
        MvccChain &ch = cit->second;
        // 其他事务未提交的插入/更新，其新值匹配谓词 → 该写会改变本次查询结果
        if (ch.writer != INVALID_TXN_ID && ch.writer != me && !ch.writer_del &&
            !ch.writer_data.empty() && ser_compiled_match(ch.writer_data.data(), cc)) {
            hit_writers.push_back(ch.writer);
        }
        // 已提交但对本事务快照不可见(commit_ts>read_ts)的写，其值匹配谓词
        for (auto &v : ch.hist) {
            if (v.commit_ts > rts && !v.is_deleted && !v.data.empty() &&
                v.writer_txn != INVALID_TXN_ID && v.writer_txn != me &&
                ser_compiled_match(v.data.data(), cc)) {
                hit_writers.push_back(v.writer_txn);
            }
        }
        // 冷判定：无在飞写者且最新已提交版本低于全局水位 → 对任何现役/未来读者都可见
        if (ch.writer == INVALID_TXN_ID &&
            (ch.hist.empty() || ch.hist.back().commit_ts <= wm)) {
            cold.push_back(rkey);
        }
    }
    if (!cold.empty()) {
        std::scoped_lock<std::mutex> rl(recent_writes_latch_);
        auto it = recent_writes_.find(tab);
        if (it != recent_writes_.end())
            for (int64_t k : cold) it->second.erase(k);
    }
    if (hit_writers.empty()) return false;
    // 统一在 meta latch 下做 ser_ 归属校验 + 建边 + 危险结构检测（与旧实现语义一致）
    std::scoped_lock<std::mutex> lck(ser_latch_);
    bool dangerous = false;
    for (txn_id_t w : hit_writers) {
        if (ser_.count(w) && ser_overlap(me, w)) {
            if (ser_add_edge(me, w)) dangerous = true;
        }
    }
    return dangerous;
}

bool TransactionManager::table_is_dirty(const std::string &tab) {
    if (!any_mvcc_dirty_.load()) return false;
    std::shared_lock<std::shared_mutex> lck(mvcc_dirty_mutex_);
    return mvcc_dirty_.count(tab) != 0;
}

void TransactionManager::prune_mvcc_after_commit(const std::string &tab, const Rid &rid,
                                                 timestamp_t watermark, timestamp_t just_committed_cts) {
    // 调用方已持分片锁。水位以下可剪枝；堆已物化且水位覆盖本次提交时可整链删除。
    int64_t rkey = mvcc_key(rid);
    auto &store = mvcc_shard_data_[mvcc_shard_idx(tab, rkey)].store;
    auto tit = store.find(tab);
    if (tit == store.end()) return;
    auto cit = tit->second.find(rkey);
    if (cit == tit->second.end()) return;
    MvccChain &ch = cit->second;
    if (ch.hist.size() <= 1) {
        if (ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
            just_committed_cts > 0 && watermark >= just_committed_cts) {
            tit->second.erase(cit);
            if (tit->second.empty()) store.erase(tit);
        }
        return;
    }
    int keep_from = -1;                    // 最新的 commit_ts <= watermark 的版本下标
    for (int i = (int)ch.hist.size() - 1; i >= 0; --i) {
        if (ch.hist[i].commit_ts <= watermark) { keep_from = i; break; }
    }
    if (keep_from > 0) {
        ch.hist.erase(ch.hist.begin(), ch.hist.begin() + keep_from);
    }
    if (ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
        just_committed_cts > 0 && watermark >= just_committed_cts) {
        if (ch.hist.size() == 1) {
            tit->second.erase(cit);
            if (tit->second.empty()) store.erase(tit);
        } else {
            MvccVer sole = std::move(ch.hist.back());
            ch.hist.clear();
            ch.hist.push_back(std::move(sole));
        }
    }
}

void TransactionManager::drain_deferred_deletes(bool force_heap_for_pending) {
    std::vector<DeferredDelete> ready;
    std::vector<DeferredDelete> still_pending;
    {
        std::scoped_lock<std::mutex> dl(deferred_del_latch_);
        if (deferred_dels_.empty()) return;
        timestamp_t wm;
        {
            std::scoped_lock<std::mutex> rl(rts_latch_);
            wm = active_rts_.empty() ? last_commit_ts_.load() : *active_rts_.begin();
        }
        // cts <= wm 的登记项已无任何快照可见其旧版本，可物化；其余留在列表
        auto keep_end = std::partition(deferred_dels_.begin(), deferred_dels_.end(),
                                       [&](const DeferredDelete &d) { return d.cts > wm; });
        ready.assign(std::make_move_iterator(keep_end), std::make_move_iterator(deferred_dels_.end()));
        deferred_dels_.erase(keep_end, deferred_dels_.end());
        if (force_heap_for_pending) {
            still_pending.assign(deferred_dels_.begin(), deferred_dels_.end());
        }
    }
    std::vector<DeferredDelete> redo;
    for (auto &d : ready) {
        auto fit = sm_manager_->fhs_.find(d.tab);
        if (fit == sm_manager_->fhs_.end()) continue;      // 表已删，无需清理
        RmFileHandle *fh = fit->second.get();
        int64_t rkey = mvcc_key(d.rid);
        size_t sh = mvcc_shard_idx(d.tab, rkey);
        if (!d.unindex_data.empty()) {
            // UPDATE 旧 key 项清理模式：水位已越过 cts，无快照再需要经旧 key 找到该行
            if (fh->is_record(d.rid)) {
                auto cur = fh->get_record(d.rid, nullptr);
                remove_stale_update_index_entries(sm_manager_, d.tab, d.rid, d.unindex_data, cur->data);
            }
            continue;
        }
        std::scoped_lock<std::mutex> shlk(mvcc_shards_[sh]);
        auto &sd = mvcc_shard_data_[sh];
        auto tit = sd.store.find(d.tab);
        if (tit == sd.store.end()) continue;               // 链已不在（如表重建），放弃
        auto cit = tit->second.find(rkey);
        if (cit == tit->second.end()) continue;
        MvccChain &ch = cit->second;
        // 校验链仍是本次登记的已提交墓碑（防表重建/rid 复用等错配）
        if (ch.writer != INVALID_TXN_ID || ch.hist.empty() ||
            !ch.hist.back().is_deleted || ch.hist.back().commit_ts != d.cts) {
            continue;
        }
        auto pit = sd.pending.find(d.tab);
        if (pit != sd.pending.end() && pit->second.count(rkey)) {
            redo.push_back(d);                             // 他人 overlay 持有，保守重排队
            continue;
        }
        // 索引 key 数据源：优先活堆槽；堆槽已被 checkpoint 强制清理时取链上墓碑之下的旧版本
        std::string key_src;
        if (fh->is_record(d.rid)) {
            auto rec = fh->get_record(d.rid, nullptr);
            key_src.assign(rec->data, (size_t)fh->get_file_hdr().record_size);
        } else if (ch.hist.size() >= 2 && !ch.hist[ch.hist.size() - 2].is_deleted) {
            key_src = ch.hist[ch.hist.size() - 2].data;
        }
        if (!key_src.empty()) {
            remove_index_entries_on_commit(sm_manager_, d.tab, d.rid, key_src);
        }
        if (fh->is_record(d.rid)) fh->delete_record(d.rid, nullptr);
        tit->second.erase(cit);                            // 整链摘除（等价 prune 的 wm>=cts 分支）
        if (tit->second.empty()) sd.store.erase(tit);
    }
    // checkpoint 专用：水位未越过的登记项也把堆槽先行清理（登记保留，索引/链留待后续 drain）
    for (auto &d : still_pending) {
        auto fit = sm_manager_->fhs_.find(d.tab);
        if (fit == sm_manager_->fhs_.end()) continue;
        RmFileHandle *fh = fit->second.get();
        size_t sh = mvcc_shard_idx(d.tab, mvcc_key(d.rid));
        std::scoped_lock<std::mutex> shlk(mvcc_shards_[sh]);
        if (fh->is_record(d.rid)) fh->delete_record(d.rid, nullptr);
    }
    if (!redo.empty()) {
        std::scoped_lock<std::mutex> dl(deferred_del_latch_);
        for (auto &d : redo) deferred_dels_.push_back(std::move(d));
    }
}

void TransactionManager::physical_undo_write_record(Transaction *txn, WriteRecord *wr) {
    (void)txn;
    const std::string &tab_name = wr->GetTableName();
    Rid rid = wr->GetRid();
    RmFileHandle *fh = sm_manager_->fhs_.at(tab_name).get();
    int rsz = (int)fh->get_file_hdr().record_size;
    WType wt = wr->GetWriteType();
    if (wt == WType::INSERT_TUPLE) {
        std::string new_data;
        RmRecord &rec = wr->GetRecord();
        if (rec.size > 0) new_data.assign(rec.data, rec.size);
        else if (fh->is_record(rid)) {
            auto hrec = fh->get_record(rid, nullptr);
            new_data.assign(hrec->data, rsz);
        }
        if (!new_data.empty())
            rollback_index_on_abort(sm_manager_, tab_name, rid, WType::INSERT_TUPLE, "", new_data);
        // 调用方(abort)已持 mvcc_shards_[mvcc_shard_idx(tab_name, mvcc_key(rid))]
        MvccChain &ch = mvcc_shard_data_[mvcc_shard_idx(tab_name, mvcc_key(rid))].store[tab_name][mvcc_key(rid)];
        ch.hist.clear();
        ch.writer = INVALID_TXN_ID;
        ch.writer_del = false;
        ch.writer_data.clear();
        {
            std::unique_lock<std::shared_mutex> meta(mvcc_dirty_mutex_);
            mvcc_dirty_.insert(tab_name);
            any_mvcc_dirty_.store(true);
        }
    } else if (wt == WType::UPDATE_TUPLE) {
        RmRecord &old_rec = wr->GetRecord();
        std::string old_bytes;
        if (old_rec.size > 0) {
            old_bytes.assign(old_rec.data, rsz);
        } else {
            // 调用方(abort)已持本 (tab_name,rk) 分片锁，直接读本分片版本链（勿重锁同一 mutex）
            int64_t rk = mvcc_key(rid);
            auto &store = mvcc_shard_data_[mvcc_shard_idx(tab_name, rk)].store;
            auto tit = store.find(tab_name);
            if (tit != store.end()) {
                auto cit = tit->second.find(rk);
                if (cit != tit->second.end() && !cit->second.hist.empty())
                    old_bytes = cit->second.hist.back().data;
            }
        }
        if (old_bytes.empty()) return;
        std::string new_data;
        if (fh->is_record(rid)) {
            auto cur = fh->get_record(rid, nullptr);
            new_data.assign(cur->data, rsz);
            if (memcmp(cur->data, old_bytes.data(), rsz) != 0)
                fh->update_record(rid, (char *)old_bytes.data(), nullptr);
        }
        if (!new_data.empty())
            rollback_index_on_abort(sm_manager_, tab_name, rid, WType::UPDATE_TUPLE, old_bytes, new_data);
    } else if (wt == WType::DELETE_TUPLE) {
        RmRecord &old_rec = wr->GetRecord();
        std::string old_bytes;
        if (old_rec.size > 0) {
            old_bytes.assign(old_rec.data, rsz);
        } else {
            // 调用方(abort)已持本 (tab_name,rk) 分片锁，直接读本分片版本链（勿重锁同一 mutex）
            int64_t rk = mvcc_key(rid);
            auto &store = mvcc_shard_data_[mvcc_shard_idx(tab_name, rk)].store;
            auto tit = store.find(tab_name);
            if (tit != store.end()) {
                auto cit = tit->second.find(rk);
                if (cit != tit->second.end() && !cit->second.hist.empty())
                    old_bytes = cit->second.hist.back().data;
            }
        }
        if (old_bytes.empty()) return;
        restore_index_if_missing(sm_manager_, tab_name, rid, old_bytes);
        if (!fh->is_record(rid)) {
            RmPageHandle ph = fh->fetch_page_handle(rid.page_no);
            Bitmap::set(ph.bitmap, rid.slot_no);
            ph.page_hdr->num_records++;
            memcpy(ph.get_slot(rid.slot_no), old_bytes.data(), rsz);
            sm_manager_->get_bpm()->unpin_page(ph.page->get_page_id(), true);
        } else {
            fh->update_record(rid, (char *)old_bytes.data(), nullptr);
        }
        // 快路径"删后重插同 key"会复用刚释放的同一槽位：逆序撤销时，先行的 INSERT 撤销
        // 已在本 rid 留下"空链墓碑"（aborted-insert 不可见标记）。本分支刚把堆恢复为
        // 事务前已提交数据，空链会让该行对所有事务永久不可见（OJ SI compound rollback
        // 实测丢行）——摘除空链，恢复"未跟踪 = 堆基础数据可见"的语义。
        {
            int64_t rk2 = mvcc_key(rid);
            auto &store2 = mvcc_shard_data_[mvcc_shard_idx(tab_name, rk2)].store;
            auto tit2 = store2.find(tab_name);
            if (tit2 != store2.end()) {
                auto cit2 = tit2->second.find(rk2);
                if (cit2 != tit2->second.end() && cit2->second.writer == INVALID_TXN_ID &&
                    cit2->second.hist.empty()) {
                    tit2->second.erase(cit2);
                    if (tit2->second.empty()) store2.erase(tit2);
                }
            }
        }
    }
}
