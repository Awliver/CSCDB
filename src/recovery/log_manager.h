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

#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <iostream>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "log_defs.h"
#include "common/config.h"
#include "record/rm_defs.h"

/* P0：WAL 管道统计（relaxed atomic，常开；RMDB_WAL_STATS=1 时 flush 线程每 10s 打一行） */
struct WalStats {
    std::atomic<uint64_t> n_fsync{0};
    std::atomic<uint64_t> fsync_us_total{0};
    std::atomic<uint64_t> fsync_us_max{0};
    std::atomic<uint64_t> n_bytes{0};
    std::atomic<uint64_t> n_batches{0};
    std::atomic<uint64_t> n_commit_waits{0};  // wait_for_persist 慢路径进入次数
    // fsync 延迟桶：<1 / 1-5 / 5-10 / 10-20 / 20-50 / >50 ms
    std::atomic<uint64_t> lat_bucket[6]{};
};

/* 日志记录对应操作的类型 */
enum LogType: int {
    UPDATE = 0,
    INSERT,
    DELETE,
    begin,
    commit,
    ABORT,
    CKPT,           // 题10：静态检查点记录
    UPDATE_DELTA    // P2：update 字节差分增量日志
};
static std::string LogTypeStr[] = {
    "UPDATE",
    "INSERT",
    "DELETE",
    "BEGIN",
    "COMMIT",
    "ABORT",
    "CKPT",
    "UPDATE_DELTA"
};

/* P2：字节差分段（最多 8 段） */
struct WalDiffRange {
    uint16_t off = 0;
    uint16_t len = 0;
};

/* 扫 old/new，合并连续差异（间隙 ≤8B 并入同段）；返回段数，>8 返回 -1 */
inline int wal_byte_diff(const char* a, const char* b, int n, WalDiffRange out[8]) {
    int nr = 0;
    int i = 0;
    while (i < n) {
        while (i < n && a[i] == b[i]) i++;
        if (i >= n) break;
        int start = i;
        int end = i + 1;
        while (end < n) {
            if (a[end] != b[end]) {
                end++;
                continue;
            }
            // 相同字节：若后续 8B 内还有差异则吞掉间隙
            int look = end;
            int gap_end = end + 8 < n ? end + 8 : n;
            bool more = false;
            while (look < gap_end) {
                if (a[look] != b[look]) { more = true; break; }
                look++;
            }
            if (!more) break;
            end = look + 1;
        }
        if (nr >= 8) return -1;
        out[nr].off = (uint16_t)start;
        out[nr].len = (uint16_t)(end - start);
        nr++;
        i = end;
    }
    return nr;
}

class LogRecord {
public:
    LogType log_type_;         /* 日志对应操作的类型 */
    lsn_t lsn_;                /* 当前日志的lsn */
    uint32_t log_tot_len_;     /* 整个日志记录的长度 */
    txn_id_t log_tid_;         /* 创建当前日志的事务ID */
    lsn_t prev_lsn_;           /* 事务创建的前一条日志记录的lsn，用于undo */

    // 把日志记录序列化到dest中
    virtual void serialize (char* dest) const {
        memcpy(dest + OFFSET_LOG_TYPE, &log_type_, sizeof(LogType));
        memcpy(dest + OFFSET_LSN, &lsn_, sizeof(lsn_t));
        memcpy(dest + OFFSET_LOG_TOT_LEN, &log_tot_len_, sizeof(uint32_t));
        memcpy(dest + OFFSET_LOG_TID, &log_tid_, sizeof(txn_id_t));
        memcpy(dest + OFFSET_PREV_LSN, &prev_lsn_, sizeof(lsn_t));
    }
    // 从src中反序列化出一条日志记录
    virtual void deserialize(const char* src) {
        log_type_ = *reinterpret_cast<const LogType*>(src);
        lsn_ = *reinterpret_cast<const lsn_t*>(src + OFFSET_LSN);
        log_tot_len_ = *reinterpret_cast<const uint32_t*>(src + OFFSET_LOG_TOT_LEN);
        log_tid_ = *reinterpret_cast<const txn_id_t*>(src + OFFSET_LOG_TID);
        prev_lsn_ = *reinterpret_cast<const lsn_t*>(src + OFFSET_PREV_LSN);
    }
    // used for debug
    virtual void format_print() {
        std::cout << "log type in father_function: " << LogTypeStr[log_type_] << "\n";
        printf("Print Log Record:\n");
        printf("log_type_: %s\n", LogTypeStr[log_type_].c_str());
        printf("lsn: %d\n", lsn_);
        printf("log_tot_len: %d\n", log_tot_len_);
        printf("log_tid: %d\n", log_tid_);
        printf("prev_lsn: %d\n", prev_lsn_);
    }
};

