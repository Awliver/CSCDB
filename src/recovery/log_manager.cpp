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
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include "log_manager.h"

LogManager* g_log_manager = nullptr;

static inline void record_fsync_latency(WalStats& s, uint64_t us) {
    s.n_fsync.fetch_add(1, std::memory_order_relaxed);
    s.fsync_us_total.fetch_add(us, std::memory_order_relaxed);
    uint64_t cur = s.fsync_us_max.load(std::memory_order_relaxed);
    while (us > cur &&
           !s.fsync_us_max.compare_exchange_weak(cur, us, std::memory_order_relaxed)) {
    }
    int bucket;
    if (us < 1000) bucket = 0;
    else if (us < 5000) bucket = 1;
    else if (us < 10000) bucket = 2;
    else if (us < 20000) bucket = 3;
    else if (us < 50000) bucket = 4;
    else bucket = 5;
    s.lat_bucket[bucket].fetch_add(1, std::memory_order_relaxed);
}

LogManager::LogManager(DiskManager* disk_manager) {
    disk_manager_ = disk_manager;
    // S5.2：默认窗口见成员声明处注释；env 存在且能解析出合法非负整数时覆盖
    // （允许显式设 0 关闭，本地 A/B 用；env 缺失/非法时维持代码默认值）。
    if (const char* env = std::getenv("RMDB_GROUP_COMMIT_WINDOW_US")) {
        char* end = nullptr;
        long v = std::strtol(env, &end, 10);
        if (end != env && v >= 0) {
            group_commit_window_us_ = v;
        }
    }
    if (const char* env = std::getenv("RMDB_WAL_STATS")) {
        wal_stats_print_ = (env[0] == '1' && env[1] == '\0');
    }
    // P3：阈值组提交（默认关；须 P0 显示 waits/fsync 低才值得开）
    if (const char* env = std::getenv("RMDB_GC_WAITERS")) {
        char* end = nullptr;
        long v = std::strtol(env, &end, 10);
        if (end != env && v >= 0 && v <= 1024) gc_waiters_ = (int)v;
    }
    if (const char* env = std::getenv("RMDB_GC_BYTES")) {
        char* end = nullptr;
        long v = std::strtol(env, &end, 10);
        if (end != env && v >= 0 && v <= (1 << 30)) gc_bytes_ = (int)v;
    }
    if (const char* env = std::getenv("RMDB_GC_DEADLINE_US")) {
        char* end = nullptr;
        long v = std::strtol(env, &end, 10);
        if (end != env && v >= 0) gc_deadline_us_ = v;
    }
    // 兜底 catch：flush 线程的异常（磁盘满等）逃出线程函数会 std::terminate 杀死
    // 整个服务器。失败时置 stop_ 并唤醒全部等待者（wait 谓词含 stop_，不会永久挂起），
    // 服务降级但进程存活。
    flush_thread_ = std::thread([this] {
        try {
            flush_worker();
        } catch (std::exception &e) {
            fprintf(stderr, "[wal-flush] fatal: %s (flush worker stopped)\n", e.what());
            stop_.store(true);
            cv_.notify_all();
            persist_cv_.notify_all();
        } catch (...) {
            fprintf(stderr, "[wal-flush] fatal: unknown exception (flush worker stopped)\n");
            stop_.store(true);
            cv_.notify_all();
            persist_cv_.notify_all();
        }
    });
}

void LogManager::dump_wal_stats(const char* tag) {
    if (!wal_stats_print_) return;
    uint64_t n_fsync = wal_stats_.n_fsync.load(std::memory_order_relaxed);
    uint64_t fsync_us = wal_stats_.fsync_us_total.load(std::memory_order_relaxed);
    uint64_t fsync_max = wal_stats_.fsync_us_max.load(std::memory_order_relaxed);
    uint64_t n_bytes = wal_stats_.n_bytes.load(std::memory_order_relaxed);
    uint64_t n_batches = wal_stats_.n_batches.load(std::memory_order_relaxed);
    uint64_t n_waits = wal_stats_.n_commit_waits.load(std::memory_order_relaxed);
    uint64_t avg_us = n_fsync ? fsync_us / n_fsync : 0;
    double bytes_per_batch = n_batches ? (double)n_bytes / (double)n_batches : 0.0;
    double waits_per_fsync = n_fsync ? (double)n_waits / (double)n_fsync : 0.0;
    fprintf(stderr,
            "[WAL_STATS %s] fsync=%llu avg_us=%llu max_us=%llu bytes=%llu batches=%llu "
            "bytes/batch=%.0f commit_waits=%llu waits/fsync=%.2f "
            "lat_ms[<1=%llu 1-5=%llu 5-10=%llu 10-20=%llu 20-50=%llu >50=%llu]\n",
            tag,
            (unsigned long long)n_fsync,
            (unsigned long long)avg_us,
            (unsigned long long)fsync_max,
            (unsigned long long)n_bytes,
            (unsigned long long)n_batches,
            bytes_per_batch,
            (unsigned long long)n_waits,
            waits_per_fsync,
            (unsigned long long)wal_stats_.lat_bucket[0].load(std::memory_order_relaxed),
            (unsigned long long)wal_stats_.lat_bucket[1].load(std::memory_order_relaxed),
            (unsigned long long)wal_stats_.lat_bucket[2].load(std::memory_order_relaxed),
            (unsigned long long)wal_stats_.lat_bucket[3].load(std::memory_order_relaxed),
            (unsigned long long)wal_stats_.lat_bucket[4].load(std::memory_order_relaxed),
            (unsigned long long)wal_stats_.lat_bucket[5].load(std::memory_order_relaxed));
}

