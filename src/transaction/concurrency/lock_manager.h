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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "transaction/transaction.h"

static const std::string GroupLockModeStr[10] = {"NON_LOCK", "IS", "IX", "S", "X", "SIX"};

class LockManager {
    /* 加锁类型，包括共享锁、排他锁、意向共享锁、意向排他锁、SIX（意向排他锁+共享锁） */
    enum class LockMode { SHARED, EXLUCSIVE, INTENTION_SHARED, INTENTION_EXCLUSIVE, S_IX };

    /* 用于标识加锁队列中排他性最强的锁类型，例如加锁队列中有SHARED和EXLUSIVE两个加锁操作，则该队列的锁模式为X */
    enum class GroupLockMode { NON_LOCK, IS, IX, S, X, SIX};

    /* 事务的加锁申请 */
    class LockRequest {
    public:
        LockRequest(txn_id_t txn_id, LockMode lock_mode)
            : txn_id_(txn_id), lock_mode_(lock_mode), granted_(false) {}

        txn_id_t txn_id_;   // 申请加锁的事务ID
        LockMode lock_mode_;    // 事务申请加锁的类型
        bool granted_;          // 该事务是否已经被赋予锁
    };

    /* 数据项上的加锁队列 */
    class LockRequestQueue {
    public:
        std::list<LockRequest> request_queue_;  // 加锁队列
        std::condition_variable cv_;            // 条件变量，用于唤醒正在等待加锁的申请，在no-wait策略下无需使用
        GroupLockMode group_lock_mode_ = GroupLockMode::NON_LOCK;   // 加锁队列的锁模式
    };

    struct RecordLockEntry {
        std::mutex mtx;
        std::condition_variable cv;
        txn_id_t owner = INVALID_TXN_ID;  // 排他持有者；INVALID 表示空闲
        int users = 0;  // 出借中的引用数（latch_ 保护）；回收器只删 users==0 && owner==INVALID
    };
    /* get_record_lock 出借引用的 RAII 归还（析构在 latch_ 内 users--）。
     * 声明顺序须在 entry.mtx 的 unique_lock 之前：先放 mtx 再归还引用，
     * 避免 latch_ → mtx 与 mtx → latch_ 的锁序交叉。 */
    struct RecordLockRef {
        LockManager *lm;
        RecordLockEntry *e;
        ~RecordLockRef() {
            std::lock_guard<std::mutex> g(lm->latch_);
            e->users--;
        }
    };

public:
    LockManager() {}

    ~LockManager() {}

    LockAcquireResult lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd);

    LockAcquireResult lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd);

    bool lock_shared_on_table(Transaction* txn, int tab_fd);

    bool lock_exclusive_on_table(Transaction* txn, int tab_fd);

    bool lock_IS_on_table(Transaction* txn, int tab_fd);

    bool lock_IX_on_table(Transaction* txn, int tab_fd);

    bool unlock(Transaction* txn, LockDataId lock_data_id);

    void unlock_all(Transaction* txn);

    /* 空闲行锁条目回收（后台周期调用，见实现注释） */
    void reclaim_idle_record_locks();

    /* 诊断：当前行锁条目数 */
    size_t record_lock_count() {
        std::lock_guard<std::mutex> g(latch_);
        return record_locks_.size();
    }

private:
    RecordLockEntry &get_record_lock(const LockDataId &id);

    bool detect_deadlock_victim(txn_id_t start, txn_id_t &victim);
    void mark_victim_abort(txn_id_t victim);
    void clear_wfg_state(txn_id_t txn);

    std::mutex latch_;      // 用于锁表的并发
    std::unordered_map<LockDataId, LockRequestQueue> lock_table_;   // 全局锁表
    std::unordered_map<LockDataId, std::unique_ptr<RecordLockEntry>> record_locks_;

    // WFG：wait_for_[T]=H 表示 T 等 H 的行锁；成环时 abort 环内 txn_id 最大者
    std::mutex wfg_latch_;
    std::unordered_map<txn_id_t, txn_id_t> wait_for_;
    std::unordered_map<txn_id_t, RecordLockEntry *> waiting_on_entry_;  // 等锁方 → 正在等的 entry
    std::unordered_map<txn_id_t, Transaction *> waiting_txn_;           // 供牺牲者打 abort 标记
};
