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

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "execution_defs.h"
#include "executor_abstract.h"
#include "common/common.h"
#include "common/repro_ring.h"
#include "analyze/analyze.h"

namespace {
inline Value agg_make_int(int v) { Value x; x.set_int(v); return x; }
inline Value agg_make_float(float v) { Value x; x.set_float(v); return x; }
inline Value agg_make_str(const std::string &s) { Value x; x.set_str(s); return x; }
inline Value agg_make_empty() { return Value(); }
}

class AggExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> prev_;

    struct AggState {
        int64_t count = 0;
        int64_t int_sum = 0;
        double float_sum = 0.0;  // 决赛 FLOAT32 规则：binary64 累加，输出前一次舍回 binary32
        int int_max = INT_MIN;
        float float_max = -FLT_MAX;
        int int_min = INT_MAX;
        float float_min = FLT_MAX;
        std::string str_max;
        std::string str_min;
        int64_t sum_cnt = 0;
        bool has_value = false;
        std::unordered_set<std::string> distinct_seen;  // 决赛：COUNT(DISTINCT col) 去重集合
    };

    std::unordered_map<std::string, std::vector<AggState>> groups_;
    std::vector<std::string> group_order_;
    size_t iter_idx_ = 0;

    std::vector<TabCol> group_cols_;
    std::vector<AggregateInfo> agg_exprs_;
    std::vector<Condition> having_conds_;
    std::vector<ColMeta> cols_;
    size_t len_;
    bool plain_agg_;
    bool done_;
    std::unique_ptr<RmRecord> cur_rec_;
    std::vector<bool> cur_nulls_;   // 当前输出行各列是否 NULL（空集聚合）

    std::unordered_map<std::string, size_t> alias_to_idx_;
    std::unordered_map<std::string, size_t> name_to_idx_;

    // 辅助函数
    std::string make_group_key(const char *data);
    float read_as_float(const char *data, const ColMeta &col);
    int read_as_int(const char *data, const ColMeta &col);
    bool is_null(const char *data, const ColMeta &col);
    std::string get_agg_func_name(ast::AggType type);
    int compare_value_by_val(const Value &a, const Value &b);
    bool evaluate_condition(const Value &lhs, const Value &rhs, CompOp op);
    bool satisfy_having(const std::vector<AggState> &states, const std::string &key);
    void build_cur();
    void advance_to_valid();
    Value get_agg_value(const AggregateInfo &agg, const AggState &st);
    Value get_group_col_value(size_t group_idx, const std::string &key);

public:
    AggExecutor(std::unique_ptr<AbstractExecutor> prev,
                const std::vector<TabCol> &group_cols,
                const std::vector<AggregateInfo> &agg_exprs,
                const std::vector<Condition> &having_conds,
                std::vector<ColMeta> output_cols);

    void beginTuple() override;
    void nextTuple() override;
    bool is_end() const override;
    std::unique_ptr<RmRecord> Next() override;
    const std::vector<bool> *null_mask() const override { return &cur_nulls_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return _abstract_rid; }
};

AggExecutor::AggExecutor(std::unique_ptr<AbstractExecutor> prev,
                         const std::vector<TabCol> &group_cols,
                         const std::vector<AggregateInfo> &agg_exprs,
                         const std::vector<Condition> &having_conds,
                         std::vector<ColMeta> output_cols) {
    prev_ = std::move(prev);
    group_cols_ = group_cols;
    agg_exprs_ = agg_exprs;
    having_conds_ = having_conds;
    cols_ = std::move(output_cols);
    len_ = cols_.empty() ? 0 : cols_.back().offset + cols_.back().len;
    plain_agg_ = group_cols_.empty();
    done_ = false;

    for (size_t i = 0; i < agg_exprs_.size(); i++) {
        if (!agg_exprs_[i].alias.empty()) {
            alias_to_idx_[agg_exprs_[i].alias] = i;
        }
        std::string agg_name = get_agg_func_name(agg_exprs_[i].type) + "(" +
                               (agg_exprs_[i].is_star ? "*" : agg_exprs_[i].col.col_name) + ")";
        name_to_idx_[agg_name] = i;
    }
}

