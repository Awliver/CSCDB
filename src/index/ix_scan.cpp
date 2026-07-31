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
#include <atomic>
#include <cstdlib>

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
    returned_key_.resize(klen);
}

bool IxScan::locate_key_mode() const {
    auto *self = const_cast<IxScan *>(this);
    const IxFileHdr *fh = ih_->get_fhdr();
    Iid pos;

    // 叶级续扫快路径：调用方持 root_latch_（共享），结构写者全部持排它——本次持锁
    // 期间整棵树冻结。若上次返回的锚定项仍在原位（key+rid 双验证，防插入位移/合并
    // 搬迁/删后重插同槽），其叶内/跨叶后继即为下一项，免去整树重下降。07-31 profile：
    // 每行从根重下降的 find_leaf_page 占 53% CPU、其 BPM 往返的分片锁 futex 占 12%。
    // 验证失败零成本回落慢路径，正确性包络不变。
    // 默认关闭（RMDB_SCAN_FASTPATH=1 显式开启）：07-31 在 churn 后的老库上实测
    // gap-EQ 宽扫描 0.1s 返回空（同查询关闭后正确返回），疑与 MVCC 残留态（墓碑/
    // 陈旧索引项）交互提前终止；全新表 5 万行×715 查询与并发搅动 37 轮均无法复现，
    // 归因未收口前不冒结果正确性的险。收益大头在 MIN 早停 + unpin 去写锁。
    static const bool fastpath_on = std::getenv("RMDB_SCAN_FASTPATH") != nullptr;
    if (fastpath_on && has_anchor_ && cached_node_ && iid_.page_no == cached_page_no_ &&
        page_no_valid(cached_page_no_) && cached_node_->is_leaf_page()) {
        cached_size_ = cached_node_->get_size();
        const int s = iid_.slot_no;
        if (s >= 0 && s < cached_size_ &&
            ix_compare(cached_node_->get_key(s), anchor_key_.data(),
                       fh->col_types_, fh->col_lens_) == 0) {
            const Rid *r = cached_node_->get_rid(s);
            if (r->page_no == anchor_rid_.page_no && r->slot_no == anchor_rid_.slot_no) {
                bool ok = true;
                self->iid_.slot_no = s + 1;
                if (self->iid_.slot_no >= cached_size_) {
                    advance_to_next_leaf();          // 跨叶（尾叶则置 end_）
                    if (!page_no_valid(iid_.page_no)) return false;
                    ensure_cached(iid_.page_no);
                    // 空叶/失效叶不该在冻结树上出现，保守回落慢路径
                    if (cached_node_ == nullptr || cached_size_ <= 0 ||
                        iid_.slot_no >= cached_size_ || !cached_node_->is_leaf_page()) {
                        ok = false;
                    }
                }
                if (ok) {
                    int c = ix_compare(cached_node_->get_key(iid_.slot_no), end_key_.data(),
                                       fh->col_types_, fh->col_lens_);
                    if (c > 0 || (c == 0 && !end_inclusive_)) return false;   // 越过范围尾
                    return true;
                }
                // 快路径失效：回落慢路径重定位（iid_ 会被重算，无需恢复）
            }
        }
    }

    // lower/upper_bound 依赖内部结点分隔键。并发 delete/merge 后若某一级分隔键
    // 短暂陈旧，树下降可能落到 anchor 之前的叶；只把位置钳到 start_key 仍会在
    // 下一轮重新返回已经消费过的行。Sort 会把这种回环持续物化到内存，最终以
    // bad_alloc/ERROR 结束。沿叶链向前找严格大于 anchor 的第一项，作为进度兜底。
    auto seek_strictly_after_anchor = [&]() -> bool {
        // 定量诊断：兜底触发频度（高频 = 重定位倒退常态化，是扫描成本热点信号）
        {
            static std::atomic<uint64_t> seek_cnt{0};
            uint64_t n = seek_cnt.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((n & 0xFFF) == 0) {
                fprintf(stderr, "[anchor-seek] fallback count=%llu\n", (unsigned long long)n);
            }
        }
        pos = ih_->upper_bound_nolock(anchor_key_.data());
        size_t leaf_hops = 0;
        while (true) {
            self->iid_ = pos;
            if (!page_no_valid(pos.page_no)) return false;
            ensure_cached(pos.page_no);
            if (cached_node_ == nullptr || pos.slot_no < 0) return false;
            if (pos.slot_no >= cached_size_) {
                if (++leaf_hops > static_cast<size_t>(fh->num_pages_)) return false;
                advance_to_next_leaf();
                pos = iid_;
                continue;
            }
            int cmp = ix_compare(cached_node_->get_key(pos.slot_no), anchor_key_.data(),
                                 fh->col_types_, fh->col_lens_);
            if (cmp > 0) return true;
            // upper_bound 本应已越过等值 run；若分隔键把下降点带早了，则顺着
            // 叶链跳过所有 <= anchor 的项，保证扫描游标绝不倒退。
            pos.slot_no++;
        }
    };

    if (!has_anchor_) {
        pos = ih_->lower_bound_nolock(start_key_.data());
    } else {
        // 锚定推进：定位到"锚点项的下一项"。同 key 多项时仅凭 key 无法推进
        //（upper_bound 只跳一格会在等值项上原地循环——MIN 假死实测；跳过整个
        // 等值 run 会把后方同 key 活项一起跳掉——删后重插丢行）。做法：
        // lower_bound(anchor_key) 起在等值 run 内找 anchor_rid_，找到则取其下一
        // 项（可能仍是同 key 的未访问项）；锚点项已被物删则停在首个 key 严格
        // 更大的项（等值 run 内未访问项让步——与"先读后 next"协议的既有
        // 尽力语义一致，不回访已访问项）。
        pos = ih_->lower_bound_nolock(anchor_key_.data());
        bool found_anchor = false;
        while (true) {
            self->iid_ = pos;
            if (!page_no_valid(pos.page_no)) return false;
            ensure_cached(pos.page_no);
            if (cached_node_ == nullptr || pos.slot_no < 0) return false;
            if (pos.slot_no >= cached_size_) {
                // 叶尾：跳下一叶继续（等值 run 可跨叶）
                advance_to_next_leaf();
                pos = iid_;
                if (!page_no_valid(pos.page_no)) return false;
                continue;
            }
            if (ix_compare(cached_node_->get_key(pos.slot_no), anchor_key_.data(),
                           fh->col_types_, fh->col_lens_) != 0) {
                break;   // 越过等值 run：锚点项已被物删，停在 key 严格更大处
            }
            if (found_anchor) break;   // 锚点项的下一项（可能仍同 key）
            const Rid *r = cached_node_->get_rid(pos.slot_no);
            if (r->page_no == anchor_rid_.page_no && r->slot_no == anchor_rid_.slot_no) {
                found_anchor = true;   // 命中锚点项本体，再前进一格
            }
            pos.slot_no++;
        }
    }
    self->iid_ = pos;
    if (!page_no_valid(pos.page_no)) return false;
    ensure_cached(pos.page_no);
    if (cached_node_ == nullptr || pos.slot_no < 0 || pos.slot_no >= cached_size_) return false;  // 树尾
    // 下界钳位：并发结构变更的窗口可把 anchor 弄到 start_key 之前（实测卡死扫描
    // 的 anchor 在别的 district/仓库——随后每步都在范围外的死项间爬行）。位置一旦
    // 低于范围起点，立即重定位回 lower_bound(start_key)：扫描位置恒被禁锢在
    // [start,end] 内。
    if (ix_compare(cached_node_->get_key(pos.slot_no), start_key_.data(),
                   fh->col_types_, fh->col_lens_) < 0) {
        pos = ih_->lower_bound_nolock(start_key_.data());
        self->iid_ = pos;
        if (!page_no_valid(pos.page_no)) return false;
        ensure_cached(pos.page_no);
        if (cached_node_ == nullptr || pos.slot_no < 0 || pos.slot_no >= cached_size_) return false;
    }

    if (has_anchor_) {
        int progress = ix_compare(cached_node_->get_key(pos.slot_no), anchor_key_.data(),
                                  fh->col_types_, fh->col_lens_);
        const Rid *candidate = cached_node_->get_rid(pos.slot_no);
        const bool repeated_item =
            progress == 0 && candidate->page_no == anchor_rid_.page_no &&
            candidate->slot_no == anchor_rid_.slot_no;
        if (progress < 0 || repeated_item) {
            if (!seek_strictly_after_anchor()) return false;
        }
    }

    int c = ix_compare(cached_node_->get_key(pos.slot_no), end_key_.data(),
                       fh->col_types_, fh->col_lens_);
    if (c > 0 || (c == 0 && !end_inclusive_)) return false;
    return true;
}

