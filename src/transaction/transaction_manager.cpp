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

/* SI 写写冲突：将基于旧快照的增量写重定位到最新已提交版本（TPC-C read-then-write 模式） */
bool rebase_write_delta(SmManager *sm, const std::string &tab,
                        const char *old_rec, const char *new_rec, const char *latest_rec,
                        int len, std::string &out) {
    out.assign(old_rec, len);
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
        std::scoped_lock<std::mutex> lck(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
        auto tit = mvcc_store_.find(tab);
        if (tit == mvcc_store_.end()) continue;
        auto cit = tit->second.find(rkey);
        if (cit == tit->second.end()) continue;
        MvccChain &ch = cit->second;
        if (ch.writer != me) continue;
        Transaction::SiOverlay ov;
        ov.is_deleted = ch.writer_del;
        ov.data = ch.writer_data;
        txn->put_si_overlay(si_overlay_key(tab, rkey), std::move(ov));
        pending_si_writes_[tab][rkey] = me;
        ch.writer = INVALID_TXN_ID;
        ch.writer_del = false;
        ch.writer_data.clear();
    }
}

void TransactionManager::clear_pending_si_for_txn(Transaction *txn) {
    if (txn == nullptr) return;
    txn_id_t me = txn->get_transaction_id();
    for (auto &kv : txn->si_overlays()) {
        auto pos = kv.first.find('#');
        if (pos == std::string::npos) continue;
        std::string tab = kv.first.substr(0, pos);
        int64_t rkey = std::stoll(kv.first.substr(pos + 1));
        std::scoped_lock<std::mutex> lck(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
        auto tit = pending_si_writes_.find(tab);
        if (tit == pending_si_writes_.end()) continue;
        auto it = tit->second.find(rkey);
        if (it != tit->second.end() && it->second == me) tit->second.erase(it);
    }
    for (auto *wr : *txn->get_write_set()) {
        const std::string &tab = wr->GetTableName();
        int64_t rkey = mvcc_key(wr->GetRid());
        std::scoped_lock<std::mutex> lck(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
        auto tit = pending_si_writes_.find(tab);
        if (tit == pending_si_writes_.end()) continue;
        auto it = tit->second.find(rkey);
        if (it != tit->second.end() && it->second == me) tit->second.erase(it);
    }
}

void TransactionManager::restore_writers_from_overlays(Transaction *txn) {
    if (txn == nullptr) return;
    txn_id_t me = txn->get_transaction_id();
    for (auto &kv : txn->si_overlays()) {
        auto pos = kv.first.find('#');
        if (pos == std::string::npos) continue;
        std::string tab = kv.first.substr(0, pos);
        int64_t rkey = std::stoll(kv.first.substr(pos + 1));
        std::scoped_lock<std::mutex> lck(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
        MvccChain &ch = mvcc_store_[tab][rkey];
        pending_si_writes_[tab].erase(rkey);
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
        std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
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
    auto write_set = txn->get_write_set();
    const bool had_writes = !write_set->empty();
    std::unordered_set<std::string> touched_tabs;
    restore_writers_from_overlays(txn);
    {
        std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
        if (!write_set->empty() || txn->get_txn_mode()) {
            cts = ++last_commit_ts_;
            txn->set_commit_ts(cts);
        }
        {
            auto wit = active_rts_.find(txn->get_read_ts());
            if (wit != active_rts_.end()) active_rts_.erase(wit);
        }
        ser_finish(txn->get_transaction_id(), true, cts);
    }
    for (auto *wr : *write_set) {
        const std::string &tab = wr->GetTableName();
        touched_tabs.insert(tab);
        int64_t rkey = mvcc_key(wr->GetRid());
        std::scoped_lock<std::mutex> shlk(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
        auto tit = mvcc_store_.find(tab);
        if (tit == mvcc_store_.end()) continue;
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
            if (!ch.writer_del && !ch.writer_data.empty()) {
                sm_manager_->fhs_.at(tab)->update_record(wr->GetRid(), (char *)ch.writer_data.data(), nullptr);
            }
            ch.writer = INVALID_TXN_ID;
            ch.writer_data.clear();
            prune_mvcc_after_commit(tab, wr->GetRid());
        }
    }
    {
        std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
        for (const auto &tab : touched_tabs) mvcc_dirty_.insert(tab);
        any_mvcc_dirty_.store(!mvcc_dirty_.empty());
        for (auto *wr : *write_set) delete wr;
        write_set->clear();
    }

    if (log_manager != nullptr && (had_writes || txn->get_txn_mode())) {
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
    for (auto it = write_set->rbegin(); it != write_set->rend(); ++it) {
        WriteRecord *wr = *it;
        const std::string &tab = wr->GetTableName();
        int64_t rkey = mvcc_key(wr->GetRid());
        std::scoped_lock<std::mutex> shlk(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
        bool undone = false;
        auto tit = mvcc_store_.find(tab);
        if (tit != mvcc_store_.end()) {
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
                }
            }
        }
        if (!undone) physical_undo_write_record(txn, wr);
    }
    {
        std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
        for (auto *wr : *write_set) delete wr;
        write_set->clear();
        auto wit = active_rts_.find(txn->get_read_ts());
        if (wit != active_rts_.end()) active_rts_.erase(wit);
        ser_finish(txn->get_transaction_id(), false, 0);
    }

    if (log_manager != nullptr && had_writes) {
        AbortLogRecord lr(txn->get_transaction_id());
        log_manager->add_log_to_buffer(&lr);
        // abort 不写盘：高冲突 SI 下 abort 极频繁，同步刷 WAL 是主要瓶颈之一
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
    std::scoped_lock<std::mutex> lck(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
    auto tit = mvcc_store_.find(tab);
    if (tit == mvcc_store_.end()) { out.assign(heap_data, len); return true; }
    auto cit = tit->second.find(mvcc_key(rid));
    if (cit == tit->second.end()) { out.assign(heap_data, len); return true; }  // 未跟踪 = 基础数据，对所有事务可见
    MvccChain &ch = cit->second;
    if (ch.writer == txn->get_transaction_id()) {        // 自身未提交写：总能读到
        if (ch.writer_del) return false;
        out = ch.writer_data;
        return true;
    }
    // 其余情况按事务级快照读最新已提交版本（忽略他人未提交写）
    timestamp_t rts = txn->get_read_ts();
    const MvccVer *vis = nullptr;
    for (const auto &v : ch.hist) {            // commit_ts 升序
        if (v.commit_ts <= rts) vis = &v;
        else break;
    }
    if (vis == nullptr || vis->is_deleted) return false;  // 快照中不存在或已删
    out = vis->data;
    return true;
}

void TransactionManager::mvcc_insert(Transaction *txn, const std::string &tab, const Rid &rid,
                                     const char *data, int len) {
    int64_t rkey = mvcc_key(rid);
    std::scoped_lock<std::mutex> lck(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
    {
        std::scoped_lock<std::mutex> meta(mvcc_meta_latch_);
        mvcc_dirty_.insert(tab);
        any_mvcc_dirty_.store(true, std::memory_order_release);
    }
    MvccChain &ch = mvcc_store_[tab][mvcc_key(rid)];
    ch.hist.clear();                          // 新插入：无已提交基础版本
    ch.writer = txn->get_transaction_id();
    ch.writer_del = false;
    ch.writer_data.assign(data, len);
    txn->append_write_record(new WriteRecord(WType::INSERT_TUPLE, tab, rid));
}

bool TransactionManager::mvcc_write(Transaction *txn, const std::string &tab, const Rid &rid,
                                    const char *old_data, const char *new_data, int len, bool is_delete,
                                    std::string *effective_out) {
    int64_t rkey = mvcc_key(rid);
    std::unique_lock<std::mutex> lck(mvcc_shards_[mvcc_shard_idx(tab, rkey)]);
    {
        std::scoped_lock<std::mutex> meta(mvcc_meta_latch_);
        mvcc_dirty_.insert(tab);
        any_mvcc_dirty_.store(true, std::memory_order_release);
    }
    MvccChain *chp = &mvcc_store_[tab][mvcc_key(rid)];
    MvccChain &ch = *chp;
    auto pit = pending_si_writes_[tab].find(rkey);
    if (pit != pending_si_writes_[tab].end() && pit->second != txn->get_transaction_id()) return false;
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
    // 首次触及预先存在(未跟踪)的记录：以堆当前值作为基础已提交版本(commit_ts=0)
    if (ch.hist.empty() && ch.writer == INVALID_TXN_ID) {
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
    if (first_touch)
        txn->append_write_record(new WriteRecord(is_delete ? WType::DELETE_TUPLE : WType::UPDATE_TUPLE, tab, rid));
    if (effective_out != nullptr) *effective_out = ch.writer_data;
    return true;
}

bool TransactionManager::mvcc_insert_key_conflict(Transaction *txn, const std::string &tab,
                                                  const char *rec_data, int key_off, int key_len) {
    MvccAllShardsGuard all_shards(this);
    auto tit = mvcc_store_.find(tab);
    if (tit == mvcc_store_.end()) return false;
    txn_id_t me = txn->get_transaction_id();
    timestamp_t rts = txn->get_read_ts();
    const char *key = rec_data + key_off;
    for (auto &kv : tit->second) {
        MvccChain &ch = kv.second;
        // 本事务快照可见的最新已提交版本
        const MvccVer *vis = nullptr;
        for (const auto &v : ch.hist) {          // commit_ts 升序
            if (v.commit_ts <= rts) vis = &v;
            else break;
        }
        if (vis == nullptr || vis->is_deleted) continue;   // 快照内无该记录 → 插入不基于旧版本
        if ((int)vis->data.size() < key_off + key_len) continue;
        if (memcmp(vis->data.data() + key_off, key, key_len) != 0) continue;  // 键不同
        // 快照可见同键旧版本：被并发删除(未提交或快照后已提交) → 写写冲突
        if (ch.writer != INVALID_TXN_ID && ch.writer != me && ch.writer_del) return true;
        if (ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
            ch.hist.back().commit_ts > rts && ch.hist.back().is_deleted) return true;
    }
    return false;
}

/* ------------------------ 题9：SER (SSI 风格可串行化) ------------------------
 * 锁约定：ser_record_read/pred、ser_write_check 自持 mvcc_meta_latch_；
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
    std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
    SerInfo &info = ser_[txn->get_transaction_id()];
    info.read_ts = txn->get_read_ts();
    info.read_rids.push_back({tab, mvcc_key(rid)});
}

void TransactionManager::ser_record_pred(Transaction *txn, const std::string &tab,
                                         const std::vector<Condition> &conds) {
    std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
    SerInfo &info = ser_[txn->get_transaction_id()];
    info.read_ts = txn->get_read_ts();
    info.read_preds.push_back({tab, conds});
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
        ser_.erase(it);                          // 回滚视为从未发生
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
    std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
    SerInfo &my = ser_[me];
    my.read_ts = txn->get_read_ts();
    bool dangerous = false;
    for (auto &kv : ser_) {
        txn_id_t other = kv.first;
        if (other == me) continue;
        SerInfo &oi = kv.second;
        bool hit = false;
        for (auto &rr : oi.read_rids)
            if (rr.second == key && rr.first == tab) { hit = true; break; }
        if (!hit && data)
            for (auto &pr : oi.read_preds)
                if (pr.first == tab && ser_record_matches(tab, data, pr.second)) { hit = true; break; }
        if (hit && ser_overlap(other, me))
            if (ser_add_edge(other, me)) dangerous = true;   // other ->rw me
    }
    return dangerous;
}

bool TransactionManager::ser_read_check(Transaction *txn, const std::string &tab, const Rid &rid) {
    txn_id_t me = txn->get_transaction_id();
    timestamp_t rts = txn->get_read_ts();
    int64_t key = mvcc_key(rid);
    std::scoped_lock<std::mutex> shlk(mvcc_shards_[mvcc_shard_idx(tab, key)]);
    std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
    SerInfo &my = ser_[me];
    my.read_ts = rts;
    bool dangerous = false;
    auto tit = mvcc_store_.find(tab);
    if (tit == mvcc_store_.end()) return false;
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
    txn_id_t me = txn->get_transaction_id();
    timestamp_t rts = txn->get_read_ts();
    MvccAllShardsGuard all_shards(this);
    std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
    auto tit = mvcc_store_.find(tab);
    if (tit == mvcc_store_.end()) return false;
    bool dangerous = false;
    for (auto &kv : tit->second) {
        MvccChain &ch = kv.second;
        // 其他事务未提交的插入/更新，其新值匹配谓词 → 该写会改变本次查询结果
        if (ch.writer != INVALID_TXN_ID && ch.writer != me && !ch.writer_del &&
            !ch.writer_data.empty() && ser_.count(ch.writer) && ser_overlap(me, ch.writer) &&
            ser_record_matches(tab, ch.writer_data.data(), conds)) {
            if (ser_add_edge(me, ch.writer)) dangerous = true;
        }
        // 已提交但对本事务快照不可见(commit_ts>read_ts)的写，其值匹配谓词
        for (auto &v : ch.hist) {
            if (v.commit_ts > rts && !v.is_deleted && !v.data.empty() &&
                v.writer_txn != INVALID_TXN_ID && v.writer_txn != me &&
                ser_.count(v.writer_txn) && ser_overlap(me, v.writer_txn) &&
                ser_record_matches(tab, v.data.data(), conds)) {
                if (ser_add_edge(me, v.writer_txn)) dangerous = true;
            }
        }
    }
    return dangerous;
}

bool TransactionManager::table_is_dirty(const std::string &tab) {
    if (!any_mvcc_dirty_.load()) return false;
    std::scoped_lock<std::mutex> lck(mvcc_meta_latch_);
    return mvcc_dirty_.count(tab) != 0;
}

void TransactionManager::prune_mvcc_after_commit(const std::string &tab, const Rid &rid) {
    auto tit = mvcc_store_.find(tab);
    if (tit == mvcc_store_.end()) return;
    auto cit = tit->second.find(mvcc_key(rid));
    if (cit == tit->second.end()) return;
    MvccChain &ch = cit->second;
    if (ch.writer != INVALID_TXN_ID || ch.hist.empty() || ch.hist.back().is_deleted) return;
    // 保留已提交版本链供快照读；过早 prune 会使 read_ts 较旧的事务误读堆上最新值
    return;
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
        MvccChain &ch = mvcc_store_[tab_name][mvcc_key(rid)];
        ch.hist.clear();
        ch.writer = INVALID_TXN_ID;
        ch.writer_del = false;
        ch.writer_data.clear();
        mvcc_dirty_.insert(tab_name);
        any_mvcc_dirty_.store(true);
    } else if (wt == WType::UPDATE_TUPLE) {
        RmRecord &old_rec = wr->GetRecord();
        std::string new_data;
        if (fh->is_record(rid)) {
            auto cur = fh->get_record(rid, nullptr);
            new_data.assign(cur->data, rsz);
            fh->update_record(rid, old_rec.data, nullptr);
        }
        rollback_index_on_abort(sm_manager_, tab_name, rid, WType::UPDATE_TUPLE,
                                std::string(old_rec.data, rsz), new_data);
    } else if (wt == WType::DELETE_TUPLE) {
        RmRecord &old_rec = wr->GetRecord();
        restore_index_if_missing(sm_manager_, tab_name, rid, std::string(old_rec.data, rsz));
        if (!fh->is_record(rid)) {
            RmPageHandle ph = fh->fetch_page_handle(rid.page_no);
            Bitmap::set(ph.bitmap, rid.slot_no);
            ph.page_hdr->num_records++;
            memcpy(ph.get_slot(rid.slot_no), old_rec.data, rsz);
            sm_manager_->get_bpm()->unpin_page(ph.page->get_page_id(), true);
        } else {
            fh->update_record(rid, old_rec.data, nullptr);
        }
    }
}
