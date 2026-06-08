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

class SeqScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;              // 表的名称
    std::vector<Condition> conds_;      // scan的条件
    RmFileHandle *fh_;                  // 表的数据文件句柄
    std::vector<ColMeta> cols_;         // scan后生成的记录的字段
    size_t len_;                        // scan后生成的每条记录的长度
    std::vector<Condition> fed_conds_;  // 同conds_，两个字段相同

    Rid rid_;
    std::unique_ptr<RecScan> scan_;     // table_iterator

    // 优化：缓存当前记录，Next() 接 move 出去
    std::unique_ptr<RmRecord> cur_rec_;

    // 题9 MVCC：表被事务写过时，逐行按快照重建可见版本
    bool mvcc_on_ = false;
    std::string mvcc_buf_;
    // 题9 SER：本次为 SER 事务的 SELECT 扫描时，记录记录读/谓词读
    bool ser_on_ = false;

    // 优化：条件预编译（offset/len/type），避免每行 find_if
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

    // 优化 2：跨记录复用 page handle
    int cached_page_no_ = -1;
    Page *cached_page_ = nullptr;
    char *cached_slots_ = nullptr;

    SmManager *sm_manager_;

   public:
    SeqScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = std::move(tab_name);
        conds_ = std::move(conds);
        TabMeta &tab = sm_manager_->db_.get_table(tab_name_);
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab.cols;
        len_ = cols_.back().offset + cols_.back().len;

        context_ = context;

        fed_conds_ = conds_;
        compile_conds();
    }

     void compile_conds() {
        compiled_.clear();
        compiled_.reserve(fed_conds_.size());
        for (const auto &cond : fed_conds_) {
            auto lhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                return c.name == cond.lhs_col.col_name &&
                       (cond.lhs_col.tab_name.empty() || c.tab_name == cond.lhs_col.tab_name);
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
                    return c.name == cond.rhs_col.col_name &&
                           (cond.rhs_col.tab_name.empty() || c.tab_name == cond.rhs_col.tab_name);
                });
                if (rhs_it == cols_.end()) continue;
                cc.rhs_val_data = nullptr;
                cc.rhs_offset = rhs_it->offset;
            }
            compiled_.push_back(cc);
        }
    }

    bool eval_compiled(const char *data) const {
        for (const auto &cc : compiled_) {
            const char *lhs = data + cc.lhs_offset;
            const char *rhs = cc.is_rhs_val ? cc.rhs_val_data : data + cc.rhs_offset;
            if (!compare_value(lhs, rhs, cc.lhs_len, cc.lhs_type, cc.op)) {
                return false;
            }
        }
        return true;
    }

    void position_to_next_match() {
        while (!scan_->is_end()) {
            rid_ = scan_->rid();
            char *slot = get_slot_ptr(rid_);
            const char *eval_data = slot;
            if (mvcc_on_) {
                // 题9：按本事务快照重建可见版本；不可见/已删则跳过
                if (!context_->txn_mgr_->mvcc_read(context_->txn_, tab_name_, rid_, slot, (int)len_, mvcc_buf_)) {
                    scan_->next();
                    continue;
                }
                eval_data = mvcc_buf_.data();
            }
            if (eval_compiled(eval_data)) {
                // 仅匹配时才分配并复制到cur_rec_
                cur_rec_ = std::make_unique<RmRecord>(len_);
                memcpy(cur_rec_->data, eval_data, len_);
                if (ser_on_) {
                    context_->txn_mgr_->ser_record_read(context_->txn_, tab_name_, rid_);   // 题9 SER 记录读
                    // 读侧：本次读到的记录若有他事务不可见写 → rw 反依赖；成 SSI 危险结构则 abort
                    if (context_->txn_mgr_->ser_read_check(context_->txn_, tab_name_, rid_))
                        throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                        AbortReason::DEADLOCK_PREVENTION);
                }
                return;
            }
            scan_->next();
        }
        release_cached_page();
    }


    /**
     * @description: 按指定类型和操作符比较两段字节数据
     * @param a, b 字节数据指针
     * @param len 比较的字节长度（仅 STRING 用）
     * @param type 列类型，决定如何解释字节数据
     * @param op 比较操作符
     * @return 比较结果是否满足 op
     */
    static bool compare_value(const char *a, const char *b, int len, ColType type, CompOp op) {
        int cmp;
        if (type == TYPE_INT) {
            int ia = *reinterpret_cast<const int *>(a);
            int ib = *reinterpret_cast<const int *>(b);
            cmp = (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
        } else if (type == TYPE_FLOAT) {
            float fa = *reinterpret_cast<const float *>(a);
            float fb = *reinterpret_cast<const float *>(b);
            cmp = (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
        } else { // TYPE_STRING
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

    void release_cached_page() {
        if (cached_page_) {
            sm_manager_->get_bpm()->unpin_page(cached_page_->get_page_id(), false);
            cached_page_ = nullptr;
            cached_page_no_ = -1;
            cached_slots_ = nullptr;
        }
    }

    char *get_slot_ptr(const Rid &rid) {
        if (rid.page_no != cached_page_no_) {
            release_cached_page();
            RmPageHandle handle = fh_->fetch_page_handle(rid.page_no);
            cached_page_ = handle.page;
            cached_page_no_ = rid.page_no;
            cached_slots_ = handle.slots;
        }
        return cached_slots_ + rid.slot_no * (int)len_;
    }


    void beginTuple() override {
        mvcc_on_ = context_ && context_->txn_mgr_ && context_->txn_ &&
                   context_->txn_mgr_->table_is_dirty(tab_name_);
        ser_on_ = context_ && context_->txn_mgr_ && context_->txn_ && context_->ser_in_select_ &&
                  context_->txn_mgr_->is_ser(context_->txn_);
        if (ser_on_) {
            context_->txn_mgr_->ser_record_pred(context_->txn_, tab_name_, fed_conds_);   // 题9 SER 谓词读(含空结果)
            // 读侧(谓词)：检测匹配本谓词但快照不可见的他事务写(幻影插入)→ rw 反依赖；成 SSI 危险结构则 abort
            if (context_->txn_mgr_->ser_read_pred_check(context_->txn_, tab_name_, fed_conds_))
                throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                AbortReason::DEADLOCK_PREVENTION);
        }
        scan_ = std::make_unique<RmScan>(fh_);
        position_to_next_match();
    }

    void nextTuple() override {
        if (scan_->is_end()) return;
        scan_->next();
        position_to_next_match();
    }

    std::unique_ptr<RmRecord> Next() override {
        return std::move(cur_rec_);
    }

    bool is_end() const override { return scan_->is_end(); }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return rid_; }

    ~SeqScanExecutor() override { release_cached_page(); }
};
