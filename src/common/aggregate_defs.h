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
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include "defs.h"

namespace ast {

enum AggregateInputMask : uint8_t {
    AGG_INPUT_INT = 1U << 0,
    AGG_INPUT_FLOAT = 1U << 1,
    AGG_INPUT_STRING = 1U << 2,
    AGG_INPUT_NUMERIC = AGG_INPUT_INT | AGG_INPUT_FLOAT,
    AGG_INPUT_ANY = AGG_INPUT_NUMERIC | AGG_INPUT_STRING,
};

enum class AggregateResultRule : uint8_t {
    SAME_AS_ARGUMENT,
    FIXED_INT,
    FIXED_FLOAT,
};

/*
 * COUNT 使用专用计数状态转移；SUMMARY 更新可复用的 count/sum/min/max 状态，
 * 足以支持比赛中常见的简单聚合。NULL 跳过与 DISTINCT 过滤会在状态转移前统一
 * 完成。确实需要不同状态转移的聚合可选择 CUSTOM，并在
 * AggExecutor::update_state() 中增加一个分支。
 */
enum class AggregateUpdateRule : uint8_t {
    COUNT,
    SUMMARY,
    CUSTOM,
};

/*
 * 聚合扩展注册表——这是新增简单聚合时两个预定修改点中的第一个，只需在这里增加一行，解析、函数名、参数校验和结果结构推导都会读取该配置
 * 第二个修改点是 executor_aggregation.h 中 finalize() 的 switch；
 * 只有 无法使用原有聚合表示的新函数（CUSTOM 状态表示）需要修改相邻的 update switch。
* X(
        枚举名,
        SQL函数名,
        接受的参数类型,
        是否允许*,
        是否允许DISTINCT,
        输出类型规则,
        状态更新规则
    )
 */
#define RMDB_AGGREGATE_FUNCTIONS(X)                                                 \
    X(COUNT, "count", AGG_INPUT_ANY,     true,  true,  FIXED_INT,        COUNT)     \
    X(MAX,   "max",   AGG_INPUT_ANY,     false, false, SAME_AS_ARGUMENT, SUMMARY)   \
    X(MIN,   "min",   AGG_INPUT_ANY,     false, false, SAME_AS_ARGUMENT, SUMMARY)   \
    X(SUM,   "sum",   AGG_INPUT_NUMERIC, false, true,  SAME_AS_ARGUMENT, SUMMARY)   \
    X(AVG,   "avg",   AGG_INPUT_NUMERIC, false, true,  FIXED_FLOAT,      SUMMARY)   \
    X(RANGE, "range", AGG_INPUT_NUMERIC, false, false, SAME_AS_ARGUMENT, SUMMARY)   \
    X(PRODUCT, "product", AGG_INPUT_NUMERIC, false, false, SAME_AS_ARGUMENT, CUSTOM) \
    X(VARIANCE, "variance", AGG_INPUT_NUMERIC, false, false, FIXED_FLOAT, CUSTOM)    \
    X(STDDEV, "stddev", AGG_INPUT_NUMERIC, false, false, FIXED_FLOAT, CUSTOM)

enum AggType {
#define RMDB_DECLARE_AGG_ENUM(symbol, name, inputs, star, distinct, result, update) AGG_##symbol,
    RMDB_AGGREGATE_FUNCTIONS(RMDB_DECLARE_AGG_ENUM)
#undef RMDB_DECLARE_AGG_ENUM
};

struct AggregateSpec {
    AggType type;
    std::string_view name;
    uint8_t accepted_inputs;
    bool accepts_star;
    bool accepts_distinct;
    AggregateResultRule result_rule;
    AggregateUpdateRule update_rule;
};

inline constexpr std::array<AggregateSpec,
#define RMDB_COUNT_AGG(symbol, name, inputs, star, distinct, result, update) +1
    0 RMDB_AGGREGATE_FUNCTIONS(RMDB_COUNT_AGG)
#undef RMDB_COUNT_AGG
> kAggregateSpecs{{
#define RMDB_DECLARE_AGG_SPEC(symbol, name, inputs, star, distinct, result, update)         \
    {AGG_##symbol, name, inputs, star, distinct, AggregateResultRule::result,               \
     AggregateUpdateRule::update},
    RMDB_AGGREGATE_FUNCTIONS(RMDB_DECLARE_AGG_SPEC)
#undef RMDB_DECLARE_AGG_SPEC
}};

#undef RMDB_AGGREGATE_FUNCTIONS

constexpr const AggregateSpec *aggregate_spec(AggType type) {
    for (const auto &spec : kAggregateSpecs) {
        if (spec.type == type) return &spec;
    }
    return nullptr;
}

inline std::string normalize_aggregate_name(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return name;
}

inline const AggregateSpec *find_aggregate(std::string name) {
    name = normalize_aggregate_name(std::move(name));
    for (const auto &spec : kAggregateSpecs) {
        if (spec.name == name) return &spec;
    }
    return nullptr;
}

inline bool aggregate_type_from_name(const std::string &name, AggType &type) {
    const auto *spec = find_aggregate(name);
    if (spec == nullptr) return false;
    type = spec->type;
    return true;
}

inline std::string aggregate_name(AggType type) {
    const auto *spec = aggregate_spec(type);
    return spec == nullptr ? std::string() : std::string(spec->name);
}

constexpr uint8_t aggregate_input_bit(ColType type) {
    switch (type) {
        case TYPE_INT: return AGG_INPUT_INT;
        case TYPE_FLOAT: return AGG_INPUT_FLOAT;
        case TYPE_STRING: return AGG_INPUT_STRING;
    }
    return 0;
}

constexpr bool aggregate_accepts_argument(AggType type, ColType argument_type) {
    const auto *spec = aggregate_spec(type);
    return spec != nullptr && (spec->accepted_inputs & aggregate_input_bit(argument_type)) != 0;
}

constexpr ColType aggregate_result_type(AggType type, ColType argument_type) {
    const auto *spec = aggregate_spec(type);
    if (spec == nullptr) return argument_type;
    switch (spec->result_rule) {
        case AggregateResultRule::FIXED_INT: return TYPE_INT;
        case AggregateResultRule::FIXED_FLOAT: return TYPE_FLOAT;
        case AggregateResultRule::SAME_AS_ARGUMENT: return argument_type;
    }
    return argument_type;
}

constexpr int aggregate_result_length(AggType type, ColType argument_type, int argument_length) {
    switch (aggregate_result_type(type, argument_type)) {
        case TYPE_INT: return sizeof(int);
        case TYPE_FLOAT: return sizeof(float);
        case TYPE_STRING: return argument_length;
    }
    return argument_length;
}

inline std::string format_aggregate_call(AggType type, const std::string &argument,
                                         bool is_star, bool distinct = false) {
    std::string result = aggregate_name(type);
    result.push_back('(');
    if (distinct) result += "distinct ";
    result += is_star ? "*" : argument;
    result.push_back(')');
    return result;
}

static_assert(kAggregateSpecs.size() > 0, "At least one aggregate must be registered");
static_assert([] {
    for (size_t i = 0; i < kAggregateSpecs.size(); ++i) {
        if (kAggregateSpecs[i].name.empty()) return false;
        for (size_t j = i + 1; j < kAggregateSpecs.size(); ++j) {
            if (kAggregateSpecs[i].type == kAggregateSpecs[j].type ||
                kAggregateSpecs[i].name == kAggregateSpecs[j].name) return false;
        }
    }
    return true;
}(), "Aggregate registry names and enum values must be unique");

}  // ast 命名空间结束
