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
    std::string right_binding_;
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
    std::vector<bool> left_nulls_;
    std::vector<bool> output_nulls_;
    std::vector<Rid> right_hits_;
    size_t hit_idx_ = 0;
    bool isend_ = true;
    bool mvcc_on_ = false;      // 题9：内表脏态时按快照重建可见版本
    bool ser_on_ = false;       // 决赛 H3：SER 下跟踪内表的实例化谓词与实际命中 RID

    static CompOp swap_comp_op(CompOp op) {
        switch (op) {
            case OP_EQ: return OP_EQ;
            case OP_NE: return OP_NE;
            case OP_LT: return OP_GT;
            case OP_GT: return OP_LT;
            case OP_LE: return OP_GE;
            case OP_GE: return OP_LE;
        }
        return op;
    }

    // 把依赖当前外表行的 join 条件实例化为纯右表谓词。
    // 例如 r.id = s.id 在 r.id=7 时登记为 s.id = 7，不能用整表读替代，
    // 否则会把 INLJ 的点查冲突集无谓扩大。
    void build_right_ser_pred(std::vector<Condition> &pred) {
        pred = right_conds_;
        pred.reserve(right_conds_.size() + join_conds_.size());
        for (const auto &cond : join_conds_) {
            if (cond.is_rhs_val) continue;

            TabCol right_col;
            TabCol left_col;
            CompOp op;
            if (cond.lhs_col.tab_name == right_binding_ && cond.rhs_col.tab_name != right_binding_) {
                right_col = cond.lhs_col;
                left_col = cond.rhs_col;
                op = cond.op;
            } else if (cond.rhs_col.tab_name == right_binding_ && cond.lhs_col.tab_name != right_binding_) {
                right_col = cond.rhs_col;
                left_col = cond.lhs_col;
                op = swap_comp_op(cond.op);
            } else {
                continue;
            }

            auto left_it = get_col(left_->cols(), left_col);
            auto right_it = get_col(right_cols_, right_col);
            const size_t left_index = static_cast<size_t>(left_it - left_->cols().begin());
            if (left_index < left_nulls_.size() && left_nulls_[left_index]) continue;
            Condition instantiated;
            instantiated.lhs_col = right_col;
            instantiated.op = op;
            instantiated.is_rhs_val = true;
            instantiated.rhs_val.type = right_it->type;
            instantiated.rhs_val.raw = std::make_shared<RmRecord>(right_it->len);
            memcpy(instantiated.rhs_val.raw->data, left_rec_->data + left_it->offset, right_it->len);
            pred.push_back(std::move(instantiated));
        }
        for (auto &cond : pred) {
            if (cond.lhs_col.tab_name == right_binding_) cond.lhs_col.tab_name = right_table_;
            if (!cond.is_rhs_val && cond.rhs_col.tab_name == right_binding_) cond.rhs_col.tab_name = right_table_;
        }
    }

    bool build_lookup_key(std::vector<char> &key) {
        key.assign(index_meta_.col_tot_len, 0);
        size_t key_off = 0;
        for (auto &idx_col : index_meta_.cols) {
            bool filled = false;
            for (auto &cond : right_conds_) {
                if (cond.op != OP_EQ || !cond.is_rhs_val) continue;
                if (cond.lhs_col.tab_name != right_binding_ || cond.lhs_col.col_name != idx_col.name) continue;
                memcpy(key.data() + key_off, cond.rhs_val.raw->data, idx_col.len);
                filled = true;
                break;
            }
            if (!filled) {
                for (auto &cond : join_conds_) {
                    if (cond.op != OP_EQ || cond.is_rhs_val) continue;
                    TabCol left_col;
                    bool matched = false;
                    if (cond.lhs_col.tab_name == right_binding_ && cond.lhs_col.col_name == idx_col.name) {
                        left_col = cond.rhs_col;
                        matched = true;
                    } else if (cond.rhs_col.tab_name == right_binding_ && cond.rhs_col.col_name == idx_col.name) {
                        left_col = cond.lhs_col;
                        matched = true;
                    }
                    if (!matched) continue;
                    auto left_it = get_col(left_->cols(), left_col);
                    const size_t left_index = static_cast<size_t>(left_it - left_->cols().begin());
                    if (left_index < left_nulls_.size() && left_nulls_[left_index]) return false;
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
        const size_t lhs_index = static_cast<size_t>(lhs_it - cols_.begin());
        if (lhs_index < left_nulls_.size() && left_nulls_[lhs_index]) return false;
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
            const size_t rhs_index = static_cast<size_t>(rhs_it - cols_.begin());
            if (rhs_index < left_nulls_.size() && left_nulls_[rhs_index]) return false;
            rhs = data + rhs_it->offset;
        }
        return SeqScanExecutor::compare_value(lhs, rhs, lhs_it->len, lhs_it->type, cond.op);
    }

    void probe_right_index() {
        right_hits_.clear();
        hit_idx_ = 0;
        std::vector<char> key;
        if (!build_lookup_key(key)) return;
        if (ser_on_) {
            std::vector<Condition> pred;
            build_right_ser_pred(pred);
            context_->txn_mgr_->ser_record_pred(context_->txn_, right_table_, pred);
            if (context_->txn_mgr_->ser_read_pred_check(context_->txn_, right_table_, pred)) {
                throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                AbortReason::SSI_DANGEROUS_STRUCTURE);
            }
        }
        auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(right_table_, index_meta_.cols)).get();
        ih->get_value(key.data(), &right_hits_, context_ ? context_->txn_ : nullptr);
    }

    // 版本链兜底读：槽位不存活（已提交删除/清理竞态）时按快照经链重建，不触碰堆字节
    bool read_right_chain_only(const Rid &rid, RmRecord &out) {
        if (!mvcc_on_ || context_ == nullptr || context_->txn_mgr_ == nullptr) return false;
        std::string buf;
        if (!context_->txn_mgr_->mvcc_read(context_->txn_, right_table_, rid,
                                           nullptr, right_record_size_, buf, false)) {
            return false;
        }
        if ((int)buf.size() < right_record_size_) return false;
        memcpy(out.data, buf.data(), right_record_size_);
        return true;
    }

    // 题9 MVCC：内表记录按本事务快照重建可见版本；不可见返回 false（与 SeqScan/IndexScan 一致）
    bool read_right_visible(const Rid &rid, RmRecord &out) {
        // 陈旧索引项防护：槽位已释放（已提交删除）时 get_record 会抛 RecordNotFoundError；
        // 无版本链兜底则该 rid 不可见，有链则按快照经版本链重建（不触碰堆字节）。
        if (!right_fh_->is_record(rid)) {
            return read_right_chain_only(rid, out);
        }
        std::unique_ptr<RmRecord> right_rec;
        try {
            right_rec = right_fh_->get_record(rid, context_);
        } catch (RecordNotFoundError &) {
            // is_record 采样与 get_record 之间槽位被物理清理（drain/checkpoint 竞态）：
            // 不是错误，按"槽位不存活"走链兜底——此前该窗口会把整条语句升级成 ERROR 终结
            return read_right_chain_only(rid, out);
        }
        if (!mvcc_on_) {
            memcpy(out.data, right_rec->data, right_record_size_);
            return true;
        }
        std::string buf;
        bool from_heap = false;
        if (!context_->txn_mgr_->mvcc_read(context_->txn_, right_table_, rid,
                                           right_rec->data, right_record_size_, buf, true,
                                           &from_heap)) {
            return false;
        }
        // drain 竞态窗口复查（同 IndexScan：堆回退的可见性依赖读前的槽位采样）
        if (from_heap && !right_fh_->is_record(rid)) return false;
        memcpy(out.data, buf.data(), right_record_size_);
        return true;
    }

    void find_next_valid_tuple() {
        RmRecord right_rec(right_record_size_);
        while (!left_->is_end()) {
            if (left_rec_ == nullptr) {
                left_nulls_.assign(left_->cols().size(), false);
                if (const auto *mask = left_->null_mask(); mask != nullptr) {
                    for (size_t i = 0; i < left_nulls_.size() && i < mask->size(); ++i) {
                        left_nulls_[i] = (*mask)[i];
                    }
                }
                left_rec_ = left_->Next();
                probe_right_index();
            }
            while (hit_idx_ < right_hits_.size()) {
                if (read_right_visible(right_hits_[hit_idx_], right_rec) &&
                    eval_joined_conds(&right_rec)) {
                    if (ser_on_) {
                        const Rid &rid = right_hits_[hit_idx_];
                        context_->txn_mgr_->ser_record_read(context_->txn_, right_table_, rid);
                        if (context_->txn_mgr_->ser_read_check(context_->txn_, right_table_, rid)) {
                            throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                            AbortReason::SSI_DANGEROUS_STRUCTURE);
                        }
                    }
                    isend_ = false;
                    output_nulls_ = left_nulls_;
                    output_nulls_.insert(output_nulls_.end(), right_cols_.size(), false);
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
        right_binding_ = right_scan.binding_name_;
        right_tab_ = sm_manager_->db_.get_table(right_table_);
        index_meta_ = *(right_tab_.get_index_meta(right_scan.index_col_names_));
        right_fh_ = sm_manager_->fhs_.at(right_table_).get();
        right_record_size_ = right_fh_->get_file_hdr().record_size;
        right_cols_ = right_tab_.cols;
        for (auto &col : right_cols_) col.tab_name = right_binding_;
        join_conds_ = std::move(join_conds);
        right_conds_ = right_scan.predicates_;

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
        ser_on_ = context_ && context_->txn_mgr_ && context_->txn_ && context_->ser_in_select_ &&
                  context_->txn_mgr_->is_ser(context_->txn_);
        left_->beginTuple();
        left_rec_.reset();
        left_nulls_.clear();
        output_nulls_.clear();
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
    const std::vector<bool> *null_mask() const override {
        return std::any_of(output_nulls_.begin(), output_nulls_.end(), [](bool value) { return value; })
                   ? &output_nulls_
                   : nullptr;
    }
    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    Rid &rid() override { return _abstract_rid; }
};
