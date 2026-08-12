/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "analyze.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>

namespace {

/* int 列 vs float 字面量比较的语义保持改写结果 */
enum class IntFloatRewrite { CONVERTED, ALWAYS_TRUE, ALWAYS_FALSE };

// SQL 的 WHERE/ON 在 NULL 上必须得到 UNKNOWN，而不是 TRUE。所谓“恒真”只对
// 非 NULL 列值成立，因此不能简单删除谓词；改写成 col = col 可同时保留这两种
// 语义，并且不依赖具体列类型。
void rewrite_true_for_non_null(Condition &cond) {
    cond.op = OP_EQ;
    cond.is_rhs_val = false;
    cond.rhs_col = cond.lhs_col;
}

/* 把 "int_col <op> float_lit" 改写为纯 int 比较，保持数值比较语义：
 * - 字面量为整数值且在 int32 范围内：直接转 int，op 不变；
 * - 非整数值：EQ 恒假、NE 恒真；LT/LE → <= floor，GT/GE → >= floor+1；
 * - 超出 int32 范围（含 ±inf）：按方向折叠为恒真/恒假；NaN 一律恒假。
 * float→double 提升精确，全部判定无舍入误差。 */
IntFloatRewrite rewrite_int_col_float_val(Condition &cond) {
    const double d = static_cast<double>(cond.rhs_val.float_val);
    // IEEE：NaN 与任何值比较除 != 恒真外均恒假
    if (std::isnan(d)) return cond.op == OP_NE ? IntFloatRewrite::ALWAYS_TRUE
                                               : IntFloatRewrite::ALWAYS_FALSE;
    const double lo = static_cast<double>(INT32_MIN);
    const double hi = static_cast<double>(INT32_MAX);
    if (d > hi) {   // 所有 int32 都 < d（含 +inf）
        if (cond.op == OP_LT || cond.op == OP_LE || cond.op == OP_NE) return IntFloatRewrite::ALWAYS_TRUE;
        return IntFloatRewrite::ALWAYS_FALSE;   // EQ/GT/GE
    }
    if (d < lo) {   // 所有 int32 都 > d（含 -inf）
        if (cond.op == OP_GT || cond.op == OP_GE || cond.op == OP_NE) return IntFloatRewrite::ALWAYS_TRUE;
        return IntFloatRewrite::ALWAYS_FALSE;   // EQ/LT/LE
    }
    if (d == std::floor(d)) {
        cond.rhs_val.set_int(static_cast<int>(d));
        return IntFloatRewrite::CONVERTED;
    }
    // 非整数值：介于 floor(d) 与 floor(d)+1 之间，且两端都在 int32 范围内
    const int fl = static_cast<int>(std::floor(d));
    switch (cond.op) {
        case OP_EQ: return IntFloatRewrite::ALWAYS_FALSE;
        case OP_NE: return IntFloatRewrite::ALWAYS_TRUE;
        case OP_LT:
        case OP_LE:
            cond.op = OP_LE;
            cond.rhs_val.set_int(fl);
            return IntFloatRewrite::CONVERTED;
        case OP_GT:
        case OP_GE:
            cond.op = OP_GE;
            cond.rhs_val.set_int(fl + 1);
            return IntFloatRewrite::CONVERTED;
        case OP_LIKE:
        case OP_IS_NULL:
        case OP_IS_NOT_NULL:
            return IntFloatRewrite::ALWAYS_FALSE;
    }
    return IntFloatRewrite::ALWAYS_FALSE;
}

/* float 字面量赋给 int 列（INSERT 值 / UPDATE SET）：整数值精确转换，非整数值
 * 四舍五入（远离零，与主流实现一致）；NaN/超出 int32 范围仍走类型错误。 */
bool coerce_float_val_to_int(Value &v) {
    const double d = static_cast<double>(v.float_val);
    if (std::isnan(d)) return false;
    const double r = std::round(d);
    if (r < static_cast<double>(INT32_MIN) || r > static_cast<double>(INT32_MAX)) return false;
    v.set_int(static_cast<int>(r));
    return true;
}

// Parentheses are encoded by tree shape, so only an actual AND at the current
// root is safe to split for LATERAL predicate pushdown. OR and NOT subtrees
// deliberately remain indivisible.
void split_ast_top_level_and(const std::shared_ptr<ast::BoolExpr> &expr,
                             std::vector<std::shared_ptr<ast::BoolExpr>> &out) {
    if (expr == nullptr) return;
    auto logical = std::dynamic_pointer_cast<ast::LogicalExpr>(expr);
    if (logical != nullptr && logical->op == ast::LogicalOp::AND) {
        split_ast_top_level_and(logical->left, out);
        split_ast_top_level_and(logical->right, out);
        return;
    }
    out.push_back(expr);
}

std::shared_ptr<ast::BoolExpr> combine_ast_with_and(
    const std::vector<std::shared_ptr<ast::BoolExpr>> &parts) {
    std::shared_ptr<ast::BoolExpr> result;
    for (const auto &part : parts) {
        if (part == nullptr) continue;
        result = result == nullptr
                     ? part
                     : std::make_shared<ast::LogicalExpr>(ast::LogicalOp::AND,
                                                          result, part);
    }
    return result;
}

}  // namespace

/**
 * @description: 分析器，进行语义分析和查询重写，需要检查不符合语义规定的部分
 * @param {shared_ptr<ast::TreeNode>} parse parser生成的结果集
 * @return {shared_ptr<Query>} Query
 */
