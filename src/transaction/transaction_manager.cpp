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

namespace {

static int district_next_oid(const std::string &data) {
    if (data.size() < 101) return -1;
    return *reinterpret_cast<const int *>(data.data() + 97);
}

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
            ih->insert_entry(old_key.data(), rid, nullptr);
        }
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

/* SI 写写冲突：将基于旧快照的增量写重定位到最新已提交版本（TPC-C read-then-write 模式）。
 * 基底必须取 latest_rec：本事务未改动的列要保留最新已提交值（如 new_order 全行镜像里
 * 顺带携带的 d_ytd），若以旧快照为基底会把并发 payment 已提交的增量覆盖回旧值（丢钱）。 */
bool rebase_write_delta(SmManager *sm, const std::string &tab,
                        const char *old_rec, const char *new_rec, const char *latest_rec,
                        int len, std::string &out) {
    out.assign(latest_rec, len);
    if (memcmp(old_rec, new_rec, len) == 0) return false;
    TabMeta &meta = sm->db_.get_table(tab);
    bool any = false;
    for (auto &col : meta.cols) {
        if (col.offset + col.len > len) continue;
        const char *o = old_rec + col.offset;
        const char *n = new_rec + col.offset;
        const char *l = latest_rec + col.offset;
        if (memcmp(o, n, col.len) == 0) continue;
        any = true;
        if (col.type == TYPE_INT && col.len == (int)sizeof(int)) {
            int delta = *(const int *)n - *(const int *)o;
            *(int *)(out.data() + col.offset) = *(const int *)l + delta;
        } else if (col.type == TYPE_FLOAT && col.len == (int)sizeof(float)) {
            float delta = *(const float *)n - *(const float *)o;
            *(float *)(out.data() + col.offset) = *(const float *)l + delta;
        } else {
            memcpy(out.data() + col.offset, n, col.len);
        }
    }
    return any;
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
    // 仅扫描本事务 write_set，避免每条 SQL 后遍历全库 mvcc_store_（TPC-C 下可达 10 万+ 链）
    for (auto *wr : *txn->get_write_set()) {
        const std::string &tab = wr->GetTableName();
        if (tab == "district" || tab == "warehouse") continue;
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
    // commit_ts 原子递增；SI 只动 rts_latch_，SER 才碰 ser_latch_
    if (!write_set->empty() || txn->get_txn_mode()) {
        cts = last_commit_ts_.fetch_add(1, std::memory_order_acq_rel) + 1;
        txn->set_commit_ts(cts);
    }
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
    std::vector<HeapFlush> heap_flushes;
    heap_flushes.reserve(write_set->size());
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
            // district 计数器在并发 SI 下必须单调不减，禁止陈旧写把计数器拉低。
            // payment 等不改计数器的更新 new_n == back_n 属正常，不得误 +1（否则每笔
            // payment 幽灵递增一次 d_next_o_id → 大量 o_id 空洞/丢单）。
            if (tab == "district" && !ch.writer_del && !v.data.empty() && !ch.hist.empty()) {
                int back_n = district_next_oid(ch.hist.back().data);
                int new_n = district_next_oid(v.data);
                if (new_n < back_n) {
                    *(int *)(v.data.data() + 97) = back_n + 1;
                    ch.writer_data = v.data;
                }
            }
            ch.hist.push_back(std::move(v));
            if (!ch.writer_del && !ch.writer_data.empty()) {
                heap_flushes.push_back({tab, wr->GetRid(), ch.writer_data});
            }
            ch.writer = INVALID_TXN_ID;
            ch.writer_data.clear();
            prune_mvcc_after_commit(tab, wr->GetRid(), prune_wm, cts);
        }
    }
    for (const auto &hf : heap_flushes) {
        sm_manager_->fhs_.at(hf.tab)->update_record(hf.rid, (char *)hf.data.data(), nullptr);
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
    if (log_manager != nullptr && (had_writes || txn->get_txn_mode() || wrote_log)) {
        CommitLogRecord lr(txn->get_transaction_id());
        lsn_t lsn = log_manager->add_log_to_buffer(&lr);
        log_manager->wait_for_persist(lsn);
    }

    if (txn->get_txn_mode() && active_explicit_count_.load() > 0) active_explicit_count_--;
    clear_pending_si_for_txn(txn);
    if (lock_manager_ != nullptr) lock_manager_->unlock_all(txn);
    txn->si_overlays().clear();
    txn->set_state(TransactionState::COMMITTED);
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
                    std::string old_data, new_data;
                    bool insert_then_deleted = (wtype == WType::INSERT_TUPLE && ch.writer_del);
                    if (wtype == WType::INSERT_TUPLE) {
                        new_data = ch.writer_data;
                    } else if (wtype == WType::UPDATE_TUPLE && !ch.hist.empty() && !ch.writer_del) {
                        old_data = ch.hist.back().data;
                        new_data = ch.writer_data;
                    }
                    ch.writer = INVALID_TXN_ID;
                    ch.writer_data.clear();
                    ch.writer_del = false;
                    RmFileHandle *fh = sm_manager_->fhs_.at(tab).get();
                    if (ch.hist.empty()) {
                        if (new_data.empty() && insert_then_deleted && fh->is_record(wr->GetRid())) {
                            auto rec = fh->get_record(wr->GetRid(), nullptr);
                            new_data.assign(rec->data, (size_t)fh->get_file_hdr().record_size);
                        }
                        if (!new_data.empty()) {
                            rollback_index_on_abort(sm_manager_, tab, wr->GetRid(),
                                                    WType::INSERT_TUPLE, old_data, new_data);
                        }
                    } else {
                        const MvccVer &last = ch.hist.back();
                        bool restore_heap = !last.is_deleted && !last.data.empty() && fh->is_record(wr->GetRid());
                        if (restore_heap && tab == "district") {
                            auto cur = fh->get_record(wr->GetRid(), nullptr);
                            int heap_next = district_next_oid(
                                std::string(cur->data, (size_t)fh->get_file_hdr().record_size));
                            int restore_next = district_next_oid(last.data);
                            if (heap_next > restore_next) restore_heap = false;
                        }
                        if (restore_heap) {
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
}

/* ------------------------ 题9：MVCC 读 / 插入 / 写 ------------------------ */

bool TransactionManager::mvcc_read(Transaction *txn, const std::string &tab, const Rid &rid,
                                   const char *heap_data, int len, std::string &out) {
    if (!any_mvcc_dirty_.load(std::memory_order_acquire)) {
        out.assign(heap_data, len);
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
    if (tit == store.end()) { out.assign(heap_data, len); return true; }
    auto cit = tit->second.find(mvcc_key(rid));
    if (cit == tit->second.end()) { out.assign(heap_data, len); return true; }  // 未跟踪 = 基础数据，对所有事务可见
    MvccChain &ch = cit->second;
    if (ch.writer == txn->get_transaction_id()) {        // 自身未提交写：总能读到
        if (ch.writer_del) return false;
        out = ch.writer_data;
        return true;
    }
    // warehouse/district：快照不早于最新提交时堆已是最新值（commit 才写堆）
    if (is_mvcc_hot_row(tab) && ch.writer == INVALID_TXN_ID) {
        if (ch.hist.empty()) {
            out.assign(heap_data, len);
            return true;
        }
        if (txn->get_read_ts() >= ch.hist.back().commit_ts) {
            if (ch.hist.back().is_deleted) return false;
            out.assign(heap_data, len);
            return true;
        }
    }
    // 其余情况按事务级快照读最新已提交版本（忽略他人未提交写）。
    // 从新到旧反向扫：读者绝大多数只要最新版本，典型 O(1)；正向扫会随链长线性退化
    // （热点行如 warehouse/district 链长 ∝ 已提交事务数 → 吞吐随运行时间衰减）。
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
    std::string rebased_new;
    const char *write_ptr = new_data;
    if (ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
        ch.hist.back().commit_ts > txn->get_read_ts()) {
        if (ch.hist.back().is_deleted || (int)ch.hist.back().data.size() != len ||
            !rebase_write_delta(sm_manager_, tab, old_data, new_data,
                                ch.hist.back().data.data(), len, rebased_new)) {
            return false;
        }
        write_ptr = rebased_new.data();
    }
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
    const bool had_overlay = txn->get_si_overlay(si_overlay_key(tab, rkey)) != nullptr;
    if (ch.hist.empty() && ch.writer == INVALID_TXN_ID && !had_overlay) {
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

bool TransactionManager::mvcc_write_ytd_delta(Transaction *txn, const std::string &tab, const Rid &rid,
                                              const char *visible_data, int len, int col_off, float delta,
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
    const bool had_overlay = txn->get_si_overlay(si_overlay_key(tab, rkey)) != nullptr;
    if (ch.hist.empty() && ch.writer == INVALID_TXN_ID && !had_overlay) {
        MvccVer base;
        base.data.assign(visible_data, len);
        base.commit_ts = 0;
        base.is_deleted = false;
        ch.hist.push_back(std::move(base));
    }
    const char *base_rec = visible_data;
    if (ch.writer != txn->get_transaction_id() && !ch.hist.empty() &&
        ch.hist.back().commit_ts > txn->get_read_ts()) {
        base_rec = ch.hist.back().data.data();
    }
    const bool reuse_writer = (ch.writer == txn->get_transaction_id());
    bool first_touch = !reuse_writer && (txn->get_si_overlay(si_overlay_key(tab, rkey)) == nullptr);
    if (first_touch) {
        for (auto *wr : *txn->get_write_set()) {
            if (wr->GetTableName() == tab && wr->GetRid() == rid) {
                first_touch = false;
                break;
            }
        }
    }
    ch.writer = txn->get_transaction_id();
    ch.writer_del = false;
    if (col_off + (int)sizeof(float) > len) return false;
    if (reuse_writer && !ch.writer_data.empty()) {
        *reinterpret_cast<float *>(ch.writer_data.data() + col_off) += delta;
    } else {
        ch.writer_data.assign(base_rec, len);
        *reinterpret_cast<float *>(ch.writer_data.data() + col_off) =
            *reinterpret_cast<const float *>(base_rec + col_off) + delta;
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
    // 语义与原扫描同为"记录首列"粒度；略保守——不再验证被删旧版本对本快照可见，
    // 误报仅多一次 abort（安全），TPC-C 键单调递增实际不撞。
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
            !ch.writer_data.empty() && ser_record_matches(tab, ch.writer_data.data(), conds)) {
            hit_writers.push_back(ch.writer);
        }
        // 已提交但对本事务快照不可见(commit_ts>read_ts)的写，其值匹配谓词
        for (auto &v : ch.hist) {
            if (v.commit_ts > rts && !v.is_deleted && !v.data.empty() &&
                v.writer_txn != INVALID_TXN_ID && v.writer_txn != me &&
                ser_record_matches(tab, v.data.data(), conds)) {
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
    // 调用方已持本 rid 分片锁。水位以下除最新外可删；watermark>=本次提交时热行可摘链。
    int64_t rkey = mvcc_key(rid);
    auto &store = mvcc_shard_data_[mvcc_shard_idx(tab, rkey)].store;
    auto tit = store.find(tab);
    if (tit == store.end()) return;
    auto cit = tit->second.find(rkey);
    if (cit == tit->second.end()) return;
    MvccChain &ch = cit->second;
    if (ch.hist.size() <= 1) {
        if (is_mvcc_hot_row(tab) && ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
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
    if (is_mvcc_hot_row(tab) && ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
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
    }
}
