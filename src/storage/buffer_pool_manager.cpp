/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "buffer_pool_manager.h"
#include "recovery/log_manager.h"
#include <cstring>

void BufferPoolManager::start_cleaner() {
    std::scoped_lock lk(cleaner_mtx_);
    if (cleaner_started_) return;
    cleaner_stop_ = false;
    cleaner_started_ = true;
    cleaner_thread_ = std::thread([this] { cleaner_loop(); });
}

void BufferPoolManager::stop_cleaner() {
    {
        std::scoped_lock lk(cleaner_mtx_);
        if (!cleaner_started_) return;
        cleaner_stop_ = true;
    }
    cleaner_cv_.notify_all();
    if (cleaner_thread_.joinable()) cleaner_thread_.join();
    std::scoped_lock lk(cleaner_mtx_);
    cleaner_started_ = false;
}

// pin==0 脏页：锁内选帧并拷贝快照，锁外 WAL+写盘，再锁内校验后清 dirty（同 delete_page 模式）。
void BufferPoolManager::cleaner_loop() {
    const size_t low_water = pool_size_ / 16;
    size_t next_shard = 0;
    while (true) {
        {
            std::unique_lock<std::mutex> lk(cleaner_mtx_);
            if (cleaner_stop_) break;
            cleaner_cv_.wait_for(lk, std::chrono::milliseconds(CLEANER_INTERVAL_MS),
                                 [this] { return cleaner_stop_; });
            if (cleaner_stop_) break;
        }
        {
            std::scoped_lock evict_lock(evict_latch_);
            if (free_list_.size() >= low_water) continue;
        }
        int flushed = 0;
        for (size_t k = 0; k < BPM_NSHARDS && flushed < CLEANER_BATCH; ++k) {
            BpmShard &sh = shards_[(next_shard + k) % BPM_NSHARDS];
            while (flushed < CLEANER_BATCH) {
                if (cleaner_stop_) return;
                frame_id_t target = INVALID_FRAME_ID;
                PageId pid{-1, INVALID_PAGE_ID};
                char flush_buf[PAGE_SIZE];
                {
                    std::unique_lock<std::shared_mutex> lock(sh.latch_);
                    for (auto &entry : sh.page_table_) {
                        frame_id_t f = entry.second;
                        Page &pg = pages_[f];
                        if (pg.pin_count_ != 0 || !pg.is_dirty_) continue;
                        {
                            std::scoped_lock io_lock(io_mutex_);
                            if (frame_io_inflight_[f]) continue;
                        }
                        target = f;
                        pid = pg.id_;
                        memcpy(flush_buf, pg.data_, PAGE_SIZE);
                        break;
                    }
                    if (target == INVALID_FRAME_ID) break;
                }
                if (g_log_manager) g_log_manager->flush_log_to_disk();
                disk_manager_->write_page(pid.fd, pid.page_no, flush_buf, PAGE_SIZE);
                {
                    std::unique_lock<std::shared_mutex> lock(sh.latch_);
                    Page &pg = pages_[target];
                    if (pg.pin_count_ == 0 && pg.id_ == pid && pg.is_dirty_) {
                        pg.is_dirty_ = false;
                    }
                }
                ++flushed;
            }
        }
        next_shard = (next_shard + 1) % BPM_NSHARDS;
    }
}