std::shared_ptr<Query> Analyze::do_analyze(std::shared_ptr<ast::TreeNode> parse)
{
    std::shared_ptr<Query> query = std::make_shared<Query>();
    if (auto e = std::dynamic_pointer_cast<ast::ExplainStmt>(parse))
    {
        query->explain_analyze = e->analyze;
        query->explain_query = do_analyze(e->query);
    }
    else if (auto group = std::dynamic_pointer_cast<ast::QueryGroup>(parse))
    {
        query = analyze_query_group(group, nullptr);
    }
    else if (auto u = std::dynamic_pointer_cast<ast::UnionStmt>(parse))
    {
        query = analyze_union_expr(u, nullptr);
    }
    else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse))
    {
        auto from_result = analyze_from(x->from);
        query->from = from_result.node;
        query->select_all = x->cols.empty() && x->aggs.empty();
        query->distinct = x->distinct;

        // Analyzer 之后列限定符始终是 binding_name。物理表名只保存在
        // AnalyzedFrom 的叶节点中，由 ScanPlan 在访问存储时使用。
        const std::vector<ColMeta> &all_cols = from_result.scope.cols;

        // 普通列
        for (auto &sv_sel_col : x->cols) {
            TabCol sel_col = resolve_column(
                from_result.scope, 
                {.tab_name = sv_sel_col->tab_name, .col_name = sv_sel_col->col_name});
            sel_col.alias = sv_sel_col->alias;        // 决赛：col AS alias（输出列名用别名）
            query->cols.push_back(sel_col);
        }
        if (query->cols.empty() && x->aggs.empty()) {
            // SELECT * 使用 joined table 的公开 row type。NATURAL JOIN 的公共列
            // 已在这里合并，SEMI/ANTI 的非保留侧也不会泄漏到结果中。
            for (auto &col : from_result.scope.output_cols) {
                query->cols.push_back({col.tab_name, col.name});
            }
        } else if (!query->cols.empty()) {
            for (auto &sel_col : query->cols) {
                sel_col = check_column(all_cols, sel_col);
            }
        }

        // 遍历 SELECT 中的每一个聚合函数，并将语法层的 AggExpr 转换成语义层的 AggregateInfo
        for (auto &sv_agg : x->aggs) {
            AggregateInfo agg = analyze_aggregate(
                sv_agg->agg_type, sv_agg->arguments, sv_agg->is_star,
                sv_agg->distinct, sv_agg->alias, from_result.scope);
            query->aggs.push_back(agg);
            query->sel_captions.push_back(agg.alias.empty() ? agg.to_string() : agg.alias);
        }

        // 构造 GROUP BY 列
        for (auto &sv_gb : x->group_by_cols) {
            TabCol gb_col = resolve_column(
                from_result.scope,
                {.tab_name = sv_gb->tab_name, .col_name = sv_gb->col_name});
            query->group_by_cols.push_back(gb_col); // 支持多列分组
        }
        check_group_by_validity(query->cols, query->aggs, query->group_by_cols);
        query->having_expr = analyze_having_clause(
            x->having_expr, query->group_by_cols, query->aggs, from_result.scope);

        if (!x->orders.empty()) {
            for (auto &sv_order : x->orders) {
                TabCol order_col;
                if (sv_order->is_ordinal) {
                    if (sv_order->ordinal <= 0) {
                        throw InternalError("ORDER BY position is out of range");
                    }
                    size_t ordinal = static_cast<size_t>(sv_order->ordinal);
                    if (ordinal <= query->cols.size()) {
                        order_col = query->cols[ordinal - 1];
                    } else {
                        size_t position = query->cols.size();
                        bool found = false;
                        for (const auto &agg : query->aggs) {
                            if (!agg.in_output) continue;
                            if (++position != ordinal) continue;
                            order_col = {agg.is_star ? "" : agg.col.tab_name,
                                         agg.alias.empty() ? agg.to_string() : agg.alias};
                            found = true;
                            break;
                        }
                        if (!found) throw InternalError("ORDER BY position is out of range");
                    }
                } else {
                    order_col = {.tab_name = sv_order->cols->tab_name,
                                 .col_name = sv_order->cols->col_name};
                    if (!order_col.tab_name.empty()) {
                        order_col = resolve_column(from_result.scope, std::move(order_col));
                    }
                    order_col = resolve_order_column(order_col, query->cols, query->group_by_cols,
                                                     query->aggs, from_result.scope.output_cols);
                }
                query->orders.emplace_back(order_col, sv_order->orderby_dir);
            }
        } else if (x->order) {
            throw InternalError("inconsistent ORDER BY representation");
        }

        query->has_limit = x->has_limit || x->limit >= 0;
        query->limit_count = x->has_limit ? x->limit_count : (x->limit >= 0 ? x->limit : 0);
        query->limit = query->has_limit ? query->limit_count : x->limit;
        if (query->has_limit && query->limit_count < 0) {
            throw InternalError("LIMIT must not be negative");
        }
        query->has_offset = x->has_offset;
        query->offset_count = x->offset_count;
        if (query->has_offset && query->offset_count < 0) {
            throw InternalError("OFFSET must not be negative");
        }

        for (auto &sel_col : query->cols) {
            query->sel_captions.push_back(sel_col.alias.empty() ? sel_col.col_name : sel_col.alias);
        }

        query->where_expr = analyze_conditions(x->where_expr, from_result.scope, true);
        check_where_no_aggregate(x->where_expr);
} else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(parse)) {
    // 处理 SET 子句
    TabMeta& tab_meta = sm_manager_->db_.get_table(x->tab_name);
    for (auto& sv_set : x->set_clauses) {
        SetClause set;
        set.lhs.tab_name = x->tab_name;
        set.lhs.col_name = sv_set->col_name;
        set.rhs = convert_sv_value(sv_set->val);
        set.is_arith = sv_set->is_arith;
        set.rhs_col = sv_set->rhs_col;
        set.arith_neg = sv_set->arith_neg;
        set.self_noop = sv_set->self_copy;

        // 查找该列元数据，若不存在抛 ColumnNotFoundError
        auto col_it = tab_meta.get_col(sv_set->col_name);

        if (!sv_set->chain.empty()) {
            // 决赛链式算术 col = col ± v1 ± v2 ...（项已带符号）：
            // int 列——整数加法结合律成立，精确折叠为单增量，沿用 delta 热路径；
            // float 列——IEEE 非结合，保留逐项链（chain_f），执行器按 f32 左结合累加。
            if (col_it->type == TYPE_INT) {
                long long acc = 0;
                bool all_int = true;
                for (auto &term : sv_set->chain) {
                    Value tv = convert_sv_value(term);
                    if (tv.type == TYPE_INT) acc += tv.int_val;
                    else { all_int = false; break; }
                }
                if (!all_int || acc < INT32_MIN || acc > INT32_MAX) {
                    throw IncompatibleTypeError(coltype2str(col_it->type), coltype2str(TYPE_FLOAT));
                }
                set.rhs.set_int(static_cast<int>(acc));
                set.arith_neg = false;
                set.rhs.init_raw(col_it->len);
                query->set_clauses.push_back(set);
                continue;
            }
            if (col_it->type == TYPE_FLOAT) {
                for (auto &term : sv_set->chain) {
                    Value tv = convert_sv_value(term);
                    if (tv.type == TYPE_INT) set.chain_f.push_back(static_cast<float>(tv.int_val));
                    else if (tv.type == TYPE_FLOAT) set.chain_f.push_back(tv.float_val);
                    else throw IncompatibleTypeError(coltype2str(col_it->type), coltype2str(tv.type));
                }
                set.arith_neg = false;
                set.rhs.set_float(0.0f);          // rhs 不参与，占位保证类型一致
                set.rhs.init_raw(col_it->len);
                query->set_clauses.push_back(set);
                continue;
            }
            throw IncompatibleTypeError(coltype2str(col_it->type), coltype2str(set.rhs.type));
        }

        if (set.self_noop) {
            // SET col = col 自赋值：字节恒等。数值列保留 delta-0 算术表示，复用现有
            // 冲突检测/回滚机制；char 列 rhs（IntLit 0）不参与执行也过不了类型检查，
            // 跳过检查与 init_raw（init_raw 按列宽写 int 会越界），执行器按原值拷贝。
            if (col_it->type == TYPE_FLOAT) {
                set.rhs.set_float(0.0f);
                set.rhs.init_raw(col_it->len);
            } else if (col_it->type == TYPE_INT) {
                set.rhs.set_int(0);
                set.rhs.init_raw(col_it->len);
            }
            query->set_clauses.push_back(set);
            continue;
        }

        // 类型提升
        if (col_it->type == TYPE_FLOAT && set.rhs.type == TYPE_INT) {
            set.rhs.set_float(static_cast<float>(set.rhs.int_val));
        } else if (col_it->type == TYPE_INT && set.rhs.type == TYPE_FLOAT) {
            // int 列赋 float 值（含 col=col±float 的增量）：数值可表示时四舍五入转 int；
            // 整数 k 有 round(k+d)=k+round(d)，增量取整与结果取整等价
            if (!coerce_float_val_to_int(set.rhs)) {
                throw IncompatibleTypeError(coltype2str(col_it->type), coltype2str(set.rhs.type));
            }
        }
        if (col_it->type != set.rhs.type) {
            throw IncompatibleTypeError(coltype2str(col_it->type),
                                        coltype2str(set.rhs.type));
        }

        // 初始化字节存储
        set.rhs.init_raw(col_it->len);
        query->set_clauses.push_back(set);
    }

    // 处理 WHERE 布尔表达式。UPDATE/DELETE 也走与 SELECT 相同的
    // 递归条件分析，避免再将根节点压平成隐式 AND。
    AnalyzeScope table_scope;
    table_scope.bindings.push_back({x->tab_name, x->tab_name});
    get_all_cols({x->tab_name}, table_scope.cols);
    table_scope.output_cols = table_scope.cols;
    query->where_expr = analyze_conditions(x->where_expr, table_scope);

    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        AnalyzeScope table_scope;
        table_scope.bindings.push_back({x->tab_name, x->tab_name});
        get_all_cols({x->tab_name}, table_scope.cols);
        table_scope.output_cols = table_scope.cols;
        query->where_expr = analyze_conditions(x->where_expr, table_scope);
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(parse)) {
        TabMeta &tab = sm_manager_->db_.get_table(x->tab_name);
        std::vector<Value> raw_vals;
        if (x->cols.empty()) {
            for (auto &sv_val : x->vals) {
                raw_vals.push_back(convert_sv_value(sv_val));
            }
        } else {
            if (x->cols.size() != x->vals.size()) {
                throw InvalidValueCountError();
            }
            for (auto &col : tab.cols) {
                size_t k = 0;
                for (; k < x->cols.size(); ++k) {
                    if (x->cols[k] == col.name) break;
                }
                if (k == x->cols.size()) {
                    throw ColumnNotFoundError(col.name);
                }
                raw_vals.push_back(convert_sv_value(x->vals[k]));
            }
        }
        for (size_t i = 0; i < tab.cols.size(); ++i) {
            Value v = raw_vals[i];
            auto &col = tab.cols[i];
            if (col.type == TYPE_FLOAT && v.type == TYPE_INT) {
                v.set_float(static_cast<float>(v.int_val));
            } else if (col.type == TYPE_INT && v.type == TYPE_FLOAT) {
                // int 列插入 float 字面量：数值可表示时四舍五入转 int，否则仍报类型错误
                if (!coerce_float_val_to_int(v)) {
                    throw IncompatibleTypeError(coltype2str(col.type), coltype2str(v.type));
                }
            }
            if (col.type != v.type) {
                throw IncompatibleTypeError(coltype2str(col.type), coltype2str(v.type));
            }
            query->values.push_back(v);
        }
    } else {
        // do nothing
    }
    query->parse = std::move(parse);
    if (std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {
        query->output_cols = infer_select_output_cols(query);
    } else if (std::dynamic_pointer_cast<ast::UnionStmt>(query->parse)) {
        query->output_cols = query->union_output_cols;
    }
    return query;
}

