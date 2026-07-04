/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <cstring>
#include "log_manager.h"

LogManager* g_log_manager = nullptr;

LogManager::LogManager(DiskManager* disk_manager) {
    disk_manager_ = disk_manager;
    flush_thread_ = std::thread(&LogManager::flush_worker, this);
}

LogManager::~LogManager() {
    {
        std::scoped_lock<std::mutex> lock(latch_);
        stop_ = true;
        flush_requested_ = true;
    }
    cv_.notify_all();
    if (flush_thread_.joinable()) flush_thread_.join();
}

/**
 * @description: 添加日志记录到日志缓冲区中，并返回日志记录号
 * @param {LogRecord*} log_record 要写入缓冲区的日志记录
 * @return {lsn_t} 返回该日志的日志记录号
 */
lsn_t LogManager::add_log_to_buffer(LogRecord* log_record) {
    std::unique_lock<std::mutex> lock(latch_);
    int len = (int)log_record->log_tot_len_;
    while (bufs_[active_].is_full(len)) {
        // active 满：请求换出并等待（另一块缓冲正被 worker 刷盘时形成自然背压）
        flush_requested_ = true;
        cv_.notify_one();
        space_cv_.wait(lock, [&] { return !bufs_[active_].is_full(len) || stop_; });
        if (stop_) break;
    }
    log_record->lsn_ = global_lsn_++;
    log_record->serialize(bufs_[active_].buffer_ + bufs_[active_].offset_);
    bufs_[active_].offset_ += len;
    total_offset_ += len;
    return log_record->lsn_;
}

/**
 * @description: 同步等待"到当前为止的全部日志"持久化（WAL 顺序：缓冲池写脏页前调用）
 */
void LogManager::flush_log_to_disk() {
    std::unique_lock<std::mutex> lock(latch_);
    lsn_t target = global_lsn_.load() - 1;
    if (persist_lsn_ >= target) return;
    flush_requested_ = true;
    cv_.notify_one();
    persist_cv_.wait(lock, [&] { return persist_lsn_ >= target || stop_; });
}

void LogManager::wait_for_persist(lsn_t target_lsn) {
    std::unique_lock<std::mutex> lock(latch_);
    if (persist_lsn_ >= target_lsn) return;
    flush_requested_ = true;
    cv_.notify_one();
    persist_cv_.wait(lock, [&] { return persist_lsn_ >= target_lsn || stop_; });
}

/* 双缓冲组提交：换出 active 后在【锁外】write+fsync——fsync 期间新日志进入新 active
 * 排队，fsync 时长天然成为聚合窗口，一次 fsync 覆盖期间到达的全部 commit。 */
void LogManager::flush_worker() {
    std::unique_lock<std::mutex> lock(latch_);
    while (true) {
        cv_.wait(lock, [&] { return stop_ || flush_requested_; });
        flush_requested_ = false;
        // 连续排空：上一轮 fsync 期间积累的日志立即成为下一批
        while (bufs_[active_].offset_ > 0) {
            int fl = active_;
            active_ ^= 1;                       // 单 worker 串行 ⇒ 换入的缓冲此刻必为空
            lsn_t target = global_lsn_.load() - 1;
            space_cv_.notify_all();             // 新 active 可写
            lock.unlock();
            disk_manager_->write_log(bufs_[fl].buffer_, bufs_[fl].offset_);
            disk_manager_->sync_log();
            lock.lock();
            bufs_[fl].offset_ = 0;
            persist_lsn_ = target;
            persist_cv_.notify_all();
            space_cv_.notify_all();
        }
        if (stop_) break;
    }
}