bool BufferPoolManager::reserve_victim_nolock(size_t pref_shard, frame_id_t* out_frame,
                                              PageId* old_page_id, bool* need_flush) {
    // 1) 优先使用全局空闲帧（无页帧，无需落盘/换出）
    if (!free_list_.empty()) {
        frame_id_t f = free_list_.front();
        free_list_.pop_front();
        *out_frame = f;
        *old_page_id = PageId{-1, INVALID_PAGE_ID};
        *need_flush = false;
        pages_[f].pin_count_ = 1;
        std::scoped_lock io_lock(io_mutex_);
        frame_io_inflight_[f] = true;
        return true;
    }
    // 2) 跨分片扫描可淘汰帧（从 pref_shard 起以提升局部性）。
    //    不变式：shards_[s].replacer_ 中的帧必承载 hash%N==s 的页且 pin_count==0、非 I/O 中。
    for (size_t k = 0; k < BPM_NSHARDS; ++k) {
        size_t s = (pref_shard + k) % BPM_NSHARDS;
        BpmShard &sh = shards_[s];
        std::unique_lock<std::shared_mutex> lk(sh.latch_);
        frame_id_t f;
        if (!sh.replacer_->victim(&f)) continue;   // 本分片无可淘汰帧
        Page &victim = pages_[f];
        *out_frame = f;
        *old_page_id = victim.id_;
        *need_flush = victim.is_dirty_;
        if (victim.id_.page_no != INVALID_PAGE_ID) {
            sh.page_table_.erase(page_key(victim.id_));
        }
        victim.pin_count_ = 1;                       // 预占，防止被并发再次选中
        std::scoped_lock io_lock(io_mutex_);
        frame_io_inflight_[f] = true;
        return true;
    }
    return false;
}

Page* BufferPoolManager::fetch_page(PageId page_id) {
    const uint64_t key = page_key(page_id);
    const size_t si = shard_of_page(page_id);
    BpmShard &shard = shards_[si];
    PageId old_page_id{-1, INVALID_PAGE_ID};
    frame_id_t frame_id = INVALID_FRAME_ID;
    bool need_flush_old = false;
    while (true) {
        {
            std::unique_lock<std::mutex> infl_lock(shard.inflight_mtx_);
            if (shard.page_io_inflight_.count(key)) {
                shard.inflight_cv_.wait(infl_lock, [&] {
                    return shard.page_io_inflight_.count(key) == 0;
                });
                continue;
            }
        }
        frame_id_t hit_frame = INVALID_FRAME_ID;
        {
            std::shared_lock<std::shared_mutex> lock(shard.latch_);
            auto it = shard.page_table_.find(key);
            if (it != shard.page_table_.end()) {
                hit_frame = it->second;
                if (pages_[hit_frame].pin_count_ > 0) {
                    pages_[hit_frame].pin_count_++;
                    return &pages_[hit_frame];
                }
            }
        }
        if (hit_frame != INVALID_FRAME_ID) {
            // pin==0 命中：unique 下 replacer->pin + pin++，I/O 中则放锁等待
            frame_id_t wait_frame = INVALID_FRAME_ID;
            {
                std::unique_lock<std::shared_mutex> lock(shard.latch_);
                auto it = shard.page_table_.find(key);
                if (it == shard.page_table_.end()) continue;
                hit_frame = it->second;
                Page &p = pages_[hit_frame];
                if (!(p.id_ == page_id)) continue;
                {
                    std::scoped_lock io_lock(io_mutex_);
                    if (frame_io_inflight_[hit_frame]) {
                        wait_frame = hit_frame;
                    }
                }
                if (wait_frame != INVALID_FRAME_ID) {
                    lock.unlock();
                    std::unique_lock<std::mutex> io_lock(io_mutex_);
                    io_cv_.wait(io_lock, [&] { return !frame_io_inflight_[wait_frame]; });
                    continue;
                }
                if (p.pin_count_ == 0) shard.replacer_->pin(hit_frame);
                p.pin_count_++;
                return &p;
            }
        }
        {
            // 未命中：在全局 evict_latch_ 下预占 victim（冷路径）
            std::scoped_lock evict_lock(evict_latch_);
            {
                std::scoped_lock infl_lock(shard.inflight_mtx_);
                if (shard.page_io_inflight_.count(key)) continue;
            }
            {
                std::shared_lock<std::shared_mutex> lock(shard.latch_);
                if (shard.page_table_.find(key) != shard.page_table_.end()) continue;
            }
            if (!reserve_victim_nolock(si, &frame_id, &old_page_id, &need_flush_old)) return nullptr;
            {
                std::scoped_lock infl_lock(shard.inflight_mtx_);
                shard.page_io_inflight_.insert(key);
            }
        }
        break;
    }
    try {
        if (need_flush_old) {
            if (g_log_manager) g_log_manager->flush_log_to_disk();
            disk_manager_->write_page(old_page_id.fd, old_page_id.page_no, pages_[frame_id].data_, PAGE_SIZE);
        }
        disk_manager_->read_page(page_id.fd, page_id.page_no, pages_[frame_id].data_, PAGE_SIZE);
    } catch (...) {
        std::scoped_lock evict_lock(evict_latch_);
        Page &victim = pages_[frame_id];
        victim.id_ = PageId{-1, INVALID_PAGE_ID};
        victim.pin_count_ = 0;
        victim.is_dirty_ = false;
        free_list_.push_back(frame_id);
        {
            std::scoped_lock io_lock(io_mutex_);
            frame_io_inflight_[frame_id] = false;
            io_cv_.notify_all();
        }
        {
            std::scoped_lock infl_lock(shard.inflight_mtx_);
            shard.page_io_inflight_.erase(key);
            shard.inflight_cv_.notify_all();
        }
        throw;
    }
    {
        std::unique_lock<std::shared_mutex> lock(shard.latch_);
        Page &victim = pages_[frame_id];
        victim.id_ = page_id;
        victim.is_dirty_ = false;
        shard.page_table_[key] = frame_id;
    }
    {
        std::scoped_lock infl_lock(shard.inflight_mtx_);
        shard.page_io_inflight_.erase(key);
        shard.inflight_cv_.notify_all();
    }
    {
        std::scoped_lock io_lock(io_mutex_);
        frame_io_inflight_[frame_id] = false;
        io_cv_.notify_all();
    }
    return &pages_[frame_id];
}