std::shared_ptr<Query> Analyze::analyze_correlated_select(
    const std::shared_ptr<ast::SelectStmt> &select,
    const AnalyzeScope &outer_scope) {
    if (select == nullptr) throw InternalError("Invalid correlated SELECT");
    // Predicate subqueries and LATERAL operands share local-first name
    // resolution.  The wrapper is internal only; its alias never enters the
    // surrounding row type.
    auto wrapper = std::make_shared<ast::LateralRef>(
        select, "\x1f" "correlated_query");
    auto analyzed = analyze_lateral_ref(wrapper, &outer_scope);
    if (analyzed.node == nullptr || analyzed.node->subquery == nullptr) {
        throw InternalError("Failed to analyze correlated SELECT");
    }
    return analyzed.node->subquery;
}

std::shared_ptr<Query> Analyze::analyze_query_expr(
    const std::shared_ptr<ast::QueryExpr> &expr,
    const AnalyzeScope *correlation_scope) {
    if (expr == nullptr) throw InternalError("Invalid query expression");
    if (correlation_scope == nullptr) return do_analyze(expr);

    if (auto select = std::dynamic_pointer_cast<ast::SelectStmt>(expr)) {
        return analyze_correlated_select(select, *correlation_scope);
    }
    if (auto group = std::dynamic_pointer_cast<ast::QueryGroup>(expr)) {
        return analyze_query_group(group, correlation_scope);
    }
    if (auto set_op = std::dynamic_pointer_cast<ast::UnionStmt>(expr)) {
        return analyze_union_expr(set_op, correlation_scope);
    }
    throw InternalError("Expected a query expression");
}

std::shared_ptr<Query> Analyze::analyze_query_group(
    const std::shared_ptr<ast::QueryGroup> &group,
    const AnalyzeScope *correlation_scope) {
    if (group == nullptr || group->child == nullptr) {
        throw InternalError("Invalid parenthesized query");
    }
    auto query = std::make_shared<Query>();
    query->group_child = analyze_query_expr(group->child, correlation_scope);
    query->output_cols = query->group_child->output_cols;
    for (auto &sv_order : group->orders) {
        const size_t column_count = query->output_cols.size();
        size_t index = column_count;
        if (sv_order->is_ordinal) {
            if (sv_order->ordinal <= 0 ||
                static_cast<size_t>(sv_order->ordinal) > column_count) {
                throw InternalError("ORDER BY position is out of range");
            }
            index = static_cast<size_t>(sv_order->ordinal - 1);
        } else {
            if (!sv_order->cols->tab_name.empty()) {
                throw InternalError("query ORDER BY cannot use a table qualifier");
            }
            size_t matches = 0;
            for (size_t i = 0; i < column_count; ++i) {
                if (query->output_cols[i].name == sv_order->cols->col_name) {
                    index = i;
                    ++matches;
                }
            }
            if (matches == 0) throw ColumnNotFoundError(sv_order->cols->col_name);
            if (matches > 1) throw AmbiguousColumnError(sv_order->cols->col_name);
        }
        TabCol order_col{"", query->output_cols[index].name};
        order_col.output_index = static_cast<int>(index);
        query->orders.emplace_back(std::move(order_col), sv_order->orderby_dir);
    }
    query->has_limit = group->has_limit;
    query->limit_count = group->limit_count;
    query->limit = group->has_limit ? group->limit_count : -1;
    if (query->has_limit && query->limit_count < 0) {
        throw InternalError("LIMIT must not be negative");
    }
    query->has_offset = group->has_offset;
    query->offset_count = group->offset_count;
    if (query->has_offset && query->offset_count < 0) {
        throw InternalError("OFFSET must not be negative");
    }
    query->parse = group;
    return query;
}

std::shared_ptr<Query> Analyze::analyze_union_expr(
    const std::shared_ptr<ast::UnionStmt> &set_op,
    const AnalyzeScope *correlation_scope) {
    if (set_op == nullptr || set_op->left == nullptr || set_op->right == nullptr) {
        throw InternalError("Invalid UNION query tree");
    }
    auto query = std::make_shared<Query>();
    query->union_left = analyze_query_expr(set_op->left, correlation_scope);
    query->union_right = analyze_query_expr(set_op->right, correlation_scope);
    query->set_op = set_op->op;
    query->union_all = set_op->all;
    std::vector<ColMeta> common_cols = query->union_left->output_cols;
    if (common_cols.empty() || query->union_right->output_cols.size() != common_cols.size()) {
        throw InternalError("failure");
    }
    for (size_t i = 0; i < common_cols.size(); ++i) {
        common_cols[i] = promote_union_col(common_cols[i], query->union_right->output_cols[i]);
    }
    int offset = 0;
    for (auto &col : common_cols) {
        col.tab_name.clear();
        col.index = false;
        col.offset = offset;
        offset += col.len;
        query->union_output_cols.push_back(col);
        query->sel_captions.push_back(col.name);
    }
    for (auto &sv_order : set_op->orders) {
        size_t index = query->union_output_cols.size();
        if (sv_order->is_ordinal) {
            if (sv_order->ordinal <= 0 ||
                static_cast<size_t>(sv_order->ordinal) > query->union_output_cols.size()) {
                throw InternalError("ORDER BY position is out of range");
            }
            index = static_cast<size_t>(sv_order->ordinal - 1);
        } else {
            if (!sv_order->cols->tab_name.empty()) {
                throw InternalError("Set operation ORDER BY cannot use a table qualifier");
            }
            size_t matches = 0;
            for (size_t i = 0; i < query->union_output_cols.size(); ++i) {
                if (query->union_output_cols[i].name == sv_order->cols->col_name) {
                    index = i;
                    ++matches;
                }
            }
            if (matches == 0) throw ColumnNotFoundError(sv_order->cols->col_name);
            if (matches > 1) throw AmbiguousColumnError(sv_order->cols->col_name);
        }
        TabCol order_col{"", query->union_output_cols[index].name};
        order_col.output_index = static_cast<int>(index);
        query->orders.emplace_back(std::move(order_col), sv_order->orderby_dir);
    }
    query->has_limit = set_op->has_limit;
    query->limit_count = set_op->limit_count;
    query->limit = set_op->has_limit ? set_op->limit_count : -1;
    if (query->has_limit && query->limit_count < 0) {
        throw InternalError("LIMIT must not be negative");
    }
    query->has_offset = set_op->has_offset;
    query->offset_count = set_op->offset_count;
    if (query->has_offset && query->offset_count < 0) {
        throw InternalError("OFFSET must not be negative");
    }
    query->output_cols = query->union_output_cols;
    query->parse = set_op;
    return query;
}

