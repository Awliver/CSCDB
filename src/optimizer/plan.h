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
#include <memory>
#include <string>
#include <vector>
#include "parser/ast.h"
#include "parser/parser.h"
#include "analyze/analyze.h"

typedef enum PlanTag{
    T_Invalid = 1,
    T_Help,
    T_ShowTable,
    T_ShowIndex,
    T_DescTable,
    T_CreateTable,
    T_DropTable,
    T_CreateIndex,
    T_DropIndex,
    T_SetKnob,
    T_Insert,
    T_Update,
    T_Delete,
    T_select,
    T_Explain,
    T_Transaction_begin,
    T_Transaction_commit,
    T_Transaction_abort,
    T_Transaction_rollback,
    T_SeqScan,
    T_IndexScan,
    T_NestLoop,
    T_IndexNestLoop,
    T_SortMerge,    // sort merge join
    T_Filter,
    T_Sort,
    T_Projection,
    T_Aggregation, // 聚合计划节点标签
    T_Limit,
    T_Distinct,
    T_Union,
    T_Rename,
    T_CorrelatedFilter,
    T_SubqueryFilter
} PlanTag;

// 查询执行计划
class Plan
{
public:
    PlanTag tag;
    virtual ~Plan() = default;
};

class ScanPlan : public Plan
{
    public:
        ScanPlan(PlanTag tag, SmManager *sm_manager, std::string tab_name, std::string binding_name,
                 ConditionExprPtr predicate, std::vector<Condition> access_conditions,
                 std::vector<std::string> index_col_names)
        {
            Plan::tag = tag;
            tab_name_ = std::move(tab_name);
            binding_name_ = std::move(binding_name);
            predicate_ = std::move(predicate);
            access_conditions_ = std::move(access_conditions);
            TabMeta &tab = sm_manager->db_.get_table(tab_name_);
            cols_ = tab.cols;
            for (auto &col : cols_) col.tab_name = binding_name_;
            len_ = cols_.back().offset + cols_.back().len;
            index_col_names_ = index_col_names;
        
        }
        ~ScanPlan(){}
        // 以下变量同ScanExecutor中的变量
        std::string tab_name_;                     // 物理表名
        std::string binding_name_;                 // SQL 中的关系实例名（别名），支持自连接
        std::vector<ColMeta> cols_;                
        // predicate_ 是查询语义的唯一来源，扫描输出前必须完整复核。
        ConditionExprPtr predicate_;
        // access_conditions_ 只是从顶层正向 AND 中安全提取的原子，
        // 仅用于选索引和构造 B+树边界，不能代替 predicate_。
        std::vector<Condition> access_conditions_;
        size_t len_;                               
        std::vector<std::string> index_col_names_;
    
};

class JoinPlan : public Plan
{
    public:
        JoinPlan(PlanTag tag, JoinType join_type, std::shared_ptr<Plan> left,
                 std::shared_ptr<Plan> right, ConditionExprPtr on_predicate,
                 bool natural = false, bool lateral = false,
                 std::vector<CoalescedJoinColumn> coalesced_cols = {})
        {
            // 分离逻辑连接类型和执行算法
            Plan::tag = tag; // 执行算法 T_NestLoop, T_IndexNestLoop
            type = join_type; // 逻辑类型 INNER, LEFT, RIGHT, FULL, CROSS
            left_ = std::move(left);
            right_ = std::move(right);
            on_predicate_ = std::move(on_predicate);
            natural_ = natural;
            lateral_ = lateral;
            coalesced_cols_ = std::move(coalesced_cols);
        }
        ~JoinPlan(){}
        // 左节点
        std::shared_ptr<Plan> left_;
        // 右节点
        std::shared_ptr<Plan> right_;
        // 连接条件
        ConditionExprPtr on_predicate_;
        // 逻辑连接类型，与 tag 表示的物理算法分离
        JoinType type;
        bool natural_ = false;
        bool lateral_ = false;
        std::vector<CoalescedJoinColumn> coalesced_cols_;
};

