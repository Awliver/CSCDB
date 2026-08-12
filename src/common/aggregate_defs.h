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
 * COUNT owns the counting transition. SUMMARY updates the reusable
 * count/sum/min/max state, which is enough for the common contest aggregates.
 * NULL skipping and DISTINCT filtering are applied before either transition.
 * A genuinely different state transition may use CUSTOM and add one case in
 * AggExecutor::update_state().
 */
enum class AggregateUpdateRule : uint8_t {
    COUNT,
    SUMMARY,
    CUSTOM,
};

/*
 * Aggregate extension registry -- this is the first of two intended edit
 * points for a new simple aggregate.  Add exactly one row here; parsing,
 * spelling, argument validation and result-schema inference all consume it.
 * The second edit point is the finalize switch in executor_aggregation.h (and,
 * only for a CUSTOM transition, its update switch).
 */
#define RMDB_AGGREGATE_FUNCTIONS(X)                                                        \
    X(COUNT, "count", AGG_INPUT_ANY,     true,  true,  FIXED_INT,        COUNT)           \
    X(MAX,   "max",   AGG_INPUT_ANY,     false, false, SAME_AS_ARGUMENT, SUMMARY)         \
    X(MIN,   "min",   AGG_INPUT_ANY,     false, false, SAME_AS_ARGUMENT, SUMMARY)         \
    X(SUM,   "sum",   AGG_INPUT_NUMERIC, false, false, SAME_AS_ARGUMENT, SUMMARY)         \
    X(AVG,   "avg",   AGG_INPUT_NUMERIC, false, false, FIXED_FLOAT,      SUMMARY)

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

}  // namespace ast