// 递归分析 jointree。outer_scope 只供 LATERAL 派生表解析相关 WHERE；普通表和
// 普通 JOIN 不会把外层名字泄漏进自己的输出作用域。
Analyze::AnalyzedFromResult Analyze::analyze_from(const std::shared_ptr<ast::FromExpr> &from,
                                                  const AnalyzeScope *outer_scope) {
    if (from == nullptr) {
        throw InternalError("SELECT has no FROM expression");
    }

    if (auto table = std::dynamic_pointer_cast<ast::TableRef>(from)) {
        // 当前节点为表节点
        // get_table 同时完成表存在性检查。
        const auto &meta = sm_manager_->db_.get_table(table->tab_name); // 检查当前表是否存在
        TableBinding binding{table->tab_name, table->alias.empty() ? table->tab_name : table->alias}; // 获取别名

        AnalyzedFromResult result; // 创建表节点
        result.node = std::make_shared<AnalyzedFrom>();
        result.node->is_table = true;
        result.node->table = binding;
        result.node->bindings.push_back(binding);
        result.node->all_bindings.push_back(binding);
        result.scope.bindings.push_back(binding);
        for (auto col : meta.cols) {
            // 语义树使用关系实例名，确保 e1/e2 这样的自连接实例不会混淆。
            col.tab_name = binding.binding_name;
            result.scope.cols.push_back(col);
            result.scope.output_cols.push_back(std::move(col));
        }
        result.node->cols = result.scope.cols;
        result.node->output_cols = result.scope.output_cols;
        return result;
    }

    if (auto derived = std::dynamic_pointer_cast<ast::DerivedTableRef>(from)) {
        if (derived->subquery == nullptr || derived->alias.empty()) {
            throw InternalError("Derived table requires a query and an alias");
        }
        AnalyzedFromResult result;
        result.node = std::make_shared<AnalyzedFrom>();
        result.node->is_subquery = true;
        result.node->subquery = do_analyze(derived->subquery);
        result.node->table = {"", derived->alias};
        result.node->bindings.push_back(result.node->table);
        result.node->all_bindings.push_back(result.node->table);
        result.scope.bindings.push_back(result.node->table);

        int offset = 0;
        for (auto col : result.node->subquery->output_cols) {
            col.tab_name = derived->alias;
            col.offset = offset;
            offset += col.len;
            result.scope.cols.push_back(col);
            result.scope.output_cols.push_back(std::move(col));
        }
        result.node->cols = result.scope.cols;
        result.node->output_cols = result.scope.output_cols;
        return result;
    }

    if (auto lateral = std::dynamic_pointer_cast<ast::LateralRef>(from)) {
        return analyze_lateral_ref(lateral, outer_scope);
    }

    auto join = std::dynamic_pointer_cast<ast::JoinExpr>(from); // 处理 JOIN 节点
    if (join == nullptr) {
        throw InternalError("Unexpected FROM expression");
    }

    auto left = analyze_from(join->left, outer_scope); // 递归分析左子树
    // LATERAL 右侧只继承已经位于它左边的 joined table 作用域。
    auto right = analyze_from(join->right, join->lateral ? &left.scope : outer_scope);

    AnalyzedFromResult result;
    const auto input_scope = merge_scopes(left.scope, right.scope);
    const auto all_bindings = merge_all_bindings(left.node->all_bindings,
                                                 right.node->all_bindings);
    if (join->type == CROSS_JOIN && join->on_expr != nullptr) {
        throw InternalError("CROSS JOIN cannot have an ON clause");
    }
    const bool coalescing_join = join->natural || !join->using_cols.empty();
    if (!coalescing_join && join->type != CROSS_JOIN && join->on_expr == nullptr && !join->on_true) {
        throw InternalError("JOIN requires an ON clause");
    }
    if (coalescing_join && (join->on_expr != nullptr || join->on_true)) {
        throw InternalError("NATURAL/USING JOIN cannot have an ON clause");
    }
    if (coalescing_join && join->type == CROSS_JOIN) {
        throw InternalError("NATURAL/USING CROSS JOIN is not supported");
    }
    if (join->lateral && coalescing_join) {
        throw InternalError("NATURAL/USING LATERAL JOIN is not supported");
    }
    if (join->lateral && join->type != INNER_JOIN && join->type != CROSS_JOIN &&
        join->type != LEFT_JOIN) {
        throw InternalError("LATERAL JOIN supports only INNER, CROSS, and LEFT");
    }

    result.node = std::make_shared<AnalyzedFrom>();
    result.node->is_table = false;
    result.node->join_type = join->type;
    // NATURAL and USING share the same coalesced-column execution contract,
    // but retain the actual NATURAL marker so EXPLAIN can distinguish them.
    result.node->natural = join->natural;
    result.node->lateral = join->lateral;
    result.node->left = left.node;
    result.node->right = right.node;
    result.node->all_bindings = all_bindings;

    if (coalescing_join) {
        // NATURAL compares every shared public name; USING compares only its
        // explicit list.  A key must identify exactly one public column on
        // each side.
        std::map<std::string, std::vector<ColMeta>> left_by_name;
        std::map<std::string, std::vector<ColMeta>> right_by_name;
        for (const auto &col : left.scope.output_cols) left_by_name[col.name].push_back(col);
        for (const auto &col : right.scope.output_cols) right_by_name[col.name].push_back(col);

        std::set<std::string> common_names;
        std::vector<std::string> merge_names;
        if (join->natural) {
            for (const auto &left_col : left.scope.output_cols) {
                if (right_by_name.count(left_col.name) != 0 &&
                    common_names.insert(left_col.name).second) {
                    merge_names.push_back(left_col.name);
                }
            }
            common_names.clear();
        } else {
            for (const auto &name : join->using_cols) {
                if (!common_names.insert(name).second) {
                    throw InternalError("duplicate column in USING: " + name);
                }
                merge_names.push_back(name);
            }
            common_names.clear();
        }

        const std::string synthetic_binding = "\x1f" "natural_" + std::to_string(++natural_id_);
        for (const auto &name : merge_names) {
            auto left_found = left_by_name.find(name);
            auto right_found = right_by_name.find(name);
            if (left_found == left_by_name.end() || right_found == right_by_name.end()) {
                throw ColumnNotFoundError(name);
            }
            if (left_found->second.size() != 1 || right_found->second.size() != 1) {
                throw AmbiguousColumnError(name);
            }
            const auto &left_col = left_found->second.front();
            const auto &right_col = right_found->second.front();
            if (left_col.type != right_col.type ||
                (left_col.type != TYPE_STRING && left_col.len != right_col.len)) {
                throw IncompatibleTypeError(coltype2str(left_col.type), coltype2str(right_col.type));
            }

            Condition cond;
            cond.lhs_col = {left_col.tab_name, left_col.name};
            cond.op = OP_EQ;
            cond.is_rhs_val = false;
            cond.rhs_col = {right_col.tab_name, right_col.name};
            auto atom = make_bool_atom(std::move(cond));
            result.node->on_expr = result.node->on_expr == nullptr
                                       ? std::move(atom)
                                       : make_bool_binary(
                                             BoolExprType::AND,
                                             std::move(result.node->on_expr),
                                             std::move(atom));

            ColMeta output = left_col;
            output.tab_name = synthetic_binding;
            if (output.type == TYPE_STRING) output.len = std::max(left_col.len, right_col.len);
            output.offset = 0;  // Executor 按实际左右布局重新计算。
            CoalescedJoinColumn merged{{left_col.tab_name, left_col.name},
                                       {right_col.tab_name, right_col.name},
                                       {synthetic_binding, left_col.name},
                                       left_col.type, output.len};
            result.node->coalesced_cols.push_back(std::move(merged));
            result.scope.output_cols.push_back(output);
            common_names.insert(name);
        }

        for (const auto &col : left.scope.output_cols) {
            if (common_names.count(col.name) == 0) result.scope.output_cols.push_back(col);
        }
        for (const auto &col : right.scope.output_cols) {
            if (common_names.count(col.name) == 0) result.scope.output_cols.push_back(col);
        }
        result.scope.cols = input_scope.cols;
        result.scope.cols.insert(result.scope.cols.end(), result.scope.output_cols.begin(),
                                 result.scope.output_cols.begin() + result.node->coalesced_cols.size());
        result.scope.bindings = input_scope.bindings;
    } else {
        check_where_no_aggregate(join->on_expr);
        result.node->on_expr = analyze_conditions(join->on_expr, input_scope);

        switch (join->type) {
            case LEFT_SEMI_JOIN:
            case LEFT_ANTI_JOIN:
                result.scope = left.scope;
                break;
            case RIGHT_SEMI_JOIN:
            case RIGHT_ANTI_JOIN:
                result.scope = right.scope;
                break;
            default:
                result.scope = input_scope;
                break;
        }
    }

    result.node->bindings = result.scope.bindings;
    result.node->cols = result.scope.cols;
    result.node->output_cols = result.scope.output_cols;
    return result;
}