/*
    以前条件分为表内条件和表外条件，这个模型无法表示外连接后的 WHERE
    必须保留为独立 Filter，不能塞进 ON 或任意下推到 Scan。
*/
    class FilterPlan : public Plan
{
    public:
        FilterPlan(PlanTag tag, std::shared_ptr<Plan> subplan, ConditionExprPtr predicate)
        {
            Plan::tag = tag;
            subplan_ = std::move(subplan);
            predicate_ = std::move(predicate);
        }
        ~FilterPlan() {}
        std::shared_ptr<Plan> subplan_;
        ConditionExprPtr predicate_;
};

class ProjectionPlan : public Plan
{
    public:
        ProjectionPlan(PlanTag tag, std::shared_ptr<Plan> subplan, std::vector<TabCol> sel_cols)
        {
            Plan::tag = tag;
            subplan_ = std::move(subplan);
            sel_cols_ = std::move(sel_cols);
        }
        ~ProjectionPlan(){}
        std::shared_ptr<Plan> subplan_;
        std::vector<TabCol> sel_cols_;
        
};

class DistinctPlan : public Plan
{
    public:
        explicit DistinctPlan(std::shared_ptr<Plan> subplan)
            : subplan_(std::move(subplan)) {
            Plan::tag = T_Distinct;
        }
        std::shared_ptr<Plan> subplan_;
};

// 派生表只改变输出限定符/列名，不改变记录布局。
class RenamePlan : public Plan
{
    public:
        RenamePlan(std::shared_ptr<Plan> subplan, std::vector<ColMeta> output_cols)
            : subplan_(std::move(subplan)), output_cols_(std::move(output_cols)) {
            Plan::tag = T_Rename;
        }
        std::shared_ptr<Plan> subplan_;
        std::vector<ColMeta> output_cols_;
};

// LATERAL 子查询中引用外层行的 WHERE。它必须在 Portal 中绑定外层行上下文，
// 不能退化成普通 Filter 或下推给 Scan。
class CorrelatedFilterPlan : public Plan
{
    public:
        CorrelatedFilterPlan(std::shared_ptr<Plan> subplan, ConditionExprPtr predicate)
            : subplan_(std::move(subplan)), predicate_(std::move(predicate)) {
            Plan::tag = T_CorrelatedFilter;
        }
        std::shared_ptr<Plan> subplan_;
        ConditionExprPtr predicate_;
};

// A WHERE filter containing EXISTS or IN(subquery).  Subqueries are kept in
// atom visitation order so the Portal can build one restartable executor per
// predicate leaf while retaining the enclosing boolean-expression tree.
class SubqueryFilterPlan : public Plan
{
    public:
        SubqueryFilterPlan(std::shared_ptr<Plan> subplan,
                           ConditionExprPtr predicate,
                           std::vector<std::shared_ptr<Plan>> subquery_plans)
            : subplan_(std::move(subplan)), predicate_(std::move(predicate)),
              subquery_plans_(std::move(subquery_plans)) {
            Plan::tag = T_SubqueryFilter;
        }
        std::shared_ptr<Plan> subplan_;
        ConditionExprPtr predicate_;
        std::vector<std::shared_ptr<Plan>> subquery_plans_;
};

class SortPlan : public Plan
{
    public:
        SortPlan(PlanTag tag, std::shared_ptr<Plan> subplan, TabCol sel_col, bool is_desc)
        {
            Plan::tag = tag;
            subplan_ = std::move(subplan);
            sort_cols_.emplace_back(sel_col, is_desc);
        }
        SortPlan(PlanTag tag, std::shared_ptr<Plan> subplan, std::vector<std::pair<TabCol, bool>> sort_cols)
        {
            Plan::tag = tag;
            subplan_ = std::move(subplan);
            sort_cols_ = std::move(sort_cols);
        }
        ~SortPlan(){}
        std::shared_ptr<Plan> subplan_;
        std::vector<std::pair<TabCol, bool>> sort_cols_;
        
};
// 专门的聚合计划
class AggPlan : public Plan
{
    public:
        AggPlan(PlanTag tag, std::shared_ptr<Plan> subplan, std::vector<TabCol> group_cols,
                std::vector<AggregateInfo> agg_exprs, HavingExprPtr having_expr,
                std::vector<ColMeta> output_cols)
        {
            Plan::tag = tag;
            subplan_ = std::move(subplan);
            group_cols_ = std::move(group_cols);
            agg_exprs_ = std::move(agg_exprs);
            having_expr_ = std::move(having_expr);
            output_cols_ = std::move(output_cols);
        }
        ~AggPlan(){}
        std::shared_ptr<Plan> subplan_;
        std::vector<TabCol> group_cols_;
        std::vector<AggregateInfo> agg_exprs_;
        HavingExprPtr having_expr_;
        std::vector<ColMeta> output_cols_;
};

