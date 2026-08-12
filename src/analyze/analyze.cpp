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
        auto from_result = analyze_from(x->from);
        query->from = from_result.node;
        query->select_all = x->cols.empty() && x->aggs.empty();

        // Analyzer 之后列限定符始终是 binding_name。物理表名只保存在
        // AnalyzedFrom 的叶节点中，由 ScanPlan 在访问存储时使用。
        const std::vector<ColMeta> &all_cols = from_result.scope.cols;

        // 普通列
        for (auto &sv_sel_col : x->cols) {
            if (sv_sel_col->agg_type != 0) continue;  // 题10 简单聚合走下方转换
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
                agg.arg_type = get_col_type(all_cols, agg.col);
            }
            query->aggs.push_back(agg);
            query->sel_captions.push_back(!agg.alias.empty() ? agg.alias : agg.to_string());
        }

        for (auto &sv_gb : x->group_by_cols) {
            TabCol gb_col = resolve_column(
                from_result.scope,
                {.tab_name = sv_gb->tab_name, .col_name = sv_gb->col_name});
            query->group_by_cols.push_back(gb_col);
        }

        get_clause(x->having_conds, query->having_conds);
        if (!x->orders.empty()) {
            for (auto &sv_order : x->orders) {
                TabCol order_col = {.tab_name = sv_order->cols->tab_name, .col_name = sv_order->cols->col_name};
                if (!order_col.tab_name.empty()) {
                    order_col = resolve_column(from_result.scope, std::move(order_col));
                }
                order_col = resolve_order_column(order_col, query->cols, query->group_by_cols,
                                                 query->aggs, from_result.scope.output_cols);
                query->orders.emplace_back(order_col, sv_order->orderby_dir);
            }
        } else if (x->order) {
            TabCol order_col = {.tab_name = x->order->cols->tab_name, .col_name = x->order->cols->col_name};
            if (!order_col.tab_name.empty()) {
                order_col = resolve_column(from_result.scope, std::move(order_col));
            }
            order_col = resolve_order_column(order_col, query->cols, query->group_by_cols,
                                             query->aggs, from_result.scope.output_cols);
            query->orders.emplace_back(order_col, x->order->orderby_dir);
        }

        query->has_limit = x->has_limit || x->limit >= 0;
        query->limit_count = x->has_limit ? x->limit_count : (x->limit >= 0 ? x->limit : 0);
        query->limit = query->has_limit ? query->limit_count : x->limit;

        check_group_by_validity(query->cols, query->aggs, query->group_by_cols);
        if (!query->having_conds.empty()) {
            check_having_clause(x->having_conds, query->group_by_cols, query->aggs,
                                from_result.scope);
        }
        for (auto &sel_col : query->cols) {
            query->sel_captions.push_back(sel_col.alias.empty() ? sel_col.col_name : sel_col.alias);
        }

        query->where_conds = analyze_conditions(x->where_conds, from_result.scope);
        check_where_no_aggregate(x->where_conds);
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
    get_clause(x->conds, query->where_conds);
    check_clause({x->tab_name}, query->where_conds);

    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        //处理where条件
        get_clause(x->conds, query->where_conds);
        check_clause({x->tab_name}, query->where_conds);
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
    if (join->type == CROSS_JOIN && !join->on_conds.empty()) {
        throw InternalError("CROSS JOIN cannot have an ON clause");
    }
    if (!join->natural && join->type != CROSS_JOIN && join->on_conds.empty() && !join->on_true) {
        throw InternalError("JOIN requires an ON clause");
    }
    if (join->natural && !join->on_conds.empty()) {
        throw InternalError("NATURAL JOIN cannot have an ON clause");
    }
    if (join->natural && join->type == CROSS_JOIN) {
        throw InternalError("NATURAL CROSS JOIN is not supported");
    }
    if (join->lateral && join->natural) {
        throw InternalError("NATURAL LATERAL JOIN is not supported");
    }
    if (join->lateral && join->type != INNER_JOIN && join->type != CROSS_JOIN &&
        join->type != LEFT_JOIN) {
        throw InternalError("LATERAL JOIN supports only INNER, CROSS, and LEFT");
    }

    result.node = std::make_shared<AnalyzedFrom>();
    result.node->is_table = false;
    result.node->join_type = join->type;
    result.node->natural = join->natural;
    result.node->lateral = join->lateral;
    result.node->left = left.node;
    result.node->right = right.node;
    result.node->all_bindings = all_bindings;

    if (join->natural) {
        // NATURAL 只比较左右公开 row type。公共名在任一侧重复时没有唯一
        // 对应列，按歧义处理。
        std::map<std::string, std::vector<ColMeta>> left_by_name;
        std::map<std::string, std::vector<ColMeta>> right_by_name;
        for (const auto &col : left.scope.output_cols) left_by_name[col.name].push_back(col);
        for (const auto &col : right.scope.output_cols) right_by_name[col.name].push_back(col);

        std::set<std::string> common_names;
        const std::string synthetic_binding = "\x1f" "natural_" + std::to_string(++natural_id_);
        for (const auto &left_col : left.scope.output_cols) {
            auto found = right_by_name.find(left_col.name);
            if (found == right_by_name.end() || common_names.count(left_col.name) != 0) continue;
            if (left_by_name[left_col.name].size() != 1 || found->second.size() != 1) {
                throw AmbiguousColumnError(left_col.name);
            }
            const auto &right_col = found->second.front();
            if (left_col.type != right_col.type ||
                (left_col.type != TYPE_STRING && left_col.len != right_col.len)) {
                throw IncompatibleTypeError(coltype2str(left_col.type), coltype2str(right_col.type));
            }

            Condition cond;
            cond.lhs_col = {left_col.tab_name, left_col.name};
            cond.op = OP_EQ;
            cond.is_rhs_val = false;
            cond.rhs_col = {right_col.tab_name, right_col.name};
            result.node->on_conds.push_back(cond);

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
            common_names.insert(left_col.name);
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
        check_where_no_aggregate(join->on_conds);
        result.node->on_conds = analyze_conditions(join->on_conds, input_scope);

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
    // 先取得子查询自己的名字空间，用 local-first 规则识别 WHERE 中的相关列。
    auto local_from = analyze_from(lateral->subquery->from);
    std::vector<std::shared_ptr<ast::BinaryExpr>> local_where;
    std::vector<std::shared_ptr<ast::BinaryExpr>> correlated_where;

    AnalyzeScope type_scope = local_from.scope;
    type_scope.cols.insert(type_scope.cols.end(), outer.cols.begin(), outer.cols.end());
    type_scope.output_cols.insert(type_scope.output_cols.end(), outer.output_cols.begin(),
                                  outer.output_cols.end());

    for (const auto &sv_cond : lateral->subquery->where_conds) {
        std::vector<Condition> one;
        get_clause({sv_cond}, one);
        bool lhs_outer = false;
        bool rhs_outer = false;
        one[0].lhs_col = resolve_lateral_column(local_from.scope, outer,
                                                one[0].lhs_col, &lhs_outer);
        if (!one[0].is_rhs_val) {
            one[0].rhs_col = resolve_lateral_column(local_from.scope, outer,
                                                    one[0].rhs_col, &rhs_outer);
        }
        check_condition_types(type_scope, one);
        // 防御性处理：若后续规整规则消去某个恒真谓词，无需再分类。
        if (one.empty()) continue;
        if (lhs_outer || rhs_outer) {
            correlated_where.push_back(sv_cond);
        } else {
            local_where.push_back(sv_cond);
        }
    }

    // 复用完整 SELECT Analyzer；仅把相关 WHERE 留给参数化 Filter。SELECT/GROUP/
    // HAVING/内部 ON 中的外层引用仍会按普通未知列报错，这是当前明确的支持边界。
    auto local_select = std::make_shared<ast::SelectStmt>(*lateral->subquery);
    local_select->where_conds = std::move(local_where);
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
    for (const auto &sv_cond : correlated_where) {
        std::vector<Condition> one;
        get_clause({sv_cond}, one);
        bool lhs_outer = false;
        bool rhs_outer = false;
        one[0].lhs_col = resolve_lateral_column(final_local, outer, one[0].lhs_col, &lhs_outer);
        if (!one[0].is_rhs_val) {
            one[0].rhs_col = resolve_lateral_column(final_local, outer,
                                                    one[0].rhs_col, &rhs_outer);
        }
        if (!lhs_outer && !rhs_outer) {
            throw InternalError("LATERAL correlated predicate lost its outer reference");
        }
        check_condition_types(final_type_scope, one);
        if (one.empty()) continue;
        subquery->correlated_conds.push_back(std::move(one[0]));
    }

    AnalyzedFromResult result;
    result.node = std::make_shared<AnalyzedFrom>();
    result.node->is_lateral_subquery = true;
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

// 统一的列解析，给col绑定一个唯一确定的table后返回
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
        for (auto &gc : query->group_by_cols) {
            ColMeta col = find_col(gc);
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
                col.len = (agg.arg_type == TYPE_STRING) ? find_col(agg.col).len :
                          (agg.arg_type == TYPE_INT ? sizeof(int) : sizeof(float));
            }
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
            bool rewritten_to_self = false;
            // 类型提升
            if (lhs_type == TYPE_FLOAT && cond.rhs_val.type == TYPE_INT) {
                cond.rhs_val.set_float(static_cast<float>(cond.rhs_val.int_val));
            } else if (lhs_type == TYPE_FLOAT && cond.rhs_val.type == TYPE_FLOAT &&
                       std::isnan(cond.rhs_val.float_val)) {
                // float 列 vs NaN 字面量（wire NaN 参数经 NAN 关键字进来）：执行器的
                // 三值比较（<、> 皆假则判相等）不符合 IEEE NaN 语义，须在此改写——
                // <> 恒真（删除条件），其余恒假（col < -inf 对任何 float 值恒假）。
                if (cond.op == OP_NE) {
                    rewrite_true_for_non_null(cond);
                    rewritten_to_self = true;
                } else {
                    cond.op = OP_LT;
                    cond.rhs_val.set_float(-INFINITY);
                }
            } else if (lhs_type == TYPE_INT && cond.rhs_val.type == TYPE_FLOAT) {
                // int 列 vs float 字面量：按数值比较语义改写为纯 int 比较
                // （直接截断字面量会改变 <、> 的语义，如 k > 0.5 ≠ k > 0）
                IntFloatRewrite rw = rewrite_int_col_float_val(cond);
                if (rw == IntFloatRewrite::ALWAYS_TRUE) {
                    rewrite_true_for_non_null(cond);
                    rewritten_to_self = true;
                } else if (rw == IntFloatRewrite::ALWAYS_FALSE) {
                    cond.op = OP_LT;                                 // k < INT32_MIN 恒假
                    cond.rhs_val.set_int(INT32_MIN);
                }
            }
            if (rewritten_to_self) {
                rhs_type = lhs_type;
            } else {
                cond.rhs_val.init_raw(lhs_col->len);
                rhs_type = cond.rhs_val.type;
            }
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
                                  const AnalyzeScope &scope) {
    for (auto &expr : sv_conds) {
        // 当前 AggExecutor 的 HAVING 右操作数是标量字面量；列列形式若放行会
        // 读取未初始化 Value，也可能绕过 SEMI/ANTI 输出作用域。
        if (std::dynamic_pointer_cast<ast::Value>(expr->rhs) == nullptr) {
            throw InternalError("failure");
        }
        TabCol lhs_col = {.tab_name = expr->lhs->tab_name, .col_name = expr->lhs->col_name};
        ast::AggType parsed_agg_type;
        std::string parsed_agg_col;
        bool parsed_agg_star = false;
        const bool lhs_is_agg = parse_agg_string(lhs_col.col_name, parsed_agg_type,
                                                 parsed_agg_col, parsed_agg_star);
        // 普通 HAVING 列先按公开 row type 解析，再和已绑定的 GROUP BY 比较；
        // 聚合别名/函数文本不是 FROM 列，留给下方聚合分支识别。
        TabCol resolved_lhs = lhs_col;
        bool resolved_as_input = false;
        if (!lhs_is_agg) {
            try {
                resolved_lhs = resolve_column(scope, lhs_col);
                resolved_as_input = true;
            } catch (const ColumnNotFoundError &) {
                if (!lhs_col.tab_name.empty()) throw;
            }
        }
        bool valid = resolved_as_input && is_in_group_by(resolved_lhs, group_by);
        if (!valid) {
            // 检查是否是聚合别名或聚合函数字符串
            for (auto &agg : aggs) {
                const bool unqualified = lhs_col.tab_name.empty();
                const bool matches_alias = unqualified && !agg.alias.empty() &&
                                           agg.alias == lhs_col.col_name;
                const bool matches_function = lhs_col.col_name == agg.to_string() &&
                    (lhs_col.tab_name.empty() ||
                     (!agg.is_star && lhs_col.tab_name == agg.col.tab_name));
                const bool matches_argument = !agg.is_star &&
                    lhs_col.col_name == agg.col.col_name &&
                    (lhs_col.tab_name.empty() || lhs_col.tab_name == agg.col.tab_name);
                if (matches_alias || matches_function || matches_argument) {
                    valid = true;
                    break;
                }
            }
        }
        if (!valid) {
            // 尝试解析为聚合函数字符串，并添加到 aggs
            if (lhs_is_agg) {
                AggregateInfo agg;
                agg.type = parsed_agg_type;
                agg.is_star = parsed_agg_star;
                agg.alias = "";
                if (!parsed_agg_star) {
                    agg.col = {.tab_name = lhs_col.tab_name, .col_name = parsed_agg_col};
                    agg.col = resolve_column(scope, agg.col);
                    agg.arg_type = get_col_type(scope.cols, agg.col);
                    if (parsed_agg_type == ast::AGG_COUNT) {
                        agg.arg_type = TYPE_INT;
                    } else if (parsed_agg_type != ast::AGG_COUNT && agg.arg_type != TYPE_INT &&
                               agg.arg_type != TYPE_FLOAT &&
                               !((parsed_agg_type == ast::AGG_MIN || parsed_agg_type == ast::AGG_MAX) &&
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
            // 非聚合形式必须是公开 GROUP BY 列。这里显式解析 qualifier，
            // 防止 SEMI/ANTI 已隐藏的输入侧通过同名聚合参数蒙混过关。
            lhs_col = resolve_column(scope, lhs_col);
            valid = is_in_group_by(lhs_col, group_by);
        }
        if (!valid) {
            throw InternalError("failure");
        }
    }
}
