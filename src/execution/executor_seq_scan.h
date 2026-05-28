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

    /**
     * @description: 判断一条记录是否满足所有 WHERE 条件（AND 关系）
     */
    bool eval_conds(const RmRecord *rec) const {
        for (const auto &cond : conds_) {
            // 1. 找 lhs 列在 cols_ 里的位置
            auto lhs_col_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                return c.tab_name == cond.lhs_col.tab_name && c.name == cond.lhs_col.col_name;
            });
            if (lhs_col_it == cols_.end()) return false;

            const char *lhs_data = rec->data + lhs_col_it->offset;
            const char *rhs_data;
            int len = lhs_col_it->len;
            ColType type = lhs_col_it->type;

            // 2. 右值：常量 或 另一列
            if (cond.is_rhs_val) {
                rhs_data = cond.rhs_val.raw->data;
            } else {
                auto rhs_col_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                    return c.tab_name == cond.rhs_col.tab_name && c.name == cond.rhs_col.col_name;
                });
                if (rhs_col_it == cols_.end()) return false;
                rhs_data = rec->data + rhs_col_it->offset;
            }

            // 3. 比较，任一条件不满足 → false
            if (!compare_value(lhs_data, rhs_data, len, type, cond.op)) {
                return false;
            }
        }
        return true;
    }

    void beginTuple() override {
        scan_ = std::make_unique<RmScan>(fh_);
        while (!scan_->is_end()) {
            rid_ = scan_->rid();
            auto rec = fh_->get_record(rid_, context_);
            if (eval_conds(rec.get())) return;
            scan_->next();
        }
    }

    void nextTuple() override {
        if (scan_->is_end()) return;
        scan_->next();
        while (!scan_->is_end()) {
            rid_ = scan_->rid();
            auto rec = fh_->get_record(rid_, context_);
            if (eval_conds(rec.get())) return;
            scan_->next();
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        return fh_->get_record(rid_, context_);
    }

    bool is_end() const override { return scan_->is_end(); }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    size_t tupleLen() const override { return len_; }

    Rid &rid() override { return rid_; }
};
