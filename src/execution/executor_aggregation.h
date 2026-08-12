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

    struct AggState { // 一个聚合状态对应一个聚合表达式
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
        // Most states are not DISTINCT; keep the per-group base state small
        // and allocate the hash set only for a DISTINCT aggregate that sees a
        // non-NULL value.
        std::unique_ptr<std::unordered_set<std::string>> distinct_seen;
    };

    std::unordered_map<std::string, std::vector<AggState>> groups_; // 使用哈希表实现快速分组，groups_[分组键] = 该组的所有聚合状态
    std::vector<std::string> group_order_; // 保存分组的首次出现顺序，后续输出为 ORDER 的分组时，结果按该顺序输出
    size_t iter_idx_ = 0;

    std::vector<TabCol> group_cols_;
    std::vector<size_t> group_col_idxs_;
    std::vector<AggregateInfo> agg_exprs_;
    std::vector<int> argument_col_idxs_;  // -1 for star; bound once, not per input row
    std::vector<HavingCondition> having_conds_;
    std::vector<ColMeta> cols_;
    size_t len_;
    bool plain_agg_;
    bool done_;
    std::unique_ptr<RmRecord> cur_rec_;
    std::vector<bool> cur_nulls_;   // 当前输出行各列是否 NULL（空集聚合）

    // 辅助函数
    std::string make_group_key(const char *data);
    float read_as_float(const char *data, const ColMeta &col);
    int read_as_int(const char *data, const ColMeta &col);
    bool is_null(size_t input_col_idx) const;
    int compare_value_by_val(const Value &a, const Value &b);
    bool evaluate_condition(const Value &lhs, const Value &rhs, CompOp op);
    bool satisfy_having(const std::vector<AggState> &states, const std::string &key);
    void build_cur();
    void advance_to_valid();
    const ColMeta *find_argument_col(const AggregateInfo &agg) const;
    void update_state(const AggregateInfo &agg, AggState &state,
                      const RmRecord &record, const ColMeta *argument_col);
    Value finalize(const AggregateInfo &agg, const AggState &state) const;
    void write_finalized_value(const Value &value, size_t output_col_idx);
    Value get_group_col_value(size_t group_idx, const std::string &key);

public:
    AggExecutor(std::unique_ptr<AbstractExecutor> prev,
                const std::vector<TabCol> &group_cols,
                const std::vector<AggregateInfo> &agg_exprs,
                const std::vector<HavingCondition> &having_conds,
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
                         const std::vector<HavingCondition> &having_conds,
                         std::vector<ColMeta> output_cols) {
    prev_ = std::move(prev);
    group_cols_ = group_cols;
    agg_exprs_ = agg_exprs;
    having_conds_ = having_conds;
    cols_ = std::move(output_cols);
    len_ = cols_.empty() ? 0 : cols_.back().offset + cols_.back().len;
    plain_agg_ = group_cols_.empty();
    done_ = false;

    const auto &input_cols = prev_->cols();
    group_col_idxs_.reserve(group_cols_.size());
    for (const auto &group : group_cols_) {
        auto col = std::find_if(input_cols.begin(), input_cols.end(), [&](const ColMeta &candidate) {
            return candidate.name == group.col_name &&
                   (group.tab_name.empty() || candidate.tab_name == group.tab_name);
        });
        if (col == input_cols.end()) throw ColumnNotFoundError(group.col_name);
        group_col_idxs_.push_back(static_cast<size_t>(col - input_cols.begin()));
    }
    argument_col_idxs_.reserve(agg_exprs_.size());
    for (const auto &agg : agg_exprs_) {
        if (agg.is_star) {
            argument_col_idxs_.push_back(-1);
            continue;
        }
        auto col = std::find_if(input_cols.begin(), input_cols.end(), [&](const ColMeta &candidate) {
            return candidate.name == agg.col.col_name &&
                   (agg.col.tab_name.empty() || candidate.tab_name == agg.col.tab_name);
        });
        if (col == input_cols.end()) throw ColumnNotFoundError(agg.col.col_name);
        argument_col_idxs_.push_back(static_cast<int>(col - input_cols.begin()));
    }

}

