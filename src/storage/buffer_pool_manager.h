/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "disk_manager.h"
#include "errors.h"
#include "page.h"
#include "replacer/lru_replacer.h"
#include "replacer/replacer.h"

class BufferPoolManager {
   private:
    static constexpr size_t BPM_NSHARDS = 64;

    struct BpmShard {
        std::mutex latch_;                  // 保护 page_table_ + 本分片 replacer_ 的成员关系
        std::mutex inflight_mtx_;
        std::condition_variable inflight_cv_;
        std::unordered_map<uint64_t, frame_id_t> page_table_;
        std::unordered_set<uint64_t> page_io_inflight_;
        // 本分片 LRU：仅存放“归属本分片(hash(page)%N==s)且 pin_count==0、非 I/O 中”的帧。
        // 所有 replacer 增删（pin/unpin/victim）必须在本分片 latch_ 下进行，故命中/unpin 热路径无全局锁。
        std::unique_ptr<Replacer> replacer_;
    };

    size_t pool_size_;
    Page *pages_;
    std::unordered_map<PageId, frame_id_t, PageIdHash> page_table_; // 题一接口保留
    std::list<frame_id_t> free_list_;          // 全局空闲帧（无页帧），由 evict_latch_ 保护
    DiskManager *disk_manager_;
    std::mutex evict_latch_;                    // 仅冷路径：守护 free_list_ + 串行跨分片 victim 扫描
    std::mutex io_mutex_;
    std::condition_variable io_cv_;
    std::vector<bool> frame_io_inflight_;
    std::array<BpmShard, BPM_NSHARDS> shards_;

    // 后台 Page Cleaner：free_list_ 偏低时刷 pin==0 脏页，减轻淘汰冷路径写盘。
    static constexpr int CLEANER_INTERVAL_MS = 20;
    static constexpr int CLEANER_BATCH = 256;
    std::thread cleaner_thread_;
    std::mutex cleaner_mtx_;
    std::condition_variable cleaner_cv_;
    bool cleaner_stop_ = false;
    bool cleaner_started_ = false;

    void cleaner_loop();

    static uint64_t page_key(const PageId &pid) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(pid.fd)) << 32) |
               static_cast<uint32_t>(pid.page_no);
    }

    size_t shard_of_page(PageId page_id) const {
        return PageIdHash{}(page_id) % BPM_NSHARDS;
    }

    BpmShard &shard_for_page(PageId page_id) { return shards_[shard_of_page(page_id)]; }

   public:
    BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
        : pool_size_(pool_size), disk_manager_(disk_manager), frame_io_inflight_(pool_size, false) {
        pages_ = new Page[pool_size_];
        for (auto &shard : shards_) {
            shard.replacer_ = std::make_unique<LRUReplacer>(pool_size_);
            shard.page_table_.reserve(pool_size_ / BPM_NSHARDS + 1);
        }
        for (size_t i = 0; i < pool_size_; ++i) {
            free_list_.emplace_back(static_cast<frame_id_t>(i));
        }
    }

    ~BufferPoolManager() {
        stop_cleaner();
        delete[] pages_;
    }

    static void mark_dirty(Page* page) { page->is_dirty_ = true; }

   public:
    // recovery 后 start、关闭前 stop；幂等。
    void start_cleaner();
    void stop_cleaner();

    Page* fetch_page(PageId page_id);

    bool unpin_page(PageId page_id, bool is_dirty);

    bool flush_page(PageId page_id);

    Page* new_page(PageId* page_id);

    bool delete_page(PageId page_id);

    void flush_all_pages(int fd);

    void delete_all_pages(int fd);

   private:
    // 调用方须持 evict_latch_。预占一个可用帧：优先全局空闲帧，否则从 pref_shard 起跨分片
    // 扫描淘汰一个 victim。成功时帧已被预占（pin_count_=1、frame_io_inflight_=true），若该帧
    // 此前承载页，则已从其所属分片 page_table_ 删除，并经 old_page_id/need_flush 返回落盘信息。
    bool reserve_victim_nolock(size_t pref_shard, frame_id_t* out_frame,
                               PageId* old_page_id, bool* need_flush);
};
