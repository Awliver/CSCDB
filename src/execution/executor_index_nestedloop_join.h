/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "executor_seq_scan.h"
#include "index/ix.h"
#include "optimizer/plan.h"
#include "system/sm.h"

class IndexNestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;
    SmManager *sm_manager_;
    std::string right_table_;
    TabMeta right_tab_;
    IndexMeta index_meta_;
    RmFileHandle *right_fh_;
    int right_record_size_;
    std::vector<ColMeta> right_cols_;
    size_t len_;
    std::vector<ColMeta> cols_;
    std::vector<Condition> join_conds_;
    std::vector<Condition> right_conds_;

    std::unique_ptr<RmRecord> left_rec_;
    std::vector<Rid> right_hits_;
    size_t hit_idx_ = 0;
    bool isend_ = true;
    bool mvcc_on_ = false;      // 题9：内表脏态时按快照重建可见版本

    bool build_lookup_key(std::vector<char> &key) {
        key.assign(index_meta_.col_tot_len, 0);
        size_t key_off = 0;
        for (auto &idx_col : index_meta_.cols) {
            bool filled = false;
            for (auto &cond : right_conds_) {
                if (cond.op != OP_EQ || !cond.is_rhs_val) continue;
                if (cond.lhs_col.tab_name != right_table_ || cond.lhs_col.col_name != idx_col.name) continue;
                memcpy(key.data() + key_off, cond.rhs_val.raw->data, idx_col.len);
                filled = true;
                break;
            }
            if (!filled) {
                for (auto &cond : join_conds_) {
                    if (cond.op != OP_EQ || cond.is_rhs_val) continue;
                    TabCol left_col;
                    bool matched = false;
                    if (cond.lhs_col.tab_name == right_table_ && cond.lhs_col.col_name == idx_col.name) {
                        left_col = cond.rhs_col;
                        matched = true;
                    } else if (cond.rhs_col.tab_name == right_table_ && cond.rhs_col.col_name == idx_col.name) {
                        left_col = cond.lhs_col;
                        matched = true;
                    }
                    if (!matched) continue;
                    auto left_it = get_col(left_->cols(), left_col);
                    memcpy(key.data() + key_off, left_rec_->data + left_it->offset, idx_col.len);
                    filled = true;
                    break;
                }
            }
            if (!filled) return false;
            key_off += idx_col.len;
        }
        return true;
    }

    bool eval_joined_conds(const RmRecord *right_rec) {
        std::vector<char> joined(len_);
        memcpy(joined.data(), left_rec_->data, left_->tupleLen());
        memcpy(joined.data() + left_->tupleLen(), right_rec->data, right_record_size_);
        for (auto &cond : right_conds_) {
            if (!eval_cond(cond, joined.data())) return false;
        }
        for (auto &cond : join_conds_) {
            if (!eval_cond(cond, joined.data())) return false;
        }
        return true;
    }

    bool eval_cond(const Condition &cond, const char *data) const {
        auto lhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
            return c.name == cond.lhs_col.col_name &&
                   (cond.lhs_col.tab_name.empty() || c.tab_name == cond.lhs_col.tab_name);
        });
        if (lhs_it == cols_.end()) return false;
        const char *lhs = data + lhs_it->offset;
        const char *rhs = nullptr;
        if (cond.is_rhs_val) {
            rhs = cond.rhs_val.raw->data;
        } else {
            auto rhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                return c.name == cond.rhs_col.col_name &&
                       (cond.rhs_col.tab_name.empty() || c.tab_name == cond.rhs_col.tab_name);
            });
            if (rhs_it == cols_.end()) return false;
            rhs = data + rhs_it->offset;
        }
        return SeqScanExecutor::compare_value(lhs, rhs, lhs_it->len, lhs_it->type, cond.op);
    }

    void probe_right_index() {
        right_hits_.clear();
        hit_idx_ = 0;
        std::vector<char> key;
        if (!build_lookup_key(key)) return;
        auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(right_table_, index_meta_.cols)).get();
        ih->get_value(key.data(), &right_hits_, context_ ? context_->txn_ : nullptr);
    }

    // 题9 MVCC：内表记录按本事务快照重建可见版本；不可见返回 false（与 SeqScan/IndexScan 一致）
    bool read_right_visible(const Rid &rid, RmRecord &out) {
        auto right_rec = right_fh_->get_record(rid, context_);
        if (!mvcc_on_) {
            memcpy(out.data, right_rec->data, right_record_size_);
            return true;
        }
        std::string buf;
        if (!context_->txn_mgr_->mvcc_read(context_->txn_, right_table_, rid,
                                           right_rec->data, right_record_size_, buf)) {
            return false;
        }
        memcpy(out.data, buf.data(), right_record_size_);
        return true;
    }

    void find_next_valid_tuple() {
        RmRecord right_rec(right_record_size_);
        while (!left_->is_end()) {
            if (left_rec_ == nullptr) {
                left_rec_ = left_->Next();
                probe_right_index();
            }
            while (hit_idx_ < right_hits_.size()) {
                if (read_right_visible(right_hits_[hit_idx_], right_rec) &&
                    eval_joined_conds(&right_rec)) {
                    isend_ = false;
                    return;
                }
                hit_idx_++;
            }
            left_->nextTuple();
            left_rec_.reset();
        }
        isend_ = true;
    }

   public:
    IndexNestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, SmManager *sm_manager,
                                const ScanPlan &right_scan, std::vector<Condition> join_conds, Context *context) {
        left_ = std::move(left);
        sm_manager_ = sm_manager;
        context_ = context;
        right_table_ = right_scan.tab_name_;
        right_tab_ = sm_manager_->db_.get_table(right_table_);
        index_meta_ = *(right_tab_.get_index_meta(right_scan.index_col_names_));
        right_fh_ = sm_manager_->fhs_.at(right_table_).get();
        right_record_size_ = right_fh_->get_file_hdr().record_size;
        right_cols_ = right_tab_.cols;
        join_conds_ = std::move(join_conds);
        right_conds_ = right_scan.conds_;

        len_ = left_->tupleLen() + right_record_size_;
        cols_ = left_->cols();
        auto shifted_right_cols = right_cols_;
        for (auto &col : shifted_right_cols) col.offset += left_->tupleLen();
        cols_.insert(cols_.end(), shifted_right_cols.begin(), shifted_right_cols.end());
    }

    void beginTuple() override {
        // 题9：内表进入 MVCC 脏态后必须按快照重建可见版本
        mvcc_on_ = context_ && context_->txn_mgr_ && context_->txn_ &&
                   context_->txn_mgr_->table_is_dirty(right_table_);
        left_->beginTuple();
        left_rec_.reset();
        isend_ = left_->is_end();
        if (!isend_) find_next_valid_tuple();
    }

    void nextTuple() override {
        if (isend_) return;
        hit_idx_++;
        find_next_valid_tuple();
    }

    std::unique_ptr<RmRecord> Next() override {
        auto rec = std::make_unique<RmRecord>(len_);
        RmRecord right_rec(right_record_size_);
        read_right_visible(right_hits_[hit_idx_], right_rec);   // 当前 hit 已在 find_next 验证过可见
        memcpy(rec->data, left_rec_->data, left_->tupleLen());
        memcpy(rec->data + left_->tupleLen(), right_rec.data, right_record_size_);
        return rec;
    }

    bool is_end() const override { return isend_; }
    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    Rid &rid() override { return _abstract_rid; }
};