Analyze::AnalyzedFromResult Analyze::analyze_lateral_ref(
    const std::shared_ptr<ast::LateralRef> &lateral, const AnalyzeScope *outer_scope) {
    if (lateral == nullptr || lateral->subquery == nullptr) {
        throw InternalError("Invalid LATERAL derived table");
    }
    if (lateral->alias.empty()) {
        throw InternalError("LATERAL derived table requires an alias");
    }

    const AnalyzeScope empty_outer;
    const AnalyzeScope &outer = outer_scope == nullptr ? empty_outer : *outer_scope;
    auto select = std::dynamic_pointer_cast<ast::SelectStmt>(lateral->subquery);
    if (select == nullptr) {
        auto subquery = analyze_query_expr(lateral->subquery, &outer);
        AnalyzedFromResult result;
        result.node = std::make_shared<AnalyzedFrom>();
        result.node->is_subquery = true;
        result.node->subquery = std::move(subquery);
        result.node->table = {"", lateral->alias};
        result.node->bindings.push_back(result.node->table);
        result.node->all_bindings.push_back(result.node->table);
        result.scope.bindings.push_back(result.node->table);

        int offset = 0;
        for (auto col : result.node->subquery->output_cols) {
            col.tab_name = lateral->alias;
            col.offset = offset;
            offset += col.len;
            result.scope.cols.push_back(col);
            result.scope.output_cols.push_back(std::move(col));
        }
        result.node->cols = result.scope.cols;
        result.node->output_cols = result.scope.output_cols;
        return result;
    }
    // 先取得子查询自己的名字空间，用 local-first 规则识别 WHERE 中的相关列。
    auto local_from = analyze_from(select->from);
    std::vector<std::shared_ptr<ast::BoolExpr>> local_where;
    std::vector<std::shared_ptr<ast::BoolExpr>> correlated_where;

    AnalyzeScope type_scope = local_from.scope;
    type_scope.cols.insert(type_scope.cols.end(), outer.cols.begin(), outer.cols.end());
    type_scope.output_cols.insert(type_scope.output_cols.end(), outer.output_cols.begin(),
                                  outer.output_cols.end());

    std::vector<std::shared_ptr<ast::BoolExpr>> conjuncts;
    split_ast_top_level_and(select->where_expr, conjuncts);
    for (const auto &part : conjuncts) {
        bool uses_outer = false;
        // 递归访问整个合取项的比较叶。只要 OR/NOT 子树中任意叶
        // 引用外层，整棵子树都必须留在 correlated 侧。
        (void)analyze_lateral_conditions(part, local_from.scope, outer,
                                         type_scope, &uses_outer);
        if (uses_outer) {
            correlated_where.push_back(part);
        } else {
            local_where.push_back(part);
        }
    }

    // 复用完整 SELECT Analyzer；仅把相关 WHERE 留给参数化 Filter。SELECT/GROUP/
    // HAVING/内部 ON 中的外层引用仍会按普通未知列报错，这是当前明确的支持边界。
    auto local_select = std::make_shared<ast::SelectStmt>(*select);
    local_select->where_expr = combine_ast_with_and(local_where);
    auto subquery = do_analyze(local_select);

    // 子查询会被完整分析一次；若其 FROM 含 NATURAL JOIN，内部 synthetic
    // binding 会在这次分析中重新生成。因此用最终 analyzed scope 再绑定相关条件，
    // 不能沿用上面仅用于 local/outer 分类的临时引用。
    AnalyzeScope final_local;
    final_local.bindings = subquery->from->bindings;
    final_local.cols = subquery->from->cols;
    final_local.output_cols = subquery->from->output_cols;
    AnalyzeScope final_type_scope = final_local;
    final_type_scope.cols.insert(final_type_scope.cols.end(), outer.cols.begin(), outer.cols.end());
    final_type_scope.output_cols.insert(final_type_scope.output_cols.end(), outer.output_cols.begin(),
                                        outer.output_cols.end());
    std::vector<ConditionExprPtr> correlated_parts;
    for (const auto &part : correlated_where) {
        bool uses_outer = false;
        auto analyzed = analyze_lateral_conditions(
            part, final_local, outer, final_type_scope, &uses_outer);
        if (!uses_outer) {
            throw InternalError("LATERAL correlated predicate lost its outer reference");
        }
        correlated_parts.push_back(std::move(analyzed));
    }
    subquery->correlated_expr = combine_with_and(correlated_parts);

    AnalyzedFromResult result;
    result.node = std::make_shared<AnalyzedFrom>();
    result.node->is_subquery = true;
    result.node->subquery = std::move(subquery);
    result.node->table = {"", lateral->alias};
    result.node->bindings.push_back(result.node->table);
    result.node->all_bindings.push_back(result.node->table);
    result.scope.bindings.push_back(result.node->table);

    int offset = 0;
    for (auto col : result.node->subquery->output_cols) {
        col.tab_name = lateral->alias;
        col.offset = offset;
        offset += col.len;
        result.scope.cols.push_back(col);
        result.scope.output_cols.push_back(std::move(col));
    }
    result.node->cols = result.scope.cols;
    result.node->output_cols = result.scope.output_cols;
    return result;
}

TabCol Analyze::resolve_lateral_column(const AnalyzeScope &local, const AnalyzeScope &outer,
                                       TabCol target, bool *is_outer) {
    auto binding_exists = [](const AnalyzeScope &scope, const std::string &name) {
        return std::any_of(scope.bindings.begin(), scope.bindings.end(),
                           [&](const TableBinding &binding) { return binding.binding_name == name; });
    };
    if (!target.tab_name.empty()) {
        if (binding_exists(local, target.tab_name)) {
            if (is_outer != nullptr) *is_outer = false;
            return resolve_column(local, std::move(target));
        }
        if (binding_exists(outer, target.tab_name)) {
            if (is_outer != nullptr) *is_outer = true;
            return resolve_column(outer, std::move(target));
        }
        throw TableNotFoundError(target.tab_name);
    }

    auto resolve_unqualified = [&](const AnalyzeScope &scope, bool outer_column,
                                   TabCol candidate, bool *found) {
        size_t matches = 0;
        std::string binding;
        for (const auto &col : scope.output_cols) {
            if (col.name != candidate.col_name) continue;
            binding = col.tab_name;
            ++matches;
        }
        if (matches > 1) throw AmbiguousColumnError(candidate.col_name);
        if (matches == 1) {
            candidate.tab_name = binding;
            if (is_outer != nullptr) *is_outer = outer_column;
            *found = true;
        }
        return candidate;
    };

    bool found = false;
    target = resolve_unqualified(local, false, std::move(target), &found);
    if (found) return target;
    target = resolve_unqualified(outer, true, std::move(target), &found);
    if (found) return target;
    throw ColumnNotFoundError(target.col_name);
}

AnalyzeScope Analyze::merge_scopes(const AnalyzeScope &left,
                                   const AnalyzeScope &right) {
    AnalyzeScope result = left;
    for (const auto &incoming : right.bindings) {
        for (const auto &existing : result.bindings) {
            if (existing.binding_name == incoming.binding_name) {
                // 出现自定义同名表，无法判断来自哪种表
                throw InternalError("Duplicate table binding: " + incoming.binding_name);
            }
        }
        result.bindings.push_back(incoming);
    }
    result.cols.insert(result.cols.end(), right.cols.begin(), right.cols.end());
    result.output_cols.insert(result.output_cols.end(), right.output_cols.begin(), right.output_cols.end());
    return result; // 左表与右表合并作用域
}

std::vector<TableBinding> Analyze::merge_all_bindings(const std::vector<TableBinding> &left,
                                                      const std::vector<TableBinding> &right) {
    std::vector<TableBinding> result = left;
    for (const auto &incoming : right) {
        for (const auto &existing : result) {
            if (existing.binding_name == incoming.binding_name) {
                throw InternalError("Duplicate table binding: " + incoming.binding_name);
            }
        }
        result.push_back(incoming);
    }
    return result;
}