bool BufferPoolManager::unpin_page(PageId page_id, bool is_dirty) {
    BpmShard &shard = shard_for_page(page_id);
    const uint64_t key = page_key(page_id);
    std::unique_lock<std::shared_mutex> lock(shard.latch_);
    auto it = shard.page_table_.find(key);
    if (it == shard.page_table_.end()) return false;
    frame_id_t frame_id = it->second;
    Page& page = pages_[frame_id];
    if (is_dirty) page.is_dirty_ = true;
    if (page.pin_count_ <= 0) return false;
    page.pin_count_--;
    // page_id 必归属本分片，故在本分片锁下操作本分片 replacer，无全局锁（热路径）
    if (page.pin_count_ == 0) shard.replacer_->unpin(frame_id);
    return true;
}

bool BufferPoolManager::flush_page(PageId page_id) {
    BpmShard &shard = shard_for_page(page_id);
    const uint64_t key = page_key(page_id);
    std::unique_lock<std::shared_mutex> lock(shard.latch_);
    auto it = shard.page_table_.find(key);
    if (it == shard.page_table_.end()) return false;
    frame_id_t frame_id = it->second;
    if (g_log_manager) g_log_manager->flush_log_to_disk();
    disk_manager_->write_page(page_id.fd, page_id.page_no, pages_[frame_id].data_, PAGE_SIZE);
    pages_[frame_id].is_dirty_ = false;
    return true;
}

Page* BufferPoolManager::new_page(PageId* page_id) {
    page_id->page_no = disk_manager_->allocate_page(page_id->fd);
    const size_t si = shard_of_page(*page_id);
    BpmShard &shard = shards_[si];
    frame_id_t frame_id = INVALID_FRAME_ID;
    PageId old_page_id{-1, INVALID_PAGE_ID};
    bool need_flush_old = false;
    {
        std::scoped_lock evict_lock(evict_latch_);
        if (!reserve_victim_nolock(si, &frame_id, &old_page_id, &need_flush_old)) return nullptr;
    }
    // 锁外刷旧脏页（与 fetch_page 一致的 WAL 顺序：先 flush_log 再写数据页）
    if (need_flush_old) {
        if (g_log_manager) g_log_manager->flush_log_to_disk();
        disk_manager_->write_page(old_page_id.fd, old_page_id.page_no, pages_[frame_id].data_, PAGE_SIZE);
    }
    {
        std::unique_lock<std::shared_mutex> lock(shard.latch_);
        Page &p = pages_[frame_id];
        p.id_ = *page_id;
        p.is_dirty_ = false;
        p.pin_count_ = 1;           // reserve 已置 1，此处冗余保险
        p.reset_memory();
        shard.page_table_[page_key(*page_id)] = frame_id;
    }
    {
        std::scoped_lock io_lock(io_mutex_);
        frame_io_inflight_[frame_id] = false;
        io_cv_.notify_all();
    }
    return &pages_[frame_id];
}

