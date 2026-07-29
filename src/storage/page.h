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

#include "common/config.h"

#include <atomic>

/**
 * @description: 存储层每个Page的id的声明
 */
struct PageId {
    int fd;  //  Page所在的磁盘文件开启后的文件描述符, 来定位打开的文件在内存中的位置
    page_id_t page_no = INVALID_PAGE_ID;

    friend bool operator==(const PageId &x, const PageId &y) { return x.fd == y.fd && x.page_no == y.page_no; }
    bool operator<(const PageId& x) const {
        if(fd < x.fd) return true;
        return page_no < x.page_no;
    }

    std::string toString() {
        return "{fd: " + std::to_string(fd) + " page_no: " + std::to_string(page_no) + "}"; 
    }

    inline int64_t Get() const {
        return (static_cast<int64_t>(fd << 16) | page_no);
    }
};

// PageId的自定义哈希算法, 用于构建unordered_map<PageId, frame_id_t, PageIdHash>
struct PageIdHash {
    size_t operator()(const PageId &x) const { return (x.fd << 16) | x.page_no; }
};

template <>
struct std::hash<PageId> {
    size_t operator()(const PageId &obj) const { return std::hash<int64_t>()(obj.Get()); }
};

/**
 * @description: Page类声明, Page是RMDB数据块的单位、是负责数据操作Record模块的操作对象，
 * Page对象在磁盘上有文件存储, 若在Buffer中则有帧偏移, 并非特指Buffer或Disk上的数据
 */
class Page {
    friend class BufferPoolManager;

   public:
    
    Page() { reset_memory(); }

    ~Page() = default;

    PageId get_page_id() const { return id_; }

    inline char *get_data() { return data_; }

    bool is_dirty() const { return is_dirty_; }

    static constexpr size_t OFFSET_PAGE_START = 0;
    static constexpr size_t OFFSET_LSN = 0;
    static constexpr size_t OFFSET_PAGE_HDR = 4;

    inline lsn_t get_page_lsn() { return *reinterpret_cast<lsn_t *>(get_data() + OFFSET_LSN) ; }

    inline void set_page_lsn(lsn_t page_lsn) { memcpy(get_data() + OFFSET_LSN, &page_lsn, sizeof(lsn_t)); }

   private:
    void reset_memory() { memset(data_, OFFSET_PAGE_START, PAGE_SIZE); }  // 将data_的PAGE_SIZE个字节填充为0

    /** page的唯一标识符 */
    PageId id_;

    /** The actual data that is stored within a page.
     *  该页面在bufferPool中的偏移地址
     */
    char data_[PAGE_SIZE] = {};

    /** 脏页判断 */
    bool is_dirty_ = false;

    /** 修改版本号（分片 unique latch 下于 unpin(dirty=true) 时自增）。cleaner 拷贝快照
     *  锁外写盘后，仅当版本未变才允许清 is_dirty_——否则写盘期间新到的修改会被误标为
     *  已落盘（脏标丢失 → 页被当干净页淘汰 → 已提交更新静默丢失）。 */
    uint32_t mod_ver_ = 0;

    /** The pin count of this page.
     *  必须原子：fetch_page 命中快路径在【共享】分片锁下自增 pin，多个读者并发命中同一页时
     *  普通 int++ 会丢失增量（两个线程同读 1 同写 2），pin 计数低于真实持有者数 → 在用页/
     *  插入缓存页被提前淘汰 → 帧被复用而旧指针仍在读写 → 堆损坏 SIGABRT（OJ TPC-C 预热
     *  32 并发实测）。命中路径用 CAS 仅在 >0 时自增（绝不把 0 复活，0→1 只走 unique 锁路径）。 */
    std::atomic<int> pin_count_{0};
};