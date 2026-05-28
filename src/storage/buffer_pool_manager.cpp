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

/**
 * @description: 从free_list或replacer中得到可淘汰帧页的 *frame_id
 * @return {bool} true: 可替换帧查找成功 , false: 可替换帧查找失败
 * @param {frame_id_t*} frame_id 帧页id指针,返回成功找到的可替换帧id
 */
bool BufferPoolManager::find_victim_page(frame_id_t* frame_id) {
    if (!free_list_.empty()) {
        *frame_id = free_list_.front();
        free_list_.pop_front();
        return true;
    }
    return replacer_->victim(frame_id);
}

/**
 * @description: 更新页面数据, 如果为脏页则需写入磁盘，再更新为新页面，更新page元数据(data, is_dirty, page_id)和page table
 * @param {Page*} page 写回页指针
 * @param {PageId} new_page_id 新的page_id
 * @param {frame_id_t} new_frame_id 新的帧frame_id
 */
void BufferPoolManager::update_page(Page *page, PageId new_page_id, frame_id_t new_frame_id) {
    if (page->is_dirty_)
    {
        disk_manager_->write_page(page->id_.fd, page->id_.page_no,
                                  page->data_, PAGE_SIZE);
        page->is_dirty_ = false;
    }
    if (page->id_.page_no != INVALID_PAGE_ID)
    {
        page_table_.erase(page->id_);
    }
    page_table_[new_page_id] = new_frame_id;
    page->id_ = new_page_id;
    page->reset_memory();
}

/**
 * @description: 从buffer pool获取需要的页。
 *              如果页表中存在page_id（说明该page在缓冲池中），并且pin_count++。
 *              如果页表不存在page_id（说明该page在磁盘中），则找缓冲池victim page，将其替换为磁盘中读取的page，pin_count置1。
 * @return {Page*} 若获得了需要的页则将其返回，否则返回nullptr
 * @param {PageId} page_id 需要获取的页的PageId
 */
Page* BufferPoolManager::fetch_page(PageId page_id) {
    std::scoped_lock lock{latch_};
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t frame_id = it->second;
        if (pages_[frame_id].pin_count_ == 0)
        {
            replacer_->pin(frame_id);
        }
        pages_[frame_id].pin_count_++;
        return &pages_[frame_id];
    }
    frame_id_t frame_id;
    if (!find_victim_page(&frame_id)) return nullptr;
    update_page(&pages_[frame_id], page_id, frame_id);
    disk_manager_->read_page(page_id.fd, page_id.page_no, pages_[frame_id].data_, PAGE_SIZE);
    pages_[frame_id].pin_count_ = 1;
    replacer_->pin(frame_id);
    return &pages_[frame_id];
}

/**
 * @description: 取消固定pin_count>0的在缓冲池中的page
 * @return {bool} 如果目标页的pin_count<=0则返回false，否则返回true
 * @param {PageId} page_id 目标page的page_id
 * @param {bool} is_dirty 若目标page应该被标记为dirty则为true，否则为false
 */
bool BufferPoolManager::unpin_page(PageId page_id, bool is_dirty) {
    std::scoped_lock lock{latch_};
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    frame_id_t frame_id = it->second;
    Page& page = pages_[frame_id];
    if (is_dirty) page.is_dirty_ = true;
    if (page.pin_count_ <= 0) return false;
    page.pin_count_--;
    if (page.pin_count_ == 0) replacer_->unpin(frame_id);
    return true;
}

/**
 * @description: 将目标页写回磁盘，不考虑当前页面是否正在被使用
 * @return {bool} 成功则返回true，否则返回false(只有page_table_中没有目标页时)
 * @param {PageId} page_id 目标页的page_id，不能为INVALID_PAGE_ID
 */
bool BufferPoolManager::flush_page(PageId page_id) {
    std::scoped_lock lock{latch_};
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    frame_id_t frame_id = it->second;
    disk_manager_->write_page(page_id.fd, page_id.page_no, pages_[frame_id].data_, PAGE_SIZE);
    pages_[frame_id].is_dirty_ = false;
    return true;
}

/**
 * @description: 创建一个新的page，即从磁盘中移动一个新建的空page到缓冲池某个位置。
 * @return {Page*} 返回新创建的page，若创建失败则返回nullptr
 * @param {PageId*} page_id 当成功创建一个新的page时存储其page_id
 */
Page* BufferPoolManager::new_page(PageId* page_id) {
    std::scoped_lock lock{latch_};
    frame_id_t frame_id;
    if (!find_victim_page(&frame_id)) return nullptr;
    page_id->page_no = disk_manager_->allocate_page(page_id->fd);
    update_page(&pages_[frame_id], *page_id, frame_id);
    pages_[frame_id].pin_count_ = 1;
    replacer_->pin(frame_id);
    return &pages_[frame_id];
}

/**
 * @description: 从buffer_pool删除目标页
 * @return {bool} 如果目标页不存在于buffer_pool或者成功被删除则返回true，若其存在于buffer_pool但无法删除则返回false
 * @param {PageId} page_id 目标页
 */
bool BufferPoolManager::delete_page(PageId page_id) {
    std::scoped_lock lock{latch_};
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return true;
    frame_id_t frame_id = it->second;
    Page& page = pages_[frame_id];
    if (page.pin_count_ != 0) return false;
    if (page.is_dirty_) {
        disk_manager_->write_page(page.id_.fd, page.id_.page_no, page.data_, PAGE_SIZE);
    }
    page_table_.erase(it);
    replacer_->pin(frame_id);
    page.id_ = PageId{-1, INVALID_PAGE_ID};
    page.pin_count_ = 0;
    page.is_dirty_ = false;
    page.reset_memory();
    free_list_.push_back(frame_id);
    return true;
}

/**
 * @description: 将buffer_pool中的所有页写回到磁盘
 * @param {int} fd 文件句柄
 */
void BufferPoolManager::flush_all_pages(int fd) {
    std::scoped_lock lock{latch_};
    for (auto& entry : page_table_) {
        const PageId& pid = entry.first;
        if (pid.fd != fd) continue;
        frame_id_t frame_id = entry.second;
        disk_manager_->write_page(pid.fd, pid.page_no, pages_[frame_id].data_, PAGE_SIZE);
        pages_[frame_id].is_dirty_ = false;
    }
}

/**
 * 清空 BPM 中给定 fd 的所有页。脏页先刷盘，然后把 frame 释放回 free_list_。
 * 用途：drop 文件前清掉 BPM 缓存，避免 fd 被 OS 重用时新文件读到旧 fd 的脏数据。
 */
void BufferPoolManager::delete_all_pages(int fd) {
    std::scoped_lock lock{latch_};
    for (auto it = page_table_.begin(); it != page_table_.end(); ) {
        const PageId& pid = it->first;
        if (pid.fd != fd) { ++it; continue; }
        frame_id_t frame_id = it->second;
        Page& page = pages_[frame_id];
        if (page.is_dirty_) {
            disk_manager_->write_page(pid.fd, pid.page_no, page.data_, PAGE_SIZE);
        }
        // 重置帧元数据
        page.id_ = PageId{-1, INVALID_PAGE_ID};
        page.is_dirty_ = false;
        page.pin_count_ = 0;
        replacer_->pin(frame_id);   // 从 LRU 中移除（防止 victim 选中它）
        free_list_.push_back(frame_id);
        it = page_table_.erase(it);
    }
}
