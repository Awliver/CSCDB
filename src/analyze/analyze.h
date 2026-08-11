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

/*
    之所以专门设置一个TableBinding，是为了区分连接中的不同关系实例
    FROM employee e1
    JOIN employee e2 ON e1.manager_id = e2.id
    虽然真实表均为employee，但e1、e2是两个实例，若不加以区分会出现语义错误
*/
struct TableBinding {
    // FROM student AS s
    // 若没有别名，则table_name = binding_name
    std::string table_name;    // 数据库中的真实表名，“student”
    std::string binding_name;  // 当前 SQL 中引用这张表所使用的名称，“s”  
};


struct AnalyzedFrom {
    bool is_table = false;

    // 叶节点表示表
    TableBinding table;

    // 非叶节点表示连接
    JoinType join_type = INNER_JOIN;
    std::shared_ptr<AnalyzedFrom> left;
    std::shared_ptr<AnalyzedFrom> right;
    std::vector<Condition> on_conds; // 每个 JOIN 节点自行保存 ON 条件

    // 当前子树中可见的关系实例
    std::vector<TableBinding> bindings;
};
/*
    表示当前 FROM 子树中可以引用哪些关系和列
    FROM a
    JOIN b ON a.id = c.id
    JOIN c ON b.id = c.id
    第一条连接的作用域不包含 c, 此时报错
*/
 struct AnalyzeScope {
    std::vector<TableBinding> bindings;
    std::vector<std::string> table_names;
    std::vector<ColMeta> cols;
};


class Query{
    public:
    bool is_outer_join = false;
    bool requires_join_tree_planner = false;
    std::shared_ptr<ast::TreeNode> parse;
    // 不再把 ON、WHERE当作同一种条件
    std::shared_ptr<AnalyzedFrom> from; // 完整的语义 jointree
    std::vector<Condition> where_conds; // JOIN 完成后的 WHERE 条件

    // 旧 Planner 的兼容视图：按连接树后序排列的 ON 条件 + WHERE 条件。
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
    struct AnalyzedFromResult {
        std::shared_ptr<AnalyzedFrom> node;
        AnalyzeScope scope;
        bool has_outer_join = false;
        bool has_repeated_table = false;
    };

    TabCol check_column(const std::vector<ColMeta> &all_cols, TabCol target);
    void get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols);
    void get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds);
    void check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds);
    AnalyzedFromResult analyze_from(const std::shared_ptr<ast::FromExpr> &from);
    AnalyzedFromResult analyze_legacy_from(const std::vector<std::string> &tabs,
                                           const std::vector<std::string> &aliases);
    AnalyzeScope merge_scopes(const AnalyzeScope &left, const AnalyzeScope &right);
    TabCol resolve_column(const AnalyzeScope &scope, TabCol target);
    std::vector<Condition> analyze_conditions(
        const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds,
        const AnalyzeScope &scope);
    void check_condition_types(const AnalyzeScope &scope, std::vector<Condition> &conds);
    void collect_join_conditions(const std::shared_ptr<AnalyzedFrom> &from,
                                 std::vector<Condition> &conds);
    TabCol to_physical_column(const AnalyzeScope &scope, TabCol col);
    Condition to_physical_condition(const AnalyzeScope &scope, Condition cond);
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