// 统一进行列解析，为列引用绑定作用域内唯一确定的表后返回。
TabCol Analyze::resolve_column(const AnalyzeScope &scope, TabCol target) {
    if (target.tab_name.empty()) { // 未指明表名前缀
        std::string binding_name;
        size_t matches = 0;
        for (const auto &col : scope.output_cols) { // 未限定名只搜索公开 row type
            if (col.name != target.col_name) continue;
            if (++matches > 1) {
                throw AmbiguousColumnError(target.col_name); // 不止一张表有该列，抛出错误
            }
            binding_name = col.tab_name;
        }
        if (binding_name.empty()) {
            throw ColumnNotFoundError(target.col_name); // 所有表都没有该列
        }
        target.tab_name = binding_name;
        return target;
    }

    bool binding_exists = false; // 是否作用域当前作用域
    for (const auto &binding : scope.bindings) {
        if (binding.binding_name == target.tab_name) {
            binding_exists = true;
            break;
        }
    }
    if (!binding_exists) {
        throw TableNotFoundError(target.tab_name);
    }
    size_t matches = 0;
    for (const auto &col : scope.cols) { // 对应表是否存在该列
        if (col.tab_name == target.tab_name && col.name == target.col_name) {
            if (++matches > 1) throw AmbiguousColumnError(target.col_name);
        }
    }
    if (matches == 1) return target;
    throw ColumnNotFoundError(target.col_name);
}

// 为 ON/WHERE/UPDATE/DELETE 建立统一的递归分析流程。每个比较
// 叶完成列绑定与类型改写，逻辑节点只保留拓扑。
ConditionExprPtr Analyze::analyze_conditions(
    const std::shared_ptr<ast::BoolExpr> &sv_expr, const AnalyzeScope &scope,
    bool allow_subquery) {
    if (sv_expr == nullptr) return nullptr;

    if (auto atom = std::dynamic_pointer_cast<ast::BinaryExpr>(sv_expr)) {
        std::vector<Condition> one{convert_condition_atom(atom)};
        one[0].lhs_col = resolve_column(scope, one[0].lhs_col);
        if (!one[0].is_rhs_val) {
            one[0].rhs_col = resolve_column(scope, one[0].rhs_col);
        }
        check_condition_types(scope, one);
        if (one.empty()) return nullptr;
        return make_bool_atom(std::move(one[0]));
    }
    if (auto predicate = std::dynamic_pointer_cast<ast::SubqueryPredicate>(sv_expr)) {
        if (!allow_subquery) {
            throw InternalError("Subquery predicates are currently supported only in SELECT WHERE");
        }
        Condition condition;
        condition.kind = predicate->type == ast::SubqueryPredicateType::EXISTS
                             ? ConditionKind::EXISTS_SUBQUERY
                             : ConditionKind::IN_SUBQUERY;
        condition.subquery = analyze_predicate_subquery(predicate->subquery, scope);
        if (condition.kind == ConditionKind::IN_SUBQUERY) {
            if (predicate->lhs == nullptr || condition.subquery->output_cols.size() != 1) {
                throw InternalError("IN subquery must return exactly one column");
            }
            condition.lhs_col = resolve_column(
                scope, {.tab_name = predicate->lhs->tab_name,
                        .col_name = predicate->lhs->col_name});
            const ColType lhs_type = get_col_type(scope.cols, condition.lhs_col);
            const ColType rhs_type = condition.subquery->output_cols.front().type;
            const bool numeric = (lhs_type == TYPE_INT || lhs_type == TYPE_FLOAT) &&
                                 (rhs_type == TYPE_INT || rhs_type == TYPE_FLOAT);
            if (lhs_type != rhs_type && !numeric) {
                throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
            }
        }
        return make_bool_atom(std::move(condition));
    }
    if (auto logical = std::dynamic_pointer_cast<ast::LogicalExpr>(sv_expr)) {
        const BoolExprType type = logical->op == ast::LogicalOp::AND
                                      ? BoolExprType::AND
                                      : BoolExprType::OR;
        return make_bool_binary(
            type, analyze_conditions(logical->left, scope, allow_subquery),
            analyze_conditions(logical->right, scope, allow_subquery));
    }
    if (auto negated = std::dynamic_pointer_cast<ast::NotExpr>(sv_expr)) {
        return make_bool_not(analyze_conditions(negated->child, scope, allow_subquery));
    }
    throw InternalError("Unexpected boolean expression node");
}

std::shared_ptr<Query> Analyze::analyze_predicate_subquery(
    const std::shared_ptr<ast::QueryExpr> &subquery,
    const AnalyzeScope &outer_scope) {
    if (subquery == nullptr) throw InternalError("Invalid predicate subquery");
    return analyze_query_expr(subquery, &outer_scope);
}

ConditionExprPtr Analyze::analyze_lateral_conditions(
    const std::shared_ptr<ast::BoolExpr> &sv_expr,
    const AnalyzeScope &local, const AnalyzeScope &outer,
    const AnalyzeScope &type_scope, bool *uses_outer) {
    if (sv_expr == nullptr) return nullptr;

    if (auto atom = std::dynamic_pointer_cast<ast::BinaryExpr>(sv_expr)) {
        std::vector<Condition> one{convert_condition_atom(atom)};
        bool lhs_outer = false;
        bool rhs_outer = false;
        one[0].lhs_col = resolve_lateral_column(local, outer, one[0].lhs_col,
                                                &lhs_outer);
        if (!one[0].is_rhs_val) {
            one[0].rhs_col = resolve_lateral_column(local, outer, one[0].rhs_col,
                                                    &rhs_outer);
        }
        if (uses_outer != nullptr && (lhs_outer || rhs_outer)) *uses_outer = true;
        check_condition_types(type_scope, one);
        if (one.empty()) return nullptr;
        return make_bool_atom(std::move(one[0]));
    }
    if (auto logical = std::dynamic_pointer_cast<ast::LogicalExpr>(sv_expr)) {
        const BoolExprType type = logical->op == ast::LogicalOp::AND
                                      ? BoolExprType::AND
                                      : BoolExprType::OR;
        return make_bool_binary(
            type,
            analyze_lateral_conditions(logical->left, local, outer,
                                       type_scope, uses_outer),
            analyze_lateral_conditions(logical->right, local, outer,
                                       type_scope, uses_outer));
    }
    if (auto negated = std::dynamic_pointer_cast<ast::NotExpr>(sv_expr)) {
        return make_bool_not(analyze_lateral_conditions(
            negated->child, local, outer, type_scope, uses_outer));
    }
    throw InternalError("Unexpected boolean expression node");
}

// 
void Analyze::check_condition_types(const AnalyzeScope &scope,
                                    std::vector<Condition> &conds) {
    auto find_col = [&](const TabCol &target) -> const ColMeta & {
        for (const auto &col : scope.cols) {
            if (col.tab_name == target.tab_name && col.name == target.col_name) {
                return col; // 
            }
        }
        throw ColumnNotFoundError(target.col_name);
    };

    std::vector<Condition> kept;
    kept.reserve(conds.size());
    for (auto &cond : conds) {
        const auto &lhs_col = find_col(cond.lhs_col);
        const ColType lhs_type = lhs_col.type;
        if (is_null_test_op(cond.op)) {
            kept.push_back(std::move(cond));
            continue;
        }
        ColType rhs_type;
        if (cond.is_rhs_val) {
            bool rewritten_to_self = false;
            if (lhs_type == TYPE_FLOAT && cond.rhs_val.type == TYPE_INT) {
                cond.rhs_val.set_float(static_cast<float>(cond.rhs_val.int_val)); // FLOAT 与 INT 比较时，将 INT 提升为 FLOAT
            } else if (lhs_type == TYPE_FLOAT && cond.rhs_val.type == TYPE_FLOAT &&
                       std::isnan(cond.rhs_val.float_val)) {
                if (cond.op == OP_NE) {
                    rewrite_true_for_non_null(cond);
                    rewritten_to_self = true;
                } else {
                    cond.op = OP_LT;
                    cond.rhs_val.set_float(-INFINITY);
                }
            } else if (lhs_type == TYPE_INT && cond.rhs_val.type == TYPE_FLOAT) {
                IntFloatRewrite rewrite = rewrite_int_col_float_val(cond);
                if (rewrite == IntFloatRewrite::ALWAYS_TRUE) {
                    rewrite_true_for_non_null(cond);
                    rewritten_to_self = true;
                } else if (rewrite == IntFloatRewrite::ALWAYS_FALSE) {
                    cond.op = OP_LT;
                    cond.rhs_val.set_int(INT32_MIN);
                }
            }
            if (rewritten_to_self) {
                rhs_type = lhs_type;
            } else {
                cond.rhs_val.init_raw(lhs_col.len);
                rhs_type = cond.rhs_val.type;
            }
        } else {
            rhs_type = find_col(cond.rhs_col).type;
        }
        if (lhs_type != rhs_type) {
            throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
        }
        if (cond.op == OP_LIKE && lhs_type != TYPE_STRING) {
            throw IncompatibleTypeError(coltype2str(lhs_type), "STRING");
        }
        kept.push_back(std::move(cond));
    }
    conds.swap(kept);
}

