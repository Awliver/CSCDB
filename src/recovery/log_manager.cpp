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
    std::scoped_lock<std::mutex> lock(latch_);
    int len = (int)log_record->log_tot_len_;
    if (log_buffer_.is_full(len)) {
        flush_nolock();
    }
    log_record->lsn_ = global_lsn_++;
    log_record->serialize(log_buffer_.buffer_ + log_buffer_.offset_);
    log_buffer_.offset_ += len;
    total_offset_ += len;
    return log_record->lsn_;
}

void LogManager::flush_nolock() {
    if (log_buffer_.offset_ == 0) return;
    disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
    disk_manager_->sync_log();
    persist_lsn_ = global_lsn_ - 1;
    log_buffer_.offset_ = 0;
    persist_cv_.notify_all();
}

/**
 * @description: 把日志缓冲区的内容刷到磁盘中，由于目前只设置了一个缓冲区，因此需要阻塞其他日志操作
 */
void LogManager::flush_log_to_disk() {
    std::scoped_lock<std::mutex> lock(latch_);
    flush_nolock();
}

void LogManager::wait_for_persist(lsn_t target_lsn) {
    std::unique_lock<std::mutex> lock(latch_);
    flush_requested_ = true;
    cv_.notify_one();
    persist_cv_.wait(lock, [&] { return persist_lsn_ >= target_lsn; });
}

void LogManager::flush_worker() {
    std::unique_lock<std::mutex> lock(latch_);
    while (true) {
        cv_.wait(lock, [&] { return stop_ || flush_requested_; });
        if (flush_requested_) {
            flush_requested_ = false;
            flush_nolock();
        }
        if (stop_) break;
    }
}