class BeginLogRecord: public LogRecord {
public:
    BeginLogRecord() {
        log_type_ = LogType::begin;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    BeginLogRecord(txn_id_t txn_id) : BeginLogRecord() {
        log_tid_ = txn_id;
    }
    // 序列化Begin日志记录到dest中
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
    }
    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);   
    }
    virtual void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }
};

/* commit操作的日志记录（仅日志头） */
class CommitLogRecord: public LogRecord {
public:
    CommitLogRecord() {
        log_type_ = LogType::commit;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    CommitLogRecord(txn_id_t txn_id) : CommitLogRecord() { log_tid_ = txn_id; }
};

/* abort操作的日志记录（仅日志头） */
class AbortLogRecord: public LogRecord {
public:
    AbortLogRecord() {
        log_type_ = LogType::ABORT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    AbortLogRecord(txn_id_t txn_id) : AbortLogRecord() { log_tid_ = txn_id; }
};

/* 题10：静态检查点日志记录（仅日志头；建点时已静默，无活跃事务列表） */
class CkptLogRecord: public LogRecord {
public:
    CkptLogRecord() {
        log_type_ = LogType::CKPT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
};

class InsertLogRecord: public LogRecord {
public:
    InsertLogRecord() {
        log_type_ = LogType::INSERT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    InsertLogRecord(txn_id_t txn_id, RmRecord& insert_value, Rid& rid, std::string table_name) 
        : InsertLogRecord() {
        log_tid_ = txn_id;
        insert_value_ = insert_value;
        rid_ = rid;
        log_tot_len_ += sizeof(int);
        log_tot_len_ += insert_value_.size;
        log_tot_len_ += sizeof(Rid);
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }

    // 把insert日志记录序列化到dest中
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &insert_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, insert_value_.data, insert_value_.size);
        offset += insert_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }
    // 从src中反序列化出一条Insert日志记录
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);  
        insert_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + insert_value_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, src + offset, table_name_size_);
    }
    void format_print() override {
        printf("insert record\n");
        LogRecord::format_print();
        printf("insert_value: %s\n", insert_value_.data);
        printf("insert rid: %d, %d\n", rid_.page_no, rid_.slot_no);
        printf("table name: %s\n", table_name_);
    }

    RmRecord insert_value_;     // 插入的记录
    Rid rid_;                   // 记录插入的位置
    char* table_name_;          // 插入记录的表名称
    size_t table_name_size_;    // 表名称的大小
};

/* delete操作的日志记录：旧记录值 + rid + 表名（undo 重插 / redo 删除） */
class DeleteLogRecord: public LogRecord {
public:
    DeleteLogRecord() {
        log_type_ = LogType::DELETE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    DeleteLogRecord(txn_id_t txn_id, RmRecord& delete_value, Rid& rid, std::string table_name)
        : DeleteLogRecord() {
        log_tid_ = txn_id;
        delete_value_ = delete_value;
        rid_ = rid;
        log_tot_len_ += sizeof(int) + delete_value_.size + sizeof(Rid);
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &delete_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, delete_value_.data, delete_value_.size);
        offset += delete_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        delete_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + sizeof(int) + delete_value_.size;
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, src + offset, table_name_size_);
    }

    RmRecord delete_value_;     // 被删除的旧记录
    Rid rid_;
    char* table_name_;
    size_t table_name_size_;
};

