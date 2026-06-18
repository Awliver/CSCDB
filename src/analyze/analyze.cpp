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
        query->tables = x->tabs;
        query->is_explain = x->is_explain;
        for (size_t i = 0; i < x->tabs.size(); ++i) {
            const std::string &real = x->tabs[i];
            std::string al = (i < x->aliases.size()) ? x->aliases[i] : "";
            query->alias2real[real] = real;
            if (!al.empty()) {
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
            std::string tn = sv_sel_col->tab_name;
            if (!tn.empty() && query->alias2real.count(tn)) tn = query->alias2real[tn];
            TabCol sel_col = {.tab_name = tn, .col_name = sv_sel_col->col_name};
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
            if (sv_agg->col) {
                agg.col = {.tab_name = sv_agg->col->tab_name, .col_name = sv_agg->col->col_name};
                if (!agg.col.tab_name.empty() && query->alias2real.count(agg.col.tab_name))
                    agg.col.tab_name = query->alias2real[agg.col.tab_name];
                agg.col = check_column(all_cols, agg.col);
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
                std::string tn = sv_sel_col->tab_name;
                if (!tn.empty() && query->alias2real.count(tn)) tn = query->alias2real[tn];
                agg.col = {tn, sv_sel_col->col_name};
                agg.col = check_column(all_cols, agg.col);
                agg.arg_type = get_col_type(all_cols, agg.col);
            }
            query->aggs.push_back(agg);
            query->sel_captions.push_back(!agg.alias.empty() ? agg.alias : agg.to_string());
        }

        for (auto &sv_gb : x->group_by_cols) {
            TabCol gb_col = {.tab_name = sv_gb->tab_name, .col_name = sv_gb->col_name};
            if (!gb_col.tab_name.empty() && query->alias2real.count(gb_col.tab_name))
                gb_col.tab_name = query->alias2real[gb_col.tab_name];
            gb_col = check_column(all_cols, gb_col);
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
                if (!order_col.tab_name.empty() && query->alias2real.count(order_col.tab_name))
                    order_col.tab_name = query->alias2real[order_col.tab_name];
                order_col = resolve_order_column(order_col, query->cols, query->group_by_cols,
                                                 query->aggs, all_cols);
                query->orders.emplace_back(order_col, sv_order->orderby_dir);
            }
        } else if (x->order) {
            TabCol order_col = {.tab_name = x->order->cols->tab_name, .col_name = x->order->cols->col_name};
            if (!order_col.tab_name.empty() && query->alias2real.count(order_col.tab_name))
                order_col.tab_name = query->alias2real[order_col.tab_name];
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
            query->sel_captions.push_back(sel_col.col_name);
        }

        get_clause(x->conds, query->conds);
        for (auto &cond : query->conds) {
            if (!cond.lhs_col.tab_name.empty() && query->alias2real.count(cond.lhs_col.tab_name))
                cond.lhs_col.tab_name = query->alias2real[cond.lhs_col.tab_name];
            if (!cond.is_rhs_val && !cond.rhs_col.tab_name.empty() && query->alias2real.count(cond.rhs_col.tab_name))
                cond.rhs_col.tab_name = query->alias2real[cond.rhs_col.tab_name];
        }
        check_clause(query->tables, query->conds);
        check_where_no_aggregate(x->conds);

        if (!query->is_explain) {
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

        // 查找该列元数据，若不存在抛 ColumnNotFoundError
        auto col_it = tab_meta.get_col(sv_set->col_name);

        // 类型提升
        if (col_it->type == TYPE_FLOAT && set.rhs.type == TYPE_INT) {
            set.rhs.set_float(static_cast<float>(set.rhs.int_val));
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
    }
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