LogManager::~LogManager() {
    stop_.store(true);
    {
        std::lock_guard<std::mutex> lock(append_mtx_);
        flush_requested_ = true;
    }
    cv_.notify_all();
    space_cv_.notify_all();
    persist_cv_.notify_all();
    if (flush_thread_.joinable()) flush_thread_.join();
    dump_wal_stats("shutdown");
}

/**
 * @description: 添加日志记录到日志缓冲区中，并返回日志记录号
 * @param {LogRecord*} log_record 要写入缓冲区的日志记录
 * @return {lsn_t} 返回该日志的日志记录号
 */
lsn_t LogManager::add_log_to_buffer(LogRecord* log_record) {
    std::unique_lock<std::mutex> lock(append_mtx_);
    int len = (int)log_record->log_tot_len_;
    while (bufs_[active_].is_full(len)) {
        // active 满：请求换出并等待（另一块缓冲正被 worker 刷盘时形成自然背压）
        flush_requested_ = true;
        space_waiters_++;
        cv_.notify_one();
        space_cv_.wait(lock, [&] { return !bufs_[active_].is_full(len) || stop_.load(); });
        space_waiters_--;
        if (stop_.load()) break;
    }
    log_record->lsn_ = global_lsn_++;
    log_record->serialize(bufs_[active_].buffer_ + bufs_[active_].offset_);
    bufs_[active_].offset_ += len;
    total_offset_ += len;
    return log_record->lsn_;
}

// 请求 flush worker 干活：只碰 append_mtx_ 一下设标志位，不在这里等结果，
// 所以不会跟正在等 persist_cv_ 的 committer 抢锁。
static inline void request_flush(std::mutex &append_mtx, std::condition_variable &cv,
                                  bool &flush_requested) {
    {
        std::lock_guard<std::mutex> lock(append_mtx);
        flush_requested = true;
    }
    cv.notify_one();
}

/**
 * @description: 同步等待"到当前为止的全部日志"持久化（WAL 顺序：缓冲池写脏页前调用）
 */
void LogManager::flush_log_to_disk() {
    lsn_t target = global_lsn_.load() - 1;
    std::unique_lock<std::mutex> plock(persist_mtx_);
    if (persist_lsn_ >= target) return;
    if (target > requested_lsn_) requested_lsn_ = target;
    plock.unlock();
    request_flush(append_mtx_, cv_, flush_requested_);
    plock.lock();
    persist_cv_.wait(plock, [&] { return persist_lsn_ >= target || stop_.load(); });
}

void LogManager::wait_for_persist(lsn_t target_lsn) {
    std::unique_lock<std::mutex> plock(persist_mtx_);
    if (persist_lsn_ >= target_lsn) return;
    // P0：慢路径——需要真正等待 fsync
    wal_stats_.n_commit_waits.fetch_add(1, std::memory_order_relaxed);
    persist_waiters_.fetch_add(1, std::memory_order_relaxed);  // P3
    if (target_lsn > requested_lsn_) requested_lsn_ = target_lsn;
    plock.unlock();
    request_flush(append_mtx_, cv_, flush_requested_);
    plock.lock();
    persist_cv_.wait(plock, [&] { return persist_lsn_ >= target_lsn || stop_.load(); });
    persist_waiters_.fetch_sub(1, std::memory_order_relaxed);
}

/* 双缓冲组提交：换出 active 后在【锁外】write+fsync——fsync 期间新日志进入新 active
 * 排队，fsync 时长天然成为聚合窗口，一次 fsync 覆盖期间到达的全部 commit。
 * append_mtx_ 只用来摘换缓冲；persist_lsn_/requested_lsn_ 单独用 persist_mtx_ 看，
 * 两把锁不嵌套持有，避免和 wait_for_persist 互相等待。 */
