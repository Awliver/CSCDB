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
    // 叶链经 IX_LEAF_HEADER_PAGE 成环（尾叶 next → header → 首叶）。并发分裂会使
    // 定位式 end_ 失效（页/槽被搬走后 iid_==end_ 永不成立），此时扫描必须在链尾
    // （即走到 header 页）硬停，否则绕环无限扫描。
    return page_no > IX_NO_PAGE && page_no != IX_LEAF_HEADER_PAGE &&
           page_no < ih_->file_hdr_->num_pages_;
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

IxScan::IxScan(const IxIndexHandle *ih, const char *start_key, const char *end_key,
               bool end_inclusive, BufferPoolManager *bpm)
    : ih_(ih), iid_({-1, -1}), end_({-1, -1}), bpm_(bpm) {
    key_mode_ = true;
    const int klen = ih->get_fhdr_col_tot_len();
    start_key_.assign(start_key, start_key + klen);
    end_key_.assign(end_key, end_key + klen);
    end_inclusive_ = end_inclusive;
    anchor_key_.resize(klen);
}

bool IxScan::locate_key_mode() const {
    auto *self = const_cast<IxScan *>(this);
    Iid pos = has_anchor_ ? ih_->upper_bound_nolock(anchor_key_.data())
                          : ih_->lower_bound_nolock(start_key_.data());
    self->iid_ = pos;
    if (!page_no_valid(pos.page_no)) return false;
    ensure_cached(pos.page_no);
    if (cached_node_ == nullptr || pos.slot_no < 0 || pos.slot_no >= cached_size_) return false;  // 树尾
    const IxFileHdr *fh = ih_->get_fhdr();
    int c = ix_compare(cached_node_->get_key(pos.slot_no), end_key_.data(),
                       fh->col_types_, fh->col_lens_);
    if (c > 0 || (c == 0 && !end_inclusive_)) return false;
    return true;
}

void IxScan::next() {
    std::shared_lock<std::shared_mutex> lock(ih_->root_latch_);
    if (key_mode_) {
        // 消费当前行：把它的 key 设为 anchor，下次 locate 用 upper_bound(anchor) 取下一行
        if (locate_key_mode()) {
            memcpy(anchor_key_.data(), cached_node_->get_key(iid_.slot_no),
                   ih_->get_fhdr_col_tot_len());
            has_anchor_ = true;
        }
        return;
    }
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
    std::shared_lock<std::shared_mutex> lock(ih_->root_latch_);
    if (key_mode_) {
        if (!locate_key_mode()) return Rid{-1, -1};
        return *cached_node_->get_rid(iid_.slot_no);
    }
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

Rid IxScan::rid_and_key(char *key_out) const {
    std::shared_lock<std::shared_mutex> lock(ih_->root_latch_);
    if (key_mode_) {
        if (!locate_key_mode()) return Rid{-1, -1};
        memcpy(key_out, cached_node_->get_key(iid_.slot_no), ih_->get_fhdr_col_tot_len());
        return *cached_node_->get_rid(iid_.slot_no);
    }
    normalize_position();
    if (iid_ == end_) {
        return Rid{-1, -1};
    }
    ensure_cached(iid_.page_no);
    if (iid_ == end_ || iid_.slot_no < 0 || iid_.slot_no >= cached_size_) {
        return Rid{-1, -1};
    }
    memcpy(key_out, cached_node_->get_key(iid_.slot_no), ih_->get_fhdr_col_tot_len());
    return *cached_node_->get_rid(iid_.slot_no);
}
