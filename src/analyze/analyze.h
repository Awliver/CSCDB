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

#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "parser/parser.h"
#include "system/sm.h"
#include "common/common.h"

struct AggregateInfo {
    ast::AggType type;
    TabCol col;
    std::string alias;
    bool is_star;
    ColType arg_type;
    bool in_output = true;
    bool distinct = false;   // 决赛：原生 COUNT(DISTINCT col)

    std::string to_string() const {
        std::string name;
        switch (type) {
            case ast::AGG_COUNT: name = "count"; break;
            case ast::AGG_MAX:   name = "max"; break;
            case ast::AGG_MIN:   name = "min"; break;
            case ast::AGG_SUM:   name = "sum"; break;
            case ast::AGG_AVG:   name = "avg"; break;
        }
        name += "(" + (is_star ? std::string("*") : col.col_name) + ")";
        return name;
    }
};

class Query{
    public:
    std::shared_ptr<ast::TreeNode> parse;
    std::vector<Condition> conds;
    std::vector<TabCol> cols;
    std::vector<std::string> tables;
    std::vector<SetClause> set_clauses;
    std::vector<Value> values;

    // 题5 聚合
    std::vector<AggregateInfo> aggs;
    std::vector<TabCol> group_by_cols;
    std::vector<Condition> having_conds;
    std::vector<std::pair<TabCol, ast::OrderByDir>> orders;

    // Planner 生成的逻辑优化结果。原始 conds 保持不变，供 EXPLAIN 等后续
    // 阶段使用；物理规划直接消费已下推的单表谓词和剩余连接谓词。
    std::map<std::string, std::vector<Condition>> table_filters;
    std::vector<Condition> join_conditions;
    // 多表非 SELECT * 查询会为每个表保存投影（即使恰好保留整表列），以便
    // 在 Join 之前显式裁列；单表查询只保存能实际裁列的投影。
    std::map<std::string, std::vector<TabCol>> table_projections;

    bool has_limit = false;
    int limit_count = 0;
    int limit = -1;  // 题10 简单 LIMIT 兼容
    std::vector<std::string> sel_captions;

    // 题6 UNION
    std::vector<std::shared_ptr<Query>> union_queries;
    std::vector<ColMeta> union_output_cols;
    std::string union_alias;

    // 题4 EXPLAIN
    bool is_explain = false;
    bool explain_analyze = false;
    bool select_all = false;
    std::shared_ptr<Query> explain_query;
    std::map<std::string, std::string> alias2real;
    std::map<std::string, std::string> real2alias;

    Query(){}
};

class Analyze
{
private:
    SmManager *sm_manager_;
public:
    Analyze(SmManager *sm_manager) : sm_manager_(sm_manager){}
    ~Analyze(){}

    std::shared_ptr<Query> do_analyze(std::shared_ptr<ast::TreeNode> root);

private:
    TabCol check_column(const std::vector<ColMeta> &all_cols, TabCol target);
    void get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols);
    void get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds);
    void check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds);
    Value convert_sv_value(const std::shared_ptr<ast::Value> &sv_val);
    CompOp convert_sv_comp_op(ast::SvCompOp op);
    bool is_compatible_type(ColType lhs, ColType rhs);

    ColType get_col_type(const std::vector<ColMeta> &all_cols, const TabCol &col);
    void check_group_by_validity(const std::vector<TabCol> &sel_cols,
                                 const std::vector<AggregateInfo> &aggs,
                                 const std::vector<TabCol> &group_by);
    bool is_in_group_by(const TabCol &col, const std::vector<TabCol> &group_by);
    bool is_aggregate_argument(const TabCol &col, const std::vector<AggregateInfo> &aggs);
    void check_having_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds,
                             const std::vector<TabCol> &group_by,
                             std::vector<AggregateInfo> &aggs,
                             const std::vector<ColMeta> &all_cols);
    void check_where_no_aggregate(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds);
    TabCol resolve_order_column(TabCol order_col,
                                const std::vector<TabCol> &sel_cols,
                                const std::vector<TabCol> &group_by_cols,
                                const std::vector<AggregateInfo> &aggs,
                                const std::vector<ColMeta> &all_cols);
    std::vector<ColMeta> infer_select_output_cols(const std::shared_ptr<Query> &query);
    ColMeta promote_union_col(const ColMeta &base, const ColMeta &incoming);
};
