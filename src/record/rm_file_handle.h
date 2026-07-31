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

#include <assert.h>

#include <memory>

#include "bitmap.h"
#include "common/context.h"
#include "rm_defs.h"
#include <mutex>
#include <unordered_set>

class RmManager;

/* 对表数据文件中的页面进行封装 */
struct RmPageHandle {
    const RmFileHdr *file_hdr;  // 当前页面所在文件的文件头指针
    Page *page;                 // 页面的实际数据，包括页面存储的数据、元信息等
    RmPageHdr *page_hdr;        // page->data的第一部分，存储页面元信息，指针指向首地址，长度为sizeof(RmPageHdr)
    char *bitmap;               // page->data的第二部分，存储页面的bitmap，指针指向首地址，长度为file_hdr->bitmap_size
    char *slots;                // page->data的第三部分，存储表的记录，指针指向首地址，每个slot的长度为file_hdr->record_size

    RmPageHandle(const RmFileHdr *fhdr_, Page *page_) : file_hdr(fhdr_), page(page_) {
        page_hdr = reinterpret_cast<RmPageHdr *>(page->get_data() + page->OFFSET_PAGE_HDR);
        bitmap = page->get_data() + sizeof(RmPageHdr) + page->OFFSET_PAGE_HDR;
        slots = bitmap + file_hdr->bitmap_size;
    }

    // 返回指定slot_no的slot存储收地址
    char* get_slot(int slot_no) const {
        return slots + slot_no * file_hdr->record_size;  // slots的首地址 + slot个数 * 每个slot的大小(每个record的大小)
    }
};

/* 每个RmFileHandle对应一个表的数据文件，里面有多个page，每个page的数据封装在RmPageHandle中 */
class RmFileHandle {
    friend class RmScan;
    friend class RmManager;

   private:
    DiskManager *disk_manager_;
    BufferPoolManager *buffer_pool_manager_;
    int fd_;        // 打开文件后产生的文件句柄
    RmFileHdr file_hdr_;    // 文件头，维护当前表文件的元数据
    // 优化：insert 顺序写页缓存
    int cached_insert_page_no_ = -1;
    Page *cached_insert_page_ = nullptr;
    RmPageHdr *cached_insert_hdr_ = nullptr;
    char *cached_insert_bitmap_ = nullptr;
    char *cached_insert_slots_ = nullptr;
    // MVCC 插入两阶段：bitmap 发布前先占槽，避免扫描落到未登记版本的堆行
    std::unordered_set<int64_t> reserved_inserts_;

    static int64_t rid_key(const Rid &rid) {
        return (static_cast<int64_t>(rid.page_no) << 32) |
               static_cast<uint32_t>(rid.slot_no);
    }

   public:
    RmFileHandle(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager, int fd)
        : disk_manager_(disk_manager), buffer_pool_manager_(buffer_pool_manager), fd_(fd) {
        // 注意：这里从磁盘中读出文件描述符为fd的文件的file_hdr，读到内存中
        // 这里实际就是初始化file_hdr，只不过是从磁盘中读出进行初始化
        // init file_hdr_
        disk_manager_->read_page(fd, RM_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(file_hdr_));
        // 头页自愈：页 0 只在 checkpoint 落盘。热表头页常驻缓存时 kill -9，盘上
        // num_pages 停在旧值而数据页俱在（BPM 淘汰会写数据页）——全表"看似空"、
        // 索引重建也建成空（W=50 崩溃恢复 orders 1.5M→0 的直接原因之一）。
        // 文件物理大小是页数的下界真相，取 max 自愈。
        long fsize = disk_manager_->get_file_size(disk_manager_->get_file_name(fd));
        if (fsize > 0) {
            int pages_on_disk = (int)(fsize / PAGE_SIZE);
            if (pages_on_disk > file_hdr_.num_pages) file_hdr_.num_pages = pages_on_disk;
        }
        // 头页派生字段自愈：nrpp/bitmap_size 是 record_size 的纯函数、建表后不变。
        // 实测存在页 0 被当数据页写坏的形态（order_line bitmap_size 被 Bitmap::set
        // 踩成负数 → get_slot 野指针 → 恢复 undo memcpy SIGSEGV），按公式重导出。
        // record_size 本身坏则无从自愈，交由上层以 PageNotExist/元数据错误失败。
        if (file_hdr_.record_size >= 1 && file_hdr_.record_size <= RM_MAX_RECORD_SIZE) {
            const int nrpp = (BITMAP_WIDTH * (PAGE_SIZE - 1 - (int)sizeof(RmFileHdr)) + 1) /
                             (1 + file_hdr_.record_size * BITMAP_WIDTH);
            const int bms = (nrpp + BITMAP_WIDTH - 1) / BITMAP_WIDTH;
            if (file_hdr_.num_records_per_page != nrpp || file_hdr_.bitmap_size != bms) {
                fprintf(stderr,
                        "[rm-hdr-heal] fd=%d rec=%d nrpp %d->%d bitmap %d->%d\n", fd,
                        file_hdr_.record_size, file_hdr_.num_records_per_page, nrpp,
                        file_hdr_.bitmap_size, bms);
                file_hdr_.num_records_per_page = nrpp;
                file_hdr_.bitmap_size = bms;
            }
        }
        // 空闲链头消毒：页 0/越界页混入链头会把后续插入导向头页（本次事故根因之一）
        if (file_hdr_.first_free_page_no != RM_NO_PAGE &&
            (file_hdr_.first_free_page_no < RM_FIRST_RECORD_PAGE ||
             file_hdr_.first_free_page_no >= file_hdr_.num_pages)) {
            fprintf(stderr, "[rm-hdr-heal] fd=%d first_free %d->-1\n", fd,
                    file_hdr_.first_free_page_no);
            file_hdr_.first_free_page_no = RM_NO_PAGE;
        }
        // disk_manager管理的fd对应的文件中，设置从file_hdr_.num_pages开始分配page_no
        disk_manager_->set_fd2pageno(fd, file_hdr_.num_pages);
    }

