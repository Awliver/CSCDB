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
    std::vector<TabCol> arguments;
    std::string alias;
    bool is_star = false;
    ColType arg_type = TYPE_INT;
    int arg_len = sizeof(int);
    bool in_output = true;
    bool distinct = false;

    std::string to_string() const {
        std::string argument;
        if (arguments.empty() && !is_star) {
            argument = col.col_name;
        } else {
            for (size_t i = 0; i < arguments.size(); ++i) {
                if (i != 0) argument += ", ";
                argument += arguments[i].col_name;
            }
        }
        return ast::format_aggregate_call(type, argument, is_star, distinct);
    }

    ColType output_type() const { return ast::aggregate_result_type(type, arg_type); }
    int output_len() const { return ast::aggregate_result_length(type, arg_type, arg_len); }
};

enum class HavingSource {
    GROUP_COLUMN,
    AGGREGATE,
};

// 分析器直接解析 HAVING 的来源和槽位，执行器不再反解聚合函数字符串。
struct HavingCondition {
    HavingSource source = HavingSource::GROUP_COLUMN;
    size_t index = 0;
    CompOp op = OP_EQ;
    Value rhs;
};

using HavingExpr = BoolExpr<HavingCondition>;
using HavingExprPtr = BoolExprPtr<HavingCondition>;

/*
    TableBinding 用于区分连接中的不同关系实例。例如，同一张 employee
    物理表分别使用别名 e1 和 e2 时，两者属于不同的关系实例。
*/
struct TableBinding {
    std::string table_name;
    std::string binding_name;
};

class Query;

struct AnalyzedFrom {
    bool is_table = false;
    bool is_subquery = false;

    // 普通表或 LATERAL 派生表的叶节点信息。
    TableBinding table;
    std::shared_ptr<Query> subquery;

    // 非叶节点表示连接。
    JoinType join_type = INNER_JOIN;
    bool natural = false;
    bool lateral = false;
    std::shared_ptr<AnalyzedFrom> left;
    std::shared_ptr<AnalyzedFrom> right;
    ConditionExprPtr on_expr;
    std::vector<CoalescedJoinColumn> coalesced_cols;

    // 半连接/反连接的 bindings 只包含保留侧；all_bindings 包含实际读取的全部关系。
    std::vector<TableBinding> bindings;
    std::vector<TableBinding> all_bindings;

    // cols 包含所有可限定寻址列；output_cols 是 SELECT * 和上层自然连接
    // 可见的公开输出行类型。
    std::vector<ColMeta> cols;
    std::vector<ColMeta> output_cols;
};

// 当前 FROM 子树中可以引用的关系与列。
struct AnalyzeScope {
    std::vector<TableBinding> bindings;
    std::vector<ColMeta> cols;
    std::vector<ColMeta> output_cols;
};

class Query {
public:
    std::shared_ptr<ast::TreeNode> parse;
    std::shared_ptr<AnalyzedFrom> from;
    ConditionExprPtr where_expr;

    std::vector<TabCol> cols;
    std::vector<SetClause> set_clauses;
    std::vector<Value> values;

    std::vector<AggregateInfo> aggs;
    std::vector<TabCol> group_by_cols;
    HavingExprPtr having_expr;
    std::vector<std::pair<TabCol, ast::OrderByDir>> orders;

    bool has_limit = false;
    int limit_count = 0;
    int limit = -1;
    bool has_offset = false;
    int offset_count = 0;
    bool distinct = false;
    std::vector<std::string> sel_captions;

    std::shared_ptr<Query> union_left;
    std::shared_ptr<Query> union_right;
    ast::SetOpType set_op = ast::SetOpType::UNION;
    bool union_all = false;
    std::vector<ColMeta> union_output_cols;
    std::shared_ptr<Query> group_child;

    // 横向派生查询中引用外层行的 WHERE 条件。
    ConditionExprPtr correlated_expr;

    // 稳定的查询输出模式，供派生表、UNION 和 EXPLAIN 使用。
    std::vector<ColMeta> output_cols;