void LogManager::flush_worker() {
    std::unique_lock<std::mutex> lock(append_mtx_);
    auto last_stats = std::chrono::steady_clock::now();
    while (true) {
        cv_.wait(lock, [&] { return stop_.load() || flush_requested_; });
        flush_requested_ = false;
        bool first_round = true;   // S5.2：只在本批第一轮判断是否微批等待，避免变成无差别后台 fsync
        while (bufs_[active_].offset_ > 0) {
            // 只为"有人等待"的目标刷盘：有 committer 等 lsn、有写者等缓冲空间、或正在停机。
            // 不做无差别排空——否则语句日志一到就被后台连续 fsync，慢盘上抢占数据页 IO
            // （OJ 实测回退 -10% 的来源）。fsync 期间积累的下一批在仍有等待者时立即接续。
            bool need_flush;
            {
                std::lock_guard<std::mutex> plock(persist_mtx_);
                need_flush = persist_lsn_ < requested_lsn_;
            }
            if (!need_flush && space_waiters_ == 0 && !stop_.load()) break;

            // P3 阈值组提交（优先于 S5.2）：阈值已满足 → 零延迟刷；
            // 否则 wait_until(deadline)，新 committer 的 notify 立刻打断重查。
            // 仅本批第一轮；space_waiters_>0 时不拖（缓冲满必须立刻腾空间）。
            const bool p3_on = (gc_waiters_ > 0 || gc_bytes_ > 0);
            if (first_round && p3_on && need_flush &&
                space_waiters_ == 0 && !stop_.load()) {
                first_round = false;
                auto thresh_met = [&] {
                    if (space_waiters_ > 0 || stop_.load()) return true;
                    if (gc_waiters_ > 0 &&
                        persist_waiters_.load(std::memory_order_relaxed) >= gc_waiters_)
                        return true;
                    if (gc_bytes_ > 0 && bufs_[active_].offset_ >= gc_bytes_) return true;
                    return false;
                };
                if (!thresh_met()) {
                    auto deadline = std::chrono::steady_clock::now() +
                                    std::chrono::microseconds(gc_deadline_us_);
                    cv_.wait_until(lock, deadline, thresh_met);
                    continue;  // 重新评估 need_flush / 是否该刷
                }
                // 阈值已满足：直接落入下方换缓冲刷盘
            } else if (first_round && group_commit_window_us_ > 0 && need_flush &&
                space_waiters_ == 0 && !stop_.load()) {
                // S5.2 微批（默认关；与 P3 互斥，P3 开时不走这条）
                first_round = false;
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::microseconds(group_commit_window_us_));
                lock.lock();
                continue;
            }
            first_round = false;
            int fl = active_;
            active_ ^= 1;                       // 单 worker 串行 ⇒ 换入的缓冲此刻必为空
            int batch_bytes = bufs_[fl].offset_;
            // P1：批头占 12B；batch_off 为批头文件偏移（= 本批记录追加前的 total_offset_）
            long batch_off = total_offset_ - batch_bytes;
            total_offset_ += WAL_BATCH_HDR_SIZE;
            lsn_t target = global_lsn_.load() - 1;
            space_cv_.notify_all();             // 新 active 可写
            lock.unlock();

            // 批帧：magic + len + crc32(body)，再写 body（一次 ensure 覆盖头+体）
            char hdr[WAL_BATCH_HDR_SIZE];
            uint32_t magic = WAL_BATCH_MAGIC;
            uint32_t blen = (uint32_t)batch_bytes;
            uint32_t crc = wal_crc32(bufs_[fl].buffer_, (size_t)batch_bytes);
            memcpy(hdr + 0, &magic, 4);
            memcpy(hdr + 4, &blen, 4);
            memcpy(hdr + 8, &crc, 4);
            disk_manager_->write_log(hdr, WAL_BATCH_HDR_SIZE, batch_off);
            disk_manager_->write_log(bufs_[fl].buffer_, batch_bytes, batch_off + WAL_BATCH_HDR_SIZE);
            int total_write = batch_bytes + WAL_BATCH_HDR_SIZE;
            wal_stats_.n_bytes.fetch_add((uint64_t)total_write, std::memory_order_relaxed);
            wal_stats_.n_batches.fetch_add(1, std::memory_order_relaxed);
            auto t0 = std::chrono::steady_clock::now();
            disk_manager_->sync_log();
            auto t1 = std::chrono::steady_clock::now();
            uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            record_fsync_latency(wal_stats_, us);
            {
                std::lock_guard<std::mutex> plock(persist_mtx_);
                persist_lsn_ = target;
            }
            persist_cv_.notify_all();
            lock.lock();
            bufs_[fl].offset_ = 0;
            space_cv_.notify_all();

            if (wal_stats_print_) {
                auto now = std::chrono::steady_clock::now();
                if (now - last_stats >= std::chrono::seconds(10)) {
                    last_stats = now;
                    dump_wal_stats("periodic");
                }
            }
        }
        if (stop_.load()) break;
    }
}
