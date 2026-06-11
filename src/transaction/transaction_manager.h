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

#include <atomic>
#include <unordered_map>
#include <set>
#include <optional>
#include <functional>
#include <shared_mutex>

#include "transaction.h"
#include "watermark.h"
#include "recovery/log_manager.h"
#include "concurrency/lock_manager.h"
#include "system/sm_manager.h"
#include "common/exception.h"

/* 系统采用的并发控制算法，当前题目中要求两阶段封锁并发控制算法 */
enum class ConcurrencyMode { TWO_PHASE_LOCKING = 0, BASIC_TO, MVCC };

/// 版本链中的第一个撤销链接，将表堆元组链接到撤销日志。
struct VersionUndoLink {
    /** 版本链中的下一个版本。 */
    UndoLink prev_;
    bool in_progress_{false};

    friend auto operator==(const VersionUndoLink &a, const VersionUndoLink &b) {
        return a.prev_ == b.prev_ && a.in_progress_ == b.in_progress_;
    }

    friend auto operator!=(const VersionUndoLink &a, const VersionUndoLink &b) { return !(a == b); }

    inline static std::optional<VersionUndoLink> FromOptionalUndoLink(std::optional<UndoLink> undo_link) {
        if (undo_link.has_value()) {
            return VersionUndoLink{*undo_link};
        }
        return std::nullopt;
    }
};

class TransactionManager{
public:
    explicit TransactionManager(LockManager *lock_manager, SmManager *sm_manager,
                             ConcurrencyMode concurrency_mode = ConcurrencyMode::TWO_PHASE_LOCKING) {
        sm_manager_ = sm_manager;
        lock_manager_ = lock_manager;
        concurrency_mode_ = concurrency_mode;
    }
    
    ~TransactionManager() = default;

    Transaction* begin(Transaction* txn, LogManager* log_manager);

    void commit(Transaction* txn, LogManager* log_manager);

    void abort(Transaction* txn, LogManager* log_manager);

    ConcurrencyMode get_concurrency_mode() { return concurrency_mode_; }

    void set_concurrency_mode(ConcurrencyMode concurrency_mode) { concurrency_mode_ = concurrency_mode; }

    LockManager* get_lock_manager() { return lock_manager_; }

    /**
     * @description: 获取事务ID为txn_id的事务对象
     * @return {Transaction*} 事务对象的指针
     * @param {txn_id_t} txn_id 事务ID
     */    
    Transaction* get_transaction(txn_id_t txn_id) {
        if(txn_id == INVALID_TXN_ID) return nullptr;
        
        std::unique_lock<std::mutex> lock(latch_);
        auto it = TransactionManager::txn_map.find(txn_id);
        Transaction *res = (it == TransactionManager::txn_map.end()) ? nullptr : it->second;
        lock.unlock();

        return res;
    }

    // 题10：已终结事务在语句尾回收，防止长会话内存增长
    void reap(Transaction* txn) {
        if (txn == nullptr) return;
        if (txn->get_state() != TransactionState::COMMITTED &&
            txn->get_state() != TransactionState::ABORTED) return;
        {
            std::unique_lock<std::mutex> lock(latch_);
            txn_map.erase(txn->get_transaction_id());
        }
        delete txn;
    }

    static std::unordered_map<txn_id_t, Transaction *> txn_map;     // 全局事务表，存放事务ID与事务对象的映射关系
    std::shared_mutex txn_map_mutex_;
    /** ------------------------以下函数仅可能在MVCC当中使用------------------------------------------*/

    /**
    * @brief 更新一个撤销链接，该链接将表堆元组与第一个撤销日志连接起来。
    * 在更新之前，将调用 `check` 函数以确保有效性。
    */
    bool UpdateUndoLink(Rid rid, std::optional<UndoLink> prev_link,
                        std::function<bool(std::optional<UndoLink>)> &&check = nullptr);

    /**
     * @brief 更新一个撤销链接，该链接将表堆元组与第一个撤销日志连接起来。
     * 在更新之前，将调用 `check` 函数以确保有效性。
     */
    bool UpdateVersionLink(Rid rid, std::optional<VersionUndoLink> prev_version,
                           std::function<bool(std::optional<VersionUndoLink>)> &&check = nullptr);

    /** @brief 获取表堆元组的第一个撤销日志。 */
    std::optional<UndoLink> GetUndoLink(Rid rid);

    /** @brief 获取表堆元组的第一个撤销日志。*/
    std::optional<VersionUndoLink> GetVersionLink(Rid rid);

