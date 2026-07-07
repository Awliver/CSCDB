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

#include <vector>

LockManager::RecordLockEntry &LockManager::get_record_lock(const LockDataId &id) {
    std::lock_guard<std::mutex> g(latch_);
    auto &slot = record_locks_[id];
    if (!slot) slot = std::make_unique<RecordLockEntry>();
    return *slot;
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
 * @description: 申请行级排他锁（等待直至获得锁）
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

    RecordLockEntry &entry = get_record_lock(lock_id);
    std::unique_lock<std::mutex> lk(entry.mtx);
    txn_id_t me = txn->get_transaction_id();
    while (entry.owner != INVALID_TXN_ID && entry.owner != me) {
        // wait-die：仅老事务等年轻持有者；年轻事务直接放弃，避免 wait-for 环
        if (me > entry.owner) {
            return false;
        }
        entry.cv.wait(lk);
    }
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
    auto lock_set = txn->get_lock_set();
    std::vector<LockDataId> ids(lock_set->begin(), lock_set->end());
    for (const auto &id : ids) unlock(txn, id);
}
