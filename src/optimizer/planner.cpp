/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "planner.h"

#include <memory>

#include "execution/executor_delete.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_update.h"
#include "index/ix.h"
#include "record_printer.h"

// 最左匹配规则：对表上每条索引按 cols 顺序贪心匹配前缀。
// 列要么能找到 OP_EQ 条件（继续匹配后续列），要么找到 OP_LT/GT/LE/GE 条件（匹配此列后停止）。
// 跨多条索引时选匹配前缀最长的。
bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds, std::vector<std::string>& index_col_names) {
    index_col_names.clear();
    TabMeta& tab = sm_manager_->db_.get_table(tab_name);

    int best_match_len = 0;
    const IndexMeta* best_index = nullptr;

    for (auto& index : tab.indexes) {
        int match_len = 0;
        for (auto& idx_col : index.cols) {
            bool found_eq = false;
            bool found_range = false;
            for (auto& cond : curr_conds) {
                if (!cond.is_rhs_val) continue;
                if (cond.lhs_col.tab_name != tab_name) continue;
                if (cond.lhs_col.col_name != idx_col.name) continue;
                if (cond.op == OP_EQ) { found_eq = true; break; }
                if (cond.op == OP_LT || cond.op == OP_GT || cond.op == OP_LE || cond.op == OP_GE) {
                    found_range = true;
                    // 不 break——继续看有没有同列的 OP_EQ（优先 EQ）
                }
            }
            if (found_eq) {
                match_len++;
            } else if (found_range) {
                match_len++;
                break;  // 范围列匹配后停止——后续列即使匹配也无序
            } else {
                break;  // 该列没有可用条件
            }
        }
        if (match_len > best_match_len) {
            best_match_len = match_len;
            best_index = &index;
        }
    }

    if (best_match_len == 0) return false;
    // 输出整条索引的全部 col_names（让 get_index_meta 能完整命中）
    // IndexScanExecutor 自己再分析 fed_conds_ 决定能用几列做 key
    for (auto& col : best_index->cols) {
        index_col_names.push_back(col.name);
    }
    return true;
}

bool Planner::get_join_index_cols(const std::string &right_table, const std::vector<Condition> &join_conds,
                                  std::vector<std::string> &index_col_names) {
    index_col_names.clear();
    TabMeta &tab = sm_manager_->db_.get_table(right_table);
    for (auto &index : tab.indexes) {
        if (index.cols.empty()) continue;
        const std::string &first_col = index.cols[0].name;
        for (auto &cond : join_conds) {
            if (cond.op != OP_EQ || cond.is_rhs_val) continue;
            bool matches = (cond.lhs_col.tab_name == right_table && cond.lhs_col.col_name == first_col) ||
                           (cond.rhs_col.tab_name == right_table && cond.rhs_col.col_name == first_col);
            if (!matches) continue;
            for (auto &col : index.cols) index_col_names.push_back(col.name);
            return true;
        }
    }
    return false;
}

std::shared_ptr<Plan> Planner::make_join_plan(std::shared_ptr<Plan> left, std::shared_ptr<Plan> right,
                                              std::vector<Condition> join_conds) {
    auto right_scan = std::dynamic_pointer_cast<ScanPlan>(right);
    if (right_scan != nullptr) {
        std::vector<std::string> index_col_names;
        if (get_join_index_cols(right_scan->tab_name_, join_conds, index_col_names)) {
            right_scan->tag = T_IndexScan;
            right_scan->index_col_names_ = std::move(index_col_names);
            return std::make_shared<JoinPlan>(T_IndexNestLoop, std::move(left), std::move(right), std::move(join_conds));
        }
    }
    return std::make_shared<JoinPlan>(T_NestLoop, std::move(left), std::move(right), std::move(join_conds));
}

/**
 * @brief 表算子条件谓词生成
 *
 * @param conds 条件
 * @param tab_names 表名
 * @return std::vector<Condition>
 */
std::vector<Condition> pop_conds(std::vector<Condition> &conds, std::string tab_names) {
    // auto has_tab = [&](const std::string &tab_name) {
    //     return std::find(tab_names.begin(), tab_names.end(), tab_name) != tab_names.end();
    // };
    std::vector<Condition> solved_conds;
    auto it = conds.begin();
    while (it != conds.end()) {
        if ((tab_names.compare(it->lhs_col.tab_name) == 0 && it->is_rhs_val) || (it->lhs_col.tab_name.compare(it->rhs_col.tab_name) == 0)) {
            solved_conds.emplace_back(std::move(*it));
            it = conds.erase(it);
        } else {
            it++;
        }
    }
    return solved_conds;
}

