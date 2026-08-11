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

namespace {

/* int 列 vs float 字面量比较的语义保持改写结果 */
enum class IntFloatRewrite { CONVERTED, ALWAYS_TRUE, ALWAYS_FALSE };

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
        query->is_explain = true;
        query->explain_analyze = e->analyze;
        query->explain_query = do_analyze(e->select);
    }
    else if (auto u = std::dynamic_pointer_cast<ast::UnionStmt>(parse))
    {
        if (u->selects.size() < 2) {
            throw InternalError("failure");
        }
        query->union_alias = u->alias;
        for (auto &sel : u->selects) {
            auto child = do_analyze(sel);
            query->union_queries.push_back(child);
        }

        std::vector<ColMeta> common_cols = infer_select_output_cols(query->union_queries[0]);
        if (common_cols.empty()) throw InternalError("failure");
        for (size_t i = 1; i < query->union_queries.size(); i++) {
            auto cols = infer_select_output_cols(query->union_queries[i]);
            if (cols.size() != common_cols.size()) {
                throw InternalError("failure");
            }
            for (size_t j = 0; j < common_cols.size(); j++) {
                common_cols[j] = promote_union_col(common_cols[j], cols[j]);
            }
        }

        int offset = 0;
        for (auto &col : common_cols) {
            col.tab_name = query->union_alias;
            col.offset = offset;
            offset += col.len;
            query->union_output_cols.push_back(col);
            query->cols.push_back({query->union_alias, col.name});
            query->sel_captions.push_back(col.name);
        }

        for (auto &sv_order : u->orders) {
            TabCol order_col = {.tab_name = sv_order->cols->tab_name, .col_name = sv_order->cols->col_name};
            bool found = false;
            for (auto &col : query->union_output_cols) {
                if (col.name == order_col.col_name &&
                    (order_col.tab_name.empty() || order_col.tab_name == query->union_alias)) {
                    found = true;
                    order_col.tab_name = query->union_alias;
                    break;
                }
            }
            if (!found) throw InternalError("failure");
            query->orders.emplace_back(order_col, sv_order->orderby_dir);
        }
        query->has_limit = u->has_limit;
        query->limit_count = u->limit_count;
    }
    else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse))
    {
        auto from_result = x->from != nullptr
                               ? analyze_from(x->from)
                               : analyze_legacy_from(x->tabs, x->aliases);
        query->from = from_result.node;
        query->tables = from_result.scope.table_names;
        query->is_outer_join = from_result.has_outer_join;
        query->requires_join_tree_planner = from_result.has_outer_join ||
                                            from_result.has_repeated_table; // 标记当前无法安全处理的查询，包含外连接或自连接时设为 true
        query->is_explain = x->is_explain;
        for (const auto &binding : from_result.scope.bindings) {
            const std::string &real = binding.table_name;
            const std::string &al = binding.binding_name;
            query->alias2real[real] = real;
            if (al != real) {
                query->alias2real[al] = real;
                query->real2alias[real] = al;
            } else {
                query->real2alias[real] = real;
            }
        }
        query->select_all = x->cols.empty() && x->aggs.empty();

        std::vector<ColMeta> all_cols;
        get_all_cols(query->tables, all_cols);

        // 普通列
        for (auto &sv_sel_col : x->cols) {
            if (sv_sel_col->agg_type != 0) continue;  // 题10 简单聚合走下方转换
            TabCol sel_col = resolve_column(
                from_result.scope, 
                {.tab_name = sv_sel_col->tab_name, .col_name = sv_sel_col->col_name});
            sel_col = to_physical_column(from_result.scope, std::move(sel_col));
            sel_col.alias = sv_sel_col->alias;        // 决赛：col AS alias（输出列名用别名）
            query->cols.push_back(sel_col);
        }
        if (query->cols.empty() && x->aggs.empty()) {
            for (auto &col : all_cols) {
                query->cols.push_back({col.tab_name, col.name});
            }
        } else if (!query->cols.empty()) {
            for (auto &sel_col : query->cols) {
                sel_col = check_column(all_cols, sel_col);
            }
        }

        // p7 AggExpr 列表
        for (auto &sv_agg : x->aggs) {
            AggregateInfo agg;
            agg.type = sv_agg->agg_type;
            agg.is_star = sv_agg->is_star;
            agg.alias = sv_agg->alias;
            agg.distinct = sv_agg->distinct;
            if (sv_agg->col) {
                agg.col = resolve_column(
                    from_result.scope,
                    {.tab_name = sv_agg->col->tab_name, .col_name = sv_agg->col->col_name});
                agg.col = to_physical_column(from_result.scope, std::move(agg.col));
                agg.arg_type = get_col_type(all_cols, agg.col);
                if (sv_agg->agg_type != ast::AGG_COUNT &&
                    agg.arg_type != TYPE_INT && agg.arg_type != TYPE_FLOAT &&
                    !((sv_agg->agg_type == ast::AGG_MIN || sv_agg->agg_type == ast::AGG_MAX) &&
                      agg.arg_type == TYPE_STRING)) {
                    throw InternalError("failure");
                }
            } else {
                agg.arg_type = TYPE_INT;
            }
            query->aggs.push_back(agg);
            query->sel_captions.push_back(agg.alias.empty() ? (sv_agg->is_star ? "count(*)" : agg.col.col_name) : agg.alias);
        }
        // 题10 简单聚合（Col.agg_type）转 AggregateInfo
        for (auto &sv_sel_col : x->cols) {
            if (sv_sel_col->agg_type == 0) continue;
            AggregateInfo agg;
            static const ast::AggType map[] = {ast::AGG_COUNT, ast::AGG_COUNT, ast::AGG_MAX, ast::AGG_MIN, ast::AGG_SUM};
            agg.type = map[sv_sel_col->agg_type];
            agg.is_star = (sv_sel_col->col_name == "*");
            agg.alias = sv_sel_col->alias;
            if (agg.is_star) {
                agg.arg_type = TYPE_INT;
            } else {
                agg.col = resolve_column(
                    from_result.scope,
                    {.tab_name = sv_sel_col->tab_name, .col_name = sv_sel_col->col_name});
                agg.col = to_physical_column(from_result.scope, std::move(agg.col));
                agg.arg_type = get_col_type(all_cols, agg.col);
            }
            query->aggs.push_back(agg);
            query->sel_captions.push_back(!agg.alias.empty() ? agg.alias : agg.to_string());
        }

        for (auto &sv_gb : x->group_by_cols) {
            TabCol gb_col = resolve_column(
                from_result.scope,
                {.tab_name = sv_gb->tab_name, .col_name = sv_gb->col_name});
            gb_col = to_physical_column(from_result.scope, std::move(gb_col));
            query->group_by_cols.push_back(gb_col);
        }

        get_clause(x->having_conds, query->having_conds);
        for (auto &cond : query->having_conds) {
            if (!cond.lhs_col.tab_name.empty() && query->alias2real.count(cond.lhs_col.tab_name))
                cond.lhs_col.tab_name = query->alias2real[cond.lhs_col.tab_name];
        }

        if (!x->orders.empty()) {
            for (auto &sv_order : x->orders) {
                TabCol order_col = {.tab_name = sv_order->cols->tab_name, .col_name = sv_order->cols->col_name};
                if (!order_col.tab_name.empty()) {
                    order_col = resolve_column(from_result.scope, std::move(order_col));
                    order_col = to_physical_column(from_result.scope, std::move(order_col));
                }
                order_col = resolve_order_column(order_col, query->cols, query->group_by_cols,
                                                 query->aggs, all_cols);
                query->orders.emplace_back(order_col, sv_order->orderby_dir);
            }
        } else if (x->order) {
            TabCol order_col = {.tab_name = x->order->cols->tab_name, .col_name = x->order->cols->col_name};
            if (!order_col.tab_name.empty()) {
                order_col = resolve_column(from_result.scope, std::move(order_col));
                order_col = to_physical_column(from_result.scope, std::move(order_col));
            }
            order_col = resolve_order_column(order_col, query->cols, query->group_by_cols,
                                             query->aggs, all_cols);
            query->orders.emplace_back(order_col, x->order->orderby_dir);
        }

        query->has_limit = x->has_limit || x->limit >= 0;
        query->limit_count = x->has_limit ? x->limit_count : (x->limit >= 0 ? x->limit : 0);
        query->limit = query->has_limit ? query->limit_count : x->limit;

        check_group_by_validity(query->cols, query->aggs, query->group_by_cols);
        if (!query->having_conds.empty()) {
            check_having_clause(x->having_conds, query->group_by_cols, query->aggs, all_cols);
        }
        for (auto &sel_col : query->cols) {
            query->sel_captions.push_back(sel_col.alias.empty() ? sel_col.col_name : sel_col.alias);
        }

        const auto &where_ast = x->from != nullptr ? x->where_conds : x->conds;
        query->where_conds = analyze_conditions(where_ast, from_result.scope);
        check_where_no_aggregate(where_ast);

        // 为旧 Planner 生成物理表名形式的兼容条件。规范语义仍保存在
        // Query::from 和 Query::where_conds 中，不能从这里反推 ON/WHERE 归属。
        std::vector<Condition> semantic_conds;
        collect_join_conditions(query->from, semantic_conds);
        semantic_conds.insert(semantic_conds.end(), query->where_conds.begin(),
                              query->where_conds.end());
        query->conds.clear();
        query->conds.reserve(semantic_conds.size());
        for (auto cond : semantic_conds) {
            query->conds.push_back(to_physical_condition(from_result.scope, std::move(cond)));
        }

        // 等值条件传播对内连接安全，但对外连接可能改变结果；因此只有当只有没有 JOIN 时，才执行条件传播
        if (!query->is_explain && !query->requires_join_tree_planner) {
            for (int pass = 0; pass < 3; ++pass) {
                bool changed = false;
                std::vector<Condition> derived;
                for (const auto &j : query->conds) {
                    if (j.op != OP_EQ || j.is_rhs_val) continue;
                    for (const auto &cc : query->conds) {
                        if (cc.op != OP_EQ || !cc.is_rhs_val) continue;
                        const TabCol *dst = nullptr;
                        if (cc.lhs_col.tab_name == j.lhs_col.tab_name && cc.lhs_col.col_name == j.lhs_col.col_name)
                            dst = &j.rhs_col;
                        else if (cc.lhs_col.tab_name == j.rhs_col.tab_name && cc.lhs_col.col_name == j.rhs_col.col_name)
                            dst = &j.lhs_col;
                        if (dst == nullptr) continue;
                        bool exists = false;
                        for (const auto &e : query->conds)
                            if (e.op == OP_EQ && e.is_rhs_val && e.lhs_col.tab_name == dst->tab_name &&
                                e.lhs_col.col_name == dst->col_name) { exists = true; break; }
                        for (const auto &e : derived)
                            if (e.lhs_col.tab_name == dst->tab_name && e.lhs_col.col_name == dst->col_name) { exists = true; break; }
                        if (exists) continue;
                        Condition nc;
                        nc.lhs_col = *dst;
                        nc.op = OP_EQ;
                        nc.is_rhs_val = true;
                        nc.rhs_val = cc.rhs_val;
                        nc.rhs_is_float_lit = cc.rhs_is_float_lit;
                        derived.push_back(nc);
                        changed = true;
                    }
                }
                for (auto &d : derived) query->conds.push_back(d);
                if (!changed) break;
            }
        }
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

    // 处理 WHERE 条件
    get_clause(x->conds, query->conds);
    check_clause({x->tab_name}, query->conds);

    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        //处理where条件
        get_clause(x->conds, query->conds);
        check_clause({x->tab_name}, query->conds);
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
    return query;
}

