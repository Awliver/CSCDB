/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "ix_scan.h"

void IxScan::release_cached() const {
    if (cached_node_) {
        bpm_->unpin_page(cached_node_->get_page_id(), false);
        delete cached_node_;
        cached_node_ = nullptr;
        cached_page_no_ = -1;
    }
}

void IxScan::ensure_cached(int page_no) const {
    if (page_no != cached_page_no_) {
        release_cached();
        cached_node_ = ih_->fetch_node(page_no);
        cached_page_no_ = page_no;
        cached_size_ = cached_node_->get_size();
    }
}

void IxScan::next() {
    assert(!is_end());
    ensure_cached(iid_.page_no);
    assert(cached_node_->is_leaf_page());
    assert(iid_.slot_no < cached_size_);
    iid_.slot_no++;
    if (iid_.page_no != ih_->file_hdr_->last_leaf_ && iid_.slot_no == cached_size_) {
        // 跨叶时记录下一页的 page_no（下次 ensure_cached 会切）
        iid_.slot_no = 0;
        iid_.page_no = cached_node_->get_next_leaf();
    }
}

Rid IxScan::rid() const {
    ensure_cached(iid_.page_no);
    return *cached_node_->get_rid(iid_.slot_no);
}