int push_conds(Condition *cond, std::shared_ptr<Plan> plan)
{
    if(auto x = std::dynamic_pointer_cast<ScanPlan>(plan))
    {
        if(x->tab_name_.compare(cond->lhs_col.tab_name) == 0) {
            return 1;
        } else if(x->tab_name_.compare(cond->rhs_col.tab_name) == 0){
            return 2;
        } else {
            return 0;
        }
    }
    else if(auto x = std::dynamic_pointer_cast<JoinPlan>(plan))
    {
        int left_res = push_conds(cond, x->left_);
        // 条件已经下推到左子节点
        if(left_res == 3){
            return 3;
        }
        int right_res = push_conds(cond, x->right_);
        // 条件已经下推到右子节点
        if(right_res == 3){
            return 3;
        }
        // 左子节点或右子节点有一个没有匹配到条件的列
        if(left_res == 0 || right_res == 0) {
            return left_res + right_res;
        }
        // 左子节点匹配到条件的右边
        if(left_res == 2) {
            // 需要将左右两边的条件变换位置
            std::map<CompOp, CompOp> swap_op = {
                {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
            };
            std::swap(cond->lhs_col, cond->rhs_col);
            cond->op = swap_op.at(cond->op);
        }
        x->conds_.emplace_back(std::move(*cond));
        return 3;
    }
    return false;
}

std::shared_ptr<Plan> pop_scan(int *scantbl, std::string table, std::vector<std::string> &joined_tables, 
                std::vector<std::shared_ptr<Plan>> plans)
{
    for (size_t i = 0; i < plans.size(); i++) {
        auto x = std::dynamic_pointer_cast<ScanPlan>(plans[i]);
        if(x->tab_name_.compare(table) == 0)
        {
            scantbl[i] = 1;
            joined_tables.emplace_back(x->tab_name_);
            return plans[i];
        }
    }
    return nullptr;
}


std::shared_ptr<Query> Planner::logical_optimization(std::shared_ptr<Query> query, Context *context)
{
    
    //TODO 实现逻辑优化规则

    return query;
}

// 题9：SER 下强制 SeqScan；SI 用 MVCC 感知 IndexScan
static bool mvcc_force_seqscan(Context *context, const std::string &tab) {
    return context && context->txn_ && context->txn_mgr_ &&
           context->txn_mgr_->is_ser(context->txn_) &&
           (context->txn_->get_txn_mode() || context->txn_mgr_->table_is_dirty(tab));
}

std::shared_ptr<Plan> Planner::physical_optimization(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plan = make_one_rel_sql_order(query, context);
    // ORDER BY 由 generate_select_plan 统一处理（含聚合别名）
    return plan;
}

std::shared_ptr<Plan> Planner::make_one_rel_sql_order(std::shared_ptr<Query> query, Context *context)
{
    std::vector<std::string> tables = query->tables;
    if (tables.empty()) return nullptr;

    std::vector<std::shared_ptr<Plan>> scans(tables.size());
    for (size_t i = 0; i < tables.size(); i++) {
        auto curr_conds = pop_conds(query->conds, tables[i]);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(tables[i], curr_conds, index_col_names);
        if (index_exist && mvcc_force_seqscan(context, tables[i])) index_exist = false;
        scans[i] = std::make_shared<ScanPlan>(index_exist ? T_IndexScan : T_SeqScan, sm_manager_,
                                              tables[i], curr_conds, index_col_names);
    }
    if (tables.size() == 1) return scans[0];

    auto conds = query->conds;
    std::shared_ptr<Plan> current = scans[0];
    std::vector<std::string> joined{tables[0]};

    for (size_t i = 1; i < tables.size(); i++) {
        const std::string &right_table = tables[i];
        std::vector<Condition> join_conds;
        auto it = conds.begin();
        while (it != conds.end()) {
            bool lhs_joined = std::find(joined.begin(), joined.end(), it->lhs_col.tab_name) != joined.end();
            bool rhs_joined = std::find(joined.begin(), joined.end(), it->rhs_col.tab_name) != joined.end();
            bool connects_right = (it->lhs_col.tab_name == right_table && rhs_joined) ||
                                  (it->rhs_col.tab_name == right_table && lhs_joined);
            if (connects_right) {
                join_conds.push_back(*it);
                it = conds.erase(it);
            } else {
                ++it;
            }
        }
        current = make_join_plan(std::move(current), scans[i], std::move(join_conds));
        joined.push_back(right_table);
    }

    for (auto &cond : conds) {
        push_conds(&cond, current);
    }
    return current;
}



std::shared_ptr<Plan> Planner::make_one_rel(std::shared_ptr<Query> query)
{
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    std::vector<std::string> tables = query->tables;
    // // Scan table , 生成表算子列表tab_nodes
    std::vector<std::shared_ptr<Plan>> table_scan_executors(tables.size());
    for (size_t i = 0; i < tables.size(); i++) {
        auto curr_conds = pop_conds(query->conds, tables[i]);
        // int index_no = get_indexNo(tables[i], curr_conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(tables[i], curr_conds, index_col_names);
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors[i] = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, tables[i], curr_conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors[i] =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, tables[i], curr_conds, index_col_names);
        }
    }
    // 只有一个表，不需要join。
    if(tables.size() == 1)
    {
        return table_scan_executors[0];
    }
    // 获取where条件
    auto conds = std::move(query->conds);
    std::shared_ptr<Plan> table_join_executors;
    
    int scantbl[tables.size()];
    for(size_t i = 0; i < tables.size(); i++)
    {
        scantbl[i] = -1;
    }
    // 假设在ast中已经添加了jointree，这里需要修改的逻辑是，先处理jointree，然后再考虑剩下的部分
    if(conds.size() >= 1)
    {
        // 有连接条件

        // 根据连接条件，生成第一层join
        std::vector<std::string> joined_tables(tables.size());
        auto it = conds.begin();
        while (it != conds.end()) {
            std::shared_ptr<Plan> left , right;
            left = pop_scan(scantbl, it->lhs_col.tab_name, joined_tables, table_scan_executors);
            right = pop_scan(scantbl, it->rhs_col.tab_name, joined_tables, table_scan_executors);
            std::vector<Condition> join_conds{*it};
            //建立join
            // 判断使用哪种join方式
            if(enable_nestedloop_join && enable_sortmerge_join) {
                // 默认nested loop join
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left), std::move(right), join_conds);
            } else if(enable_nestedloop_join) {
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left), std::move(right), join_conds);
            } else if(enable_sortmerge_join) {
                table_join_executors = std::make_shared<JoinPlan>(T_SortMerge, std::move(left), std::move(right), join_conds);
            } else {
                // error
                throw RMDBError("No join executor selected!");
            }

            // table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left), std::move(right), join_conds);
            it = conds.erase(it);
            break;
        }
        // 根据连接条件，生成第2-n层join
        it = conds.begin();
        while (it != conds.end()) {
            std::shared_ptr<Plan> left_need_to_join_executors = nullptr;
            std::shared_ptr<Plan> right_need_to_join_executors = nullptr;
            bool isneedreverse = false;
            if (std::find(joined_tables.begin(), joined_tables.end(), it->lhs_col.tab_name) == joined_tables.end()) {
                left_need_to_join_executors = pop_scan(scantbl, it->lhs_col.tab_name, joined_tables, table_scan_executors);
            }
            if (std::find(joined_tables.begin(), joined_tables.end(), it->rhs_col.tab_name) == joined_tables.end()) {
                right_need_to_join_executors = pop_scan(scantbl, it->rhs_col.tab_name, joined_tables, table_scan_executors);
                isneedreverse = true;
            } 

            if(left_need_to_join_executors != nullptr && right_need_to_join_executors != nullptr) {
                std::vector<Condition> join_conds{*it};
                std::shared_ptr<Plan> temp_join_executors = std::make_shared<JoinPlan>(T_NestLoop, 
                                                                    std::move(left_need_to_join_executors), 
                                                                    std::move(right_need_to_join_executors), 
                                                                    join_conds);
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(temp_join_executors), 
                                                                    std::move(table_join_executors), 
                                                                    std::vector<Condition>());
            } else if(left_need_to_join_executors != nullptr || right_need_to_join_executors != nullptr) {
                if(isneedreverse) {
                    std::map<CompOp, CompOp> swap_op = {
                        {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
                    };
                    std::swap(it->lhs_col, it->rhs_col);
                    it->op = swap_op.at(it->op);
                    left_need_to_join_executors = std::move(right_need_to_join_executors);
                }
                std::vector<Condition> join_conds{*it};
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left_need_to_join_executors), 
                                                                    std::move(table_join_executors), join_conds);
            } else {
                push_conds(std::move(&(*it)), table_join_executors);
            }
            it = conds.erase(it);
        }
    } else {
        table_join_executors = table_scan_executors[0];
        scantbl[0] = 1;
    }

    //连接剩余表
    for (size_t i = 0; i < tables.size(); i++) {
        if(scantbl[i] == -1) {
            table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(table_scan_executors[i]), 
                                                    std::move(table_join_executors), std::vector<Condition>());
        }
    }

    return table_join_executors;

}