    /** @brief 访问事务撤销日志缓冲区并获取撤销日志。如果事务不存在，返回 nullopt。
     * 如果索引超出范围仍然会抛出异常。 */
    std::optional<UndoLog> GetUndoLogOptional(UndoLink link);

    /** @brief 访问事务撤销日志缓冲区并获取撤销日志。除非访问当前事务缓冲区，
     * 否则应该始终调用此函数以获取撤销日志，而不是手动检索事务 shared_ptr 并访问缓冲区。 */
    UndoLog GetUndoLog(UndoLink link);

    /** @brief 获取系统中的最低读时间戳。 */
    timestamp_t GetWatermark();

    /** @brief 垃圾回收。仅在所有事务都未访问时调用。 */
    void GarbageCollection();

    struct PageVersionInfo {
        std::shared_mutex mutex_;
        /** 存储所有槽的先前版本信息。注意：不要使用 `[x]` 来访问它，因为
         * 即使不存在也会创建新元素。请使用 `find` 来代替。
         */
        std::unordered_map<slot_offset_t, VersionUndoLink> prev_version_;
    };

    /** 保护版本信息 */
    std::shared_mutex version_info_mutex_;
    /** 存储表堆中每个元组的先前版本。 */
    std::unordered_map<page_id_t, std::shared_ptr<PageVersionInfo>> version_info_;

    /** ------------------------ 题9：MVCC 版本存储（SI / SER） ------------------------ */
    /* 单个已提交版本：完整记录字节 + 提交时间戳 + 删除标记 */
    struct MvccVer {
        std::string data;
        timestamp_t commit_ts;
        bool is_deleted;
        txn_id_t writer_txn = INVALID_TXN_ID;   // 题9 SER：提交该版本的事务（读侧 rw 归因）
    };
    /* 一条逻辑记录 (table,rid) 的版本链：已提交版本(commit_ts 升序) + 至多一个未提交写覆盖 */
    struct MvccChain {
        std::vector<MvccVer> hist;          // 已提交版本，commit_ts 升序
        txn_id_t writer = INVALID_TXN_ID;   // 未提交写者（同一时刻至多一个，否则写写冲突）
        bool writer_del = false;            // 未提交写是否为删除
        std::string writer_data;            // 未提交写的新值（非删除时有效）
    };
    static inline int64_t mvcc_key(const Rid &rid) {
        return ((int64_t)rid.page_no << 32) | (uint32_t)rid.slot_no;
    }

    /* 活跃显式事务数：>0 时写操作才需维护版本，否则单语句直接落堆（避免批量加载开销） */
    bool mvcc_should_version() const { return active_explicit_count_.load() > 0; }
    void inc_explicit() { active_explicit_count_++; }
    /* 该表是否被 MVCC 写过（读时才需查版本链，未脏表直接读堆，保持非事务负载性能） */
    bool table_is_dirty(const std::string &tab) {
        std::scoped_lock<std::mutex> lck(mvcc_latch_);
        return mvcc_dirty_.count(tab) > 0;
    }
    /* 写是否需维护版本：有活跃显式事务，或该表已被 MVCC 写过(否则脏表上的隐式写会绕过版本链，
       使后续快照读取到陈旧值)。无任何 MVCC 活动时(批量加载)走快路径，零开销。 */
    bool needs_versioning(const std::string &tab) {
        if (active_explicit_count_.load() > 0) return true;
        if (!any_mvcc_dirty_.load()) return false;
        return table_is_dirty(tab);
    }
    /* 题9 唯一索引: 该 (table,rid) 是否被另一活跃事务持写(未提交插入/更新/删除)。用于
       并发同键插入的写写冲突检测——避免 MVCC 感知唯一检查把他人未提交插入误判为可重插。*/
    bool mvcc_other_writer(const std::string &tab, const Rid &rid, txn_id_t me) {
        std::scoped_lock<std::mutex> lck(mvcc_latch_);
        auto tit = mvcc_store_.find(tab);
        if (tit == mvcc_store_.end()) return false;
        auto cit = tit->second.find(mvcc_key(rid));
        if (cit == tit->second.end()) return false;
        txn_id_t w = cit->second.writer;
        return w != INVALID_TXN_ID && w != me;
    }
    /* 读：返回 txn 在其快照下对 (table,rid) 可见的记录字节；不可见/已删返回 false */
    /* 题9 删-插写写冲突: 插入键 K(首列)时,若本事务快照内可见同键旧版本,且该记录正被其他活跃
       事务未提交删除、或在快照之后被提交删除 → 本插入与该删除基于同一旧版本 → 冲突(调用方 abort)。
       新键插入(无可见旧版本)、自删重插(writer==me)、早已提交的重复键 均不冲突。 */
    bool mvcc_insert_key_conflict(Transaction *txn, const std::string &tab,
                                  const char *rec_data, int key_off, int key_len);
    bool mvcc_read(Transaction *txn, const std::string &tab, const Rid &rid,
                   const char *heap_data, int len, std::string &out);
    /* 插入：登记一条未提交插入版本（rid 为堆插入返回的位置） */
    void mvcc_insert(Transaction *txn, const std::string &tab, const Rid &rid,
                     const char *data, int len);
    /* 写(update/delete)：写写冲突检测 + 登记未提交版本；冲突返回 false（调用方应 abort 该事务） */
    bool mvcc_write(Transaction *txn, const std::string &tab, const Rid &rid,
                    const char *old_data, const char *new_data, int len, bool is_delete);