// 构造 GROUP BY Key
std::string AggExecutor::make_group_key(const char *data) {
    if (plain_agg_) return ""; // 没有 GROUP BY 时直接返回

    std::string key;
    std::string null_bitmap((group_cols_.size() + 7) / 8, 0); // 使用掩码判断值是否为NULL
    size_t bit_idx = 0;

    for (const size_t input_col_idx : group_col_idxs_) {
        const ColMeta &col = prev_->cols()[input_col_idx];
        if (is_null(input_col_idx)) {
            null_bitmap[bit_idx / 8] |= (1 << (bit_idx % 8));
            key.append(col.len, '\0');
        } else {
            key.append(data + col.offset, col.len);
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

bool AggExecutor::is_null(size_t input_col_idx) const {
    const auto *mask = prev_->null_mask();
    return mask != nullptr && input_col_idx < mask->size() && (*mask)[input_col_idx];
}

const ColMeta *AggExecutor::find_argument_col(const AggregateInfo &agg) const {
    const size_t agg_idx = static_cast<size_t>(&agg - agg_exprs_.data());
    if (agg_idx >= argument_col_idxs_.size() || argument_col_idxs_[agg_idx] < 0) return nullptr;
    return &prev_->cols().at(static_cast<size_t>(argument_col_idxs_[agg_idx]));
}

void AggExecutor::update_state(const AggregateInfo &agg, AggState &st,
                               const RmRecord &record, const ColMeta *argument_col) {
    const auto *spec = ast::aggregate_spec(agg.type);
    if (spec == nullptr) throw InternalError("Unknown aggregate function");

    // Common SQL input policy: argument aggregates ignore NULL, and DISTINCT
    // filters equal physical values before the transition.  New simple
    // aggregates inherit both without another per-function branch.
    if (!agg.is_star) {
        if (argument_col == nullptr) throw InternalError("Aggregate argument is not bound");
        const size_t input_col_idx = static_cast<size_t>(argument_col - prev_->cols().data());
        if (is_null(input_col_idx)) return;
        if (agg.distinct) {
            std::string distinct_key(record.data + argument_col->offset, argument_col->len);
            if (st.distinct_seen == nullptr) {
                st.distinct_seen = std::make_unique<std::unordered_set<std::string>>();
            }
            if (!st.distinct_seen->insert(std::move(distinct_key)).second) return;
        }
    }

    if (spec->update_rule == ast::AggregateUpdateRule::COUNT) {
        ++st.count;
        return;
    }

    if (spec->update_rule == ast::AggregateUpdateRule::SUMMARY) {
        if (argument_col == nullptr) throw InternalError("SUMMARY aggregate requires an argument");
        if (argument_col->type == TYPE_INT) {
            const int value = read_as_int(record.data, *argument_col);
            if (!st.has_value) {
                st.int_max = st.int_min = value;
                st.has_value = true;
            } else {
                st.int_max = std::max(st.int_max, value);
                st.int_min = std::min(st.int_min, value);
            }
            st.int_sum += value;
        } else if (argument_col->type == TYPE_FLOAT) {
            const float value = read_as_float(record.data, *argument_col);
            if (!st.has_value) {
                st.float_max = st.float_min = value;
                st.has_value = true;
            } else {
                st.float_max = std::max(st.float_max, value);
                st.float_min = std::min(st.float_min, value);
            }
            st.float_sum += value;
        } else {
            std::string value(record.data + argument_col->offset, argument_col->len);
            const size_t end = value.find('\0');
            if (end != std::string::npos) value.resize(end);
            if (!st.has_value) {
                st.str_max = st.str_min = value;
                st.has_value = true;
            } else {
                if (value > st.str_max) st.str_max = value;
                if (value < st.str_min) st.str_min = value;
            }
        }
        ++st.sum_cnt;
        return;
    }

    // A registry entry marked CUSTOM must add its transition in this single,
    // adjacent switch.  Failing loudly prevents a half-registered function
    // from returning plausible but wrong contest answers.
    switch (agg.type) {
        default: throw InternalError("Aggregate CUSTOM update is not implemented");
    }
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
        const double fa = (a.type == TYPE_INT) ? static_cast<double>(a.int_val)
                                               : static_cast<double>(a.float_val);
        const double fb = (b.type == TYPE_INT) ? static_cast<double>(b.int_val)
                                               : static_cast<double>(b.float_val);
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

Value AggExecutor::finalize(const AggregateInfo &agg, const AggState &st) const {
    auto zero = [&]() {
        switch (agg.output_type()) {
            case TYPE_INT: return agg_make_int(0);
            case TYPE_FLOAT: return agg_make_float(0.0f);
            case TYPE_STRING: return agg_make_str("");
        }
        return agg_make_int(0);
    };

    // Second and final extension point for a simple aggregate.  Functions
    // registered with SUMMARY can reuse the fields above and need one case.
    switch (agg.type) {
        case ast::AGG_COUNT: return agg_make_int(static_cast<int>(st.count));
        case ast::AGG_SUM:
            if (agg.arg_type == TYPE_INT) return agg_make_int(static_cast<int>(st.int_sum));
            else return agg_make_float(static_cast<float>(st.float_sum));
        case ast::AGG_MAX:
            if (!st.has_value) return zero();
            if (agg.arg_type == TYPE_INT) return agg_make_int(st.int_max);
            if (agg.arg_type == TYPE_FLOAT) return agg_make_float(st.float_max);
            return agg_make_str(st.str_max);
        case ast::AGG_MIN:
            if (!st.has_value) return zero();
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
    throw InternalError("Aggregate finalize is not implemented");
}

void AggExecutor::write_finalized_value(const Value &value, size_t output_col_idx) {
    const ColMeta &column = cols_.at(output_col_idx);
    char *slot = cur_rec_->data + column.offset;
    switch (column.type) {
        case TYPE_INT:
            *(int *)slot = value.int_val;
            break;
        case TYPE_FLOAT:
            *(float *)slot = value.float_val;
            break;
        case TYPE_STRING: {
            const size_t bytes = std::min(value.str_val.size(), static_cast<size_t>(column.len));
            memcpy(slot, value.str_val.data(), bytes);
            break;
        }
    }
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

// 检查条件是否满足
bool AggExecutor::satisfy_having(const std::vector<AggState> &states, const std::string &key) {
    if (having_conds_.empty()) return true;

    for (const auto &cond : having_conds_) {
        Value lhs;
        if (cond.source == HavingSource::GROUP_COLUMN) {
            if (cond.index >= group_cols_.size()) return false;
            const bool is_null =
                (static_cast<unsigned char>(key[cond.index / 8]) &
                 (1U << (cond.index % 8))) != 0;
            // Comparisons against NULL are UNKNOWN; HAVING retains TRUE only.
            if (is_null) return false;
            lhs = get_group_col_value(cond.index, key);
        } else {
            if (cond.index >= agg_exprs_.size() || cond.index >= states.size()) return false;
            lhs = finalize(agg_exprs_[cond.index], states[cond.index]);
        }
        if (!evaluate_condition(lhs, cond.rhs, cond.op)) return false;
    }
    return true;
}

void AggExecutor::advance_to_valid() {
    while (iter_idx_ < group_order_.size() &&
           !satisfy_having(groups_[group_order_[iter_idx_]], group_order_[iter_idx_])) {
        ++iter_idx_; // 跳过不满足条件的分组
    }
}

void AggExecutor::beginTuple() {
    groups_.clear();
    group_order_.clear();
    done_ = false;

    prev_->beginTuple(); // 下层执行器启动
    // MIN 索引早停：无分组、唯一聚合是 MIN(col)、无 HAVING，且子执行器保证输出按
    // col 升序（见 IndexScanExecutor::sorted_asc_on）——首个非 NULL 值即全局最小，
    // 不必耗尽子扫描。Delivery 的 min(no_o_id) 在热点长队列上从 O(队列) 降为 O(1)，
    // 这是 07-31 定量分析里 ~90% 线程时间的来源（Docs/Optimize/14）。
    static const bool min_es_off = std::getenv("RMDB_NO_MIN_EARLYSTOP") != nullptr;  // A/B 归因开关
    const bool min_early_stop_ = !min_es_off && plain_agg_ && agg_exprs_.size() == 1 &&
                                 agg_exprs_[0].type == ast::AGG_MIN && !agg_exprs_[0].is_star &&
                                 having_conds_.empty() && prev_->sorted_asc_on(agg_exprs_[0].col);

    for (; !prev_->is_end(); prev_->nextTuple()) {
        auto rec = prev_->Next(); // 逐行读取记录
        if (!rec) continue;

        std::string key = make_group_key(rec->data); // 将记录中的 GROUP BY key 提取出来

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
            update_state(agg, st, *rec, find_argument_col(agg));
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

    iter_idx_ = 0; // 定位到第一个分组
    advance_to_valid(); // 跳过不满足 HAVING 的分组
    build_cur(); // 把当前分组转换为一条输出记录
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

// 把当前分组的分组 key 和聚合状态，编码为一条标准 RmRecord。
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
                cur_nulls_[i] = true; // 当前为 NULL
            } else {
                memcpy(dst + cols_[i].offset, key.data() + key_off, col_len); // 不为 NULL，则将对应结果写回记录
            }
            key_off += col_len;
        }
    }

    // 2. 写聚合结果列
    for (size_t i = 0; i < agg_exprs_.size(); ++i) {
        const size_t col_idx = plain_agg_ ? i : (group_cols_.size() + i);
        write_finalized_value(finalize(agg_exprs_[i], states[i]), col_idx);
    }
}