std::shared_ptr<Plan> Planner::generate_sort_plan(std::shared_ptr<Query> query, std::shared_ptr<Plan> plan)
{
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    if(!x->has_sort) {
        return plan;
    }
    std::vector<std::string> tables = query->tables;
    std::vector<ColMeta> all_cols;
    for (auto &sel_tab_name : tables) {
        const auto &sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
    std::vector<std::pair<TabCol, bool>> sort_cols;
    for (auto &order : x->orders) {
        TabCol sel_col;
        for (auto &col : all_cols) {
            if(col.name.compare(order->cols->col_name) == 0 )
                sel_col = {.tab_name = col.tab_name, .col_name = col.name};
        }
        sort_cols.emplace_back(sel_col, order->orderby_dir == ast::OrderBy_DESC);
    }
    if (sort_cols.size() == 1) {
        return std::make_shared<SortPlan>(T_Sort, std::move(plan), sort_cols[0].first, sort_cols[0].second);
    }
    // TODO: multi-column SortPlan
    return std::make_shared<SortPlan>(T_Sort, std::move(plan), sort_cols[0].first, sort_cols[0].second);
}


/**
 * @brief select plan 生成
 *
 * @param sel_cols select plan 选取的列
 * @param tab_names select plan 目标的表
 * @param conds select plan 选取条件
 */
std::shared_ptr<Plan> Planner::generate_select_plan(std::shared_ptr<Query> query, Context *context) {
    //逻辑优化
    query = logical_optimization(std::move(query), context);

    //物理优化
    auto sel_cols = query->cols;
    std::shared_ptr<Plan> plannerRoot = physical_optimization(query, context);

    // 聚合+分组
    if (!query->aggs.empty() || !query->group_by_cols.empty()) {
        std::vector<ColMeta> output_cols;
        int offset = 0;
        // GROUP BY 列
        for (auto &gc : query->group_by_cols) {
            auto tab = sm_manager_->db_.get_table(gc.tab_name);
            auto col_it = tab.get_col(gc.col_name);
            ColMeta col = *col_it;
            col.offset = offset;
            offset += col.len;
            output_cols.push_back(col);
        }
        // 聚合列
        for (auto &agg : query->aggs) {
            ColMeta col;
            col.name = agg.alias.empty() ? agg.to_string() : agg.alias;
            col.tab_name = agg.col.tab_name;
            if (agg.type == ast::AGG_COUNT) {
                col.type = TYPE_INT;
                col.len = sizeof(int);
            } else if (agg.type == ast::AGG_AVG) {
                col.type = TYPE_FLOAT;
                col.len = sizeof(float);
            } else {
                col.type = agg.arg_type;
                if (agg.arg_type == TYPE_INT) {
                    col.len = sizeof(int);
                } else if (agg.arg_type == TYPE_FLOAT) {
                    col.len = sizeof(float);
                } else {
                    auto tab = sm_manager_->db_.get_table(agg.col.tab_name);
                    col.len = tab.get_col(agg.col.col_name)->len;
                }
            }
            col.offset = offset;
            offset += col.len;
            output_cols.push_back(col);
        }
        plannerRoot = std::make_shared<AggPlan>(T_Aggregation, std::move(plannerRoot),
                                                query->group_by_cols, query->aggs,
                                                query->having_conds, output_cols);
        // 聚合查询中，Projection 只选择用户 SELECT 的列
        sel_cols.clear();
        for (auto &gc : query->group_by_cols) {
            sel_cols.push_back(gc);
        }
        for (auto &agg : query->aggs) {
            if (agg.in_output) {
                std::string name = agg.alias.empty() ? agg.to_string() : agg.alias;
                sel_cols.push_back({"", name});
            }
        }
    }

    // ORDER BY
    if (!query->orders.empty()) {
        std::vector<std::pair<TabCol, bool>> sort_cols;
        for (auto &order : query->orders) {
            sort_cols.emplace_back(order.first, order.second == ast::OrderBy_DESC);
        }
        plannerRoot = std::make_shared<SortPlan>(T_Sort, std::move(plannerRoot), sort_cols);
    }

    // Projection
    plannerRoot = std::make_shared<ProjectionPlan>(T_Projection, std::move(plannerRoot),
                                                    std::move(sel_cols));

    // LIMIT
    if (query->has_limit) {
        plannerRoot = std::make_shared<LimitPlan>(T_Limit, std::move(plannerRoot), query->limit_count);
    }

    return plannerRoot;
}

// 生成DDL语句和DML语句的查询执行计划
std::shared_ptr<Plan> Planner::do_planner(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plannerRoot;
    if (auto x = std::dynamic_pointer_cast<ast::CreateTable>(query->parse)) {
        // create table;
        std::vector<ColDef> col_defs;
        for (auto &field : x->fields) {
            if (auto sv_col_def = std::dynamic_pointer_cast<ast::ColDef>(field)) {
                ColDef col_def = {.name = sv_col_def->col_name,
                                  .type = interp_sv_type(sv_col_def->type_len->type),
                                  .len = sv_col_def->type_len->len};
                col_defs.push_back(col_def);
            } else {
                throw InternalError("Unexpected field type");
            }
        }
        plannerRoot = std::make_shared<DDLPlan>(T_CreateTable, x->tab_name, std::vector<std::string>(), col_defs);
    } else if (auto x = std::dynamic_pointer_cast<ast::DropTable>(query->parse)) {
        // drop table;
        plannerRoot = std::make_shared<DDLPlan>(T_DropTable, x->tab_name, std::vector<std::string>(), std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::CreateIndex>(query->parse)) {
        // create index;
        plannerRoot = std::make_shared<DDLPlan>(T_CreateIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DropIndex>(query->parse)) {
        // drop index
        plannerRoot = std::make_shared<DDLPlan>(T_DropIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(query->parse)) {
        // insert;
        plannerRoot = std::make_shared<DMLPlan>(T_Insert, std::shared_ptr<Plan>(),  x->tab_name,  
                                                    query->values, std::vector<Condition>(), std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(query->parse)) {
        // delete;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);
        
        if (index_exist && mvcc_force_seqscan(context, x->tab_name)) index_exist = false;
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors =
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }

        plannerRoot = std::make_shared<DMLPlan>(T_Delete, table_scan_executors, x->tab_name,  
                                                std::vector<Value>(), query->conds, std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(query->parse)) {
        // update;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);

        if (index_exist && mvcc_force_seqscan(context, x->tab_name)) index_exist = false;
        if (index_exist == false) {  // 该表没有索引
        index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }
        plannerRoot = std::make_shared<DMLPlan>(T_Update, table_scan_executors, x->tab_name,
                                                     std::vector<Value>(), query->conds, 
                                                     query->set_clauses);
    } else if (auto x = std::dynamic_pointer_cast<ast::ExplainStmt>(query->parse)) {
        auto select_plan = generate_select_plan(query->explain_query, context);
        plannerRoot = std::make_shared<ExplainPlan>(select_plan, query->explain_query, query->explain_analyze);
    } else if (auto x = std::dynamic_pointer_cast<ast::UnionStmt>(query->parse)) {
        std::vector<std::shared_ptr<Plan>> subplans;
        for (auto &child : query->union_queries) {
            subplans.push_back(generate_select_plan(child, context));
        }
        std::shared_ptr<Plan> union_plan =
            std::make_shared<UnionPlan>(T_Union, std::move(subplans), query->union_output_cols);
        if (!query->orders.empty()) {
            std::vector<std::pair<TabCol, bool>> sort_cols;
            for (auto &order : query->orders) {
                sort_cols.emplace_back(order.first, order.second == ast::OrderBy_DESC);
            }
            union_plan = std::make_shared<SortPlan>(T_Sort, std::move(union_plan), sort_cols);
        }
        if (query->has_limit) {
            union_plan = std::make_shared<LimitPlan>(T_Limit, std::move(union_plan), query->limit_count);
        }
        plannerRoot = std::make_shared<DMLPlan>(T_select, union_plan, std::string(), std::vector<Value>(),
                                                std::vector<Condition>(), std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {

        std::shared_ptr<plannerInfo> root = std::make_shared<plannerInfo>(x);
        // 生成select语句的查询执行计划
        std::shared_ptr<Plan> projection = generate_select_plan(std::move(query), context);
        plannerRoot = std::make_shared<DMLPlan>(T_select, projection, std::string(), std::vector<Value>(),
                                                    std::vector<Condition>(), std::vector<SetClause>());
    } else {
        throw InternalError("Unexpected AST root");
    }
    return plannerRoot;
}