std::string AggExecutor::make_group_key(const char *data) {
    if (plain_agg_) return "";

    std::string key;
    std::string null_bitmap((group_cols_.size() + 7) / 8, 0);
    size_t bit_idx = 0;

    for (auto &gc : group_cols_) {
        auto it = std::find_if(prev_->cols().begin(), prev_->cols().end(),
            [&](const ColMeta &c) {
                return c.name == gc.col_name &&
                       (gc.tab_name.empty() || c.tab_name == gc.tab_name);
            });
        if (it == prev_->cols().end()) continue;

        if (is_null(data, *it)) {
            null_bitmap[bit_idx / 8] |= (1 << (bit_idx % 8));
            key.append(it->len, '\0');
        } else {
            key.append(data + it->offset, it->len);
        }
        bit_idx++;
    }

    key.insert(0, null_bitmap);
    return key;
}

float AggExecutor::read_as_float(const char *data, const ColMeta &col) {
    if (col.type == TYPE_INT) {
        return static_cast<float>(*(const int *)(data + col.offset));
    } else if (col.type == TYPE_FLOAT) {
        return *(const float *)(data + col.offset);
    }
    return 0.0f;
}

int AggExecutor::read_as_int(const char *data, const ColMeta &col) {
    return *(const int *)(data + col.offset);
}

bool AggExecutor::is_null(const char *data, const ColMeta &col) {
    (void)data;
    const auto *mask = prev_->null_mask();
    if (mask == nullptr) return false;
    const auto &input_cols = prev_->cols();
    auto it = std::find_if(input_cols.begin(), input_cols.end(), [&](const ColMeta &candidate) {
        return &candidate == &col ||
               (candidate.offset == col.offset && candidate.name == col.name &&
                candidate.tab_name == col.tab_name);
    });
    if (it == input_cols.end()) return false;
    const size_t index = static_cast<size_t>(it - input_cols.begin());
    return index < mask->size() && (*mask)[index];
}

std::string AggExecutor::get_agg_func_name(ast::AggType type) {
    switch (type) {
        case ast::AGG_COUNT: return "count";
        case ast::AGG_MAX:   return "max";
        case ast::AGG_MIN:   return "min";
        case ast::AGG_SUM:   return "sum";
        case ast::AGG_AVG:   return "avg";
    }
    return "";
}