class LimitPlan : public Plan
{
    public:
        LimitPlan(PlanTag tag, std::shared_ptr<Plan> subplan, size_t limit,
                  size_t offset = 0)
        {
            Plan::tag = tag;
            subplan_ = std::move(subplan);
            limit_ = limit;
            offset_ = offset;
        }
        ~LimitPlan(){}
        std::shared_ptr<Plan> subplan_;
        size_t limit_;
        size_t offset_ = 0;
};

class UnionPlan : public Plan
{
    public:
        UnionPlan(PlanTag tag, std::shared_ptr<Plan> left, std::shared_ptr<Plan> right,
                  std::vector<ColMeta> output_cols, ast::SetOpType op, bool all)
        {
            Plan::tag = tag;
            subplans_.push_back(std::move(left));
            subplans_.push_back(std::move(right));
            output_cols_ = std::move(output_cols);
            op_ = op;
            all_ = all;
        }
        UnionPlan(PlanTag tag, std::shared_ptr<Plan> left, std::shared_ptr<Plan> right,
                  std::vector<ColMeta> output_cols, bool all)
            : UnionPlan(tag, std::move(left), std::move(right),
                        std::move(output_cols), ast::SetOpType::UNION, all) {}
        ~UnionPlan(){}
        std::vector<std::shared_ptr<Plan>> subplans_;
        std::vector<ColMeta> output_cols_;
        ast::SetOpType op_ = ast::SetOpType::UNION;
        bool all_ = false;
};

using SetOperationPlan = UnionPlan;

// dml语句，包括insert; delete; update; select语句　
class ExplainPlan : public Plan
{
    public:
        ExplainPlan(std::shared_ptr<Plan> select_plan, std::shared_ptr<Query> query, bool analyze)
        {
            Plan::tag = T_Explain;
            select_plan_ = std::move(select_plan);
            query_ = std::move(query);
            analyze_ = analyze;
        }
        ~ExplainPlan(){}
        std::shared_ptr<Plan> select_plan_;
        std::shared_ptr<Query> query_;
        bool analyze_;
};

class DMLPlan : public Plan
{
    public:
        DMLPlan(PlanTag tag, std::shared_ptr<Plan> subplan, std::string tab_name,
                std::vector<Value> values, std::vector<SetClause> set_clauses)
        {
            Plan::tag = tag;
            subplan_ = std::move(subplan);
            tab_name_ = std::move(tab_name);
            values_ = std::move(values);
            set_clauses_ = std::move(set_clauses);
        }
        ~DMLPlan(){}
        std::shared_ptr<Plan> subplan_;
        std::string tab_name_;
        std::vector<Value> values_;
        std::vector<SetClause> set_clauses_;
};

// ddl语句, 包括create/drop table; create/drop index;
class DDLPlan : public Plan
{
    public:
        DDLPlan(PlanTag tag, std::string tab_name, std::vector<std::string> col_names, std::vector<ColDef> cols)
        {
            Plan::tag = tag;
            tab_name_ = std::move(tab_name);
            cols_ = std::move(cols);
            tab_col_names_ = std::move(col_names);
        }
        ~DDLPlan(){}
        std::string tab_name_;
        std::vector<std::string> tab_col_names_;
        std::vector<ColDef> cols_;
};

// help; show tables; desc tables; begin; abort; commit; rollback语句对应的plan
class OtherPlan : public Plan
{
    public:
        OtherPlan(PlanTag tag, std::string tab_name)
        {
            Plan::tag = tag;
            tab_name_ = std::move(tab_name);            
        }
        ~OtherPlan(){}
        std::string tab_name_;
};

// Set Knob Plan
class SetKnobPlan : public Plan
{
    public:
        SetKnobPlan(ast::SetKnobType knob_type, bool bool_value) {
            Plan::tag = T_SetKnob;
            set_knob_type_ = knob_type;
            bool_value_ = bool_value;
        }
    ast::SetKnobType set_knob_type_;
    bool bool_value_;
};