bool BufferPoolManager::delete_page(PageId page_id) {
    BpmShard &shard = shard_for_page(page_id);
    const uint64_t key = page_key(page_id);
    frame_id_t frame_id = INVALID_FRAME_ID;
    PageId flush_id{-1, INVALID_PAGE_ID};
    char flush_buf[PAGE_SIZE];
    bool need_flush = false;
    {
        std::unique_lock<std::shared_mutex> lock(shard.latch_);
        auto it = shard.page_table_.find(key);
        if (it == shard.page_table_.end()) return true;
        frame_id = it->second;
        Page& page = pages_[frame_id];
        if (page.pin_count_ != 0) return false;
        if (page.is_dirty_) {
            flush_id = page.id_;
            memcpy(flush_buf, page.data_, PAGE_SIZE);
            need_flush = true;
        }
        // 删除前该帧 pin_count==0 → 必在本分片 replacer 中，本分片锁下摘除
        shard.replacer_->pin(frame_id);
        shard.page_table_.erase(it);
        page.id_ = PageId{-1, INVALID_PAGE_ID};
        page.pin_count_ = 0;
        page.is_dirty_ = false;
        page.reset_memory();
    }
    if (need_flush) {
        if (g_log_manager) g_log_manager->flush_log_to_disk();
        disk_manager_->write_page(flush_id.fd, flush_id.page_no, flush_buf, PAGE_SIZE);
    }
    {
        std::scoped_lock evict_lock(evict_latch_);
        free_list_.push_back(frame_id);
    }
    return true;
}

void BufferPoolManager::flush_all_pages(int fd) {
    if (g_log_manager) g_log_manager->flush_log_to_disk();
    for (auto &shard : shards_) {
        std::unique_lock<std::shared_mutex> lock(shard.latch_);
        for (auto& entry : shard.page_table_) {
            const PageId pid{static_cast<int>(entry.first >> 32),
                             static_cast<page_id_t>(entry.first)};
            if (pid.fd != fd) continue;
            frame_id_t frame_id = entry.second;
            disk_manager_->write_page(pid.fd, pid.page_no, pages_[frame_id].data_, PAGE_SIZE);
            pages_[frame_id].is_dirty_ = false;
        }
    }
}

void BufferPoolManager::delete_all_pages(int fd) {
    if (g_log_manager) g_log_manager->flush_log_to_disk();
    std::vector<frame_id_t> freed;
    for (auto &shard : shards_) {
        std::unique_lock<std::shared_mutex> lock(shard.latch_);
        for (auto it = shard.page_table_.begin(); it != shard.page_table_.end(); ) {
            const PageId pid{static_cast<int>(it->first >> 32),
                             static_cast<page_id_t>(it->first)};
            if (pid.fd != fd) { ++it; continue; }
            frame_id_t frame_id = it->second;
            Page& page = pages_[frame_id];
            if (page.is_dirty_) {
                disk_manager_->write_page(pid.fd, pid.page_no, page.data_, PAGE_SIZE);
            }
            shard.replacer_->pin(frame_id);   // 若在本分片 replacer 中则摘除（否则 no-op）
            page.id_ = PageId{-1, INVALID_PAGE_ID};
            page.is_dirty_ = false;
            page.pin_count_ = 0;
            page.reset_memory();
            freed.push_back(frame_id);
            it = shard.page_table_.erase(it);
        }
    }
    if (!freed.empty()) {
        std::scoped_lock evict_lock(evict_latch_);
        for (frame_id_t frame_id : freed) {
            free_list_.push_back(frame_id);
        }
    }
}
