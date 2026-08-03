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

#include "common/config.h"
#include "defs.h"
#include "record/rm_defs.h"

/* 标识事务状态 */
enum class TransactionState { DEFAULT, GROWING, SHRINKING, COMMITTED, ABORTED };

/* 系统的隔离级别。题9 需要 SNAPSHOT_ISOLATION 与 SERIALIZABLE 两种 */
enum class IsolationLevel { READ_UNCOMMITTED, REPEATABLE_READ, READ_COMMITTED, SERIALIZABLE, SNAPSHOT_ISOLATION };

/* 事务写操作类型，包括插入、删除、更新三种操作 */
enum class WType { INSERT_TUPLE = 0, DELETE_TUPLE, UPDATE_TUPLE};

/**
 * @brief 事务的写操作记录，用于事务的回滚
 * INSERT
 * --------------------------------
 * | wtype | tab_name | tuple_rid |
 * --------------------------------
 * DELETE / UPDATE
 * ----------------------------------------------
 * | wtype | tab_name | tuple_rid | tuple_value |
 * ----------------------------------------------
 */
class WriteRecord {
   public:
    WriteRecord() = default;

    // constructor for insert operation
    WriteRecord(WType wtype, const std::string &tab_name, const Rid &rid)
        : wtype_(wtype), tab_name_(tab_name), rid_(rid) {}

    // constructor for delete & update operation
    WriteRecord(WType wtype, const std::string &tab_name, const Rid &rid, const RmRecord &record)
        : wtype_(wtype), tab_name_(tab_name), rid_(rid), record_(record) {}

    ~WriteRecord() = default;

    inline RmRecord &GetRecord() { return record_; }

    inline Rid &GetRid() { return rid_; }

    inline WType &GetWriteType() { return wtype_; }

    inline std::string &GetTableName() { return tab_name_; }

   private:
    WType wtype_;
    std::string tab_name_;
    Rid rid_;
    RmRecord record_;
};

/* 多粒度锁，加锁对象的类型，包括记录和表 */
enum class LockDataType { TABLE = 0, RECORD = 1 };

/**
 * @description: 加锁对象的唯一标识
 */
class LockDataId {
   public:
    /* 表级锁 */
    LockDataId(int fd, LockDataType type) {
        assert(type == LockDataType::TABLE);
        fd_ = fd;
        type_ = type;
        rid_.page_no = -1;
        rid_.slot_no = -1;
    }

    /* 行级锁 */
    LockDataId(int fd, const Rid &rid, LockDataType type) {
        assert(type == LockDataType::RECORD);
        fd_ = fd;
        rid_ = rid;
        type_ = type;
    }

    inline int64_t Get() const {
        if (type_ == LockDataType::TABLE) {
            // fd_
            return static_cast<int64_t>(fd_);
        } else {
            // fd_, rid_.page_no, rid.slot_no
            return ((static_cast<int64_t>(type_)) << 63) | ((static_cast<int64_t>(fd_)) << 31) |
                   ((static_cast<int64_t>(rid_.page_no)) << 16) | rid_.slot_no;
        }
    }

    bool operator==(const LockDataId &other) const {
        if (type_ != other.type_) return false;
        if (fd_ != other.fd_) return false;
        return rid_ == other.rid_;
    }
    int fd_;
    Rid rid_;
    LockDataType type_;
};

template <>
struct std::hash<LockDataId> {
    size_t operator()(const LockDataId &obj) const { return std::hash<int64_t>()(obj.Get()); }
};

/*
 * 稳定的事务异常归因。枚举值和 token 是 Wire diagnostic / P-A1 统计契约，
 * 不得用异常英文文本反推原因。旧的锁异常保留在末尾，避免改变既有控制流。
 */