void IxScan::next() {
    if (key_mode_) {
        // 消费的必须是【调用方实际读到过的行】：anchor 取 rid()/rid_and_key() 存下的
        // returned_key_，绝不能重定位后取"现在停在哪"的 key——当前行被并发
        // delete_entry 摘除时重定位会落到下一行，把从未返回的行当作已消费而吞行。
        // 纯本地状态更新，无需树锁。
        if (has_returned_) {
            anchor_key_.swap(returned_key_);
            anchor_rid_ = returned_rid_;
            has_anchor_ = true;
            has_returned_ = false;
            returned_key_.resize(anchor_key_.size());
        }
        return;
    }
    std::shared_lock<FairSharedMutex> lock(ih_->root_latch_);
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
    std::shared_lock<FairSharedMutex> lock(ih_->root_latch_);
    if (key_mode_) {
        if (!locate_key_mode()) return Rid{-1, -1};
        memcpy(returned_key_.data(), cached_node_->get_key(iid_.slot_no),
               ih_->get_fhdr_col_tot_len());
        returned_rid_ = *cached_node_->get_rid(iid_.slot_no);
        has_returned_ = true;
        return returned_rid_;
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
    std::shared_lock<FairSharedMutex> lock(ih_->root_latch_);
    if (key_mode_) {
        if (!locate_key_mode()) return Rid{-1, -1};
        memcpy(key_out, cached_node_->get_key(iid_.slot_no), ih_->get_fhdr_col_tot_len());
        memcpy(returned_key_.data(), key_out, ih_->get_fhdr_col_tot_len());
        returned_rid_ = *cached_node_->get_rid(iid_.slot_no);
        has_returned_ = true;
        return returned_rid_;
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
