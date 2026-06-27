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

bool IxScan::page_no_valid(int page_no) const {
    return page_no > IX_NO_PAGE && page_no < ih_->file_hdr_->num_pages_;
}

void IxScan::ensure_cached(int page_no) const {
    if (!page_no_valid(page_no)) {
        const_cast<IxScan *>(this)->iid_ = end_;
        release_cached();
        return;
    }
    if (page_no != cached_page_no_) {
        release_cached();
        cached_node_ = ih_->fetch_node(page_no);
        cached_page_no_ = page_no;
    }
    cached_size_ = cached_node_->get_size();
}

void IxScan::advance_to_next_leaf() const {
    auto *self = const_cast<IxScan *>(this);
    page_id_t next_pg = IX_NO_PAGE;
    if (cached_node_) {
        next_pg = cached_node_->get_next_leaf();
    }
    release_cached();
    if (!page_no_valid(next_pg)) {
        self->iid_ = end_;
        return;
    }
    self->iid_.page_no = next_pg;
    self->iid_.slot_no = 0;
}

void IxScan::normalize_position() const {
    auto *self = const_cast<IxScan *>(this);
    while (self->iid_ != self->end_) {
        if (!page_no_valid(self->iid_.page_no)) {
            self->iid_ = end_;
            release_cached();
            return;
        }
        ensure_cached(self->iid_.page_no);
        if (self->iid_ == end_) return;
        if (self->iid_.slot_no < cached_size_) return;
        advance_to_next_leaf();
    }
}

void IxScan::next() {
    normalize_position();
    if (iid_ == end_) return;
    iid_.slot_no++;
    ensure_cached(iid_.page_no);
    if (iid_ == end_) return;
    if (iid_.slot_no >= cached_size_) {
        advance_to_next_leaf();
    }
}

Rid IxScan::rid() const {
    normalize_position();
    if (iid_ == end_) {
        return Rid{-1, -1};
    }
    ensure_cached(iid_.page_no);
    if (iid_ == end_ || iid_.slot_no < 0 || iid_.slot_no >= cached_size_) {
        return Rid{-1, -1};
    }
    return *cached_node_->get_rid(iid_.slot_no);
}