/* update操作的日志记录：旧值 + 新值 + rid + 表名（undo 用旧值，redo 用新值） */
class UpdateLogRecord: public LogRecord {
public:
    UpdateLogRecord() {
        log_type_ = LogType::UPDATE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    UpdateLogRecord(txn_id_t txn_id, RmRecord& old_value, RmRecord& new_value, Rid& rid,
                    std::string table_name)
        : UpdateLogRecord() {
        log_tid_ = txn_id;
        old_value_ = old_value;
        new_value_ = new_value;
        rid_ = rid;
        log_tot_len_ += sizeof(int) + old_value_.size;
        log_tot_len_ += sizeof(int) + new_value_.size;
        log_tot_len_ += sizeof(Rid);
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &old_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, old_value_.data, old_value_.size);
        offset += old_value_.size;
        memcpy(dest + offset, &new_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, new_value_.data, new_value_.size);
        offset += new_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        old_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + sizeof(int) + old_value_.size;
        new_value_.Deserialize(src + offset);
        offset += sizeof(int) + new_value_.size;
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, src + offset, table_name_size_);
    }

    RmRecord old_value_;
    RmRecord new_value_;
    Rid rid_;
    char* table_name_;
    size_t table_name_size_;
};

/* P2：update 增量日志 —— 仅存差异字节段（old/new 各一份）
 * payload: u8 tab_len | name | Rid | u8 n_ranges |
 *          n × { u16 off, u16 len, old[len], new[len] } */
class UpdateDeltaLogRecord: public LogRecord {
public:
    static constexpr int MAX_RANGES = 8;

    UpdateDeltaLogRecord() {
        log_type_ = LogType::UPDATE_DELTA;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
        table_name_size_ = 0;
        n_ranges_ = 0;
        memset(ranges_, 0, sizeof(ranges_));
        memset(old_ptrs_, 0, sizeof(old_ptrs_));
        memset(new_ptrs_, 0, sizeof(new_ptrs_));
    }

    /* 从全行 old/new 构造；若不宜用增量则 ranges 为空（调用方应回退全量） */
    UpdateDeltaLogRecord(txn_id_t txn_id, const char* old_data, const char* new_data, int rec_size,
                         Rid& rid, const std::string& table_name)
        : UpdateDeltaLogRecord() {
        log_tid_ = txn_id;
        rid_ = rid;
        table_name_size_ = table_name.length();
        if (table_name_size_ > 255) table_name_size_ = 255;
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, table_name.c_str(), table_name_size_);

        WalDiffRange diffs[MAX_RANGES];
        int nr = wal_byte_diff(old_data, new_data, rec_size, diffs);
        if (nr <= 0) {
            // 无差异或段过多：保持 n_ranges_=0，调用方回退
            return;
        }
        // 估算增量 payload vs 全量（old+new 各 rec_size + 两 size_t 表名等粗算）
        int delta_payload = 1 + (int)table_name_size_ + (int)sizeof(Rid) + 1;
        for (int i = 0; i < nr; i++) delta_payload += 4 + 2 * diffs[i].len;
        int full_payload = 2 * (int)sizeof(int) + 2 * rec_size + (int)sizeof(Rid)
                           + (int)sizeof(size_t) + (int)table_name_size_;
        if (delta_payload >= full_payload * 2 / 3) {
            return;  // 省不到 1/3，回退全量
        }

