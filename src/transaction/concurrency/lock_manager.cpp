/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lock_manager.h"

#include <chrono>
#include <vector>

LockManager::RecordLockEntry &LockManager::get_record_lock(const LockDataId &id) {
    std::lock_guard<std::mutex> g(latch_);
    auto &slot = record_locks_[id];
    if (!slot) slot = std::make_unique<RecordLockEntry>();
    return *slot;
}

bool LockManager::detect_deadlock_victim(txn_id_t start, txn_id_t &victim) {
    // 沿 wait_for_ 从 start 走回 start；要求调用前已写入 wait_for_[start]
    auto it = wait_for_.find(start);
    if (it == wait_for_.end()) return false;

    txn_id_t youngest = start;
    txn_id_t cur = it->second;
    size_t guard = wait_for_.size() + 1;
    while (cur != start) {
        if (cur > youngest) youngest = cur;
        auto nit = wait_for_.find(cur);
        if (nit == wait_for_.end()) return false;
        cur = nit->second;
        if (--guard == 0) return false;
    }
    victim = youngest;
    return true;
}

void LockManager::mark_victim_abort(txn_id_t victim) {
    // 先 atomic 置位，再 try_lock 唤醒：避免持本 entry 锁时去抢对方 entry 锁
    Transaction *victim_txn = nullptr;
    RecordLockEntry *entry = nullptr;
    {
        std::lock_guard<std::mutex> g(wfg_latch_);
        auto tit = waiting_txn_.find(victim);
        if (tit != waiting_txn_.end()) victim_txn = tit->second;
        auto eit = waiting_on_entry_.find(victim);
        if (eit != waiting_on_entry_.end()) entry = eit->second;
    }
    if (victim_txn != nullptr) victim_txn->request_lock_abort();
    if (entry == nullptr) return;
    std::unique_lock<std::mutex> lk(entry->mtx, std::try_to_lock);
    if (lk.owns_lock()) entry->cv.notify_all();
}

void LockManager::clear_wfg_state(txn_id_t txn) {
    std::lock_guard<std::mutex> g(wfg_latch_);
    wait_for_.erase(txn);
    waiting_on_entry_.erase(txn);
    waiting_txn_.erase(txn);
}

/**
 * @description: 申请行级共享锁
 * @return {bool} 加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {Rid&} rid 加锁的目标记录ID 记录所在的表的fd
 * @param {int} tab_fd
 */
bool LockManager::lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    return lock_exclusive_on_record(txn, rid, tab_fd);
}

/**
 * @description: 申请行级排他锁；无环则等待，成环则牺牲环内最年轻事务（返回 false）
 * @return {bool} 加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {Rid&} rid 加锁的目标记录ID
 * @param {int} tab_fd 记录所在的表的fd
 */
bool LockManager::lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    if (txn == nullptr) return false;
    LockDataId lock_id(tab_fd, rid, LockDataType::RECORD);
    auto lock_set = txn->get_lock_set();
    if (lock_set->count(lock_id)) return true;

    txn->clear_lock_abort();
    {
        std::lock_guard<std::mutex> g(wfg_latch_);
        waiting_txn_[txn->get_transaction_id()] = txn;
    }

    RecordLockEntry &entry = get_record_lock(lock_id);
    std::unique_lock<std::mutex> lk(entry.mtx);
    txn_id_t me = txn->get_transaction_id();
    while (entry.owner != INVALID_TXN_ID && entry.owner != me) {
        if (txn->lock_abort_requested()) {
            clear_wfg_state(me);
            txn->clear_lock_abort();
            return false;
        }

        txn_id_t wake_victim = INVALID_TXN_ID;
        {
            std::lock_guard<std::mutex> g(wfg_latch_);
            wait_for_[me] = entry.owner;
            waiting_on_entry_[me] = &entry;

            txn_id_t victim = INVALID_TXN_ID;
            if (detect_deadlock_victim(me, victim)) {
                if (victim == me) {
                    // 本事务是环内最年轻者，直接放弃（上层按死锁预防 abort）
                    wait_for_.erase(me);
                    waiting_on_entry_.erase(me);
                    txn->clear_lock_abort();
                    return false;
                }
                wake_victim = victim;
            }
        }
        if (wake_victim != INVALID_TXN_ID) {
            mark_victim_abort(wake_victim);
        }

        if (txn->lock_abort_requested()) {
            clear_wfg_state(me);
            txn->clear_lock_abort();
            return false;
        }

        // 1ms 超时：notify 可能在对方尚未 wait 时丢失，靠轮询 lock_abort_ 兜底
        entry.cv.wait_for(lk, std::chrono::milliseconds(1), [&] {
            return entry.owner == INVALID_TXN_ID || entry.owner == me || txn->lock_abort_requested();
        });

        {
            std::lock_guard<std::mutex> g(wfg_latch_);
            wait_for_.erase(me);
            waiting_on_entry_.erase(me);
        }
        if (txn->lock_abort_requested()) {
            clear_wfg_state(me);
            txn->clear_lock_abort();
            return false;
        }
    }
    clear_wfg_state(me);
    txn->clear_lock_abort();
    entry.owner = me;
    lock_set->insert(lock_id);
    return true;
}

/**
 * @description: 申请表级读锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_shared_on_table(Transaction* txn, int tab_fd) {
    (void)txn;
    (void)tab_fd;
    return true;
}

/**
 * @description: 申请表级写锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_exclusive_on_table(Transaction* txn, int tab_fd) {
    (void)txn;
    (void)tab_fd;
    return true;
}

/**
 * @description: 申请表级意向读锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_IS_on_table(Transaction* txn, int tab_fd) {
    (void)txn;
    (void)tab_fd;
    return true;
}

/**
 * @description: 申请表级意向写锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_IX_on_table(Transaction* txn, int tab_fd) {
    (void)txn;
    (void)tab_fd;
    return true;
}

/**
 * @description: 释放锁
 * @return {bool} 返回解锁是否成功
 * @param {Transaction*} txn 要释放锁的事务对象指针
 * @param {LockDataId} lock_data_id 要释放的锁ID
 */
bool LockManager::unlock(Transaction* txn, LockDataId lock_data_id) {
    if (txn == nullptr) return false;
    auto lock_set = txn->get_lock_set();
    if (!lock_set->count(lock_data_id)) return true;

    if (lock_data_id.type_ == LockDataType::RECORD) {
        RecordLockEntry &entry = get_record_lock(lock_data_id);
        std::lock_guard<std::mutex> lk(entry.mtx);
        if (entry.owner == txn->get_transaction_id()) {
            entry.owner = INVALID_TXN_ID;
            entry.cv.notify_all();
        }
    }
    lock_set->erase(lock_data_id);
    return true;
}

void LockManager::unlock_all(Transaction* txn) {
    if (txn == nullptr) return;
    txn->clear_lock_abort();
    clear_wfg_state(txn->get_transaction_id());
    auto lock_set = txn->get_lock_set();
    std::vector<LockDataId> ids(lock_set->begin(), lock_set->end());
    for (const auto &id : ids) unlock(txn, id);
}
