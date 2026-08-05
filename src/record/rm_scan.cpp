/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_scan.h"
#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    rid_.page_no = RM_FIRST_RECORD_PAGE;
    rid_.slot_no = -1;
    next();
}

RmScan::~RmScan() {
    release_current_page();
}

void RmScan::pin_current_page() {
    if (pinned_page_no_ == rid_.page_no) return;
    release_current_page();
    RmPageHandle page_handle = file_handle_->fetch_page_handle(rid_.page_no);
    pinned_page_no_ = rid_.page_no;
    pinned_page_ = page_handle.page;
    pinned_bitmap_ = page_handle.bitmap;
    pinned_slots_ = page_handle.slots;
}

void RmScan::release_current_page() {
    if (pinned_page_ == nullptr) return;
    file_handle_->buffer_pool_manager_->unpin_page(pinned_page_->get_page_id(), false);
    pinned_page_no_ = -1;
    pinned_page_ = nullptr;
    pinned_bitmap_ = nullptr;
    pinned_slots_ = nullptr;
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    while (rid_.page_no < file_handle_->file_hdr_.num_pages) {
        // 获取当前页的 bitmap
        pin_current_page();

        // 找当前页内下一个 set 位
        int next_slot = Bitmap::next_bit(true, pinned_bitmap_,
                                        file_handle_->file_hdr_.num_records_per_page,
                                        rid_.slot_no);

        if (next_slot < file_handle_->file_hdr_.num_records_per_page) {
            // 当前页内还有有效记录
            rid_.slot_no = next_slot;
            return;
        }

        // 下一页
        release_current_page();
        rid_.page_no++;
        rid_.slot_no = -1;
    }
    release_current_page();
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    return rid_.page_no >= file_handle_->file_hdr_.num_pages;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const {
    return rid_;
}

const char *RmScan::record_data() const {
    if (is_end() || pinned_slots_ == nullptr) return nullptr;
    return pinned_slots_ + rid_.slot_no * file_handle_->file_hdr_.record_size;
}
