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

#include <cerrno>
#include <cstring>
#include <string>
#include "optimizer/plan.h"
#include "execution/executor_abstract.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_distinct.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_index_nestedloop_join.h"
#include "execution/executor_filter.h"
#include "execution/executor_subquery_filter.h"
#include "execution/executor_correlated_filter.h"
#include "execution/executor_lateral_join.h"
#include "execution/executor_rename.h"
#include "execution/executor_update.h"
#include "execution/executor_insert.h"
#include "execution/executor_delete.h"
#include "execution/execution_sort.h"
#include "execution/executor_aggregation.h"
#include "execution/executor_limit.h"
#include "execution/executor_union.h"
#include "common/common.h"

typedef enum portalTag{
    PORTAL_Invalid_Query = 0,
    PORTAL_ONE_SELECT,
    PORTAL_DML_WITHOUT_SELECT,
    PORTAL_MULTI_QUERY,
    PORTAL_CMD_UTILITY
} portalTag;


struct PortalStmt {
    portalTag tag;
    
    std::vector<TabCol> sel_cols;
    std::unique_ptr<AbstractExecutor> root;
    std::shared_ptr<Plan> plan;
    
    PortalStmt(portalTag tag_, std::vector<TabCol> sel_cols_, std::unique_ptr<AbstractExecutor> root_, std::shared_ptr<Plan> plan_) :
            tag(tag_), sel_cols(std::move(sel_cols_)), root(std::move(root_)), plan(std::move(plan_)) {}
};

class Portal
{
   private:
    SmManager *sm_manager_;
    

   public:
    Portal(SmManager *sm_manager) : sm_manager_(sm_manager){}
    ~Portal(){}

