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

#include "ix_defs.h"
#include "ix_index_handle.h"

// class IxIndexHandle;

// 用于遍历叶子结点
// 用于直接遍历叶子结点，而不用findleafpage来得到叶子结点
// TODO：对page遍历时，要加上读锁
class IxScan : public RecScan {
    const IxIndexHandle *ih_;
    Iid iid_;  // 初始为lower（用于遍历的指针）
    Iid end_;  // 初始为upper
    BufferPoolManager *bpm_;

    // 优化：跨 next()/rid() 缓存当前 leaf 节点，仅切叶时 fetch+new
    mutable int cached_page_no_ = -1;
    mutable IxNodeHandle *cached_node_ = nullptr;
    mutable int cached_size_ = 0;

    void release_cached() const;
    void ensure_cached(int page_no) const;
    void normalize_position() const;
    void advance_to_next_leaf() const;
    bool page_no_valid(int page_no) const;

    // key 锚定模式：定位式 (page,slot) 迭代在并发 delete_entry/分裂下会失效（条目
    // 左移/搬走 → 跳行、空扫，OJ Delivery MIN canary 实测 min 间歇返回空/错值）。
    // key 模式每次访问在锁内按 key 实时重定位：首行 lower_bound(start_key)，推进
    // upper_bound(anchor=上一行 key)，终止按 end_key 比较——天然抗并发结构变更。
    bool key_mode_ = false;
    std::vector<char> start_key_;
    std::vector<char> end_key_;
    bool end_inclusive_ = true;
    mutable std::vector<char> anchor_key_;
    mutable bool has_anchor_ = false;
    /* 锚点的 rid：同 key 多项（删后重插未 drain 的墓碑项、MVCC 版本化项）时，
     * 仅凭 key 无法表达"该 key 的第几项已消费"。只跳一格会在 ≥2 个等值项上
     * 原地循环（anchor 永不前进，MIN 实测假死）；严格跳过整个等值 run 则会把
     * 排在后面的同 key 活项一起跳掉（删后重插丢行）。重定位在等值 run 内按
     * rid 找到锚点项、取其下一项；锚点项已被物删时退化为跳到 key 严格更大处。*/
    mutable Rid anchor_rid_{-1, -1};
    mutable Rid returned_rid_{-1, -1};
    /* 最后一次经 rid()/rid_and_key() 真正交给调用方的行 key。next() 只能锚定它：
     * 若 next() 重定位"当前行"再取 key 当 anchor，当前行恰被并发 delete_entry 摘除
     * 时 upper_bound(旧anchor) 会落到下一行——把从未返回过的行当作已消费，整行被
     * 静默吞掉（OJ Delivery MIN canary "MIN was not the earliest key"，ring 实测）。*/
    mutable std::vector<char> returned_key_;
    mutable bool has_returned_ = false;
    /* 锁内定位当前行；true=iid_/cached_node_ 有效且未越过 end_key */
    bool locate_key_mode() const;

   public:
    IxScan(const IxIndexHandle *ih, const Iid &lower, const Iid &upper, BufferPoolManager *bpm)
        : ih_(ih), iid_(lower), end_(upper), bpm_(bpm) {}

    /* key 锚定模式构造：start/end 为 col_tot_len 完整 key 字节 */
    IxScan(const IxIndexHandle *ih, const char *start_key, const char *end_key,
           bool end_inclusive, BufferPoolManager *bpm);

    ~IxScan() override { release_cached(); }

    void next() override;

    bool is_end() const override {
        std::shared_lock<FairSharedMutex> lock(ih_->root_latch_);
        if (key_mode_) return !locate_key_mode();
        normalize_position();
        return iid_ == end_;
    }

    Rid rid() const override;

    /* 同一把锁内取 rid + 拷贝当前索引项 key（key_out 须有 col_tot_len 字节）。
     * MVCC 下同一 rid 可有多个索引项（未提交 UPDATE 的新旧 key），扫描端需用
     * "项 key == 可见版本 key" 做一致性过滤，否则同一行经多个项重复输出。 */
    Rid rid_and_key(char *key_out) const;

    const Iid &iid() const { return iid_; }
};