// 递归分析 jointree
Analyze::AnalyzedFromResult Analyze::analyze_from(const std::shared_ptr<ast::FromExpr> &from) {
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
        result.scope.bindings.push_back(binding);
        result.scope.table_names.push_back(table->tab_name);
        for (auto col : meta.cols) {
            // 语义树使用关系实例名，确保 e1/e2 这样的自连接实例不会混淆。
            col.tab_name = binding.binding_name;
            result.scope.cols.push_back(std::move(col));
        }
        return result;
    }

    auto join = std::dynamic_pointer_cast<ast::JoinExpr>(from); // 处理 JOIN 节点
    if (join == nullptr) {
        throw InternalError("Unexpected FROM expression");
    }

    auto left = analyze_from(join->left); // 递归分析左子树
    auto right = analyze_from(join->right); // 递归分析右子树

    AnalyzedFromResult result;
    result.scope = merge_scopes(left.scope, right.scope); // 合并左右作用域
    result.has_outer_join = left.has_outer_join || right.has_outer_join ||
                            join->type == LEFT_JOIN || join->type == RIGHT_JOIN ||
                            join->type == FULL_JOIN;
    result.has_repeated_table = left.has_repeated_table || right.has_repeated_table; // 检查
    for (const auto &left_binding : left.scope.bindings) {
        for (const auto &right_binding : right.scope.bindings) {
            if (left_binding.table_name == right_binding.table_name) {
                result.has_repeated_table = true;
            }
        }
    }

    if (join->type == CROSS_JOIN && !join->on_conds.empty()) {
        throw InternalError("CROSS JOIN cannot have an ON clause");
    }
    if (join->type != CROSS_JOIN && join->on_conds.empty()) {
        throw InternalError("JOIN requires an ON clause");
    }

    result.node = std::make_shared<AnalyzedFrom>();
    result.node->is_table = false;
    result.node->join_type = join->type;
    result.node->left = std::move(left.node);
    result.node->right = std::move(right.node);
    check_where_no_aggregate(join->on_conds);
    result.node->on_conds = analyze_conditions(join->on_conds, result.scope);
    result.node->bindings = result.scope.bindings;
    return result;
}