        n_ranges_ = (uint8_t)nr;
        log_tot_len_ = LOG_HEADER_SIZE + delta_payload;
        for (int i = 0; i < nr; i++) {
            ranges_[i] = diffs[i];
            old_ptrs_[i] = new char[diffs[i].len];
            new_ptrs_[i] = new char[diffs[i].len];
            memcpy(old_ptrs_[i], old_data + diffs[i].off, diffs[i].len);
            memcpy(new_ptrs_[i], new_data + diffs[i].off, diffs[i].len);
        }
    }

    ~UpdateDeltaLogRecord() {
        delete[] table_name_;
        for (int i = 0; i < MAX_RANGES; i++) {
            delete[] old_ptrs_[i];
            delete[] new_ptrs_[i];
        }
    }

    bool useful() const { return n_ranges_ > 0; }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        uint8_t tlen = (uint8_t)table_name_size_;
        memcpy(dest + offset, &tlen, 1); offset += 1;
        memcpy(dest + offset, table_name_, table_name_size_); offset += (int)table_name_size_;
        memcpy(dest + offset, &rid_, sizeof(Rid)); offset += sizeof(Rid);
        memcpy(dest + offset, &n_ranges_, 1); offset += 1;
        for (int i = 0; i < n_ranges_; i++) {
            memcpy(dest + offset, &ranges_[i].off, 2); offset += 2;
            memcpy(dest + offset, &ranges_[i].len, 2); offset += 2;
            memcpy(dest + offset, old_ptrs_[i], ranges_[i].len); offset += ranges_[i].len;
            memcpy(dest + offset, new_ptrs_[i], ranges_[i].len); offset += ranges_[i].len;
        }
    }

    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        int offset = OFFSET_LOG_DATA;
        uint8_t tlen = *reinterpret_cast<const uint8_t*>(src + offset); offset += 1;
        table_name_size_ = tlen;
        table_name_ = new char[table_name_size_];
        memcpy(table_name_, src + offset, table_name_size_); offset += (int)table_name_size_;
        rid_ = *reinterpret_cast<const Rid*>(src + offset); offset += sizeof(Rid);
        n_ranges_ = *reinterpret_cast<const uint8_t*>(src + offset); offset += 1;
        for (int i = 0; i < n_ranges_; i++) {
            ranges_[i].off = *reinterpret_cast<const uint16_t*>(src + offset); offset += 2;
            ranges_[i].len = *reinterpret_cast<const uint16_t*>(src + offset); offset += 2;
            old_ptrs_[i] = new char[ranges_[i].len];
            new_ptrs_[i] = new char[ranges_[i].len];
            memcpy(old_ptrs_[i], src + offset, ranges_[i].len); offset += ranges_[i].len;
            memcpy(new_ptrs_[i], src + offset, ranges_[i].len); offset += ranges_[i].len;
        }
    }

    Rid rid_;
    char* table_name_;
    size_t table_name_size_;
    uint8_t n_ranges_;
    WalDiffRange ranges_[MAX_RANGES];
    char* old_ptrs_[MAX_RANGES];
    char* new_ptrs_[MAX_RANGES];
};

/* 日志缓冲区，只有一个buffer，因此需要阻塞地去把日志写入缓冲区中 */

class LogBuffer {
public:
    LogBuffer() { 
        offset_ = 0; 
        memset(buffer_, 0, sizeof(buffer_));
    }

    bool is_full(int append_size) {
        if(offset_ + append_size > LOG_BUFFER_SIZE)
            return true;
        return false;
    }

    char buffer_[LOG_BUFFER_SIZE+1];
    int offset_;    // 写入log的offset
};

/* 日志管理器，负责把日志写入日志缓冲区，以及把日志缓冲区中的内容写入磁盘中 */
class LogManager {
public:
    LogManager(DiskManager* disk_manager);
    ~LogManager();

    lsn_t add_log_to_buffer(LogRecord* log_record);
    void flush_log_to_disk();
    void wait_for_persist(lsn_t target_lsn);

    LogBuffer* get_log_buffer() { return &bufs_[active_]; }

    // 题10：以磁盘上既有日志长度初始化追加偏移（启动恢复后调用）
    void init_offset(long disk_bytes) {
        std::scoped_lock<std::mutex> lock(append_mtx_);
        total_offset_ = disk_bytes;
    }
    // 题10：当前日志总长（含缓冲区未刷部分）= 下一条记录的起始偏移
    long cur_offset() {
        std::scoped_lock<std::mutex> lock(append_mtx_);
        return total_offset_;
    }

    WalStats& wal_stats() { return wal_stats_; }

private:
    void flush_worker();
    void dump_wal_stats(const char* tag);

    std::atomic<lsn_t> global_lsn_{0};  // 全局lsn，递增，用于为每条记录分发lsn
    std::atomic<bool> stop_{false};      // 两把锁都要看到，用 atomic 免得互相等锁