std::vector<ColMeta> Analyze::infer_select_output_cols(const std::shared_ptr<Query> &query) {
    std::vector<ColMeta> result;
    int offset = 0;
    auto find_col = [&](const TabCol &target) -> ColMeta {
        if (query->from != nullptr) {
            for (const auto &col : query->from->cols) {
                if (col.name == target.col_name && col.tab_name == target.tab_name) return col;
            }
        }
        throw ColumnNotFoundError(target.col_name);
    };
    if (!query->aggs.empty() || !query->group_by_cols.empty()) {
        // UNION 依据 SELECT 列表确定输出，而不是依据内部 GROUP BY 键。
        for (auto &selected : query->cols) {
            ColMeta col = find_col(selected);
            if (!selected.alias.empty()) col.name = selected.alias;
            col.offset = offset;
            offset += col.len;
            result.push_back(col);
        }
        for (auto &agg : query->aggs) {
            if (!agg.in_output) continue;
            ColMeta col;
            col.name = agg.alias.empty() ? agg.to_string() : agg.alias;
            col.tab_name = agg.col.tab_name;
            col.type = agg.output_type();
            col.len = agg.output_len();
            col.offset = offset;
            offset += col.len;
            result.push_back(col);
        }
        return result;
    }

    for (auto &tc : query->cols) {
        ColMeta col = find_col(tc);
        if (!tc.alias.empty()) col.name = tc.alias;   // 决赛：col AS alias
        col.offset = offset;
        offset += col.len;
        result.push_back(col);
    }
    return result;
}

ColMeta Analyze::promote_union_col(const ColMeta &base, const ColMeta &incoming) {
    ColMeta result = base;
    if (base.type == incoming.type) {
        if (base.type == TYPE_STRING) result.len = std::max(base.len, incoming.len);
        return result;
    }
    bool numeric = (base.type == TYPE_INT || base.type == TYPE_FLOAT) &&
                   (incoming.type == TYPE_INT || incoming.type == TYPE_FLOAT);
    if (numeric) {
        result.type = TYPE_FLOAT;
        result.len = sizeof(float);
        return result;
    }
    throw InternalError("failure");
}

TabCol Analyze::check_column(const std::vector<ColMeta> &all_cols, TabCol target) {
    if (target.tab_name.empty()) {
        std::string tab_name;
        for (auto &col : all_cols) {
            if (col.name == target.col_name) {
                if (!tab_name.empty()) {
                    throw AmbiguousColumnError(target.col_name);
                }
                tab_name = col.tab_name;
            }
        }
        if (tab_name.empty()) {
            throw ColumnNotFoundError(target.col_name);
        }
        target.tab_name = tab_name;
    }
    return target;
}

TabCol Analyze::resolve_order_column(TabCol order_col,
                                     const std::vector<TabCol> &sel_cols,
                                     const std::vector<TabCol> &group_by_cols,
                                     const std::vector<AggregateInfo> &aggs,
                                     const std::vector<ColMeta> &all_cols) {
    for (auto &agg : aggs) {
        std::string name = agg.alias.empty() ? agg.to_string() : agg.alias;
        if (order_col.col_name == name) {
            TabCol resolved;
            resolved.tab_name = "";
            resolved.col_name = name;
            return resolved;
        }
    }
    for (auto &gc : group_by_cols) {
        if (order_col.col_name == gc.col_name &&
            (order_col.tab_name.empty() || order_col.tab_name == gc.tab_name)) {
            return gc;
        }
    }
    for (auto &sc : sel_cols) {
        if (order_col.col_name == sc.col_name &&
            (order_col.tab_name.empty() || order_col.tab_name == sc.tab_name)) {
            return sc;
        }
        // 决赛：ORDER BY 引用 SELECT 列别名 → 解析回底层列
        if (!sc.alias.empty() && order_col.tab_name.empty() && order_col.col_name == sc.alias) {
            return sc;
        }
    }
    return check_column(all_cols, order_col);
}

