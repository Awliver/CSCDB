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
    int table_record_size_ = 0;

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
        }
    }

    const char *get_table_slot(const Rid &rid) {
        if (rid.page_no != cached_table_page_no_) {
            release_table_page();
            RmPageHandle handle = fh_->fetch_page_handle(rid.page_no);
            cached_table_page_ = handle.page;
            cached_table_page_no_ = rid.page_no;
            cached_table_slots_ = handle.slots;
        }
        return cached_table_slots_ + rid.slot_no * table_record_size_;
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
            rid_ = scan_->rid();
            const char *slot = get_table_slot(rid_);  // 0-alloc 直接读 slot

            // EQ 前缀已经被超出 → 早期终止
            if (eq_match_count_ > 0) {
                int offset = 0;
                bool match = true;
                for (int i = 0; i < eq_match_count_; i++) {
                    const auto &col = index_meta_.cols[i];
                    if (memcmp(slot + col.offset, eq_prefix_data_.data() + offset, col.len) != 0) {
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

            // 在 slot 上直接 eval_conds，避免 alloc + memcpy
            if (eval_conds_on_slot(slot)) return;
            scan_->next();
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

    void beginTuple() override {
        auto ih = sm_manager_->ihs_.at(
            sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_col_names_)).get();

        analyze_conditions();
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

        // 计算 lo
        Iid lo;
        if (has_lower) {
            lo = lower_inclusive ? ih->lower_bound(start_key.data()) : ih->upper_bound(start_key.data());
        } else if (eq_len > 0) {
            lo = ih->lower_bound(start_key.data());
        } else {
            lo = ih->leaf_begin();
        }

        // 计算 hi
        Iid hi;
        if (has_upper) {
            hi = upper_inclusive ? ih->upper_bound(end_key.data()) : ih->lower_bound(end_key.data());
        } else {
            // 没显式上界：靠 eq_prefix_matches 早期终止或扫到 leaf_end
            hi = ih->leaf_end();
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
        // 从缓存的 slot 复制到新 RmRecord（仅匹配时调一次）
        const char *slot = get_table_slot(rid_);
        auto rec = std::make_unique<RmRecord>(table_record_size_);
        memcpy(rec->data, slot, table_record_size_);
        return rec;
    }

    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }

    Rid &rid() override { return rid_; }
};