    /* 锁拆分：append_mtx_ 只护"往缓冲区写"这条路径（add_log_to_buffer + worker 摘缓冲），
     * persist_mtx_ 只护"等持久化"这条路径（persist_lsn_/requested_lsn_ + persist_cv_）。
     * 拆开之前两者共用一把锁，高并发 commit 时一堆线程在 persist_cv_ 上排队醒来，
     * 会跟正在写日志的线程抢同一把锁；分开后 commit 线程之间的排队不再挡 add_log。 */
    std::mutex append_mtx_;
    std::mutex persist_mtx_;
    std::condition_variable cv_;         // 配 append_mtx_，唤醒 flush worker
    std::condition_variable persist_cv_; // 配 persist_mtx_，唤醒等持久化的 committer
    std::condition_variable space_cv_;   // 配 append_mtx_，active 缓冲满时等待换出的写日志者
    std::thread flush_thread_;
    bool flush_requested_{false};        // append_mtx_
    /* 双缓冲组提交：worker 把 active 换出后在【锁外】write+fsync，期间到达的日志
     * 进入新 active 排队——fsync 时长天然成为聚合窗口，一次 fsync 覆盖一批 commit。
     * 旧实现 write+fsync 在锁内：fsync 期间所有日志追加被锁死，每笔 commit 实付一次
     * fsync，慢盘上吞吐上限 = 1/fsync 延迟（OJ tpmC 天花板主因）。 */
    LogBuffer bufs_[2];                 // append_mtx_
    int active_ = 0;                    // append_mtx_，当前接收写入的缓冲下标
    lsn_t persist_lsn_ = INVALID_LSN;   // persist_mtx_，已经持久化到磁盘的最后一条日志号
    lsn_t requested_lsn_ = INVALID_LSN; // persist_mtx_，被等待持久化的最大 lsn（只为它们 fsync——
                                        // 无人等待时不刷，避免后台连续 fsync 抢占慢盘 IO）
    int space_waiters_ = 0;             // append_mtx_，等待缓冲空间的写日志者数
    long total_offset_ = 0;             // append_mtx_，题10：日志文件逻辑总长（磁盘已刷 + 缓冲未刷）
    DiskManager* disk_manager_;

    /* S5.2：组提交微批窗口（微秒）。2026-07-09 曾把默认改成硬编码 80，
     * 上线后 OJ tpmC 从 ~950 掉到 ~800——本机裸机 sleep_for 精度好测不出问题，
     * OJ 判题机大概率是虚拟化/共享环境，sleep_for(80us) 实际唤醒延迟可能是
     * 几百微秒到毫秒级，而 first_round 几乎每个刷盘批次都会触发一次，等于
     * 给几乎每次 commit 都加了一次不可控的调度延迟税。已回退默认关闭（0），
     * 只能靠 RMDB_GROUP_COMMIT_WINDOW_US 显式开启做受控实验，不再默认生效。
     * 只读，构造后不再修改，flush_worker 里访问不用加锁。 */
    long group_commit_window_us_ = 0;

    /* P3：字节/等待者阈值组提交（默认关）。与 S5.2 sleep_for 的区别：
     * cv_.wait_until + 谓词，新 committer 的 notify 立刻打断；阈值满足零延迟开刷。
     * RMDB_GC_WAITERS / RMDB_GC_BYTES / RMDB_GC_DEADLINE_US */
    int gc_waiters_ = 0;            // ≥N 个 persist 等待者才刷；0=忽略
    int gc_bytes_ = 0;              // 积压 ≥M 字节才刷；0=忽略
    long gc_deadline_us_ = 200;     // 孤独 committer 最长等待
    std::atomic<int> persist_waiters_{0};  // wait_for_persist 慢路径在途数

    WalStats wal_stats_;
    bool wal_stats_print_ = false;  // RMDB_WAL_STATS=1
};

// 题10：全局日志管理器指针——缓冲池在把任意脏页写盘前先刷日志（WAL 顺序）
extern LogManager* g_log_manager; 
