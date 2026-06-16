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
#include <algorithm>
#include <cstring>
#include <limits>

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
    {
        std::scoped_lock<std::mutex> lck(mvcc_latch_);
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
    {
        std::scoped_lock<std::mutex> lck(mvcc_latch_);
        auto write_set = txn->get_write_set();
        // 显式事务即使只读也分配提交序（SER 危险结构判定需提交顺序）
        if (!write_set->empty() || txn->get_txn_mode()) {
            cts = ++last_commit_ts_;
            txn->set_commit_ts(cts);
        }
        {
            auto wit = active_rts_.find(txn->get_read_ts());
            if (wit != active_rts_.end()) active_rts_.erase(wit);
        }
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
                v.writer_txn = txn->get_transaction_id();
                if (!ch.writer_del) v.data = ch.writer_data;
                ch.hist.push_back(std::move(v));
                ch.writer = INVALID_TXN_ID;
                ch.writer_data.clear();
            }
        }
        for (auto *wr : *write_set) delete wr;
        write_set->clear();
        ser_finish(txn->get_transaction_id(), true, cts);   // 题9 SER：记录提交序，保留信息供并发事务判定
    }

    // 题10 WAL：提交记录落盘后事务方告提交
    if (log_manager != nullptr) {
        CommitLogRecord lr(txn->get_transaction_id());
        log_manager->add_log_to_buffer(&lr);
        log_manager->flush_log_to_disk();
    }

    if (txn->get_txn_mode() && active_explicit_count_.load() > 0) active_explicit_count_--;
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
                // 本事务新插入回滚 → 保留堆槽作为不可见墓碑：hist 为空且 writer 已清，
                // mvcc_read 判定不可见(SeqScan 跳过)。不物理删除、不复用槽位——否则后续 insert
                // 复用空槽，使 select * 行序与标准(全程 MVCC,中止插入行不复用槽,新行恒在末尾)不一致。
                // 差分测试 seed16 已复现该分歧。fh 在此分支不再使用。
                (void)fh;
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
        auto wit = active_rts_.find(txn->get_read_ts());
        if (wit != active_rts_.end()) active_rts_.erase(wit);
        ser_finish(txn->get_transaction_id(), false, 0);   // 题9 SER：回滚视为从未发生，清理读写集与 rw 边
    }

    // 题10 WAL：回滚记录（恢复时该事务计入 undo list）
    if (log_manager != nullptr) {
        AbortLogRecord lr(txn->get_transaction_id());
        log_manager->add_log_to_buffer(&lr);
        log_manager->flush_log_to_disk();
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
    any_mvcc_dirty_.store(true);
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
    any_mvcc_dirty_.store(true);
    MvccChain &ch = mvcc_store_[tab][mvcc_key(rid)];
    // 写写冲突检测
    if (ch.writer != INVALID_TXN_ID && ch.writer != txn->get_transaction_id()) {
        return false;   // 另一未提交事务正持有该记录
    }
    if (ch.writer == INVALID_TXN_ID && !ch.hist.empty() &&
        ch.hist.back().commit_ts > txn->get_read_ts()) {
        return false;   // 该记录在本事务快照之后已被其他事务提交修改
    }
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

bool TransactionManager::mvcc_insert_key_conflict(Transaction *txn, const std::string &tab,
                                                  const char *rec_data, int key_off, int key_len) {
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
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
 * 锁约定：ser_record_read/pred、ser_write_check、ser_read_check 自持 mvcc_latch_；
 * 内部 helper(ser_finish/ser_add_edge/ser_overlap/ser_dangerous/ser_record_matches) 假定调用方已持锁。*/

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
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
    SerInfo &info = ser_[txn->get_transaction_id()];
    info.read_ts = txn->get_read_ts();
    info.read_rids.push_back({tab, mvcc_key(rid)});
}

void TransactionManager::ser_record_pred(Transaction *txn, const std::string &tab,
                                         const std::vector<Condition> &conds) {
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
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
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
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
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
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
    std::scoped_lock<std::mutex> lck(mvcc_latch_);
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
