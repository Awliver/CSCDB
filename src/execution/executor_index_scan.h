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

#include <cfloat>
#include <climits>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class IndexScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;                      // 表名称
    TabMeta tab_;                               // 表的元数据
    std::vector<Condition> conds_;              // 扫描条件
    RmFileHandle *fh_;                          // 表的数据文件句柄
    std::vector<ColMeta> cols_;                 // 需要读取的字段
    size_t len_;                                // 选取出来的一条记录的长度
    std::vector<Condition> fed_conds_;          // 扫描条件，和conds_字段相同

    std::vector<std::string> index_col_names_;  // index scan涉及到的索引包含的字段
    IndexMeta index_meta_;                      // index scan涉及到的索引元数据

    Rid rid_;
    std::unique_ptr<RecScan> scan_;

    SmManager *sm_manager_;

    // 题3 批 8：最左匹配支持
    int eq_match_count_ = 0;            // 前 N 个索引列有 EQ 条件
    std::vector<char> eq_prefix_data_;  // EQ 前缀的拼接字节（按索引列顺序）
    bool range_exhausted_ = false;      // 标记"EQ 前缀已被超出"

    // 题3 批 9：表数据页缓存（仿 SeqScan 批 2），避免每条记录都 BPM 往返 + alloc
    int cached_table_page_no_ = -1;
    Page *cached_table_page_ = nullptr;
    char *cached_table_slots_ = nullptr;
    char *cached_table_bitmap_ = nullptr;   // 槽位存活性校验（陈旧索引项防护）
    int table_record_size_ = 0;

    // 题3 批 11：预编译条件 + 跳过冗余检查
    struct CompiledCond {
        int lhs_offset;
        int lhs_len;
        ColType lhs_type;
        CompOp op;
        bool is_rhs_val;
        const char *rhs_val_data;
        int rhs_offset;
    };
    std::vector<CompiledCond> compiled_;
    bool need_eval_ = true;             // false = 所有 cond 都被 index range 吸收，跳过 eval
    bool need_prefix_check_ = true;     // false = hi 是精确的（EQ 全匹配 或 range 上界），EQ 前缀检查冗余

    // 题9 MVCC：索引扫描与 SeqScan 同等对待——表进入 MVCC 脏态后，堆上的裸记录可能
    // 包含未提交写或缺少本事务自己的链上写(未提交版本仅存链/overlay，commit 才物化到堆)。
    // 不做可见性重建会导致：事务读不到自己的未提交更新 → 计数器错位 → 丢单/孤儿行。
    bool mvcc_on_ = false;
    std::string mvcc_buf_;              // 当前 rid 的可见版本字节（mvcc_on_ 时有效）
    // 题9 SER：与 SeqScan 相同的 SSI 读跟踪（谓词读 + 逐行读检查）。有了这套钩子，
    // SER 下单表点查可走索引而不必强制全表扫（旧实现强制 SeqScan 是 OJ 性能塌方主因）。
    bool ser_on_ = false;

   public:
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, std::vector<std::string> index_col_names,
                    Context *context) {
        sm_manager_ = sm_manager;
        context_ = context;
        tab_name_ = std::move(tab_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        conds_ = std::move(conds);
        // index_no_ = index_no;
        index_col_names_ = index_col_names; 
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab_.cols;
        len_ = cols_.back().offset + cols_.back().len;
        std::map<CompOp, CompOp> swap_op = {
            {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
        };

        for (auto &cond : conds_) {
            if (cond.lhs_col.tab_name != tab_name_) {
                // lhs is on other table, now rhs must be on this table
                assert(!cond.is_rhs_val && cond.rhs_col.tab_name == tab_name_);
                // swap lhs and rhs
                std::swap(cond.lhs_col, cond.rhs_col);
                cond.op = swap_op.at(cond.op);
            }
        }
        fed_conds_ = conds_;
        table_record_size_ = fh_->get_file_hdr().record_size;
    }

    ~IndexScanExecutor() override { release_table_page(); }

    void release_table_page() {
        if (cached_table_page_) {
            sm_manager_->get_bpm()->unpin_page(cached_table_page_->get_page_id(), false);
            cached_table_page_ = nullptr;
            cached_table_page_no_ = -1;
            cached_table_slots_ = nullptr;
            cached_table_bitmap_ = nullptr;
        }
    }

    const char *get_table_slot(const Rid &rid) {
        if (rid.page_no != cached_table_page_no_) {
            release_table_page();
            RmPageHandle handle = fh_->fetch_page_handle(rid.page_no);
            cached_table_page_ = handle.page;
            cached_table_page_no_ = rid.page_no;
            cached_table_slots_ = handle.slots;
            cached_table_bitmap_ = handle.bitmap;
        }
        return cached_table_slots_ + rid.slot_no * table_record_size_;
    }

    /* 当前缓存页上该槽位是否存活（bitmap 置位）。须在 get_table_slot(rid) 之后调用 */
    bool table_slot_live(const Rid &rid) const {
        return cached_table_bitmap_ != nullptr && Bitmap::is_set(cached_table_bitmap_, rid.slot_no);
    }

    /**
     * 字节级比较两段数据（与 SeqScanExecutor 同一套逻辑）
     */
    static bool cmp_bytes(const char *a, const char *b, int len, ColType type, CompOp op) {
        int cmp;
        if (type == TYPE_INT) {
            int ia = *reinterpret_cast<const int *>(a);
            int ib = *reinterpret_cast<const int *>(b);
            cmp = (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
        } else if (type == TYPE_FLOAT) {
            float fa = *reinterpret_cast<const float *>(a);
            float fb = *reinterpret_cast<const float *>(b);
            cmp = (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
        } else {
            cmp = memcmp(a, b, len);
        }
        switch (op) {
            case OP_EQ: return cmp == 0;
            case OP_NE: return cmp != 0;
            case OP_LT: return cmp < 0;
            case OP_GT: return cmp > 0;
            case OP_LE: return cmp <= 0;
            case OP_GE: return cmp >= 0;
        }
        return false;
    }

    /**
     * 用 fed_conds_ 中的所有等值条件对单条记录做过滤
     */
    bool eval_conds(const RmRecord *rec) const {
        for (const auto &cond : fed_conds_) {
            auto col_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                return c.name == cond.lhs_col.col_name;
            });
            if (col_it == cols_.end()) return false;
            const char *lhs = rec->data + col_it->offset;
            const char *rhs;
            if (cond.is_rhs_val) {
                rhs = cond.rhs_val.raw->data;
            } else {
                auto rhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                    return c.name == cond.rhs_col.col_name;
                });
                if (rhs_it == cols_.end()) return false;
                rhs = rec->data + rhs_it->offset;
            }
            if (!cmp_bytes(lhs, rhs, col_it->len, col_it->type, cond.op)) return false;
        }
        return true;
    }

    /**
     * 按索引列顺序分析 fed_conds_：找出前缀有多少 EQ 条件，并构造 EQ 前缀字节
     */
    void analyze_conditions() {
        eq_match_count_ = 0;
        eq_prefix_data_.clear();
        for (const auto &col : index_meta_.cols) {
            bool found_eq = false;
            for (const auto &cond : fed_conds_) {
                if (cond.is_rhs_val && cond.op == OP_EQ &&
                    cond.lhs_col.tab_name == tab_name_ &&
                    cond.lhs_col.col_name == col.name) {
                    eq_prefix_data_.insert(eq_prefix_data_.end(),
                                           cond.rhs_val.raw->data,
                                           cond.rhs_val.raw->data + col.len);
                    found_eq = true;
                    break;
                }
            }
            if (found_eq) eq_match_count_++;
            else break;
        }
    }

    /**
     * 检查当前 rec 是否仍在 EQ 前缀范围内（用于范围扫描早期终止）
     */
    bool eq_prefix_matches(const RmRecord *rec) const {
        int offset = 0;
        for (int i = 0; i < eq_match_count_; i++) {
            const auto &col = index_meta_.cols[i];
            if (memcmp(rec->data + col.offset,
                       eq_prefix_data_.data() + offset,
                       col.len) != 0) {
                return false;
            }
            offset += col.len;
        }
        return true;
    }

    void position_to_match() {
        while (!range_exhausted_ && !scan_->is_end()) {
            // 与 SeqScan 一致：扫描中途表变脏时打开 MVCC 读
            if (!mvcc_on_ && context_ && context_->txn_mgr_ && context_->txn_) {
                mvcc_on_ = context_->txn_mgr_->table_is_dirty(tab_name_);
            }
            rid_ = scan_->rid();
            if (scan_->is_end() || rid_.page_no < 0 || rid_.slot_no < 0) {
                break;
            }

            // Fast path：range 已精确，且无残余 cond，直接返回匹配（无需 MVCC 重建/SER 跟踪时）
            if (!mvcc_on_ && !ser_on_ && !need_eval_ && !need_prefix_check_) {
                return;
            }

            const char *slot = get_table_slot(rid_);  // 0-alloc 直接读 slot
            const char *rec_data = slot;
            // 陈旧索引项防护：已提交删除会释放堆槽，而快照读期间索引项可能尚在。
            // 槽位不存活时，无版本链兜底的记录必须判为不可见，绝不能返回残留字节。
            const bool slot_live = table_slot_live(rid_);

            if (mvcc_on_) {
                // 题9：按本事务快照重建可见版本；不可见/已删则跳过（与 SeqScan 一致）
                if (!context_->txn_mgr_->mvcc_read(context_->txn_, tab_name_, rid_,
                                                   slot, table_record_size_, mvcc_buf_, slot_live)) {
                    scan_->next();
                    continue;
                }
                rec_data = mvcc_buf_.data();
            } else if (!slot_live) {
                scan_->next();
                continue;
            }

            if (need_prefix_check_) {
                int offset = 0;
                bool match = true;
                for (int i = 0; i < eq_match_count_; i++) {
                    const auto &col = index_meta_.cols[i];
                    if (memcmp(rec_data + col.offset, eq_prefix_data_.data() + offset, col.len) != 0) {
                        match = false;
                        break;
                    }
                    offset += col.len;
                }
                if (!match) {
                    range_exhausted_ = true;
                    return;
                }
            }

            if (need_eval_) {
                if (!eval_compiled(rec_data)) {
                    scan_->next();
                    continue;
                }
            }
            if (ser_on_) {
                // 题9 SER：记录读集 + 读侧 rw 反依赖检查（与 SeqScan 一致）
                context_->txn_mgr_->ser_record_read(context_->txn_, tab_name_, rid_);
                if (context_->txn_mgr_->ser_read_check(context_->txn_, tab_name_, rid_))
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                    AbortReason::DEADLOCK_PREVENTION);
            }
            return;
        }
    }

    /**
     * 在 slot 指针上直接评估条件（无 alloc 版）
     */
    bool eval_conds_on_slot(const char *slot) const {
        for (const auto &cond : fed_conds_) {
            auto col_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                return c.name == cond.lhs_col.col_name;
            });
            if (col_it == cols_.end()) return false;
            const char *lhs = slot + col_it->offset;
            const char *rhs;
            if (cond.is_rhs_val) {
                rhs = cond.rhs_val.raw->data;
            } else {
                auto rhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                    return c.name == cond.rhs_col.col_name;
                });
                if (rhs_it == cols_.end()) return false;
                rhs = slot + rhs_it->offset;
            }
            if (!cmp_bytes(lhs, rhs, col_it->len, col_it->type, cond.op)) return false;
        }
        return true;
    }

    void compile_conds() {
        compiled_.clear();
        compiled_.reserve(fed_conds_.size());
        for (const auto &cond : fed_conds_) {
            auto lhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                return c.name == cond.lhs_col.col_name;
            });
            if (lhs_it == cols_.end()) continue;
            CompiledCond cc;
            cc.lhs_offset = lhs_it->offset;
            cc.lhs_len = lhs_it->len;
            cc.lhs_type = lhs_it->type;
            cc.op = cond.op;
            cc.is_rhs_val = cond.is_rhs_val;
            if (cond.is_rhs_val) {
                cc.rhs_val_data = cond.rhs_val.raw->data;
                cc.rhs_offset = -1;
            } else {
                auto rhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                    return c.name == cond.rhs_col.col_name;
                });
                if (rhs_it == cols_.end()) continue;
                cc.rhs_val_data = nullptr;
                cc.rhs_offset = rhs_it->offset;
            }
            compiled_.push_back(cc);
        }
    }

    bool eval_compiled(const char *slot) const {
        for (const auto &cc : compiled_) {
            const char *lhs = slot + cc.lhs_offset;
            const char *rhs = cc.is_rhs_val ? cc.rhs_val_data : slot + cc.rhs_offset;
            if (!cmp_bytes(lhs, rhs, cc.lhs_len, cc.lhs_type, cc.op)) return false;
        }
        return true;
    }

    /**
     * 把 index key 中第 from_col 列及之后的所有列填成该列类型的极值。
     * 多列索引前缀范围查询时，range 列之后的 suffix 列必须填类型极值（而非字节0）
     * 才能正确界定 B+ 树扫描边界——否则如 (id,name,score) 上 `id>1`，零填充会让
     * upper_bound([1,0,0]) 落到第一个 id=1 的键（因 name 字节 > 0），错误地把 id=1 扫进来。
     * want_max=true 填类型最大值（INT_MAX / FLT_MAX / 0xFF），否则填最小值。
     */
    void fill_extreme_from(char *key, int from_col, bool want_max) const {
        int off = 0;
        for (int i = 0; i < (int)index_meta_.cols.size(); ++i) {
            const auto &c = index_meta_.cols[i];
            if (i >= from_col) {
                if (c.type == TYPE_INT) {
                    int v = want_max ? INT_MAX : INT_MIN;
                    memcpy(key + off, &v, sizeof(int));
                } else if (c.type == TYPE_FLOAT) {
                    float v = want_max ? FLT_MAX : -FLT_MAX;
                    memcpy(key + off, &v, sizeof(float));
                } else {  // TYPE_STRING：按字节比较，0xFF 最大 / 0x00 最小
                    memset(key + off, want_max ? 0xFF : 0x00, c.len);
                }
            }
            off += c.len;
        }
    }

    void beginTuple() override {
        // 题9：与 SeqScan 相同的 MVCC 开关——表被 MVCC 写过后必须按快照重建可见版本
        mvcc_on_ = context_ && context_->txn_mgr_ && context_->txn_ &&
                   context_->txn_mgr_->table_is_dirty(tab_name_);
        // 题9 SER：与 SeqScan 相同的 SSI 读跟踪（仅 SELECT 记录读集）
        ser_on_ = context_ && context_->txn_mgr_ && context_->txn_ && context_->ser_in_select_ &&
                  context_->txn_mgr_->is_ser(context_->txn_);
        if (ser_on_) {
            context_->txn_mgr_->ser_record_pred(context_->txn_, tab_name_, fed_conds_);
            // 读侧(谓词)：匹配本谓词但快照不可见的他事务写(幻影插入) → rw 反依赖；危险结构则 abort
            if (context_->txn_mgr_->ser_read_pred_check(context_->txn_, tab_name_, fed_conds_))
                throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                AbortReason::DEADLOCK_PREVENTION);
        }
        auto ih = sm_manager_->ihs_.at(
            sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_col_names_)).get();

        analyze_conditions();
        compile_conds();
        range_exhausted_ = false;

        // 构造 start_key / end_key：默认 EQ 前缀 + 零填充
        int eq_len = (int)eq_prefix_data_.size();
        std::vector<char> start_key(index_meta_.col_tot_len, 0);
        std::vector<char> end_key(index_meta_.col_tot_len, 0);
        if (eq_len > 0) {
            memcpy(start_key.data(), eq_prefix_data_.data(), eq_len);
            memcpy(end_key.data(), eq_prefix_data_.data(), eq_len);
        }

        // 找 EQ 前缀之后第一个索引列的范围条件，取最紧的上下界
        bool has_lower = false, has_upper = false;
        bool lower_inclusive = false, upper_inclusive = false;
        if (eq_match_count_ < (int)index_meta_.cols.size()) {
            const auto &range_col = index_meta_.cols[eq_match_count_];
            for (const auto &cond : fed_conds_) {
                if (!cond.is_rhs_val) continue;
                if (cond.lhs_col.tab_name != tab_name_) continue;
                if (cond.lhs_col.col_name != range_col.name) continue;

                const char *cv = cond.rhs_val.raw->data;
                if (cond.op == OP_GT || cond.op == OP_GE) {
                    if (!has_lower ||
                        ix_compare(cv, start_key.data() + eq_len, range_col.type, range_col.len) > 0) {
                        memcpy(start_key.data() + eq_len, cv, range_col.len);
                        has_lower = true;
                        lower_inclusive = (cond.op == OP_GE);
                    }
                } else if (cond.op == OP_LT || cond.op == OP_LE) {
                    if (!has_upper ||
                        ix_compare(cv, end_key.data() + eq_len, range_col.type, range_col.len) < 0) {
                        memcpy(end_key.data() + eq_len, cv, range_col.len);
                        has_upper = true;
                        upper_inclusive = (cond.op == OP_LE);
                    }
                }
            }
        }

        // 计算 lo —— 多列索引前缀范围：range 列之后的 suffix 列需填类型极值
        //   >=val(含)：suffix 填 min + lower_bound（含 val 的所有后缀）
        //   >val (排)：suffix 填 max + upper_bound（跳过 val 的所有后缀）
        Iid lo;
        if (has_lower) {
            if (lower_inclusive) {
                fill_extreme_from(start_key.data(), eq_match_count_ + 1, false);
                lo = ih->lower_bound(start_key.data());
            } else {
                fill_extreme_from(start_key.data(), eq_match_count_ + 1, true);
                lo = ih->upper_bound(start_key.data());
            }
        } else if (eq_len > 0) {
            // 纯 EQ 前缀（无范围）：range 列及之后填 min，取首个匹配前缀的 key
            fill_extreme_from(start_key.data(), eq_match_count_, false);
            lo = ih->lower_bound(start_key.data());
        } else {
            lo = ih->leaf_begin();
        }

        // 计算 hi
        //   <val (排)：suffix 填 min + lower_bound（停在 val 之前）
        //   <=val(含)：suffix 填 max + upper_bound（停在 val 的所有后缀之后）
        Iid hi;
        bool full_eq = (eq_match_count_ == (int)index_meta_.cols.size());
        if (has_upper) {
            if (upper_inclusive) {
                fill_extreme_from(end_key.data(), eq_match_count_ + 1, true);
                hi = ih->upper_bound(end_key.data());
            } else {
                fill_extreme_from(end_key.data(), eq_match_count_ + 1, false);
                hi = ih->lower_bound(end_key.data());
            }
        } else if (full_eq) {
            // 全 EQ：精确末尾 = upper_bound(prefix)（唯一索引下仅 1 条）
            hi = ih->upper_bound(start_key.data());
        } else {
            // 部分 EQ 或纯前缀：靠 eq_prefix_matches 早期终止或扫到 leaf_end
            hi = ih->leaf_end();
        }

        // 并发修正：IxScan 的 end_ 是定位式 (page,slot)，并发分裂会使其失效（条目搬走后
        // iid_==end_ 永不成立），扫描可能越过逻辑上界继续走。因此不能信任"范围已被
        // lo/hi 完全吸收"而跳过逐行检查：
        //   - 所有值条件始终逐行 eval（吸收的 range 条件也在 compiled_ 里，代价极小）；
        //   - EQ 前缀检查始终开启，作为越界后的早期硬停（range_exhausted_）。
        need_eval_ = !fed_conds_.empty();
        need_prefix_check_ = (eq_match_count_ > 0);

        // 矛盾/空范围保护：显式上下界交叉时（如 w_id > 500 and w_id < 400），
        // lo 会落在 hi 之后，IxScan 顺序前进永远到不了 end_，会越过树尾导致
        // 越界读 / ix_scan.cpp 的 assert 崩溃。这里在 key 层面判定空结果，
        // 直接返回一个空扫描（lo==lo 使 is_end 立即为真）。
        if (has_lower && has_upper) {
            int off = 0, kc = 0;
            for (const auto &col : index_meta_.cols) {
                kc = ix_compare(start_key.data() + off, end_key.data() + off, col.type, col.len);
                if (kc != 0) break;
                off += col.len;
            }
            if (kc > 0 || (kc == 0 && !(lower_inclusive && upper_inclusive))) {
                range_exhausted_ = true;
                scan_ = std::make_unique<IxScan>(ih, lo, lo, sm_manager_->get_bpm());
                return;
            }
        }

        scan_ = std::make_unique<IxScan>(ih, lo, hi, sm_manager_->get_bpm());
        position_to_match();
    }

    void nextTuple() override {
        if (range_exhausted_ || scan_->is_end()) return;
        scan_->next();
        position_to_match();
    }

    bool is_end() const override { return range_exhausted_ || scan_->is_end(); }

    std::unique_ptr<RmRecord> Next() override {
        auto rec = std::make_unique<RmRecord>(table_record_size_);
        if (mvcc_on_) {
            // 题9：position_to_match 已为当前 rid_ 重建可见版本，直接物化
            memcpy(rec->data, mvcc_buf_.data(), table_record_size_);
        } else {
            // 从缓存的 slot 复制到新 RmRecord（仅匹配时调一次）
            const char *slot = get_table_slot(rid_);
            memcpy(rec->data, slot, table_record_size_);
        }
        return rec;
    }

    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }

    Rid &rid() override { return rid_; }
};