    RmFileHdr& get_file_hdr_mut() { return file_hdr_; }
    RmFileHdr get_file_hdr() { return file_hdr_; }

    /* 空闲链指针消毒：next_free_page_no 只有在页曾正规入链时才可信。恢复 redo 在
     * 全零盘面上重建整页时该字段是 0（而非 -1），一旦被弹上链头，后续插入会直写
     * 头页（页 0）。凡从页头读出准备写回 first_free 的值都过这一道。 */
    int sanitize_free_link(int v) const {
        return (v >= RM_FIRST_RECORD_PAGE && v < file_hdr_.num_pages) ? v : RM_NO_PAGE;
    }

    /* slot 越界硬防线：get_slot 是裸指针算术，超界 slot_no（坏索引项/坏 WAL rid 携带）
     * 的 memcpy 会越过本帧 data_ 末尾——先踩本帧 dirty/mod_ver/pin，再踩 BPM 池中
     * 【邻帧的 id_】→ 帧身份被改写而映射仍在 = 陈旧映射（fetch 活锁/错页返回的上游，
     * 2026-07-31 bpm-heal canary 实测两例 id_ 为 ASCII 行数据）。所有按 rid 写堆的
     * 入口先过这一道，宁可单语句失败也不让越界写发生。 */
    void check_slot_bounds(const Rid &rid) const {
        if (rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page) {
            fprintf(stderr, "[rm-slot-guard] reject rid=(%d,%d) nrpp=%d fd=%d\n",
                    rid.page_no, rid.slot_no, file_hdr_.num_records_per_page, fd_);
            throw RecordNotFoundError(rid.page_no, rid.slot_no);
        }
    }
    int GetFd() { return fd_; }

    /* 判断指定位置上是否已经存在一条记录，通过Bitmap来判断 */
    bool is_record(const Rid &rid) const {
        // 头页/越界 rid（历史损坏的 WAL/索引里可能残留）一律视作"无此记录"，
        // 不能让 fetch 的护栏异常炸穿恢复/扫描路径
        if (rid.page_no < RM_FIRST_RECORD_PAGE || rid.page_no >= file_hdr_.num_pages ||
            rid.slot_no < 0 || rid.slot_no >= file_hdr_.num_records_per_page) {
            return false;
        }
        RmPageHandle page_handle = fetch_page_handle(rid.page_no);
        bool exists = Bitmap::is_set(page_handle.bitmap, rid.slot_no);
        buffer_pool_manager_->unpin_page({fd_, rid.page_no}, false);
        return exists;
    }

    std::unique_ptr<RmRecord> get_record(const Rid &rid, Context *context) const;

    Rid insert_record(char *buf, Context *context);

    void insert_record(const Rid &rid, char *buf);

    /* 两阶段插入（MVCC）：先占槽位但不置 bitmap，登记版本后再 publish */
    Rid reserve_insert_slot();
    void publish_insert_slot(const Rid &rid, char *buf);
    void cancel_insert_slot(const Rid &rid);

    void delete_record(const Rid &rid, Context *context);

    void update_record(const Rid &rid, char *buf, Context *context);

    RmPageHandle create_new_page_handle();

    RmPageHandle fetch_page_handle(int page_no) const;

    ~RmFileHandle() {
        if (cached_insert_page_) {
            buffer_pool_manager_->unpin_page(cached_insert_page_->get_page_id(), true);
        }
    }

   public:
    std::mutex op_latch_;   // 题10:结构性操作(槽位分配/位图)互斥,多线程并发插入防竞态

   private:
    int find_free_slot_on_cached_page();
    void ensure_insert_page_cached();

    RmPageHandle create_page_handle();

    void release_page_handle(RmPageHandle &page_handle);
};
