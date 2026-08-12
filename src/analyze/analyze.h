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

class Query;

struct AnalyzedFrom {
    bool is_table = false;
    bool is_lateral_subquery = false;

    // 叶节点表示表
    TableBinding table;

    // LATERAL 派生表叶节点。subquery 的输出在本层统一重命名为 table.binding_name。
    std::shared_ptr<Query> subquery;

    // 非叶节点表示连接
    JoinType join_type = INNER_JOIN;
    bool natural = false;
    bool lateral = false;
    std::shared_ptr<AnalyzedFrom> left;
    std::shared_ptr<AnalyzedFrom> right;
    std::vector<Condition> on_conds; // 每个 JOIN 节点自行保存 ON 条件
    std::vector<CoalescedJoinColumn> coalesced_cols;

    // 当前子树对上层可见的关系实例；SEMI/ANTI 只包含保留侧。
    std::vector<TableBinding> bindings;

    // 当前子树实际读取的全部关系实例，用于全局别名唯一性检查和规划统计。
    std::vector<TableBinding> all_bindings;

    // cols 包含所有可限定寻址列以及 NATURAL 的内部合并列；output_cols 是
    // SELECT * / 上层 NATURAL JOIN 所看到的公开 row type。
    std::vector<ColMeta> cols;
    std::vector<ColMeta> output_cols;
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
    std::vector<ColMeta> cols;
    std::vector<ColMeta> output_cols;
};


class Query{
    public:
    std::shared_ptr<ast::TreeNode> parse;
    // 不再把 ON、WHERE当作同一种条件
    std::shared_ptr<AnalyzedFrom> from; // 完整的语义 jointree
    std::vector<Condition> where_conds; // JOIN 完成后的 WHERE 条件

    std::vector<TabCol> cols;
    std::vector<SetClause> set_clauses;
    std::vector<Value> values;

    // 题5 聚合
    std::vector<AggregateInfo> aggs;
    std::vector<TabCol> group_by_cols;
    std::vector<Condition> having_conds;
    std::vector<std::pair<TabCol, ast::OrderByDir>> orders;

    bool has_limit = false;
    int limit_count = 0;
    int limit = -1;  // 题10 简单 LIMIT 兼容
    std::vector<std::string> sel_captions;

    // 题6 UNION
    std::vector<std::shared_ptr<Query>> union_queries;
    std::vector<ColMeta> union_output_cols;
    std::string union_alias;

    // LATERAL 派生查询中引用外层行的 WHERE 条件。Planner 将它们保留为
    // 参数化 Filter，不能下推给普通 Scan。
    std::vector<Condition> correlated_conds;

    // 稳定的查询输出 schema，供派生表、UNION 和 EXPLAIN 使用，不再从物理表反推。
    std::vector<ColMeta> output_cols;

    // 题4 EXPLAIN
    bool explain_analyze = false;
    bool select_all = false;
    std::shared_ptr<Query> explain_query;
    Query(){}
};

class Analyze
{
private:
    SmManager *sm_manager_;
    size_t natural_id_ = 0;
public:
    Analyze(SmManager *sm_manager) : sm_manager_(sm_manager){}
    ~Analyze(){}

    std::shared_ptr<Query> do_analyze(std::shared_ptr<ast::TreeNode> root);

private:
    struct AnalyzedFromResult {
        std::shared_ptr<AnalyzedFrom> node;
        AnalyzeScope scope;
    };

    TabCol check_column(const std::vector<ColMeta> &all_cols, TabCol target);
    void get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols);
    void get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds);
    void check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds);
    AnalyzedFromResult analyze_from(const std::shared_ptr<ast::FromExpr> &from,
                                    const AnalyzeScope *outer_scope = nullptr);
    AnalyzedFromResult analyze_lateral_ref(const std::shared_ptr<ast::LateralRef> &lateral,
                                           const AnalyzeScope *outer_scope);
    AnalyzeScope merge_scopes(const AnalyzeScope &left, const AnalyzeScope &right);
    std::vector<TableBinding> merge_all_bindings(const std::vector<TableBinding> &left,
                                                 const std::vector<TableBinding> &right);
    TabCol resolve_column(const AnalyzeScope &scope, TabCol target);
    TabCol resolve_lateral_column(const AnalyzeScope &local, const AnalyzeScope &outer,
                                  TabCol target, bool *is_outer);
    std::vector<Condition> analyze_conditions(
        const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds,
        const AnalyzeScope &scope);
    void check_condition_types(const AnalyzeScope &scope, std::vector<Condition> &conds);
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
                             const AnalyzeScope &scope);
    void check_where_no_aggregate(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds);
    TabCol resolve_order_column(TabCol order_col,
                                const std::vector<TabCol> &sel_cols,
                                const std::vector<TabCol> &group_by_cols,
                                const std::vector<AggregateInfo> &aggs,
                                const std::vector<ColMeta> &all_cols);
    std::vector<ColMeta> infer_select_output_cols(const std::shared_ptr<Query> &query);
    ColMeta promote_union_col(const ColMeta &base, const ColMeta &incoming);
};