enum class AbortReason : uint8_t {
    NONE = 0,
    ACTIVE_WRITE_CONFLICT,
    STALE_SNAPSHOT_WRITE,
    SSI_DANGEROUS_STRUCTURE,
    WFG_DEADLOCK,
    BUFFER_POOL_PRESSURE,
    OTHER,
    LOCK_ON_SHIRINKING,
    UPGRADE_CONFLICT,
    DEADLOCK_PREVENTION,
};

inline const char *abort_reason_token(AbortReason reason) {
    switch (reason) {
        case AbortReason::NONE: return "NONE";
        case AbortReason::ACTIVE_WRITE_CONFLICT: return "ACTIVE_WRITE_CONFLICT";
        case AbortReason::STALE_SNAPSHOT_WRITE: return "STALE_SNAPSHOT_WRITE";
        case AbortReason::SSI_DANGEROUS_STRUCTURE: return "SSI_DANGEROUS_STRUCTURE";
        case AbortReason::WFG_DEADLOCK: return "WFG_DEADLOCK";
        case AbortReason::BUFFER_POOL_PRESSURE: return "BUFFER_POOL_PRESSURE";
        default: return "OTHER";
    }
}

enum class MvccWriteResult : uint8_t {
    OK = 0,
    ACTIVE_WRITE_CONFLICT,
    STALE_SNAPSHOT_WRITE,
    INVALID,
};

enum class LockAcquireResult : uint8_t {
    GRANTED = 0,
    ACTIVE_WRITE_CONFLICT,
    WFG_DEADLOCK,
    INVALID,
};

inline AbortReason abort_reason_from(MvccWriteResult result) {
    if (result == MvccWriteResult::ACTIVE_WRITE_CONFLICT) return AbortReason::ACTIVE_WRITE_CONFLICT;
    if (result == MvccWriteResult::STALE_SNAPSHOT_WRITE) return AbortReason::STALE_SNAPSHOT_WRITE;
    return AbortReason::OTHER;
}

inline AbortReason abort_reason_from(LockAcquireResult result) {
    if (result == LockAcquireResult::ACTIVE_WRITE_CONFLICT) return AbortReason::ACTIVE_WRITE_CONFLICT;
    if (result == LockAcquireResult::WFG_DEADLOCK) return AbortReason::WFG_DEADLOCK;
    return AbortReason::OTHER;
}

/* 事务回滚异常，在rmdb.cpp中进行处理 */
class TransactionAbortException : public std::exception {
    txn_id_t txn_id_;
    AbortReason abort_reason_;

   public:
    explicit TransactionAbortException(txn_id_t txn_id, AbortReason abort_reason)
        : txn_id_(txn_id), abort_reason_(abort_reason) {}

    txn_id_t get_transaction_id() const { return txn_id_; }
    AbortReason GetAbortReason() const { return abort_reason_; }
    std::string GetInfo() const {
        switch (abort_reason_) {
            case AbortReason::LOCK_ON_SHIRINKING: {
                return "Transaction " + std::to_string(txn_id_) +
                       " aborted because it cannot request locks on SHRINKING phase\n";
            } break;

            case AbortReason::UPGRADE_CONFLICT: {
                return "Transaction " + std::to_string(txn_id_) +
                       " aborted because another transaction is waiting for upgrading\n";
            } break;

            case AbortReason::DEADLOCK_PREVENTION: {
                return "Transaction " + std::to_string(txn_id_) + " aborted for deadlock prevention\n";
            } break;

            case AbortReason::ACTIVE_WRITE_CONFLICT:
            case AbortReason::STALE_SNAPSHOT_WRITE:
            case AbortReason::SSI_DANGEROUS_STRUCTURE:
            case AbortReason::WFG_DEADLOCK:
            case AbortReason::BUFFER_POOL_PRESSURE:
            case AbortReason::OTHER: {
                return "RMDB_ABORT reason=" + std::string(abort_reason_token(abort_reason_));
            } break;

            default: {
                return "Transaction aborted\n";
            } break;
        }
    }
};
