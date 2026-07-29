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
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
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
        std::shared_mutex latch_;           // 保护 page_table_ + replacer_ + free_frames_；读路径 shared、写/replacer/free_frames_ unique
        std::mutex inflight_mtx_;
        std::condition_variable inflight_cv_;
        std::unordered_map<uint64_t, frame_id_t> page_table_;
        std::unordered_set<uint64_t> page_io_inflight_;
        // 本分片 LRU：仅存放“归属本分片(hash(page)%N==s)且 pin_count==0、非 I/O 中”的帧。
        // 所有 replacer 增删（pin/unpin/victim）必须在本分片 latch_ 下进行，故命中/unpin 热路径无全局锁。
        std::unique_ptr<Replacer> replacer_;
        // 空闲帧（无页帧）按 frame_id % BPM_NSHARDS 静态分片持有，缺页/归还都只碰本分片
        // latch_，不再需要一把全局锁——这是 evict 从"全局串行"改成"分片并发"的关键。
        std::deque<frame_id_t> free_frames_;
    };

    size_t pool_size_;
    Page *pages_;
    std::unordered_map<PageId, frame_id_t, PageIdHash> page_table_; // 题一接口保留
    DiskManager *disk_manager_;
    std::mutex io_mutex_;
    std::condition_variable io_cv_;
    /* 原子：fetch 命中快路径需在共享锁下无锁读取（淘汰刷盘期间表项刻意保留，
     * 命中方必须能看到 in-flight 标记并退避）；写入仍在 io_mutex_ 内（cv 语义） */
    std::vector<std::atomic<bool>> frame_io_inflight_;

    /* 若 page_table_ 中 pid 仍映射到帧 f 则移除（淘汰刷盘完成后的延迟摘表） */
    void erase_page_mapping(PageId pid, frame_id_t f);
    std::array<BpmShard, BPM_NSHARDS> shards_;

    // 后台 Page Cleaner：全局空闲帧总数偏低时刷 pin==0 脏页，减轻淘汰冷路径写盘。
    static constexpr int CLEANER_INTERVAL_MS = 20;
    static constexpr int CLEANER_BATCH = 256;
    std::thread cleaner_thread_;
    int trim_tick_ = 0;
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
        : pool_size_(pool_size), disk_manager_(disk_manager), frame_io_inflight_(pool_size) {
        pages_ = new Page[pool_size_];
        for (auto &shard : shards_) {
            shard.replacer_ = std::make_unique<LRUReplacer>(pool_size_);
            shard.page_table_.reserve(pool_size_ / BPM_NSHARDS + 1);
        }
        for (size_t i = 0; i < pool_size_; ++i) {
            shards_[i % BPM_NSHARDS].free_frames_.push_back(static_cast<frame_id_t>(i));
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
    // 调用方无需持有任何全局锁：内部从 pref_shard 起逐个分片加各自 latch_ 尝试取帧
    // （先看该分片 free_frames_，再看该分片 replacer_ 能否淘汰一个 victim）。成功时帧已被
    // 预占（pin_count_=1、frame_io_inflight_=true），若该帧此前承载页，则已从其所属分片
    // page_table_ 删除，并经 old_page_id/need_flush 返回落盘信息。
    bool reserve_victim_nolock(size_t pref_shard, frame_id_t* out_frame,
                               PageId* old_page_id, bool* need_flush);
};