int AggExecutor::compare_value_by_val(const Value &a, const Value &b) {
    if (a.type == b.type) {
        switch (a.type) {
            case TYPE_INT:
                return (a.int_val < b.int_val) ? -1 : (a.int_val > b.int_val) ? 1 : 0;
            case TYPE_FLOAT:
                return (a.float_val < b.float_val) ? -1 : (a.float_val > b.float_val) ? 1 : 0;
            case TYPE_STRING:
                return a.str_val.compare(b.str_val);
            default:
                return 0;
        }
    }
    // INT vs FLOAT: promote to float
    if ((a.type == TYPE_INT && b.type == TYPE_FLOAT) ||
        (a.type == TYPE_FLOAT && b.type == TYPE_INT)) {
        float fa = (a.type == TYPE_INT) ? static_cast<float>(a.int_val) : a.float_val;
        float fb = (b.type == TYPE_INT) ? static_cast<float>(b.int_val) : b.float_val;
        return (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
    }
    return static_cast<int>(a.type) - static_cast<int>(b.type);
}

bool AggExecutor::evaluate_condition(const Value &lhs, const Value &rhs, CompOp op) {
    int cmp = compare_value_by_val(lhs, rhs);
    switch (op) {
        case OP_EQ: return cmp == 0;
        case OP_NE: return cmp != 0;
        case OP_LT: return cmp < 0;
        case OP_LE: return cmp <= 0;
        case OP_GT: return cmp > 0;
        case OP_GE: return cmp >= 0;
        default:    return false;
    }
}

Value AggExecutor::get_agg_value(const AggregateInfo &agg, const AggState &st) {
    switch (agg.type) {
        case ast::AGG_COUNT: return agg_make_int(static_cast<int>(st.count));
        case ast::AGG_SUM:
            if (agg.arg_type == TYPE_INT) return agg_make_int(static_cast<int>(st.int_sum));
            else return agg_make_float(static_cast<float>(st.float_sum));
        case ast::AGG_MAX:
            if (!st.has_value) return agg_make_empty();
            if (agg.arg_type == TYPE_INT) return agg_make_int(st.int_max);
            if (agg.arg_type == TYPE_FLOAT) return agg_make_float(st.float_max);
            return agg_make_str(st.str_max);
        case ast::AGG_MIN:
            if (!st.has_value) return agg_make_empty();
            if (agg.arg_type == TYPE_INT) return agg_make_int(st.int_min);
            if (agg.arg_type == TYPE_FLOAT) return agg_make_float(st.float_min);
            return agg_make_str(st.str_min);
        case ast::AGG_AVG:
            return (st.sum_cnt > 0)
                       ? agg_make_float(static_cast<float>((agg.arg_type == TYPE_INT)
                                   ? (static_cast<double>(st.int_sum) / st.sum_cnt)
                                   : (st.float_sum / st.sum_cnt)))
                       : agg_make_float(0.0f);
    }
    return agg_make_empty();
}

Value AggExecutor::get_group_col_value(size_t group_idx, const std::string &key) {
    size_t key_off = (group_cols_.size() + 7) / 8;
    for (size_t i = 0; i < group_cols_.size(); i++) {
        if (i == group_idx) {
            size_t byte_idx = i / 8;
            size_t bit_idx = i % 8;
            if ((key[byte_idx] & (1 << bit_idx)) != 0) return agg_make_empty();

            const ColMeta &col = cols_[i];
            if (col.type == TYPE_INT) {
                int val;
                memcpy(&val, key.data() + key_off, sizeof(int));
                return agg_make_int(val);
            } else if (col.type == TYPE_FLOAT) {
                float val;
                memcpy(&val, key.data() + key_off, sizeof(float));
                return agg_make_float(val);
            } else {
                std::string str_val(key.data() + key_off, col.len);
                size_t null_pos = str_val.find('\0');
                if (null_pos != std::string::npos) str_val.resize(null_pos);
                return agg_make_str(str_val);
            }
        }
        key_off += cols_[i].len;
    }
    return agg_make_empty();
}

bool AggExecutor::satisfy_having(const std::vector<AggState> &states, const std::string &key) {
    if (having_conds_.empty()) return true;

    for (auto &cond : having_conds_) {
        Value lhs_val, rhs_val;
        bool lhs_found = false;
        size_t agg_idx = 0;

        // 1. 先检查是否是 GROUP BY 列
        for (size_t i = 0; i < group_cols_.size(); i++) {
            if (group_cols_[i].col_name == cond.lhs_col.col_name &&
                (cond.lhs_col.tab_name.empty() ||
                 group_cols_[i].tab_name == cond.lhs_col.tab_name)) {
                lhs_val = get_group_col_value(i, key);
                lhs_found = true;
                break;
            }
        }

        // 2. 再检查是否是聚合函数（别名或函数名）
        if (!lhs_found) {
            auto it = alias_to_idx_.find(cond.lhs_col.col_name);
            if (it != alias_to_idx_.end()) {
                lhs_found = true;
                agg_idx = it->second;
            } else {
                auto it2 = name_to_idx_.find(cond.lhs_col.col_name);
                if (it2 != name_to_idx_.end()) {
                    lhs_found = true;
                    agg_idx = it2->second;
                }
            }
            if (lhs_found) {
                lhs_val = get_agg_value(agg_exprs_[agg_idx], states[agg_idx]);
            }
        }

        if (!lhs_found) return false;

        rhs_val = cond.rhs_val;
        if (!evaluate_condition(lhs_val, rhs_val, cond.op)) {
            return false;
        }
    }
    return true;
}

void AggExecutor::advance_to_valid() {
    while (iter_idx_ < group_order_.size() &&
           !satisfy_having(groups_[group_order_[iter_idx_]], group_order_[iter_idx_])) {
        ++iter_idx_;
    }
}

void AggExecutor::beginTuple() {
    groups_.clear();
    group_order_.clear();
    done_ = false;

    prev_->beginTuple();
    // MIN 索引早停：无分组、唯一聚合是 MIN(col)、无 HAVING，且子执行器保证输出按
    // col 升序（见 IndexScanExecutor::sorted_asc_on）——首个非 NULL 值即全局最小，
    // 不必耗尽子扫描。Delivery 的 min(no_o_id) 在热点长队列上从 O(队列) 降为 O(1)，
    // 这是 07-31 定量分析里 ~90% 线程时间的来源（Docs/Optimize/14）。
    static const bool min_es_off = std::getenv("RMDB_NO_MIN_EARLYSTOP") != nullptr;  // A/B 归因开关
    const bool min_early_stop_ = !min_es_off && plain_agg_ && agg_exprs_.size() == 1 &&
                                 agg_exprs_[0].type == ast::AGG_MIN && !agg_exprs_[0].is_star &&
                                 having_conds_.empty() && prev_->sorted_asc_on(agg_exprs_[0].col);

    for (; !prev_->is_end(); prev_->nextTuple()) {
        auto rec = prev_->Next();
        if (!rec) continue;

        std::string key = make_group_key(rec->data);

        auto it = groups_.find(key);
        if (it == groups_.end()) {
            std::vector<AggState> states(agg_exprs_.size());
            groups_.emplace(key, std::move(states));
            group_order_.push_back(key);
        }
        auto &states = groups_[key];

        for (size_t i = 0; i < agg_exprs_.size(); ++i) {
            auto &agg = agg_exprs_[i];
            auto &st = states[i];

            switch (agg.type) {
                case ast::AGG_COUNT:
                    if (agg.is_star) {
                        st.count++;
                    } else {
                        auto col_it = std::find_if(prev_->cols().begin(), prev_->cols().end(),
                            [&](const ColMeta &c) {
                                return c.name == agg.col.col_name &&
                                       (agg.col.tab_name.empty() || c.tab_name == agg.col.tab_name);
                            });
                        if (col_it != prev_->cols().end() && !is_null(rec->data, *col_it)) {
                            if (agg.distinct) {
                                // 决赛：COUNT(DISTINCT col) —— 按列原始字节去重后计数
                                std::string dkey(rec->data + col_it->offset, col_it->len);
                                if (st.distinct_seen.insert(std::move(dkey)).second) {
                                    st.count++;
                                }
                            } else {
                                st.count++;
                            }
                        }
                    }
                    break;

                case ast::AGG_MAX:
                case ast::AGG_MIN:
                case ast::AGG_SUM:
                case ast::AGG_AVG: {
                    auto col_it = std::find_if(prev_->cols().begin(), prev_->cols().end(),
                        [&](const ColMeta &c) {
                            return c.name == agg.col.col_name &&
                                   (agg.col.tab_name.empty() || c.tab_name == agg.col.tab_name);
                        });
                    if (col_it == prev_->cols().end()) break;
                    if (is_null(rec->data, *col_it)) break;

                    if (col_it->type == TYPE_INT) {
                        int ival = read_as_int(rec->data, *col_it);
                        if (!st.has_value) {
                            st.int_max = st.int_min = ival;
                            st.has_value = true;
                        } else {
                            st.int_max = std::max(st.int_max, ival);
                            st.int_min = std::min(st.int_min, ival);
                        }
                        st.int_sum += ival;
                    } else if (col_it->type == TYPE_FLOAT) {
                        float fval = read_as_float(rec->data, *col_it);
                        if (!st.has_value) {
                            st.float_max = st.float_min = fval;
                            st.has_value = true;
                        } else {
                            st.float_max = std::max(st.float_max, fval);
                            st.float_min = std::min(st.float_min, fval);
                        }
                        st.float_sum += fval;
                    } else if (col_it->type == TYPE_STRING) {
                        std::string sval((char *)(rec->data + col_it->offset), col_it->len);
                        sval.resize(strlen(sval.c_str()));
                        if (!st.has_value) {
                            st.str_max = st.str_min = sval;
                            st.has_value = true;
                        } else {
                            if (sval > st.str_max) st.str_max = sval;
                            if (sval < st.str_min) st.str_min = sval;
                        }
                    }
                    st.sum_cnt++;
                    break;
                }
            }
        }

        // NULL 行不置 has_value，须继续找首个非 NULL 值才能停
        if (min_early_stop_ && states[0].has_value) break;
    }

    if (groups_.empty() && plain_agg_) {
        groups_[""] = std::vector<AggState>(agg_exprs_.size());
        group_order_.push_back("");
    }
    if (ReproRing::on() && plain_agg_ && !groups_.empty()) {
        for (size_t i = 0; i < agg_exprs_.size(); ++i) {
            if (agg_exprs_[i].type != ast::AGG_MIN) continue;
            auto &st = groups_.begin()->second[i];
            ReproRing::push(ReproRing::AGGOUT, st.has_value ? st.int_min : -999,
                            (int32_t)st.sum_cnt, 0, 0);
        }
    }

    iter_idx_ = 0;
    advance_to_valid();
    build_cur();
}

void AggExecutor::nextTuple() {
    if (iter_idx_ < group_order_.size()) {
        ++iter_idx_;
    }
    advance_to_valid();
    build_cur();
}

bool AggExecutor::is_end() const {
    return iter_idx_ >= group_order_.size();
}

std::unique_ptr<RmRecord> AggExecutor::Next() {
    if (!cur_rec_) return nullptr;
    return std::make_unique<RmRecord>(*cur_rec_);
}

void AggExecutor::build_cur() {
    if (iter_idx_ >= group_order_.size()) {
        cur_rec_.reset();
        return;
    }

    cur_rec_ = std::make_unique<RmRecord>(len_);
    char *dst = cur_rec_->data;
    memset(dst, 0, len_);
    cur_nulls_.assign(cols_.size(), false);

    const std::string &key = group_order_[iter_idx_];
    const auto &states = groups_[key];

    // 1. 写分组列
    if (!plain_agg_) {
        size_t key_off = (group_cols_.size() + 7) / 8;
        for (size_t i = 0; i < group_cols_.size(); ++i) {
            size_t col_len = cols_[i].len;
            const bool is_null_group =
                (static_cast<unsigned char>(key[i / 8]) & (1U << (i % 8))) != 0;
            if (is_null_group) {
                cur_nulls_[i] = true;
            } else {
                memcpy(dst + cols_[i].offset, key.data() + key_off, col_len);
            }
            key_off += col_len;
        }
    }

    // 2. 写聚合结果列
    for (size_t i = 0; i < agg_exprs_.size(); ++i) {
        auto &agg = agg_exprs_[i];
        auto &st = states[i];
        int col_idx = plain_agg_ ? i : (group_cols_.size() + i);
        char *slot = dst + cols_[col_idx].offset;
        ColType out_type = cols_[col_idx].type;

        switch (agg.type) {
            case ast::AGG_COUNT:
                *(int *)slot = static_cast<int>(st.count);
                break;

            case ast::AGG_SUM:
                if (out_type == TYPE_INT) {
                    *(int *)slot = static_cast<int>(st.int_sum);
                } else {
                    // 决赛 FLOAT32：SUM 全程 binary64 累加，此处一次性舍回 binary32
                    *(float *)slot = (agg.arg_type == TYPE_INT)
                                         ? static_cast<float>(st.int_sum)
                                         : static_cast<float>(st.float_sum);
                }
                break;

            case ast::AGG_MAX:
                // 空集输出 0（评测家族语义：P2 边界基线期望 0.000000，非 SQL 标准 NULL）
                if (st.has_value) {
                    if (out_type == TYPE_INT) {
                        *(int *)slot = st.int_max;
                    } else if (out_type == TYPE_FLOAT) {
                        *(float *)slot = st.float_max;
                    } else {
                        memset(slot, 0, cols_[col_idx].len);
                        size_t cpy = std::min(st.str_max.size(), static_cast<size_t>(cols_[col_idx].len));
                        memcpy(slot, st.str_max.c_str(), cpy);
                    }
                }
                break;

            case ast::AGG_MIN:
                if (st.has_value) {
                    if (out_type == TYPE_INT) {
                        *(int *)slot = st.int_min;
                    } else if (out_type == TYPE_FLOAT) {
                        *(float *)slot = st.float_min;
                    } else {
                        memset(slot, 0, cols_[col_idx].len);
                        size_t cpy = std::min(st.str_min.size(), static_cast<size_t>(cols_[col_idx].len));
                        memcpy(slot, st.str_min.c_str(), cpy);
                    }
                }
                break;

            case ast::AGG_AVG:
                *(float *)slot = (st.sum_cnt > 0)
                                     ? static_cast<float>((agg.arg_type == TYPE_INT)
                                            ? (static_cast<double>(st.int_sum) / st.sum_cnt)
                                            : (st.float_sum / st.sum_cnt))
                                     : 0.0f;
                break;
        }
    }
}
