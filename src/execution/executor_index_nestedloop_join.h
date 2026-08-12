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
    ConditionExprPtr on_predicate_;
    ConditionExprPtr right_predicate_;
    // 只有顶层正向 AND 上的原子才可用于构造精确索引 key。
    std::vector<Condition> on_access_conditions_;
    std::vector<Condition> right_access_conditions_;

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
            case OP_LIKE: return OP_LIKE;
            case OP_IS_NULL: return OP_IS_NULL;
            case OP_IS_NOT_NULL: return OP_IS_NOT_NULL;
        }
        return op;
    }

    // 把依赖当前外表行的 join 条件实例化为纯右表谓词。
    // 例如 r.id = s.id 在 r.id=7 时登记为 s.id = 7，不能用整表读替代，
    // 否则会把 INLJ 的点查冲突集无谓扩大。
    Condition instantiate_right_atom(const Condition &input) const {
        Condition cond = input;
        auto instantiate_operand = [&](const TabCol &column, Value &value) {
            auto left_it = std::find_if(left_->cols().begin(), left_->cols().end(),
                                        [&](const ColMeta &candidate) {
                return candidate.name == column.col_name &&
                       (column.tab_name.empty() || candidate.tab_name == column.tab_name);
            });
            if (left_it == left_->cols().end()) throw ColumnNotFoundError(column.col_name);
            const size_t left_index = static_cast<size_t>(left_it - left_->cols().begin());
            if (left_index < left_nulls_.size() && left_nulls_[left_index]) {
                throw InternalError("NULL outer operand reached INLJ SSI atom instantiation");
            }
            value.type = left_it->type;
            value.raw = std::make_shared<RmRecord>(left_it->len);
            memcpy(value.raw->data, left_rec_->data + left_it->offset, left_it->len);
        };

        const bool lhs_right = cond.lhs_col.tab_name == right_binding_;
        const bool rhs_right = !cond.is_rhs_val && cond.rhs_col.tab_name == right_binding_;
        if (lhs_right && !cond.is_rhs_val && !rhs_right) {
            Value value;
            instantiate_operand(cond.rhs_col, value);
            cond.is_rhs_val = true;
            cond.rhs_val = std::move(value);
        } else if (!lhs_right && rhs_right) {
            Value value;
            instantiate_operand(cond.lhs_col, value);
            cond.lhs_col = cond.rhs_col;
            cond.op = swap_comp_op(cond.op);
            cond.is_rhs_val = true;
            cond.rhs_val = std::move(value);
        }
        if (cond.lhs_col.tab_name == right_binding_) cond.lhs_col.tab_name = right_table_;
        if (!cond.is_rhs_val && cond.rhs_col.tab_name == right_binding_) {
            cond.rhs_col.tab_name = right_table_;
        }
        return cond;
    }

    bool outer_operand_is_null(const TabCol &column) const {
        auto it = std::find_if(left_->cols().begin(), left_->cols().end(),
                               [&](const ColMeta &candidate) {
            return candidate.name == column.col_name &&
                   (column.tab_name.empty() || candidate.tab_name == column.tab_name);
        });
        if (it == left_->cols().end()) throw ColumnNotFoundError(column.col_name);
        const size_t index = static_cast<size_t>(it - left_->cols().begin());
        return index < left_nulls_.size() && left_nulls_[index];
    }

    // 把 ON 树中依赖当前外表行的原子递归实例化：混合原子改成
    // 右列-vs-字面量，纯左原子改成 TRUE/FALSE/UNKNOWN 常量。因此在
    // NOT/OR 下也不会丢失 SQL 三值语义。
    ConditionExprPtr instantiate_right_expr(const ConditionExprPtr &expr) const {
        if (expr == nullptr) return nullptr;
        switch (expr->type) {
            case BoolExprType::CONSTANT:
                return make_bool_constant<Condition>(expr->constant);
            case BoolExprType::NOT:
                return make_bool_not<Condition>(instantiate_right_expr(expr->left));
            case BoolExprType::AND:
            case BoolExprType::OR:
                return make_bool_binary<Condition>(expr->type,
                                                    instantiate_right_expr(expr->left),
                                                    instantiate_right_expr(expr->right));
            case BoolExprType::ATOM: {
                const Condition &cond = expr->atom;
                const bool lhs_right = cond.lhs_col.tab_name == right_binding_;
                const bool rhs_right = !is_null_test_op(cond.op) && !cond.is_rhs_val &&
                                       cond.rhs_col.tab_name == right_binding_;
                if (!lhs_right && !rhs_right) {
                    std::vector<char> joined(len_, 0);
                    memcpy(joined.data(), left_rec_->data, left_->tupleLen());
                    return make_bool_constant<Condition>(eval_cond(cond, joined.data()));
                }
                if (!cond.is_rhs_val && lhs_right != rhs_right) {
                    const TabCol &outer_col = lhs_right ? cond.rhs_col : cond.lhs_col;
                    if (outer_operand_is_null(outer_col)) {
                        return make_bool_constant<Condition>(TruthValue::UNKNOWN_VALUE);
                    }
                }
                return make_bool_atom<Condition>(instantiate_right_atom(cond));
            }
        }
        throw InternalError("Unknown INLJ SSI boolean expression node");
    }

    // 递归实例化完整的右表谓词 AND ON 树，保留 OR/NOT 拓扑。
    ConditionExprPtr build_right_ser_pred() const {
        auto physical_right = instantiate_right_expr(right_predicate_);
        auto physical_on = instantiate_right_expr(on_predicate_);
        if (physical_right == nullptr) return physical_on;
        if (physical_on == nullptr) return physical_right;
        return make_bool_binary<Condition>(BoolExprType::AND, std::move(physical_right),
                                           std::move(physical_on));
    }

    bool condition_supplies_right_key(const Condition &cond, const ColMeta &idx_col,
                                      const char *&value) const {
        value = nullptr;
        if (cond.op != OP_EQ) return false;
        if (cond.is_rhs_val) {
            if (cond.lhs_col.tab_name == right_binding_ &&
                cond.lhs_col.col_name == idx_col.name && cond.rhs_val.raw != nullptr) {
                value = cond.rhs_val.raw->data;
                return true;
            }
            return false;
        }

        TabCol left_col;
        if (cond.lhs_col.tab_name == right_binding_ &&
            cond.lhs_col.col_name == idx_col.name &&
            cond.rhs_col.tab_name != right_binding_) {
            left_col = cond.rhs_col;
        } else if (cond.rhs_col.tab_name == right_binding_ &&
                   cond.rhs_col.col_name == idx_col.name &&
                   cond.lhs_col.tab_name != right_binding_) {
            left_col = cond.lhs_col;
        } else {
            return false;
        }
        auto left_it = std::find_if(left_->cols().begin(), left_->cols().end(),
                                    [&](const ColMeta &candidate) {
            return candidate.name == left_col.col_name &&
                   (left_col.tab_name.empty() || candidate.tab_name == left_col.tab_name);
        });
        if (left_it == left_->cols().end()) throw ColumnNotFoundError(left_col.col_name);
        const size_t left_index = static_cast<size_t>(left_it - left_->cols().begin());
        if (left_index < left_nulls_.size() && left_nulls_[left_index]) return false;
        value = left_rec_->data + left_it->offset;
        return true;
    }

    bool build_lookup_key(std::vector<char> &key) {
        key.assign(index_meta_.col_tot_len, 0);
        size_t key_off = 0;
        for (auto &idx_col : index_meta_.cols) {
            bool filled = false;
            const char *value = nullptr;
            for (const auto &cond : right_access_conditions_) {
                if (condition_supplies_right_key(cond, idx_col, value)) {
                    memcpy(key.data() + key_off, value, idx_col.len);
                    filled = true;
                    break;
                }
            }
            if (!filled) {
                for (const auto &cond : on_access_conditions_) {
                    if (condition_supplies_right_key(cond, idx_col, value)) {
                        memcpy(key.data() + key_off, value, idx_col.len);
                        filled = true;
                        break;
                    }
                }
            }
            if (!filled) return false;
            key_off += idx_col.len;
        }
        return true;
    }

    bool eval_joined_conds(const RmRecord *right_rec) const {
        std::vector<char> joined(len_);
        memcpy(joined.data(), left_rec_->data, left_->tupleLen());
        memcpy(joined.data() + left_->tupleLen(), right_rec->data, right_record_size_);
        const auto evaluate_tree = [&](const ConditionExprPtr &expr) {
            return evaluate_bool_expr(expr, [&](const Condition &cond) {
                       return eval_cond(cond, joined.data());
                   }) == TruthValue::TRUE_VALUE;
        };
        return evaluate_tree(right_predicate_) && evaluate_tree(on_predicate_);
    }

    TruthValue eval_cond(const Condition &cond, const char *data) const {
        auto lhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
            return c.name == cond.lhs_col.col_name &&
                   (cond.lhs_col.tab_name.empty() || c.tab_name == cond.lhs_col.tab_name);
        });
        if (lhs_it == cols_.end()) throw ColumnNotFoundError(cond.lhs_col.col_name);
        const size_t lhs_index = static_cast<size_t>(lhs_it - cols_.begin());
        const bool lhs_is_null = lhs_index < left_nulls_.size() && left_nulls_[lhs_index];
        if (is_null_test_op(cond.op)) return evaluate_null_test(lhs_is_null, cond.op);
        if (lhs_is_null) {
            return TruthValue::UNKNOWN_VALUE;
        }
        const char *lhs = data + lhs_it->offset;
        const char *rhs = nullptr;
        if (cond.is_rhs_val) {
            if (cond.rhs_val.raw == nullptr) {
                throw InternalError("INLJ predicate literal has no raw value");
            }
            rhs = cond.rhs_val.raw->data;
        } else {
            auto rhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                return c.name == cond.rhs_col.col_name &&
                       (cond.rhs_col.tab_name.empty() || c.tab_name == cond.rhs_col.tab_name);
            });
            if (rhs_it == cols_.end()) throw ColumnNotFoundError(cond.rhs_col.col_name);
            const size_t rhs_index = static_cast<size_t>(rhs_it - cols_.begin());
            if (rhs_index < left_nulls_.size() && left_nulls_[rhs_index]) {
                return TruthValue::UNKNOWN_VALUE;
            }
            rhs = data + rhs_it->offset;
        }
        return SeqScanExecutor::compare_value(lhs, rhs, lhs_it->len, lhs_it->type, cond.op)
                   ? TruthValue::TRUE_VALUE
                   : TruthValue::FALSE_VALUE;
    }

    void probe_right_index() {
        right_hits_.clear();
        hit_idx_ = 0;
        std::vector<char> key;
        if (!build_lookup_key(key)) return;
        if (ser_on_) {
            ConditionExprPtr pred = build_right_ser_pred();
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
                                const ScanPlan &right_scan, ConditionExprPtr on_predicate,
                                Context *context) {
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
        on_predicate_ = std::move(on_predicate);
        right_predicate_ = right_scan.predicate_;
        right_access_conditions_ = right_scan.access_conditions_;
        extract_conjunctive_atoms(on_predicate_, on_access_conditions_);

        len_ = left_->tupleLen() + right_record_size_;
        cols_ = left_->cols();
        auto shifted_right_cols = right_cols_;
        for (auto &col : shifted_right_cols) col.offset += left_->tupleLen();
        cols_.insert(cols_.end(), shifted_right_cols.begin(), shifted_right_cols.end());
    }

    IndexNestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, SmManager *sm_manager,
                                const ScanPlan &right_scan,
                                std::vector<Condition> join_conds, Context *context)
        : IndexNestedLoopJoinExecutor(
              std::move(left), sm_manager, right_scan,
              SeqScanExecutor::conditions_to_expr(std::move(join_conds)), context) {}

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
