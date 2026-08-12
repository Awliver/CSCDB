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
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "defs.h"
#include "record/rm_defs.h"

class Query;

// Records and index keys are packed byte arrays; neither their base address
// nor a column offset is guaranteed to satisfy int/float alignment.  memcpy
// is optimized to a normal load/store on platforms that permit it and remains
// defined on strict-alignment targets and under UBSan.
template <typename T>
inline T load_unaligned(const char *data) {
    static_assert(std::is_trivially_copyable<T>::value,
                  "load_unaligned requires a trivially copyable type");
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

template <typename T>
inline void store_unaligned(char *data, const T &value) {
    static_assert(std::is_trivially_copyable<T>::value,
                  "store_unaligned requires a trivially copyable type");
    std::memcpy(data, &value, sizeof(T));
}

struct TabCol {
    std::string tab_name;
    std::string col_name;
    std::string alias;
    // Zero-based output position for ORDER BY ordinals.  -1 means resolve by
    // relation/name as before.
    int output_index = -1;

    friend bool operator<(const TabCol &x, const TabCol &y) {
        return std::make_pair(x.tab_name, x.col_name) < std::make_pair(y.tab_name, y.col_name);
    }
};

/*
 * A synthesized JOIN output column whose value is COALESCE(left, right).
 *
 * NATURAL/USING analysis keeps the two physical input columns addressable for
 * ON evaluation, then asks the join executor to append one logical output
 * column.  Keeping this description independent from Plan/Executor types lets
 * the same contract flow through the whole query pipeline.
 */
struct CoalescedJoinColumn {
    TabCol left;
    TabCol right;
    TabCol output;
    ColType type;
    int len;
};

struct Value {
    ColType type = TYPE_INT;  // type of value
    union {
        int int_val = 0;  // int value
        float float_val;  // float value
    };
    std::string str_val;  // string value

    std::shared_ptr<RmRecord> raw;  // raw record buffer

    void set_int(int int_val_) {
        type = TYPE_INT;
        int_val = int_val_;
    }

    void set_float(float float_val_) {
        type = TYPE_FLOAT;
        float_val = float_val_;
    }

    void set_str(std::string str_val_) {
        type = TYPE_STRING;
        str_val = std::move(str_val_);
    }

    void init_raw(int len) {
        assert(raw == nullptr);
        raw = std::make_shared<RmRecord>(len);
        if (type == TYPE_INT) {
            assert(len == sizeof(int));
            store_unaligned(raw->data, int_val);
        } else if (type == TYPE_FLOAT) {
            assert(len == sizeof(float));
            store_unaligned(raw->data, float_val);
        } else if (type == TYPE_STRING) {
            if (len < (int)str_val.size()) {
                throw StringOverflowError();
            }
            memset(raw->data, 0, len);
            memcpy(raw->data, str_val.c_str(), str_val.size());
        }
    }
};

enum CompOp {
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE, OP_LIKE,
    OP_IS_NULL, OP_IS_NOT_NULL
};

inline bool is_null_test_op(CompOp op) {
    return op == OP_IS_NULL || op == OP_IS_NOT_NULL;
}

enum class ConditionKind {
    COMPARISON,
    EXISTS_SUBQUERY,
    IN_SUBQUERY,
};

struct Condition {
    ConditionKind kind = ConditionKind::COMPARISON;
    TabCol lhs_col;   // left-hand side column
    CompOp op = OP_EQ;        // comparison operator
    bool is_rhs_val = true;   // true if right-hand side is a value (not a column)
    TabCol rhs_col;   // right-hand side column
    Value rhs_val;    // right-hand side value
    bool rhs_is_float_lit = false;  // 原始字面量是否为浮点(类型提升后丢失，EXPLAIN 渲染用)
    // EXISTS/IN subqueries retain the Query interface on semantic leaves.
    // The planner creates one subplan per leaf, which the executor restarts
    // with parameters from each outer row.
    std::shared_ptr<Query> subquery;
};

inline bool is_subquery_condition(const Condition &condition) {
    return condition.kind == ConditionKind::EXISTS_SUBQUERY ||
           condition.kind == ConditionKind::IN_SUBQUERY;
}

// SQL LIKE matcher for zero-padded CHAR storage. '%' matches any byte sequence,
// '_' matches one byte, and '\\' quotes the following wildcard character.
inline bool sql_like_match(const char *value, int value_len,
                           const char *pattern, int pattern_len) {
    int n = 0;
    while (n < value_len && value[n] != '\0') ++n;
    int m = 0;
    while (m < pattern_len && pattern[m] != '\0') ++m;

    std::vector<unsigned char> previous(static_cast<size_t>(n) + 1, 0);
    std::vector<unsigned char> current(static_cast<size_t>(n) + 1, 0);
    previous[0] = 1;
    for (int j = 0; j < m; ++j) {
        std::fill(current.begin(), current.end(), 0);
        const char token = pattern[j];
        if (token == '%') {
            current[0] = previous[0];
            for (int i = 1; i <= n; ++i) {
                current[static_cast<size_t>(i)] =
                    previous[static_cast<size_t>(i)] ||
                    current[static_cast<size_t>(i - 1)];
            }
        } else {
            char literal = token;
            bool wildcard = token == '_';
            if (token == '\\' && j + 1 < m) {
                literal = pattern[++j];
                wildcard = false;
            }
            for (int i = 1; i <= n; ++i) {
                current[static_cast<size_t>(i)] =
                    previous[static_cast<size_t>(i - 1)] &&
                    (wildcard || value[i - 1] == literal);
            }
        }
        previous.swap(current);
    }
    return previous[static_cast<size_t>(n)] != 0;
}

/*
 * SQL 布尔表达式的语义层表示。
 *
 * Condition 仍然是一个“比较原子”，BoolExpr 只负责保留 AND / OR /
 * NOT 的拓扑。这样索引、连接和 SSI 仍可复用已有的比较叶子，
 * 同时不再把 vector<Condition> 隐式当成 AND。括号不需要运行时
 * 节点：语法树的形状已经完整保留了分组。
 *
 * 空指针在各层统一表示“没有谓词 / 恒真”。CONSTANT 节点主要用于
 * 分析和参数化执行中的安全改写。
 */
enum class TruthValue {
    FALSE_VALUE,
    TRUE_VALUE,
    UNKNOWN_VALUE,
};

inline TruthValue evaluate_null_test(bool is_null, CompOp op) {
    const bool matches = op == OP_IS_NULL ? is_null : !is_null;
    return matches ? TruthValue::TRUE_VALUE : TruthValue::FALSE_VALUE;
}

inline TruthValue truth_not(TruthValue value) {
    if (value == TruthValue::TRUE_VALUE) return TruthValue::FALSE_VALUE;
    if (value == TruthValue::FALSE_VALUE) return TruthValue::TRUE_VALUE;
    return TruthValue::UNKNOWN_VALUE;
}

inline TruthValue truth_and(TruthValue left, TruthValue right) {
    if (left == TruthValue::FALSE_VALUE || right == TruthValue::FALSE_VALUE) {
        return TruthValue::FALSE_VALUE;
    }
    if (left == TruthValue::TRUE_VALUE && right == TruthValue::TRUE_VALUE) {
        return TruthValue::TRUE_VALUE;
    }
    return TruthValue::UNKNOWN_VALUE;
}

inline TruthValue truth_or(TruthValue left, TruthValue right) {
    if (left == TruthValue::TRUE_VALUE || right == TruthValue::TRUE_VALUE) {
        return TruthValue::TRUE_VALUE;
    }
    if (left == TruthValue::FALSE_VALUE && right == TruthValue::FALSE_VALUE) {
        return TruthValue::FALSE_VALUE;
    }
    return TruthValue::UNKNOWN_VALUE;
}

enum class BoolExprType {
    CONSTANT,
    ATOM,
    NOT,
    AND,
    OR,
};

template <typename Atom>
struct BoolExpr {
    BoolExprType type = BoolExprType::CONSTANT;
    TruthValue constant = TruthValue::TRUE_VALUE;
    Atom atom{};
    std::shared_ptr<BoolExpr<Atom>> left;
    std::shared_ptr<BoolExpr<Atom>> right;
};

template <typename Atom>
using BoolExprPtr = std::shared_ptr<BoolExpr<Atom>>;

template <typename Atom>
inline BoolExprPtr<Atom> make_bool_constant(TruthValue value) {
    auto result = std::make_shared<BoolExpr<Atom>>();
    result->type = BoolExprType::CONSTANT;
    result->constant = value;
    return result;
}

template <typename Atom>
inline BoolExprPtr<Atom> make_bool_atom(Atom atom) {
    auto result = std::make_shared<BoolExpr<Atom>>();
    result->type = BoolExprType::ATOM;
    result->atom = std::move(atom);
    return result;
}

template <typename Atom>
inline BoolExprPtr<Atom> make_bool_not(BoolExprPtr<Atom> child) {
    auto result = std::make_shared<BoolExpr<Atom>>();
    result->type = BoolExprType::NOT;
    result->left = std::move(child);
    return result;
}

template <typename Atom>
inline BoolExprPtr<Atom> make_bool_binary(BoolExprType type, BoolExprPtr<Atom> left,
                                         BoolExprPtr<Atom> right) {
    assert(type == BoolExprType::AND || type == BoolExprType::OR);
    auto result = std::make_shared<BoolExpr<Atom>>();
    result->type = type;
    result->left = std::move(left);
    result->right = std::move(right);
    return result;
}

// 把若干已完整分析的子表达式合并为 AND；空集即恒真(nullptr)。
template <typename Atom>
inline BoolExprPtr<Atom> combine_with_and(const std::vector<BoolExprPtr<Atom>> &parts) {
    BoolExprPtr<Atom> result;
    for (const auto &part : parts) {
        if (part == nullptr) continue;
        result = result == nullptr
                     ? part
                     : make_bool_binary<Atom>(BoolExprType::AND, result, part);
    }
    return result;
}

// 仅拆最外层 AND。OR/NOT 子树作为不可分割的整体返回。
template <typename Atom>
inline void split_top_level_and(const BoolExprPtr<Atom> &expr,
                                std::vector<BoolExprPtr<Atom>> &out) {
    if (expr == nullptr) return;
    if (expr->type == BoolExprType::AND) {
        split_top_level_and(expr->left, out);
        split_top_level_and(expr->right, out);
        return;
    }
    out.push_back(expr);
}

template <typename Atom, typename Visitor>
inline void visit_bool_atoms(const BoolExprPtr<Atom> &expr, Visitor &&visitor) {
    if (expr == nullptr) return;
    switch (expr->type) {
        case BoolExprType::ATOM:
            visitor(expr->atom);
            return;
        case BoolExprType::NOT:
            visit_bool_atoms(expr->left, std::forward<Visitor>(visitor));
            return;
        case BoolExprType::AND:
        case BoolExprType::OR:
            visit_bool_atoms(expr->left, std::forward<Visitor>(visitor));
            visit_bool_atoms(expr->right, std::forward<Visitor>(visitor));
            return;
        case BoolExprType::CONSTANT:
            return;
    }
}

template <typename Atom, typename Mapper>
inline BoolExprPtr<Atom> map_bool_atoms(const BoolExprPtr<Atom> &expr, Mapper &&mapper) {
    if (expr == nullptr) return nullptr;
    switch (expr->type) {
        case BoolExprType::CONSTANT:
            return make_bool_constant<Atom>(expr->constant);
        case BoolExprType::ATOM:
            return make_bool_atom<Atom>(mapper(expr->atom));
        case BoolExprType::NOT:
            return make_bool_not<Atom>(map_bool_atoms(expr->left, std::forward<Mapper>(mapper)));
        case BoolExprType::AND:
        case BoolExprType::OR:
            return make_bool_binary<Atom>(
                expr->type,
                map_bool_atoms(expr->left, std::forward<Mapper>(mapper)),
                map_bool_atoms(expr->right, std::forward<Mapper>(mapper)));
    }
    return nullptr;
}

// 仅提取从根开始经过全 AND 路径可达的正向原子。它们是整个
// 表达式为 TRUE 时的必要条件，可安全用于索引访问边界或 INLJ key。
template <typename Atom>
inline void extract_conjunctive_atoms(const BoolExprPtr<Atom> &expr, std::vector<Atom> &out) {
    if (expr == nullptr) return;
    if (expr->type == BoolExprType::ATOM) {
        out.push_back(expr->atom);
        return;
    }
    if (expr->type == BoolExprType::AND) {
        extract_conjunctive_atoms(expr->left, out);
        extract_conjunctive_atoms(expr->right, out);
    }
}

template <typename Atom, typename LeafEvaluator>
inline TruthValue evaluate_bool_expr(const BoolExprPtr<Atom> &expr,
                                     LeafEvaluator &&evaluate_leaf) {
    if (expr == nullptr) return TruthValue::TRUE_VALUE;
    switch (expr->type) {
        case BoolExprType::CONSTANT:
            return expr->constant;
        case BoolExprType::ATOM:
            return evaluate_leaf(expr->atom);
        case BoolExprType::NOT:
            return truth_not(evaluate_bool_expr(expr->left,
                                                std::forward<LeafEvaluator>(evaluate_leaf)));
        case BoolExprType::AND: {
            const TruthValue left = evaluate_bool_expr(
                expr->left, std::forward<LeafEvaluator>(evaluate_leaf));
            // SQL 三值逻辑也可安全短路：FALSE AND x 恒为 FALSE。
            if (left == TruthValue::FALSE_VALUE) return TruthValue::FALSE_VALUE;
            return truth_and(left, evaluate_bool_expr(
                                       expr->right,
                                       std::forward<LeafEvaluator>(evaluate_leaf)));
        }
        case BoolExprType::OR: {
            const TruthValue left = evaluate_bool_expr(
                expr->left, std::forward<LeafEvaluator>(evaluate_leaf));
            // TRUE OR x 恒为 TRUE。
            if (left == TruthValue::TRUE_VALUE) return TruthValue::TRUE_VALUE;
            return truth_or(left, evaluate_bool_expr(
                                      expr->right,
                                      std::forward<LeafEvaluator>(evaluate_leaf)));
        }
    }
    return TruthValue::FALSE_VALUE;
}

using ConditionExpr = BoolExpr<Condition>;
using ConditionExprPtr = BoolExprPtr<Condition>;

struct SetClause {
    TabCol lhs;
    Value rhs;
    bool is_arith = false;   // 题9：lhs = rhs_col ± rhs 的算术增量形式（如 v=v+1）
    std::string rhs_col;     // 算术时右侧引用的列名
    bool arith_neg = false;  // 带空格减号(v = v - 1)：rhs 为正、需取负
    bool self_noop = false;  // 决赛：SET col = col 自赋值——字节恒等，rhs 不参与；
                             // 写路径（锁/冲突检测/WAL/回滚）仍完整执行
    std::vector<float> chain_f;  // 决赛：float 列链式算术 v = v ± v1 ± v2 ...（项带符号）。
                                 // IEEE 加法非结合，须按 f32 逐步左结合累加，不能折叠；
                                 // int 列的链在 analyze 已精确折叠进 rhs，不用此字段
};
