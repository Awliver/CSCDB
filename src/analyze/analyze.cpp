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

/**
 * @description: 分析器，进行语义分析和查询重写，需要检查不符合语义规定的部分
 * @param {shared_ptr<ast::TreeNode>} parse parser生成的结果集
 * @return {shared_ptr<Query>} Query
 */
std::shared_ptr<Query> Analyze::do_analyze(std::shared_ptr<ast::TreeNode> parse)
{
    std::shared_ptr<Query> query = std::make_shared<Query>();
    if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse))
    {
        // 处理表名（真实表名，按 FROM/JOIN 顺序）+ 题4 别名映射
        query->tables = x->tabs;
        query->is_explain = x->is_explain;
        query->limit = x->limit;
        for (size_t i = 0; i < x->tabs.size(); ++i) {
            const std::string &real = x->tabs[i];
            std::string al = (i < x->aliases.size()) ? x->aliases[i] : "";
            query->alias2real[real] = real;            // 允许用真表名直接引用
            if (!al.empty()) {
                query->alias2real[al] = real;          // 别名 -> 真表
                query->real2alias[real] = al;          // 真表 -> 别名（显示用）
            } else {
                query->real2alias[real] = real;        // 无别名则显示真名
            }
        }
        // 题4：记录是否 SELECT *（Project 输出 [*] 用）
        query->select_all = x->cols.empty();

        // 处理 target list；列的 tab_name 可能是别名，先解析为真表名
        for (auto &sv_sel_col : x->cols) {
            std::string tn = sv_sel_col->tab_name;
            if (!tn.empty() && query->alias2real.count(tn)) tn = query->alias2real[tn];
            TabCol sel_col = {.tab_name = tn, .col_name = sv_sel_col->col_name};
            sel_col.agg_type = sv_sel_col->agg_type;
            sel_col.alias = sv_sel_col->alias;
            query->cols.push_back(sel_col);
        }

        std::vector<ColMeta> all_cols;
        get_all_cols(query->tables, all_cols);
        if (query->cols.empty()) {
            // select all columns
            for (auto &col : all_cols) {
                TabCol sel_col = {.tab_name = col.tab_name, .col_name = col.name};
                query->cols.push_back(sel_col);
            }
        } else {
            // infer table name from column name
            for (auto &sel_col : query->cols) {
                if (sel_col.agg_type != 0 && sel_col.col_name == "*") {
                    // COUNT(*)：绑定到首列（仅作扫描载体）
                    if (sel_col.alias.empty()) sel_col.alias = "count(*)";
                    sel_col.tab_name = all_cols[0].tab_name;
                    sel_col.col_name = all_cols[0].name;
                    continue;
                }
                sel_col = check_column(all_cols, sel_col);  // 列元数据校验
            }
        }
        //处理where条件（WHERE + JOIN..ON 已并入 x->conds）；条件里别名先解析为真表名
        get_clause(x->conds, query->conds);
        for (auto &cond : query->conds) {
            if (!cond.lhs_col.tab_name.empty() && query->alias2real.count(cond.lhs_col.tab_name))
                cond.lhs_col.tab_name = query->alias2real[cond.lhs_col.tab_name];
            if (!cond.is_rhs_val && !cond.rhs_col.tab_name.empty() && query->alias2real.count(cond.rhs_col.tab_name))
                cond.rhs_col.tab_name = query->alias2real[cond.rhs_col.tab_name];
        }
        check_clause(query->tables, query->conds);
        // 等值常量经连接等值传递：col_a=col_b 且 col_a=常量 → 派生 col_b=常量，
        // 使连接内侧也能走索引而不必全表扫。EXPLAIN 不执行，跳过以保持计划树输出。
        if (!query->is_explain) {
            for (int pass = 0; pass < 3; ++pass) {
                bool changed = false;
                std::vector<Condition> derived;
                for (const auto &j : query->conds) {
                    if (j.op != OP_EQ || j.is_rhs_val) continue;   // 仅列=列的连接等值
                    for (const auto &cc : query->conds) {
                        if (cc.op != OP_EQ || !cc.is_rhs_val) continue;   // 仅列=常量
                        const TabCol *src = nullptr; const TabCol *dst = nullptr;
                        if (cc.lhs_col.tab_name == j.lhs_col.tab_name && cc.lhs_col.col_name == j.lhs_col.col_name) {
                            src = &j.lhs_col; dst = &j.rhs_col;
                        } else if (cc.lhs_col.tab_name == j.rhs_col.tab_name && cc.lhs_col.col_name == j.rhs_col.col_name) {
                            src = &j.rhs_col; dst = &j.lhs_col;
                        }
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
        set.is_arith = sv_set->is_arith;   // 题9：算术增量 v=v±字面量
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
        // 处理insert 的values值
        if (x->cols.empty()) {
            for (auto &sv_val : x->vals) {
                query->values.push_back(convert_sv_value(sv_val));
            }
        } else {
            // 题9：列清单 insert——按表列顺序重排 values；列须覆盖全部列且数量匹配
            TabMeta &tab = sm_manager_->db_.get_table(x->tab_name);
            if (x->cols.size() != x->vals.size()) {
                throw InvalidValueCountError();
            }
            for (auto &col : tab.cols) {
                size_t k = 0;
                for (; k < x->cols.size(); ++k) {
                    if (x->cols[k] == col.name) break;
                }
                if (k == x->cols.size()) {
                    throw ColumnNotFoundError(col.name);  // 列清单未覆盖该列
                }
                query->values.push_back(convert_sv_value(x->vals[k]));
            }
        }
    } else {
        // do nothing
    }
    query->parse = std::move(parse);
    return query;
}


TabCol Analyze::check_column(const std::vector<ColMeta> &all_cols, TabCol target) {
    if (target.tab_name.empty()) {
        // Table name not specified, infer table name from column name
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
    } else {
        /** TODO: Make sure target column exists */

    }
    return target;
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