    bool explain_analyze = false;
    bool select_all = false;
    std::shared_ptr<Query> explain_query;
    Query() = default;
};

class Analyze {
private:
    SmManager *sm_manager_;
    size_t natural_id_ = 0;

public:
    explicit Analyze(SmManager *sm_manager) : sm_manager_(sm_manager) {}
    ~Analyze() = default;

    std::shared_ptr<Query> do_analyze(std::shared_ptr<ast::TreeNode> root);

private:
    struct AnalyzedFromResult {
        std::shared_ptr<AnalyzedFrom> node;
        AnalyzeScope scope;
    };

    TabCol check_column(const std::vector<ColMeta> &all_cols, TabCol target);
    std::shared_ptr<Query> analyze_query_expr(
        const std::shared_ptr<ast::QueryExpr> &expr,
        const AnalyzeScope *correlation_scope);
    std::shared_ptr<Query> analyze_query_group(
        const std::shared_ptr<ast::QueryGroup> &group,
        const AnalyzeScope *correlation_scope);
    std::shared_ptr<Query> analyze_union_expr(
        const std::shared_ptr<ast::UnionStmt> &set_op,
        const AnalyzeScope *correlation_scope);
    std::shared_ptr<Query> analyze_correlated_select(
        const std::shared_ptr<ast::SelectStmt> &select,
        const AnalyzeScope &outer_scope);
    void get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols);
    Condition convert_condition_atom(const std::shared_ptr<ast::BinaryExpr> &sv_cond);
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
    ConditionExprPtr analyze_conditions(const std::shared_ptr<ast::BoolExpr> &sv_expr,
                                        const AnalyzeScope &scope,
                                        bool allow_subquery = false);
    std::shared_ptr<Query> analyze_predicate_subquery(
        const std::shared_ptr<ast::QueryExpr> &subquery,
        const AnalyzeScope &outer_scope);
    ConditionExprPtr analyze_lateral_conditions(
        const std::shared_ptr<ast::BoolExpr> &sv_expr,
        const AnalyzeScope &local, const AnalyzeScope &outer,
        const AnalyzeScope &type_scope, bool *uses_outer);
    void check_condition_types(const AnalyzeScope &scope, std::vector<Condition> &conds);
    Value convert_sv_value(const std::shared_ptr<ast::Value> &sv_val);
    CompOp convert_sv_comp_op(ast::SvCompOp op);
    bool is_compatible_type(ColType lhs, ColType rhs);

    ColType get_col_type(const std::vector<ColMeta> &all_cols, const TabCol &col);
    AggregateInfo analyze_aggregate(ast::AggType type,
                                    const std::vector<std::shared_ptr<ast::Col>> &arguments,
                                    bool is_star, bool distinct,
                                    const std::string &alias,
                                    const AnalyzeScope &scope);
    void check_group_by_validity(const std::vector<TabCol> &sel_cols,
                                 const std::vector<AggregateInfo> &aggs,
                                 const std::vector<TabCol> &group_by);
    bool is_in_group_by(const TabCol &col, const std::vector<TabCol> &group_by);
    HavingExprPtr analyze_having_clause(
        const std::shared_ptr<ast::BoolExpr> &sv_expr,
        const std::vector<TabCol> &group_by,
        std::vector<AggregateInfo> &aggs,
        const AnalyzeScope &scope);
    void check_where_no_aggregate(const std::shared_ptr<ast::BoolExpr> &sv_expr);
    TabCol resolve_order_column(TabCol order_col,
                                const std::vector<TabCol> &sel_cols,
                                const std::vector<TabCol> &group_by_cols,
                                const std::vector<AggregateInfo> &aggs,
                                const std::vector<ColMeta> &all_cols);
    std::vector<ColMeta> infer_select_output_cols(const std::shared_ptr<Query> &query);
    ColMeta promote_union_col(const ColMeta &base, const ColMeta &incoming);
};