void Analyze::get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols) {
    for (auto &sel_tab_name : tab_names) {
        // 这里db_不能写成get_db(), 注意要传指针
        const auto &sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
}

Condition Analyze::convert_condition_atom(
    const std::shared_ptr<ast::BinaryExpr> &expr) {
    if (expr == nullptr || expr->lhs == nullptr) {
        throw InternalError("Invalid comparison atom outside HAVING");
    }
    Condition cond;
    cond.lhs_col = {.tab_name = expr->lhs->tab_name, .col_name = expr->lhs->col_name};
    cond.op = convert_sv_comp_op(expr->op);
    if (is_null_test_op(cond.op)) {
        cond.is_rhs_val = true;  // unary predicate; only lhs participates in binding
        return cond;
    }
    if (auto rhs_val = std::dynamic_pointer_cast<ast::Value>(expr->rhs)) {
        cond.is_rhs_val = true;
        cond.rhs_val = convert_sv_value(rhs_val);
        cond.rhs_is_float_lit =
            (std::dynamic_pointer_cast<ast::FloatLit>(rhs_val) != nullptr);
    } else if (auto rhs_col = std::dynamic_pointer_cast<ast::Col>(expr->rhs)) {
        cond.is_rhs_val = false;
        cond.rhs_col = {.tab_name = rhs_col->tab_name,
                        .col_name = rhs_col->col_name};
    } else {
        throw InternalError("Unexpected comparison right-hand side");
    }
    return cond;
}

Value Analyze::convert_sv_value(const std::shared_ptr<ast::Value> &sv_val) {
    Value val;
    if (auto int_lit = std::dynamic_pointer_cast<ast::IntLit>(sv_val)) {
        val.set_int(int_lit->val);
    } else if (auto float_lit = std::dynamic_pointer_cast<ast::FloatLit>(sv_val)) {
        val.set_float(float_lit->val);
    } else if (auto str_lit = std::dynamic_pointer_cast<ast::StringLit>(sv_val)) {
        val.set_str(str_lit->val);
    } else {
        throw InternalError("Unexpected sv value type");
    }
    return val;
}

CompOp Analyze::convert_sv_comp_op(ast::SvCompOp op) {
    std::map<ast::SvCompOp, CompOp> m = {
        {ast::SV_OP_EQ, OP_EQ}, {ast::SV_OP_NE, OP_NE}, {ast::SV_OP_LT, OP_LT},
        {ast::SV_OP_GT, OP_GT}, {ast::SV_OP_LE, OP_LE}, {ast::SV_OP_GE, OP_GE},
        {ast::SV_OP_LIKE, OP_LIKE},
        {ast::SV_OP_IS_NULL, OP_IS_NULL},
        {ast::SV_OP_IS_NOT_NULL, OP_IS_NOT_NULL},
    };
    return m.at(op);
}

ColType Analyze::get_col_type(const std::vector<ColMeta> &all_cols, const TabCol &col) {
    for (auto &c : all_cols) {
        if (c.tab_name == col.tab_name && c.name == col.col_name) {
            return c.type;
        }
    }
    return TYPE_INT;  // should not reach here if check_column passed
}

AggregateInfo Analyze::analyze_aggregate(ast::AggType type,
                                         const std::vector<std::shared_ptr<ast::Col>> &arguments,
                                         bool is_star, bool distinct,
                                         const std::string &alias,
                                         const AnalyzeScope &scope) {
    const auto *spec = ast::aggregate_spec(type);
    if (spec == nullptr || is_star != arguments.empty() ||
        (is_star && (!spec->accepts_star || distinct)) ||
        (distinct && !spec->accepts_distinct) ||
        (arguments.size() > 1 && (type != ast::AGG_COUNT || !distinct))) {
        throw InternalError("failure");
    }

    AggregateInfo agg;
    agg.type = type;
    agg.alias = alias;
    agg.is_star = is_star;
    agg.distinct = distinct;

    if (is_star) {
        // 星号聚合没有物理输入槽位。这里用 INT/sizeof(int) 作为稳定占位，
        // 实际输出约束由注册表统一定义。
        agg.arg_type = TYPE_INT;
        agg.arg_len = sizeof(int);
        return agg;
    }

    for (const auto &argument : arguments) {
        TabCol resolved = resolve_column(
            scope, {.tab_name = argument->tab_name, .col_name = argument->col_name});
        const auto col_it = std::find_if(scope.cols.begin(), scope.cols.end(), [&](const ColMeta &meta) {
            return meta.tab_name == resolved.tab_name && meta.name == resolved.col_name;
        });
        if (col_it == scope.cols.end() || !ast::aggregate_accepts_argument(type, col_it->type)) {
            throw InternalError("failure");
        }
        agg.arguments.push_back(std::move(resolved));
        if (agg.arguments.size() == 1) {
            agg.col = agg.arguments.front();
            agg.arg_type = col_it->type;
            agg.arg_len = col_it->len;
        }
    }
    return agg;
}

// 检查列是否出现 GROUP BY 中
bool Analyze::is_in_group_by(const TabCol &col, const std::vector<TabCol> &group_by) {
    for (auto &g : group_by) {
        if (g.tab_name == col.tab_name && g.col_name == col.col_name)
            return true;
    }
    return false;
}

void Analyze::check_where_no_aggregate(const std::shared_ptr<ast::BoolExpr> &sv_expr) {
    // 当前 WHERE 语法左侧只接受 Col，不接受 AggExpr。保留此入口，作为以后加入
    // 通用表达式时的语义边界。
    (void)sv_expr;
}
// 检查 GROUP BY 的语义合法性，如果查询用了聚合，那么 SELECT 中直接输出的普通列必须受到 GROUP BY 的约束
void Analyze::check_group_by_validity(const std::vector<TabCol> &sel_cols,
                                      const std::vector<AggregateInfo> &aggs,
                                      const std::vector<TabCol> &group_by) {
    bool has_agg = !aggs.empty();
    bool has_plain_col = !sel_cols.empty();

    if (!has_agg && group_by.empty()) return;  // 无聚合且无 GROUP BY，无需检查

    if (group_by.empty()) {
        // sql 中，若聚合函数与普通列同时出现，则必须要有GROUP BY 约束
        if (has_plain_col) {
            throw InternalError("failure");
        }
        return;
    }

    // 有 GROUP BY，检查所有普通列都在 GROUP BY 中
    for (auto &col : sel_cols) {
        if (!is_in_group_by(col, group_by)) {
            throw InternalError("failure");
        }
    }
}

HavingExprPtr Analyze::analyze_having_clause(
    const std::shared_ptr<ast::BoolExpr> &sv_expr,
    const std::vector<TabCol> &group_by, std::vector<AggregateInfo> &aggs,
    const AnalyzeScope &scope) {
    if (sv_expr == nullptr) return nullptr;

    if (auto logical = std::dynamic_pointer_cast<ast::LogicalExpr>(sv_expr)) {
        const BoolExprType type = logical->op == ast::LogicalOp::AND
                                      ? BoolExprType::AND
                                      : BoolExprType::OR;
        // Analyze left-to-right so hidden aggregate slots receive stable,
        // deterministic indexes independent of function argument ordering.
        auto left = analyze_having_clause(logical->left, group_by, aggs, scope);
        auto right = analyze_having_clause(logical->right, group_by, aggs, scope);
        return make_bool_binary<HavingCondition>(type, std::move(left),
                                                 std::move(right));
    }
    if (auto negated = std::dynamic_pointer_cast<ast::NotExpr>(sv_expr)) {
        return make_bool_not<HavingCondition>(
            analyze_having_clause(negated->child, group_by, aggs, scope));
    }
    auto expr = std::dynamic_pointer_cast<ast::BinaryExpr>(sv_expr);
    if (expr == nullptr) throw InternalError("Unexpected HAVING expression node");

    auto same_aggregate = [](const AggregateInfo &left, const AggregateInfo &right) {
        if (left.type != right.type || left.is_star != right.is_star ||
            left.distinct != right.distinct ||
            left.arguments.size() != right.arguments.size()) {
            return false;
        }
        for (size_t i = 0; i < left.arguments.size(); ++i) {
            if (left.arguments[i].tab_name != right.arguments[i].tab_name ||
                left.arguments[i].col_name != right.arguments[i].col_name) {
                return false;
            }
        }
        return true;
    };
    auto result_type = [&](const HavingCondition &cond) {
        if (cond.source == HavingSource::AGGREGATE) {
            return aggs.at(cond.index).output_type();
        }
        const TabCol &target = group_by.at(cond.index);
        return get_col_type(scope.cols, target);
    };

        auto rhs = std::dynamic_pointer_cast<ast::Value>(expr->rhs);
        HavingCondition cond;
        cond.op = convert_sv_comp_op(expr->op);
        if (rhs == nullptr && !is_null_test_op(cond.op)) {
            // 执行器只支持已经解析的标量右操作数。这里直接拒绝列，比旧路径先
            // 接受、执行时再读取未初始化 rhs_val 更安全。
            throw InternalError("failure");
        }
        if (!is_null_test_op(cond.op)) cond.rhs = convert_sv_value(rhs);

        if (expr->lhs_agg != nullptr) {
            const auto &syntax = expr->lhs_agg;
            AggregateInfo resolved = analyze_aggregate(
                syntax->agg_type, syntax->arguments, syntax->is_star,
                syntax->distinct, "", scope);
            auto found = std::find_if(aggs.begin(), aggs.end(), [&](const AggregateInfo &candidate) {
                return same_aggregate(candidate, resolved);
            });
            if (found == aggs.end()) {
                resolved.in_output = false;
                aggs.push_back(std::move(resolved));
                cond.index = aggs.size() - 1;
            } else {
                cond.index = static_cast<size_t>(found - aggs.begin());
            }
            cond.source = HavingSource::AGGREGATE;
        } else if (expr->lhs != nullptr) {
            const TabCol syntax_col = {
                .tab_name = expr->lhs->tab_name,
                .col_name = expr->lhs->col_name,
            };

            // 未带限定符的 SELECT 别名优先于来源列。
            if (syntax_col.tab_name.empty()) {
                auto alias = std::find_if(aggs.begin(), aggs.end(), [&](const AggregateInfo &agg) {
                    return !agg.alias.empty() && agg.alias == syntax_col.col_name;
                });
                if (alias != aggs.end()) {
                    cond.source = HavingSource::AGGREGATE;
                    cond.index = static_cast<size_t>(alias - aggs.begin());
                } else {
                    TabCol resolved = resolve_column(scope, syntax_col);
                    auto group = std::find_if(group_by.begin(), group_by.end(), [&](const TabCol &col) {
                        return col.tab_name == resolved.tab_name && col.col_name == resolved.col_name;
                    });
                    if (group == group_by.end()) throw InternalError("failure");
                    cond.source = HavingSource::GROUP_COLUMN;
                    cond.index = static_cast<size_t>(group - group_by.begin());
                }
            } else {
                TabCol resolved = resolve_column(scope, syntax_col);
                auto group = std::find_if(group_by.begin(), group_by.end(), [&](const TabCol &col) {
                    return col.tab_name == resolved.tab_name && col.col_name == resolved.col_name;
                });
                if (group == group_by.end()) throw InternalError("failure");
                cond.source = HavingSource::GROUP_COLUMN;
                cond.index = static_cast<size_t>(group - group_by.begin());
            }
        } else {
            throw InternalError("failure");
        }

        if (is_null_test_op(cond.op)) {
            return make_bool_atom<HavingCondition>(std::move(cond));
        }

        const ColType lhs_type = result_type(cond);
        if (lhs_type == TYPE_FLOAT && cond.rhs.type == TYPE_INT) {
            cond.rhs.set_float(static_cast<float>(cond.rhs.int_val));
        } else {
            const bool both_numeric =
                (lhs_type == TYPE_INT || lhs_type == TYPE_FLOAT) &&
                (cond.rhs.type == TYPE_INT || cond.rhs.type == TYPE_FLOAT);
            if (lhs_type != cond.rhs.type && !both_numeric) {
                throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(cond.rhs.type));
            }
        }
        return make_bool_atom<HavingCondition>(std::move(cond));
}