    std::shared_ptr<PortalStmt> start(std::shared_ptr<Plan> plan, Context *context)
    {
        if (auto x = std::dynamic_pointer_cast<OtherPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(),plan);
        } else if(auto x = std::dynamic_pointer_cast<SetKnobPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(), plan); 
        } else if(auto x = std::dynamic_pointer_cast<ExplainPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_CMD_UTILITY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(), plan);
        } else if (auto x = std::dynamic_pointer_cast<DDLPlan>(plan)) {
            return std::make_shared<PortalStmt>(PORTAL_MULTI_QUERY, std::vector<TabCol>(), std::unique_ptr<AbstractExecutor>(),plan);
        } else if (auto x = std::dynamic_pointer_cast<DMLPlan>(plan)) {
            switch(x->tag) {
                case T_select:
                {
                    std::unique_ptr<AbstractExecutor> root = convert_plan_executor(x->subplan_, context);
                    std::vector<TabCol> sel_cols;
                    for (const auto &col : root->cols()) {
                        sel_cols.push_back({col.tab_name, col.name});
                    }
                    return std::make_shared<PortalStmt>(PORTAL_ONE_SELECT, std::move(sel_cols), std::move(root), plan);
                }
                    
                case T_Update:
                {
                    std::unique_ptr<AbstractExecutor> scan= convert_plan_executor(x->subplan_, context);
                    std::vector<Rid> rids;
                    for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
                        rids.push_back(scan->rid());
                    }
                    std::unique_ptr<AbstractExecutor> root = std::make_unique<UpdateExecutor>(
                        sm_manager_, x->tab_name_, x->set_clauses_, rids, context);
                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }
                case T_Delete:
                {
                    std::unique_ptr<AbstractExecutor> scan= convert_plan_executor(x->subplan_, context);
                    std::vector<Rid> rids;
                    for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
                        rids.push_back(scan->rid());
                    }

                    std::unique_ptr<AbstractExecutor> root =
                        std::make_unique<DeleteExecutor>(sm_manager_, x->tab_name_, rids, context);

                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }

                case T_Insert:
                {
                    std::unique_ptr<AbstractExecutor> root =
                            std::make_unique<InsertExecutor>(sm_manager_, x->tab_name_, x->values_, context);
            
                    return std::make_shared<PortalStmt>(PORTAL_DML_WITHOUT_SELECT, std::vector<TabCol>(), std::move(root), plan);
                }


                default:
                    throw InternalError("Unexpected field type");
                    break;
            }
        } else {
            throw InternalError("Unexpected field type");
        }
        return nullptr;
    }

    void run(std::shared_ptr<PortalStmt> portal, QlManager* ql, txn_id_t *txn_id, Context *context){
        switch(portal->tag) {
            case PORTAL_ONE_SELECT:
            {
                // LIMIT/OFFSET are physical executors.  Applying a second
                // row-count cap here used to be harmless for LIMIT alone but
                // becomes incorrect once OFFSET can skip input rows.
                ql->select_from(std::move(portal->root), std::move(portal->sel_cols), context, -1);
                break;
            }

            case PORTAL_DML_WITHOUT_SELECT:
            {
                ql->run_dml(std::move(portal->root));
                break;
            }
            case PORTAL_MULTI_QUERY:
            {
                ql->run_mutli_query(portal->plan, context);
                break;
            }
            case PORTAL_CMD_UTILITY:
            {
                ql->run_cmd_utility(portal->plan, txn_id, context);
                break;
            }
            default:
            {
                throw InternalError("Unexpected field type");
            }
        }
    }

    void drop(){}


    std::unique_ptr<AbstractExecutor> convert_plan_executor(
        std::shared_ptr<Plan> plan, Context *context,
        std::shared_ptr<CorrelatedTupleContext> correlated = nullptr)
    {
        if(auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)){
            return std::make_unique<ProjectionExecutor>(convert_plan_executor(x->subplan_, context, correlated),
                                                        x->sel_cols_);
        } else if (auto x = std::dynamic_pointer_cast<DistinctPlan>(plan)) {
            return std::make_unique<DistinctExecutor>(
                convert_plan_executor(x->subplan_, context, correlated));
        } else if (auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
            return std::make_unique<FilterExecutor>(convert_plan_executor(x->subplan_, context, correlated),
                                                    x->predicate_);
        } else if (auto x = std::dynamic_pointer_cast<SubqueryFilterPlan>(plan)) {
            auto child = convert_plan_executor(x->subplan_, context, correlated);
            std::vector<std::unique_ptr<AbstractExecutor>> subqueries;
            std::vector<std::shared_ptr<CorrelatedTupleContext>> contexts;
            subqueries.reserve(x->subquery_plans_.size());
            contexts.reserve(x->subquery_plans_.size());
            for (const auto &subplan : x->subquery_plans_) {
                auto subquery_context = std::make_shared<CorrelatedTupleContext>();
                subqueries.push_back(convert_plan_executor(subplan, context, subquery_context));
                contexts.push_back(std::move(subquery_context));
            }
            return std::make_unique<SubqueryFilterExecutor>(
                std::move(child), x->predicate_, std::move(subqueries),
                std::move(contexts));
        } else if (auto x = std::dynamic_pointer_cast<CorrelatedFilterPlan>(plan)) {
            if (correlated == nullptr) {
                throw InternalError("Correlated filter used outside LATERAL JOIN");
            }
            return std::make_unique<CorrelatedFilterExecutor>(
                convert_plan_executor(x->subplan_, context, correlated), x->predicate_, correlated);
        } else if (auto x = std::dynamic_pointer_cast<RenamePlan>(plan)) {
            return std::make_unique<RenameExecutor>(
                convert_plan_executor(x->subplan_, context, correlated), x->output_cols_);
        } else if(auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
            if(x->tag == T_SeqScan) {
                return std::make_unique<SeqScanExecutor>(sm_manager_, x->tab_name_, x->binding_name_,
                                                         x->predicate_, context);
            }
            else {
                return std::make_unique<IndexScanExecutor>(sm_manager_, x->tab_name_, x->binding_name_,
                                                           x->predicate_, x->access_conditions_,
                                                           x->index_col_names_, context);
            } 
        } else if(auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
            std::unique_ptr<AbstractExecutor> left = convert_plan_executor(x->left_, context, correlated);
            if (x->lateral_) {
                auto lateral_context = std::make_shared<CorrelatedTupleContext>();
                std::unique_ptr<AbstractExecutor> right =
                    convert_plan_executor(x->right_, context, lateral_context);
                return std::make_unique<LateralNestedLoopJoinExecutor>(
                    std::move(left), std::move(right), x->on_predicate_, x->type,
                    std::move(lateral_context));
            }
            if (x->tag == T_IndexNestLoop) {
                auto right_scan = std::dynamic_pointer_cast<ScanPlan>(x->right_);
                if (right_scan == nullptr) throw InternalError("Unexpected INLJ right plan");
                return std::make_unique<IndexNestedLoopJoinExecutor>(std::move(left), sm_manager_, *right_scan,
                                                                     x->on_predicate_, context);
            }
            std::unique_ptr<AbstractExecutor> right = convert_plan_executor(x->right_, context, correlated);
            std::unique_ptr<AbstractExecutor> join = std::make_unique<NestedLoopJoinExecutor>(
                                std::move(left), 
                                std::move(right), x->on_predicate_, x->type,
                                x->coalesced_cols_);
            return join;
        } else if(auto x = std::dynamic_pointer_cast<SortPlan>(plan)) {
            if (x->sort_cols_.size() == 1) {
                return std::make_unique<SortExecutor>(convert_plan_executor(x->subplan_, context, correlated),
                                                x->sort_cols_[0].first, x->sort_cols_[0].second);
            } else {
                auto prev = convert_plan_executor(x->subplan_, context, correlated);
                return std::make_unique<SortExecutor>(std::move(prev), x->sort_cols_);
            }
        } else if(auto x = std::dynamic_pointer_cast<AggPlan>(plan)) {
            return std::make_unique<AggExecutor>(convert_plan_executor(x->subplan_, context, correlated),
                                                 x->group_cols_, x->agg_exprs_,
                                                 x->having_expr_, x->output_cols_);
        } else if(auto x = std::dynamic_pointer_cast<LimitPlan>(plan)) {
            return std::make_unique<LimitExecutor>(convert_plan_executor(x->subplan_, context, correlated),
                                                   x->limit_, x->offset_);
        } else if(auto x = std::dynamic_pointer_cast<UnionPlan>(plan)) {
            std::vector<std::unique_ptr<AbstractExecutor>> children;
            for (auto &subplan : x->subplans_) {
                children.push_back(convert_plan_executor(subplan, context, correlated));
            }
            return std::make_unique<UnionExecutor>(std::move(children), x->output_cols_,
                                                   x->op_, x->all_);
        }
        return nullptr;
    }

};
