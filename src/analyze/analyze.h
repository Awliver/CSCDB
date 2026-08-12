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

// 聚合函数表
struct AggregateInfo {
    ast::AggType type = ast::AGG_COUNT;
    TabCol col;
    std::string alias;
    bool is_star = false;
    ColType arg_type = TYPE_INT;
    int arg_len = sizeof(int);
    bool in_output = true;     // 是否出现在最终输出中
    bool distinct = false;   // 支持 COUNT(DISTINCT col) 与 COUNT(DISTINCT (col))

    std::string to_string() const {
        return ast::format_aggregate_call(type, col.col_name, is_star, distinct);
    }

    ColType output_type() const { return ast::aggregate_result_type(type, arg_type); }
    int output_len() const { return ast::aggregate_result_length(type, arg_type, arg_len); }
};

enum class HavingSource {
    GROUP_COLUMN,
    AGGREGATE,
};

/* Analyzer-resolved HAVING operand.  Slots replace the old aggregate-name
 * strings, so execution never reparses SQL text or guesses between aliases. */
struct HavingCondition {
    HavingSource source = HavingSource::GROUP_COLUMN;
    size_t index = 0;
    CompOp op = OP_EQ;
    Value rhs;
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
    std::vector<ColMeta> cols;
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
    std::vector<HavingCondition> having_conds;
    std::vector<std::pair<TabCol, ast::OrderByDir>> orders;

    bool has_limit = false;
    int limit_count = 0;
    int limit = -1;  // 题10 简单 LIMIT 兼容
    std::vector<std::string> sel_captions;

    // 题6 UNION
    std::vector<std::shared_ptr<Query>> union_queries;
    std::vector<ColMeta> union_output_cols;
    std::string union_alias;

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
    AnalyzedFromResult analyze_from(const std::shared_ptr<ast::FromExpr> &from);
    AnalyzeScope merge_scopes(const AnalyzeScope &left, const AnalyzeScope &right);
    TabCol resolve_column(const AnalyzeScope &scope, TabCol target);
    std::vector<Condition> analyze_conditions(
        const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds,
        const AnalyzeScope &scope);
    void check_condition_types(const AnalyzeScope &scope, std::vector<Condition> &conds);
    Value convert_sv_value(const std::shared_ptr<ast::Value> &sv_val);
    CompOp convert_sv_comp_op(ast::SvCompOp op);
    bool is_compatible_type(ColType lhs, ColType rhs);

    ColType get_col_type(const std::vector<ColMeta> &all_cols, const TabCol &col);
    AggregateInfo analyze_aggregate(ast::AggType type,
                                    const std::shared_ptr<ast::Col> &column,
                                    bool is_star, bool distinct,
                                    const std::string &alias,
                                    const AnalyzeScope &scope);
    void check_group_by_validity(const std::vector<TabCol> &sel_cols,
                                 const std::vector<AggregateInfo> &aggs,
                                 const std::vector<TabCol> &group_by);
    bool is_in_group_by(const TabCol &col, const std::vector<TabCol> &group_by);
    void analyze_having_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds,
                               const std::vector<TabCol> &group_by,
                               std::vector<AggregateInfo> &aggs,
                               const AnalyzeScope &scope,
                               std::vector<HavingCondition> &result);
    void check_where_no_aggregate(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds);
    TabCol resolve_order_column(TabCol order_col,
                                const std::vector<TabCol> &sel_cols,
                                const std::vector<TabCol> &group_by_cols,
                                const std::vector<AggregateInfo> &aggs,
                                const std::vector<ColMeta> &all_cols);
    std::vector<ColMeta> infer_select_output_cols(const std::shared_ptr<Query> &query);
    ColMeta promote_union_col(const ColMeta &base, const ColMeta &incoming);
};
