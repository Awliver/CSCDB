/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // 拿到页句柄
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);

    // 检查slot有无记录
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page({fd_, rid.page_no}, false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    // 复制记录数据
    auto record = std::make_unique<RmRecord>(file_hdr_.record_size);
    memcpy(record->data, page_handle.get_slot(rid.slot_no), file_hdr_.record_size);

    // 释放页
    buffer_pool_manager_->unpin_page({fd_, rid.page_no}, false);
    return record;
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char *buf, Context *context) {
    std::scoped_lock<std::mutex> op_lock(op_latch_);
    // Step 1：决定写入页 —— 优先用缓存
    bool use_cache = (cached_insert_page_no_ != -1 &&
                      cached_insert_hdr_->num_records < file_hdr_.num_records_per_page);

    if (!use_cache) {
        // 释放过期缓存（page 已满或第一次插入）
        if (cached_insert_page_) {
            buffer_pool_manager_->unpin_page(cached_insert_page_->get_page_id(), true);
            cached_insert_page_ = nullptr;
            cached_insert_page_no_ = -1;
        }
        // 找新的可写页
        RmPageHandle ph = (file_hdr_.first_free_page_no == RM_NO_PAGE)
                              ? create_new_page_handle()
                              : fetch_page_handle(file_hdr_.first_free_page_no);
        cached_insert_page_ = ph.page;
        cached_insert_page_no_ = ph.page->get_page_id().page_no;
        cached_insert_hdr_ = ph.page_hdr;
        cached_insert_bitmap_ = ph.bitmap;
        cached_insert_slots_ = ph.slots;
    }

    // Step 2：bitmap 找空 slot
    int slot_no = Bitmap::first_bit(false, cached_insert_bitmap_, file_hdr_.num_records_per_page);

    // Step 3：写入
    char *slot = cached_insert_slots_ + slot_no * file_hdr_.record_size;
    memcpy(slot, buf, file_hdr_.record_size);
    Bitmap::set(cached_insert_bitmap_, slot_no);
    cached_insert_hdr_->num_records++;

    Rid rid{cached_insert_page_no_, slot_no};

    // Step 4：页满，推进 first_free_page + 释放缓存（让下次 insert 重选页）
    if (cached_insert_hdr_->num_records == file_hdr_.num_records_per_page) {
        file_hdr_.first_free_page_no = cached_insert_hdr_->next_free_page_no;
        buffer_pool_manager_->unpin_page(cached_insert_page_->get_page_id(), true);
        cached_insert_page_ = nullptr;
        cached_insert_page_no_ = -1;
    }

    return rid;
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
    std::scoped_lock<std::mutex> op_lock(op_latch_);
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);

    // 直接写入指定 slot
    memcpy(page_handle.get_slot(rid.slot_no), buf, file_hdr_.record_size);
    Bitmap::set(page_handle.bitmap, rid.slot_no);
    page_handle.page_hdr->num_records++;

    // 若页已满，则从空闲链表上删去
    if (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page) {
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
    }

    buffer_pool_manager_->unpin_page({fd_, rid.page_no}, true);
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    std::scoped_lock<std::mutex> op_lock(op_latch_);
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);

    // 检查记录存在
    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page({fd_, rid.page_no}, false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    // 判断该页是否已满
    bool was_full = (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page);

    Bitmap::reset(page_handle.bitmap, rid.slot_no);
    page_handle.page_hdr->num_records--;

    // 若该页变为非满，则加回空闲链表
    if (was_full) {
        release_page_handle(page_handle);
    }

    buffer_pool_manager_->unpin_page({fd_, rid.page_no}, true);
}


/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    std::scoped_lock<std::mutex> op_lock(op_latch_);
    RmPageHandle page_handle = fetch_page_handle(rid.page_no);

    if (!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {
        buffer_pool_manager_->unpin_page({fd_, rid.page_no}, false);
        throw RecordNotFoundError(rid.page_no, rid.slot_no);
    }

    memcpy(page_handle.get_slot(rid.slot_no), buf, file_hdr_.record_size);

    buffer_pool_manager_->unpin_page({fd_, rid.page_no}, true);

}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    // 校验 page_no 合法
    if (page_no >= file_hdr_.num_pages || page_no < 0) {
        throw PageNotExistError("", page_no);
    }

    PageId pid{fd_, page_no};
    Page* page = buffer_pool_manager_->fetch_page(pid);
    if (page == nullptr) {
        throw PageNotExistError("", page_no);
    }
    return RmPageHandle(&file_hdr_, page);
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    PageId pid{fd_, INVALID_PAGE_ID};
    Page* page = buffer_pool_manager_->new_page(&pid);
    if (page == nullptr) {
        throw InternalError("RmFileHandle::create_new_page_handle: new_page failed");
    }

    RmPageHandle page_handle(&file_hdr_, page);

    // 初始化页头
    page_handle.page_hdr->num_records = 0;
    page_handle.page_hdr->next_free_page_no = RM_NO_PAGE;

    // 初始化位图为全0
    Bitmap::init(page_handle.bitmap, file_hdr_.bitmap_size);

    // 更新文件头
    file_hdr_.num_pages++;

    return page_handle;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    // 若没有空闲页，则创建新页并挂到空闲链表头
    if (file_hdr_.first_free_page_no == RM_NO_PAGE) {
        RmPageHandle page_handle = create_new_page_handle();
        file_hdr_.first_free_page_no = page_handle.page->get_page_id().page_no;
        return page_handle;
    }

    // 若有空闲页，则直接获取链表头的空闲页
    return fetch_page_handle(file_hdr_.first_free_page_no);
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle&page_handle) {
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
    file_hdr_.first_free_page_no = page_handle.page->get_page_id().page_no;
}