    /* ------------------------ 题9：SER（SSI 风格可串行化） ------------------------ */
    bool is_ser(Transaction *txn);
    /* 记录 SER 事务的一次记录读 / 谓词读（仅 SELECT 调用） */
    void ser_record_read(Transaction *txn, const std::string &tab, const Rid &rid);
    void ser_record_pred(Transaction *txn, const std::string &tab, const std::vector<Condition> &conds);
    /* 写时：被写记录 vs 其他 SER 事务的读 → 建 rw 反依赖；成危险结构返回 true（调用方 abort 本事务） */
    bool ser_write_check(Transaction *txn, const std::string &tab, const Rid &rid, const char *data);
    /* 读时：本次读到的记录 vs 其他 SER 事务对它的不可见写 → 建 rw 反依赖；危险结构返回 true */
    bool ser_read_check(Transaction *txn, const std::string &tab, const Rid &rid);
    /* 读时(谓词)：版本存储中匹配本次谓词、但本事务快照不可见的他事务写(含幻影插入) → rw 反依赖 */
    bool ser_read_pred_check(Transaction *txn, const std::string &tab, const std::vector<Condition> &conds);

private:
    ConcurrencyMode concurrency_mode_;      // 事务使用的并发控制算法，目前只需要考虑2PL
    std::atomic<txn_id_t> next_txn_id_{0};  // 用于分发事务ID
    std::atomic<timestamp_t> next_timestamp_{0};    // 用于分发事务时间戳
    std::mutex latch_;  // 用于txn_map的并发
    SmManager *sm_manager_;
    LockManager *lock_manager_;

    std::atomic<timestamp_t> last_commit_ts_{0};    // 最后提交的时间戳,仅用于MVCC
    std::multiset<timestamp_t> active_rts_;         // 题10:活跃事务 read_ts 水位(SER 状态 GC 用)
    Watermark running_txns_{0};             // 存储所有正在运行事务的读取时间戳，以便于垃圾回收，仅用于MVCC

    /* 题9 MVCC 状态 */
    std::atomic<int> active_explicit_count_{0};   // 活跃显式事务数
    std::atomic<bool> any_mvcc_dirty_{false};     // 是否曾有任何 MVCC 写（无则全程快路径，零开销）
    std::mutex mvcc_latch_;                       // 保护 mvcc_store_ / mvcc_dirty_
    std::unordered_map<std::string, std::unordered_map<int64_t, MvccChain>> mvcc_store_;  // table -> ridkey -> 版本链
    std::unordered_set<std::string> mvcc_dirty_;  // 曾被 MVCC 写过的表

    /* 题9 SER (SSI) 状态 —— 复用 mvcc_latch_ 保护 */
    struct SerInfo {
        timestamp_t read_ts = 0;
        timestamp_t commit_ts = 0;     // 0 = 未提交(活跃)
        bool committed = false;
        std::vector<std::pair<std::string, int64_t>> read_rids;                   // (table, ridkey)
        std::vector<std::pair<std::string, std::vector<Condition>>> read_preds;   // (table, 谓词)
        std::unordered_set<txn_id_t> in_rw;    // X ->rw 本事务
        std::unordered_set<txn_id_t> out_rw;   // 本事务 ->rw Y
    };
    std::unordered_map<txn_id_t, SerInfo> ser_;
    void ser_begin(txn_id_t id, timestamp_t read_ts);
    void ser_finish(txn_id_t id, bool committed, timestamp_t commit_ts);
    bool ser_add_edge(txn_id_t reader, txn_id_t writer);    // 加 rw 边 + 查危险结构(true=危险)
    bool ser_overlap(txn_id_t a, txn_id_t b);
    bool ser_dangerous(txn_id_t tin, txn_id_t tpiv, txn_id_t tout);
    bool ser_record_matches(const std::string &tab, const char *data, const std::vector<Condition> &conds);
};