// 用于兼容旧式快速解析器，多个表使用逗号连接使用 CROSS JOIN
Analyze::AnalyzedFromResult Analyze::analyze_legacy_from(
    const std::vector<std::string> &tabs, const std::vector<std::string> &aliases) {
    if (tabs.empty()) {
        throw InternalError("SELECT has no table");
    }

    std::shared_ptr<ast::FromExpr> from;
    for (size_t i = 0; i < tabs.size(); ++i) {
        const std::string alias = i < aliases.size() ? aliases[i] : "";
        auto table = std::make_shared<ast::TableRef>(tabs[i], alias);
        if (from == nullptr) {
            from = table;
        } else {
            from = std::make_shared<ast::JoinExpr>(
                CROSS_JOIN, std::move(from), table,
                std::vector<std::shared_ptr<ast::BinaryExpr>>{});
        }
    }
    return analyze_from(from);
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
    result.table_names.insert(result.table_names.end(), right.table_names.begin(),
                              right.table_names.end());
    result.cols.insert(result.cols.end(), right.cols.begin(), right.cols.end());
    return result; // 左表与右表合并作用域
}

// 统一的列解析，给col绑定一个唯一确定的table后返回
TabCol Analyze::resolve_column(const AnalyzeScope &scope, TabCol target) {
    if (target.tab_name.empty()) { // 未指明表名前缀
        std::string binding_name;
        for (const auto &col : scope.cols) { // 搜索当前作用域的所有列
            if (col.name != target.col_name) continue;
            if (!binding_name.empty() && binding_name != col.tab_name) {
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
    for (const auto &col : scope.cols) { // 对应表是否存在该列
        if (col.tab_name == target.tab_name && col.name == target.col_name) {
            return target;
        }
    }
    throw ColumnNotFoundError(target.col_name);
}

// 为 ON 和 WHERE 建立统一的条件分析流程
std::vector<Condition> Analyze::analyze_conditions(
    const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds,
    const AnalyzeScope &scope) {
    std::vector<Condition> conds;
    get_clause(sv_conds, conds); // 转换为 condition
    for (auto &cond : conds) {
        cond.lhs_col = resolve_column(scope, cond.lhs_col); // 解析左侧列
        if (!cond.is_rhs_val) {
            cond.rhs_col = resolve_column(scope, cond.rhs_col); // 解析右侧列
        }
    }
    check_condition_types(scope, conds); // 检查左右类型是否匹配
    return conds;
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
        ColType rhs_type;
        if (cond.is_rhs_val) {
            if (lhs_type == TYPE_FLOAT && cond.rhs_val.type == TYPE_INT) {
                cond.rhs_val.set_float(static_cast<float>(cond.rhs_val.int_val)); // FLOAT 与 INT 比较时，将 INT 提升为 FLOAT
            } else if (lhs_type == TYPE_FLOAT && cond.rhs_val.type == TYPE_FLOAT &&
                       std::isnan(cond.rhs_val.float_val)) {
                if (cond.op == OP_NE) continue;
                cond.op = OP_LT;
                cond.rhs_val.set_float(-INFINITY);
            } else if (lhs_type == TYPE_INT && cond.rhs_val.type == TYPE_FLOAT) {
                IntFloatRewrite rewrite = rewrite_int_col_float_val(cond);
                if (rewrite == IntFloatRewrite::ALWAYS_TRUE) continue;
                if (rewrite == IntFloatRewrite::ALWAYS_FALSE) {
                    cond.op = OP_LT;
                    cond.rhs_val.set_int(INT32_MIN);
                }
            }
            cond.rhs_val.init_raw(lhs_col.len);
            rhs_type = cond.rhs_val.type;
        } else {
            rhs_type = find_col(cond.rhs_col).type;
        }
        if (lhs_type != rhs_type) {
            throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
        }
        kept.push_back(std::move(cond));
    }
    conds.swap(kept);
}

// 递归收集 JOIN 条件
void Analyze::collect_join_conditions(const std::shared_ptr<AnalyzedFrom> &from,
                                      std::vector<Condition> &conds) {
    if (from == nullptr || from->is_table) return;
    collect_join_conditions(from->left, conds);
    collect_join_conditions(from->right, conds);
    conds.insert(conds.end(), from->on_conds.begin(), from->on_conds.end());
}

// 返回给定列的数据库中的列
TabCol Analyze::to_physical_column(const AnalyzeScope &scope, TabCol col) {
    for (const auto &binding : scope.bindings) {
        if (binding.binding_name == col.tab_name) {
            col.tab_name = binding.table_name;
            return col;
        }
    }
    throw TableNotFoundError(col.tab_name);
}

Condition Analyze::to_physical_condition(const AnalyzeScope &scope, Condition cond) {
    cond.lhs_col = to_physical_column(scope, std::move(cond.lhs_col));
    if (!cond.is_rhs_val) {
        cond.rhs_col = to_physical_column(scope, std::move(cond.rhs_col));
    }
    return cond;
}

std::vector<ColMeta> Analyze::infer_select_output_cols(const std::shared_ptr<Query> &query) {
    std::vector<ColMeta> result;
    int offset = 0;
    if (!query->aggs.empty() || !query->group_by_cols.empty()) {
        for (auto &gc : query->group_by_cols) {
            auto tab = sm_manager_->db_.get_table(gc.tab_name);
            auto col_it = tab.get_col(gc.col_name);
            ColMeta col = *col_it;
            col.offset = offset;
            offset += col.len;
            result.push_back(col);
        }
        for (auto &agg : query->aggs) {
            if (!agg.in_output) continue;
            ColMeta col;
            col.name = agg.alias.empty() ? (agg.is_star ? "count(*)" : agg.col.col_name) : agg.alias;
            col.tab_name = agg.col.tab_name;
            if (agg.type == ast::AGG_COUNT) {
                col.type = TYPE_INT;
                col.len = sizeof(int);
            } else if (agg.type == ast::AGG_AVG) {
                col.type = TYPE_FLOAT;
                col.len = sizeof(float);
            } else {
                col.type = agg.arg_type;
                col.len = agg.arg_type == TYPE_INT ? sizeof(int) : sizeof(float);
            }
            col.offset = offset;
            offset += col.len;
            result.push_back(col);
        }
        return result;
    }

    for (auto &tc : query->cols) {
        auto tab = sm_manager_->db_.get_table(tc.tab_name);
        auto col_it = tab.get_col(tc.col_name);
        ColMeta col = *col_it;
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
        std::string name = agg.alias.empty() ? (agg.is_star ? "count(*)" : agg.col.col_name) : agg.alias;
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

void Analyze::get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds) {
    conds.clear();
    for (auto &expr : sv_conds) {
        Condition cond;
        cond.lhs_col = {.tab_name = expr->lhs->tab_name, .col_name = expr->lhs->col_name};
        cond.op = convert_sv_comp_op(expr->op);
        if (auto rhs_val = std::dynamic_pointer_cast<ast::Value>(expr->rhs)) {
            cond.is_rhs_val = true;
            cond.rhs_val = convert_sv_value(rhs_val);
            cond.rhs_is_float_lit = (std::dynamic_pointer_cast<ast::FloatLit>(rhs_val) != nullptr);
        } else if (auto rhs_col = std::dynamic_pointer_cast<ast::Col>(expr->rhs)) {
            cond.is_rhs_val = false;
            cond.rhs_col = {.tab_name = rhs_col->tab_name, .col_name = rhs_col->col_name};
        }
        conds.push_back(cond);
    }
}

void Analyze::check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds) {
    // auto all_cols = get_all_cols(tab_names);
    std::vector<ColMeta> all_cols;
    get_all_cols(tab_names, all_cols);
    // Get raw values in where clause
    std::vector<Condition> kept;
    kept.reserve(conds.size());
    for (auto &cond : conds) {
        // Infer table name from column name
        cond.lhs_col = check_column(all_cols, cond.lhs_col);
        if (!cond.is_rhs_val) {
            cond.rhs_col = check_column(all_cols, cond.rhs_col);
        }
        TabMeta &lhs_tab = sm_manager_->db_.get_table(cond.lhs_col.tab_name);
        auto lhs_col = lhs_tab.get_col(cond.lhs_col.col_name);
        ColType lhs_type = lhs_col->type;
        ColType rhs_type;
        if (cond.is_rhs_val) {
            // 类型提升
            if (lhs_type == TYPE_FLOAT && cond.rhs_val.type == TYPE_INT) {
                cond.rhs_val.set_float(static_cast<float>(cond.rhs_val.int_val));
            } else if (lhs_type == TYPE_FLOAT && cond.rhs_val.type == TYPE_FLOAT &&
                       std::isnan(cond.rhs_val.float_val)) {
                // float 列 vs NaN 字面量（wire NaN 参数经 NAN 关键字进来）：执行器的
                // 三值比较（<、> 皆假则判相等）不符合 IEEE NaN 语义，须在此改写——
                // <> 恒真（删除条件），其余恒假（col < -inf 对任何 float 值恒假）。
                if (cond.op == OP_NE) continue;
                cond.op = OP_LT;
                cond.rhs_val.set_float(-INFINITY);
            } else if (lhs_type == TYPE_INT && cond.rhs_val.type == TYPE_FLOAT) {
                // int 列 vs float 字面量：按数值比较语义改写为纯 int 比较
                // （直接截断字面量会改变 <、> 的语义，如 k > 0.5 ≠ k > 0）
                IntFloatRewrite rw = rewrite_int_col_float_val(cond);
                if (rw == IntFloatRewrite::ALWAYS_TRUE) continue;   // 恒真条件直接删除
                if (rw == IntFloatRewrite::ALWAYS_FALSE) {
                    cond.op = OP_LT;                                 // k < INT32_MIN 恒假
                    cond.rhs_val.set_int(INT32_MIN);
                }
            }
            cond.rhs_val.init_raw(lhs_col->len);
            rhs_type = cond.rhs_val.type;
        } else {
            TabMeta &rhs_tab = sm_manager_->db_.get_table(cond.rhs_col.tab_name);
            auto rhs_col = rhs_tab.get_col(cond.rhs_col.col_name);
            rhs_type = rhs_col->type;
        }
        if (lhs_type != rhs_type) {
            throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
        }
        kept.push_back(std::move(cond));
    }
    conds.swap(kept);
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

bool Analyze::is_in_group_by(const TabCol &col, const std::vector<TabCol> &group_by) {
    for (auto &g : group_by) {
        if (g.tab_name == col.tab_name && g.col_name == col.col_name)
            return true;
    }
    return false;
}

bool Analyze::is_aggregate_argument(const TabCol &col, const std::vector<AggregateInfo> &aggs) {
    for (auto &agg : aggs) {
        if (agg.col.col_name == col.col_name &&
            (col.tab_name.empty() || agg.col.tab_name == col.tab_name))
            return true;
    }
    return false;
}

void Analyze::check_where_no_aggregate(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds) {
    for (auto &expr : sv_conds) {
        if (expr->lhs->agg_type != 0) throw InternalError("failure");
        const std::string &n = expr->lhs->col_name;
        if (n.find('(') != std::string::npos) throw InternalError("failure");
    }
}

void Analyze::check_group_by_validity(const std::vector<TabCol> &sel_cols,
                                      const std::vector<AggregateInfo> &aggs,
                                      const std::vector<TabCol> &group_by) {
    bool has_agg = !aggs.empty();
    bool has_plain_col = !sel_cols.empty();

    if (!has_agg && group_by.empty()) return;  // 无聚合且无 GROUP BY，无需检查

    if (group_by.empty()) {
        // R2: 无 GROUP BY，但不能有普通列
        if (has_plain_col) {
            throw InternalError("failure");
        }
        return;
    }

    // R1: 有 GROUP BY，检查每个非聚合参数列都在 GROUP BY 中
    for (auto &col : sel_cols) {
        if (!is_in_group_by(col, group_by) && !is_aggregate_argument(col, aggs)) {
            throw InternalError("failure");
        }
    }
}

// 从聚合函数字符串（如 "count(*)", "max(score)"）解析聚合信息
static bool parse_agg_string(const std::string &s, ast::AggType &type, std::string &col_name, bool &is_star) {
    if (s.size() < 4) return false;
    size_t lp = s.find('(');
    size_t rp = s.find(')');
    if (lp == std::string::npos || rp == std::string::npos || rp != s.size() - 1) return false;
    std::string name = s.substr(0, lp);
    std::string arg = s.substr(lp + 1, rp - lp - 1);
    if (name == "count") type = ast::AGG_COUNT;
    else if (name == "max") type = ast::AGG_MAX;
    else if (name == "min") type = ast::AGG_MIN;
    else if (name == "sum") type = ast::AGG_SUM;
    else if (name == "avg") type = ast::AGG_AVG;
    else return false;
    if (arg == "*") {
        is_star = true;
        col_name = "";
    } else {
        is_star = false;
        col_name = arg;
    }
    return true;
}

void Analyze::check_having_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds,
                                  const std::vector<TabCol> &group_by,
                                  std::vector<AggregateInfo> &aggs,
                                  const std::vector<ColMeta> &all_cols) {
    for (auto &expr : sv_conds) {
        TabCol lhs_col = {.tab_name = expr->lhs->tab_name, .col_name = expr->lhs->col_name};
        // 检查 lhs_col 是否在 GROUP BY 中
        bool valid = is_in_group_by(lhs_col, group_by);
        if (!valid) {
            // 检查是否是聚合别名或聚合函数字符串
            for (auto &agg : aggs) {
                if (agg.alias == lhs_col.col_name ||
                    (lhs_col.col_name == agg.col.col_name && !agg.is_star) ||
                    lhs_col.col_name == agg.to_string()) {
                    valid = true;
                    break;
                }
            }
        }
        if (!valid) {
            // 尝试解析为聚合函数字符串，并添加到 aggs
            ast::AggType agg_type;
            std::string agg_col_name;
            bool is_star;
            if (parse_agg_string(lhs_col.col_name, agg_type, agg_col_name, is_star)) {
                AggregateInfo agg;
                agg.type = agg_type;
                agg.is_star = is_star;
                agg.alias = "";
                if (!is_star) {
                    agg.col = {.tab_name = "", .col_name = agg_col_name};
                    agg.col = check_column(all_cols, agg.col);
                    agg.arg_type = get_col_type(all_cols, agg.col);
                    if (agg_type == ast::AGG_COUNT) {
                        agg.arg_type = TYPE_INT;
                    } else if (agg_type != ast::AGG_COUNT && agg.arg_type != TYPE_INT &&
                               agg.arg_type != TYPE_FLOAT &&
                               !((agg_type == ast::AGG_MIN || agg_type == ast::AGG_MAX) &&
                                 agg.arg_type == TYPE_STRING)) {
                        throw InternalError("failure");
                    }
                } else {
                    agg.col = {.tab_name = "", .col_name = ""};
                    agg.arg_type = TYPE_INT;
                }
                agg.in_output = false;  // HAVING 中的聚合不出现在输出中
                aggs.push_back(agg);
                valid = true;
            }
        }
        if (!valid) {
            throw InternalError("failure");
        }
    }
}
