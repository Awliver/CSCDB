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
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

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

    {
        std::scoped_lock<std::mutex> lck(mvcc_latch_);
        auto write_set = txn->get_write_set();
        if (!write_set->empty()) {
            timestamp_t cts = ++last_commit_ts_;     // 单调递增提交序
            txn->set_commit_ts(cts);
            for (auto *wr : *write_set) {
                auto tit = mvcc_store_.find(wr->GetTableName());
                if (tit == mvcc_store_.end()) continue;
                auto cit = tit->second.find(mvcc_key(wr->GetRid()));
                if (cit == tit->second.end()) continue;
                MvccChain &ch = cit->second;
                if (ch.writer == txn->get_transaction_id()) {
                    MvccVer v;
                    v.commit_ts = cts;
                    v.is_deleted = ch.writer_del;
                    if (!ch.writer_del) v.data = ch.writer_data;
                    ch.hist.push_back(std::move(v));
                    ch.writer = INVALID_TXN_ID;
                    ch.writer_data.clear();
                }
            }
        }
        for (auto *wr : *write_set) delete wr;
        write_set->clear();
    }

    if (txn->get_txn_mode() && active_explicit_count_.load() > 0) active_explicit_count_--;
    txn->set_state(TransactionState::COMMITTED);
}

/**
 * @description: 事务回滚。题9：逆序撤销本事务所有未提交写——
 * 新插入的记录物理删除，update/delete 则将堆恢复为最新已提交版本，并清理未提交覆盖。
 */
void TransactionManager::abort(Transaction * txn, LogManager *log_manager) {
    if (txn == nullptr) return;
    if (txn->get_state() == TransactionState::COMMITTED ||
        txn->get_state() == TransactionState::ABORTED) return;

    {
        std::scoped_lock<std::mutex> lck(mvcc_latch_);
        auto write_set = txn->get_write_set();
        for (auto it = write_set->rbegin(); it != write_set->rend(); ++it) {
            WriteRecord *wr = *it;
            auto tit = mvcc_store_.find(wr->GetTableName());
            if (tit == mvcc_store_.end()) continue;
            auto &chains = tit->second;
            auto cit = chains.find(mvcc_key(wr->GetRid()));
            if (cit == chains.end()) continue;
            MvccChain &ch = cit->second;
            if (ch.writer != txn->get_transaction_id()) continue;
            ch.writer = INVALID_TXN_ID;
            ch.writer_data.clear();
            ch.writer_del = false;
            RmFileHandle *fh = sm_manager_->fhs_.at(wr->GetTableName()).get();
            if (ch.hist.empty()) {
                // 本事务新插入且未提交 → 物理删除堆记录 + 清链
                if (fh->is_record(wr->GetRid())) fh->delete_record(wr->GetRid(), nullptr);
                chains.erase(cit);
            } else {
                // update/delete 回滚：把堆恢复为最新已提交版本（供无事务快路径读取一致）
                const MvccVer &last = ch.hist.back();
                if (!last.is_deleted && !last.data.empty() && fh->is_record(wr->GetRid())) {
                    fh->update_record(wr->GetRid(), (char *)last.data.data(), nullptr);
                }
            }
        }
        for (auto *wr : *write_set) delete wr;
        write_set->clear();
    }

    if (txn->get_txn_mode() && active_explicit_count_.load() > 0) active_explicit_count_--;
    txn->set_state(TransactionState::ABORTED);
}

/* ------------------------ 题9：MVCC 读 / 插入 / 写 ------------------------ */

bool TransactionManager::mvcc_read(Transaction *txn, const std::string &tab, const Rid &rid,
                                   const char *heap_data, int len, std::string &out) {
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
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
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
    mvcc_dirty_.insert(tab);
    MvccChain &ch = mvcc_store_[tab][mvcc_key(rid)];
    ch.hist.clear();                          // 新插入：无已提交基础版本
    ch.writer = txn->get_transaction_id();
    ch.writer_del = false;
    ch.writer_data.assign(data, len);
    txn->append_write_record(new WriteRecord(WType::INSERT_TUPLE, tab, rid));
}

bool TransactionManager::mvcc_write(Transaction *txn, const std::string &tab, const Rid &rid,
                                    const char *old_data, const char *new_data, int len, bool is_delete) {
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
    mvcc_dirty_.insert(tab);
    MvccChain &ch = mvcc_store_[tab][mvcc_key(rid)];
    // 写写冲突检测
    if (ch.writer != INVALID_TXN_ID && ch.writer != txn->get_transaction_id())
        return false;   // 另一未提交事务正持有该记录
    if (ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
        ch.hist.back().commit_ts > txn->get_read_ts())
        return false;   // 该记录在本事务快照之后已被其他事务提交修改
    bool first_touch = (ch.writer != txn->get_transaction_id());
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
    else ch.writer_data.assign(new_data, len);
    if (first_touch)
        txn->append_write_record(new WriteRecord(is_delete ? WType::DELETE_TUPLE : WType::UPDATE_TUPLE, tab, rid));
    return true;
}
