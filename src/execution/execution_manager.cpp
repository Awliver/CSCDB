/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "execution_manager.h"

#include "executor_delete.h"
#include "executor_aggregation.h"
#include "executor_index_scan.h"
#include "executor_insert.h"
#include "executor_nestedloop_join.h"
#include "executor_projection.h"
#include "executor_seq_scan.h"
#include "executor_update.h"
#include "executor_filter.h"
#include "executor_correlated_filter.h"
#include "executor_lateral_join.h"
#include "executor_rename.h"
#include "executor_limit.h"
#include "execution_sort.h"
#include "index/ix.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "record_printer.h"
#include "analyze/analyze.h"
#include "common/output_control.h"
#include "parser/ast.h"
#include "recovery/log_manager.h"
#include <fstream>

const char *help_info = "Supported SQL syntax:\n"
                   "  command ;\n"
                   "command:\n"
                   "  CREATE TABLE table_name (column_name type [, column_name type ...])\n"
                   "  DROP TABLE table_name\n"
                   "  CREATE INDEX table_name (column_name)\n"
                   "  DROP INDEX table_name (column_name)\n"
                   "  INSERT INTO table_name VALUES (value [, value ...])\n"
                   "  DELETE FROM table_name [WHERE where_clause]\n"
                   "  UPDATE table_name SET column_name = value [, column_name = value ...] [WHERE where_clause]\n"
                   "  SELECT selector FROM table_name [WHERE where_clause]\n"
                   "type:\n"
                   "  {INT | FLOAT | CHAR(n)}\n"
                   "where_clause:\n"
                   "  condition [AND condition ...]\n"
                   "condition:\n"
                   "  column op {column | value}\n"
                   "column:\n"
                   "  [table_name.]column_name\n"
                   "op:\n"
                   "  {= | <> | < | > | <= | >=}\n"
                   "selector:\n"
                   "  {* | column [, column ...]}\n";

// 主要负责执行DDL语句
void QlManager::run_mutli_query(std::shared_ptr<Plan> plan, Context *context){
    if (auto x = std::dynamic_pointer_cast<DDLPlan>(plan)) {
        switch(x->tag) {
            case T_CreateTable:
            {
                sm_manager_->create_table(x->tab_name_, x->cols_, context);
                break;
            }
            case T_DropTable:
            {
                sm_manager_->drop_table(x->tab_name_, context);
                break;
            }
            case T_CreateIndex:
            {
                sm_manager_->create_index(x->tab_name_, x->tab_col_names_, context);
                break;
            }
            case T_DropIndex:
            {
                sm_manager_->drop_index(x->tab_name_, x->tab_col_names_, context);
                break;
            }
            default:
                throw InternalError("Unexpected field type");
                break;  
        }
    }
}

// 执行help; show tables; desc table; begin; commit; abort;语句
void QlManager::run_cmd_utility(std::shared_ptr<Plan> plan, txn_id_t *txn_id, Context *context) {
    if (auto x = std::dynamic_pointer_cast<OtherPlan>(plan)) {
        switch(x->tag) {
            case T_Help:
            {
                memcpy(context->data_send_ + *(context->offset_), help_info, strlen(help_info));
                *(context->offset_) = strlen(help_info);
                break;
            }
            case T_ShowTable:
            {
                sm_manager_->show_tables(context);
                break;
            }
            case T_ShowIndex:
            {
                sm_manager_->show_indexes(x->tab_name_, context);
                break;
            }
            case T_DescTable:
            {
                sm_manager_->desc_table(x->tab_name_, context);
                break;
            }
            case T_Transaction_begin:
            {
                // 显示开启一个事务
                context->txn_->set_txn_mode(true);
                txn_mgr_->inc_explicit();   // 题9：活跃显式事务数 +1（写操作据此决定是否维护版本）
                break;
            }
            case T_Transaction_commit:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->commit(context->txn_, context->log_mgr_);
                break;
            }    
            case T_Transaction_rollback:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->abort(context->txn_, context->log_mgr_);
                break;
            }    
            case T_Transaction_abort:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->abort(context->txn_, context->log_mgr_);
                break;
            }     
            default:
                throw InternalError("Unexpected field type");
                break;                        
        }

    } else if(auto x = std::dynamic_pointer_cast<SetKnobPlan>(plan)) {
        switch (x->set_knob_type_)
        {
        case ast::SetKnobType::EnableNestLoop: {
            planner_->set_enable_nestedloop_join(x->bool_value_);
            break;
        }
        case ast::SetKnobType::EnableSortMerge: {
            planner_->set_enable_sortmerge_join(x->bool_value_);
            break;
        }
        default: {
            throw RMDBError("Not implemented!\n");
            break;
        }
        }
    } else if (auto x = std::dynamic_pointer_cast<ExplainPlan>(plan)) {
        explain_query_plan(x, context);
    }
}

namespace {

std::string join_op_to_string(CompOp op) {
    switch (op) {
        case OP_EQ: return "=";
        case OP_NE: return "<>";
        case OP_LT: return "<";
        case OP_GT: return ">";
        case OP_LE: return "<=";
        case OP_GE: return ">=";
    }
    return "";
}

std::string join_strings_sorted(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    std::ostringstream os;
    for (size_t i = 0; i < values.size(); i++) {
        if (i) os << ", ";
        os << values[i];
    }
    return os.str();
}

bool is_natural_synthetic(const std::string &binding) {
    return !binding.empty() && binding.front() == '\x1f';
}

std::string display_col(const TabCol &col) {
    if (col.tab_name.empty() || is_natural_synthetic(col.tab_name)) return col.col_name;
    return col.tab_name + "." + col.col_name;
}

std::string cond_to_string(const Condition &cond) {
    std::ostringstream os;
    os << display_col(cond.lhs_col) << join_op_to_string(cond.op);
    if (!cond.is_rhs_val) {
        os << display_col(cond.rhs_col);
    } else if (cond.rhs_val.type == TYPE_INT) {
        os << cond.rhs_val.int_val;
    } else if (cond.rhs_val.type == TYPE_FLOAT) {
        os << cond.rhs_val.float_val;
    } else {
        os << "'" << cond.rhs_val.str_val << "'";
    }
    return os.str();
}

size_t count_scan_rows(SmManager *sm_manager, const ScanPlan &scan) {
    size_t count = 0;
    SeqScanExecutor exec(sm_manager, scan.tab_name_, scan.binding_name_, scan.predicates_, nullptr);
    for (exec.beginTuple(); !exec.is_end(); exec.nextTuple()) count++;
    return count;
}

void collect_plan_bindings(const std::shared_ptr<Plan> &plan, std::set<std::string> &bindings) {
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        bindings.insert(scan->binding_name_);
    } else if (auto join = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        collect_plan_bindings(join->left_, bindings);
        collect_plan_bindings(join->right_, bindings);
    } else if (auto proj = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        collect_plan_bindings(proj->subplan_, bindings);
    } else if (auto filter = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        collect_plan_bindings(filter->subplan_, bindings);
    } else if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) {
        collect_plan_bindings(sort->subplan_, bindings);
    } else if (auto limit = std::dynamic_pointer_cast<LimitPlan>(plan)) {
        collect_plan_bindings(limit->subplan_, bindings);
    } else if (auto agg = std::dynamic_pointer_cast<AggPlan>(plan)) {
        collect_plan_bindings(agg->subplan_, bindings);
    } else if (auto rename = std::dynamic_pointer_cast<RenamePlan>(plan)) {
        if (!rename->output_cols_.empty()) bindings.insert(rename->output_cols_.front().tab_name);
    } else if (auto correlated = std::dynamic_pointer_cast<CorrelatedFilterPlan>(plan)) {
        collect_plan_bindings(correlated->subplan_, bindings);
    }
}

std::unique_ptr<AbstractExecutor> make_explain_executor(SmManager *sm_manager,
    const std::shared_ptr<Plan> &plan,
    std::shared_ptr<CorrelatedTupleContext> correlated = nullptr) {
    if (auto projection = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        auto child = make_explain_executor(sm_manager, projection->subplan_, correlated);
        if (child == nullptr) return nullptr;
        return std::make_unique<ProjectionExecutor>(std::move(child), projection->sel_cols_);
    }
    if (auto filter = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        auto child = make_explain_executor(sm_manager, filter->subplan_, correlated);
        if (child == nullptr) return nullptr;
        return std::make_unique<FilterExecutor>(std::move(child), filter->predicates_);
    }
    if (auto filter = std::dynamic_pointer_cast<CorrelatedFilterPlan>(plan)) {
        if (correlated == nullptr) return nullptr;
        auto child = make_explain_executor(sm_manager, filter->subplan_, correlated);
        if (child == nullptr) return nullptr;
        return std::make_unique<CorrelatedFilterExecutor>(
            std::move(child), filter->predicates_, correlated);
    }
    if (auto rename = std::dynamic_pointer_cast<RenamePlan>(plan)) {
        auto child = make_explain_executor(sm_manager, rename->subplan_, correlated);
        if (child == nullptr) return nullptr;
        return std::make_unique<RenameExecutor>(std::move(child), rename->output_cols_);
    }
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        // 统计只关心真实输出行；统一用顺序扫描可避免把 EXPLAIN 绑定到某个
        // 物理索引游标，同时仍执行完全相同的局部谓词。
        return std::make_unique<SeqScanExecutor>(sm_manager, scan->tab_name_, scan->binding_name_,
                                                 scan->predicates_, nullptr);
    }
    if (auto join = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        auto left = make_explain_executor(sm_manager, join->left_, correlated);
        if (left == nullptr) return nullptr;
        if (join->lateral_) {
            auto lateral_context = std::make_shared<CorrelatedTupleContext>();
            auto right = make_explain_executor(sm_manager, join->right_, lateral_context);
            if (right == nullptr) return nullptr;
            return std::make_unique<LateralNestedLoopJoinExecutor>(
                std::move(left), std::move(right), join->on_predicates_, join->type,
                std::move(lateral_context));
        }
        auto right = make_explain_executor(sm_manager, join->right_, correlated);
        if (right == nullptr) return nullptr;
        return std::make_unique<NestedLoopJoinExecutor>(
            std::move(left), std::move(right),
            join->on_predicates_, join->type, join->coalesced_cols_);
    }
    if (auto agg = std::dynamic_pointer_cast<AggPlan>(plan)) {
        auto child = make_explain_executor(sm_manager, agg->subplan_, correlated);
        if (child == nullptr) return nullptr;
        return std::make_unique<AggExecutor>(
            std::move(child), agg->group_cols_, agg->agg_exprs_,
            agg->having_conds_, agg->output_cols_);
    }
    if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) {
        auto child = make_explain_executor(sm_manager, sort->subplan_, correlated);
        if (child == nullptr) return nullptr;
        if (sort->sort_cols_.size() == 1) {
            return std::make_unique<SortExecutor>(std::move(child),
                                                  sort->sort_cols_[0].first,
                                                  sort->sort_cols_[0].second);
        }
        std::vector<std::pair<ColMeta, bool>> columns;
        for (const auto &[target, desc] : sort->sort_cols_) {
            auto it = std::find_if(child->cols().begin(), child->cols().end(),
                                   [&](const ColMeta &col) {
                                       return col.name == target.col_name &&
                                              (target.tab_name.empty() || col.tab_name == target.tab_name);
                                   });
            if (it == child->cols().end()) throw ColumnNotFoundError(target.col_name);
            columns.emplace_back(*it, desc);
        }
        return std::make_unique<SortExecutor>(std::move(child), columns);
    }
    if (auto limit = std::dynamic_pointer_cast<LimitPlan>(plan)) {
        auto child = make_explain_executor(sm_manager, limit->subplan_, correlated);
        if (child == nullptr) return nullptr;
        return std::make_unique<LimitExecutor>(std::move(child), limit->limit_);
    }
    return nullptr;
}

struct ExplainOuterTuple {
    std::unique_ptr<RmRecord> record;
    std::vector<ColMeta> columns;
    std::vector<bool> nulls;
};

using ExplainOuterRows = std::vector<ExplainOuterTuple>;

ExplainOuterRows materialize_plan_output(SmManager *sm_manager,
                                         const std::shared_ptr<Plan> &plan,
                                         const ExplainOuterRows *outer_rows = nullptr) {
    ExplainOuterRows result;
    auto correlated = outer_rows == nullptr
                          ? std::shared_ptr<CorrelatedTupleContext>()
                          : std::make_shared<CorrelatedTupleContext>();
    auto executor = make_explain_executor(sm_manager, plan, correlated);
    if (executor == nullptr) return result;

    auto collect_current_run = [&]() {
        for (executor->beginTuple(); !executor->is_end(); executor->nextTuple()) {
            ExplainOuterTuple tuple;
            tuple.columns = executor->cols();
            tuple.nulls.assign(tuple.columns.size(), false);
            if (const auto *mask = executor->null_mask(); mask != nullptr) {
                for (size_t i = 0; i < tuple.nulls.size() && i < mask->size(); ++i) {
                    tuple.nulls[i] = (*mask)[i];
                }
            }
            tuple.record = executor->Next();
            if (tuple.record == nullptr) {
                throw InternalError("EXPLAIN executor returned no current tuple");
            }
            result.push_back(std::move(tuple));
        }
    };

    if (outer_rows == nullptr) {
        collect_current_run();
    } else {
        for (const auto &outer : *outer_rows) {
            correlated->bind(*outer.record, outer.columns, &outer.nulls);
            collect_current_run();
        }
        correlated->clear();
    }
    return result;
}

size_t count_plan_output(SmManager *sm_manager, const std::shared_ptr<Plan> &plan,
                         const ExplainOuterRows *outer_rows = nullptr) {
    if (auto projection = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        return count_plan_output(sm_manager, projection->subplan_, outer_rows);
    }
    if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) {
        return count_plan_output(sm_manager, sort->subplan_, outer_rows);
    }
    if (auto limit = std::dynamic_pointer_cast<LimitPlan>(plan)) {
        if (outer_rows != nullptr) return materialize_plan_output(sm_manager, plan, outer_rows).size();
        return std::min(limit->limit_, count_plan_output(sm_manager, limit->subplan_));
    }
    if (auto agg = std::dynamic_pointer_cast<AggPlan>(plan)) {
        return materialize_plan_output(sm_manager, plan, outer_rows).size();
    }
    if (auto union_plan = std::dynamic_pointer_cast<UnionPlan>(plan)) {
        if (outer_rows != nullptr) return materialize_plan_output(sm_manager, plan, outer_rows).size();
        size_t rows = 0;
        for (const auto &child : union_plan->subplans_) rows += count_plan_output(sm_manager, child);
        return rows;
    }
    if (outer_rows != nullptr) return materialize_plan_output(sm_manager, plan, outer_rows).size();
    auto executor = make_explain_executor(sm_manager, plan);
    if (executor == nullptr) return 0;
    size_t rows = 0;
    for (executor->beginTuple(); !executor->is_end(); executor->nextTuple()) rows++;
    return rows;
}

std::set<std::string> plan_table_set(const std::shared_ptr<Plan> &plan) {
    std::set<std::string> bindings;
    collect_plan_bindings(plan, bindings);
    return bindings;
}

void render_explain_plan(SmManager *sm_manager, const std::shared_ptr<Plan> &plan,
                         int depth, size_t outer_rows, long long forced_rows, bool root_select_all,
                         std::vector<std::string> &lines,
                         const ExplainOuterRows *correlated_rows = nullptr) {
    if (auto proj = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        std::vector<std::string> cols;
        if (root_select_all) {
            cols.push_back("*");
        } else {
            for (const auto &col : proj->sel_cols_) {
                cols.push_back(display_col(col));
            }
        }
        lines.push_back(std::string(depth, '\t') + "Project(columns=[" +
                        join_strings_sorted(cols) + "], rows=" +
                        std::to_string(count_plan_output(sm_manager, plan, correlated_rows)) + ")");
        render_explain_plan(sm_manager, proj->subplan_, depth + 1, outer_rows, forced_rows,
                            false, lines, correlated_rows);
        return;
    }
    if (auto filter = std::dynamic_pointer_cast<CorrelatedFilterPlan>(plan)) {
        std::vector<std::string> conds;
        for (const auto &cond : filter->predicates_) conds.push_back(cond_to_string(cond));
        lines.push_back(std::string(depth, '\t') + "CorrelatedFilter(condition=[" +
                        join_strings_sorted(conds) + "])");
        render_explain_plan(sm_manager, filter->subplan_, depth + 1, outer_rows,
                            forced_rows, false, lines, correlated_rows);
        return;
    }
    if (auto rename = std::dynamic_pointer_cast<RenamePlan>(plan)) {
        std::string alias = rename->output_cols_.empty() ? "" : rename->output_cols_[0].tab_name;
        lines.push_back(std::string(depth, '\t') + "DerivedTable(alias=" + alias + ")");
        render_explain_plan(sm_manager, rename->subplan_, depth + 1, outer_rows,
                            forced_rows, false, lines, correlated_rows);
        return;
    }
    if (auto filter = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        std::vector<std::string> conds;
        for (const auto &cond : filter->predicates_) conds.push_back(cond_to_string(cond));
        lines.push_back(std::string(depth, '\t') + "Filter(condition=[" +
                        join_strings_sorted(conds) + "])");
        render_explain_plan(sm_manager, filter->subplan_, depth + 1, outer_rows,
                            forced_rows, false, lines, correlated_rows);
        return;
    }
    if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) {
        std::vector<std::string> cols;
        for (const auto &[col, desc] : sort->sort_cols_) {
            cols.push_back(display_col(col) + (desc ? " DESC" : " ASC"));
        }
        lines.push_back(std::string(depth, '\t') + "Sort(columns=[" + join_strings_sorted(cols) + "])");
        render_explain_plan(sm_manager, sort->subplan_, depth + 1, outer_rows, forced_rows,
                            false, lines, correlated_rows);
        return;
    }
    if (auto limit = std::dynamic_pointer_cast<LimitPlan>(plan)) {
        lines.push_back(std::string(depth, '\t') + "Limit(count=" + std::to_string(limit->limit_) + ")");
        render_explain_plan(sm_manager, limit->subplan_, depth + 1, outer_rows, forced_rows,
                            false, lines, correlated_rows);
        return;
    }
    if (auto agg = std::dynamic_pointer_cast<AggPlan>(plan)) {
        std::vector<std::string> groups;
        std::vector<std::string> aggs;
        for (const auto &col : agg->group_cols_) groups.push_back(display_col(col));
        for (const auto &expr : agg->agg_exprs_) aggs.push_back(expr.to_string());
        lines.push_back(std::string(depth, '\t') + "Aggregate(group_by=[" +
                        join_strings_sorted(groups) + "], aggregates=[" + join_strings_sorted(aggs) +
                        "], rows=" +
                        std::to_string(count_plan_output(sm_manager, plan, correlated_rows)) + ")");
        render_explain_plan(sm_manager, agg->subplan_, depth + 1, outer_rows, forced_rows,
                            false, lines, correlated_rows);
        return;
    }
    std::string indent(depth, '\t');
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        size_t rows = forced_rows >= 0 ? static_cast<size_t>(forced_rows) : count_scan_rows(sm_manager, *scan) * outer_rows;
        const std::string relation = scan->binding_name_ == scan->tab_name_
                                         ? scan->tab_name_
                                         : scan->tab_name_ + " AS " + scan->binding_name_;
        if (!scan->predicates_.empty()) {
            std::vector<std::string> predicates;
            for (const auto &cond : scan->predicates_) predicates.push_back(cond_to_string(cond));
            lines.push_back(indent + "Filter(condition=[" + join_strings_sorted(predicates) +
                            "], rows=" + std::to_string(rows) + ")");
            indent.push_back('\t');
        }
        size_t raw_rows = 0;
        auto fh = sm_manager->fhs_.find(scan->tab_name_);
        if (fh != sm_manager->fhs_.end() && fh->second != nullptr) {
            for (RmScan raw_scan(fh->second.get()); !raw_scan.is_end(); raw_scan.next()) raw_rows++;
        }
        std::string scan_line = std::string(depth + (scan->predicates_.empty() ? 0 : 1), '\t') +
                                "Scan(table=" + relation + ", type=" +
                                (scan->tag == T_IndexScan ? "IndexScan" : "SeqScan");
        if (scan->tag == T_IndexScan && !scan->index_col_names_.empty()) {
            scan_line += ", using_index=(" + join_strings_sorted(scan->index_col_names_) + ")";
        }
        // NLJ reports every inner-table visit (outer rows * raw rows).  INLJ
        // passes forced_rows as the number of successful index probes, which is
        // the runtime statistic required by the assignment rather than the
        // hypothetical full-scan count.
        const size_t scan_rows = forced_rows >= 0 ? static_cast<size_t>(forced_rows)
                                                  : raw_rows * outer_rows;
        scan_line += ", rows=" + std::to_string(scan_rows) + ")";
        lines.push_back(scan_line);
        return;
    }
    if (auto join = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        size_t left_rows = count_plan_output(sm_manager, join->left_, correlated_rows);
        size_t join_rows = count_plan_output(sm_manager, plan, correlated_rows);
        auto tables = plan_table_set(plan);
        std::vector<std::string> table_names(tables.begin(), tables.end());
        std::vector<std::string> conds;
        for (auto &cond : join->on_predicates_) conds.push_back(cond_to_string(cond));
        static const std::map<JoinType, std::string> join_types = {
            {INNER_JOIN, "INNER"}, {LEFT_JOIN, "LEFT"}, {RIGHT_JOIN, "RIGHT"},
            {FULL_JOIN, "FULL"}, {CROSS_JOIN, "CROSS"},
            {LEFT_SEMI_JOIN, "LEFT SEMI"}, {RIGHT_SEMI_JOIN, "RIGHT SEMI"},
            {LEFT_ANTI_JOIN, "LEFT ANTI"}, {RIGHT_ANTI_JOIN, "RIGHT ANTI"}};
        std::string type_name = join_types.at(join->type);
        if (join->natural_) type_name = "NATURAL " + type_name;
        if (join->lateral_) type_name += " LATERAL";
        // Keep the original assignment's exact EXPLAIN format for ordinary
        // INNER joins.  Both explicit INNER JOIN and comma joins connected by
        // an equality predicate are normalized to this JoinType by Planner.
        // Modified join semantics retain type= so they remain distinguishable.
        const bool legacy_inner = join->type == INNER_JOIN &&
                                  !join->natural_ && !join->lateral_;
        const std::string type_field = legacy_inner ? "" : "type=" + type_name + ", ";
        lines.push_back(indent + "Join(" + type_field + "tables=[" +
                        join_strings_sorted(table_names) + "], condition=[" +
                        join_strings_sorted(conds) + "], rows=" + std::to_string(join_rows) + ")");
        render_explain_plan(sm_manager, join->left_, depth + 1, 1, -1, false, lines,
                            correlated_rows);
        bool inlj = join->tag == T_IndexNestLoop;
        if (join->lateral_) {
            auto lateral_rows = materialize_plan_output(sm_manager, join->left_, correlated_rows);
            render_explain_plan(sm_manager, join->right_, depth + 1, lateral_rows.size(),
                                -1, false, lines, &lateral_rows);
        } else {
            render_explain_plan(sm_manager, join->right_, depth + 1, left_rows,
                                inlj ? static_cast<long long>(join_rows) : -1, false, lines,
                                correlated_rows);
        }
    }
}

}  // namespace

void QlManager::explain_query_plan(std::shared_ptr<ExplainPlan> plan, Context *context) {
    auto select_ast = std::dynamic_pointer_cast<ast::SelectStmt>(plan->query_->parse);
    std::vector<std::string> lines;
    const bool select_all = select_ast != nullptr && select_ast->cols.empty() && select_ast->aggs.empty();
    render_explain_plan(sm_manager_, plan->select_plan_, 0, 1, -1, select_all, lines);

    std::ostringstream os;
    for (auto &line : lines) os << line << "\n";
    std::string out = os.str();

    // Wire v3 has no legacy text-response frame.  Expose an EXPLAIN plan as a
    // regular one-column result set so clients can assert both the plan shape
    // and its runtime row counts.  Keep the historical text buffer below for
    // the original NUL-terminated protocol and output.txt compatibility.
    if (context != nullptr && context->wire_sink_ != nullptr) {
        context->wire_sink_->on_meta({{"QUERY PLAN", TYPE_STRING}});
        uint64_t emitted = 0;
        for (const auto &line : lines) {
            WireCell cell;
            cell.type = TYPE_STRING;
            cell.str_val = line;
            context->wire_sink_->on_row({std::move(cell)});
            ++emitted;
            if (context->wire_sink_->failed()) break;
        }
        context->wire_sink_->on_end(emitted);
    }
    if (context->data_send_ && context->offset_) {
        memcpy(context->data_send_ + *(context->offset_), out.c_str(), out.size());
        *(context->offset_) += (int)out.size();
        context->data_send_[*(context->offset_)] = '\0';
    }

    append_output_file(out);
}

// 执行select语句，select语句的输出除了需要返回客户端外，还需要写入output.txt文件中
void QlManager::select_from(std::unique_ptr<AbstractExecutor> executorTreeRoot, std::vector<TabCol> sel_cols,
                            Context *context, int limit) {
    if (context) context->ser_in_select_ = true;   // 题9 SER：本次扫描属于 SELECT，记录读集

    std::vector<std::string> captions;
    captions.reserve(sel_cols.size());
    for (auto &sel_col : sel_cols) {
        // 决赛：col AS alias——输出列名（wire META / 文本表头）用别名
        captions.push_back(sel_col.alias.empty() ? sel_col.col_name : sel_col.alias);
    }
    // Union / 部分聚合路径无 ProjectionPlan，从执行器输出列推断表头
    if (captions.empty()) {
        for (auto &col : executorTreeRoot->cols()) {
            captions.push_back(col.name);
        }
    }

    // 决赛 Wire Protocol v3：结果走二进制 META/ROW*/RESULT_END，不进文本 RecordPrinter/output.txt
    if (context && context->wire_sink_) {
        auto &wcols = executorTreeRoot->cols();
        std::vector<std::pair<std::string, ColType>> meta;
        meta.reserve(wcols.size());
        for (size_t i = 0; i < wcols.size() && i < captions.size(); i++) {
            meta.emplace_back(captions[i], wcols[i].type);
        }
        context->wire_sink_->on_meta(meta);
        uint64_t num_rec = 0;
        for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
            if (limit >= 0 && num_rec >= (uint64_t)limit) break;
            auto Tuple = executorTreeRoot->Next();
            const std::vector<bool> *nulls = executorTreeRoot->null_mask();
            std::vector<WireCell> cells;
            cells.reserve(wcols.size());
            size_t ci = 0;
            for (auto &col : wcols) {
                WireCell cell;
                cell.type = col.type;
                if (nulls != nullptr && ci < nulls->size() && (*nulls)[ci]) cell.is_null = true;
                ci++;
                char *p = Tuple->data + col.offset;
                if (col.type == TYPE_INT) {
                    cell.int_val = *(int *)p;
                } else if (col.type == TYPE_FLOAT) {
                    cell.float_val = *(float *)p;
                } else {
                    std::string s((char *)p, col.len);
                    s.resize(strlen(s.c_str()));
                    cell.str_val = std::move(s);
                }
                cells.push_back(std::move(cell));
            }
            context->wire_sink_->on_row(cells);
            num_rec++;
            if (context->wire_sink_->failed()) break;
        }
        context->wire_sink_->on_end(num_rec);
        return;
    }

    // Print header into buffer
    RecordPrinter rec_printer(captions.size());
    rec_printer.print_separator(context);
    rec_printer.print_record(captions, context);
    rec_printer.print_separator(context);
    // print header into file（框架原始紧凑格式：output.txt 与客户端带框输出是两种格式）
    std::fstream outfile;
    if (output_file_enabled()) {
        outfile.open("output.txt", std::ios::out | std::ios::app);
        outfile << "|";
        for(size_t i = 0; i < captions.size(); ++i) {
            outfile << " " << captions[i] << " |";
        }
        outfile << "\n";
    }

    // Print records
    size_t num_rec = 0;
    // 执行query_plan
    for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
        if (limit >= 0 && num_rec >= (size_t)limit) break;
        auto Tuple = executorTreeRoot->Next();
        const std::vector<bool> *nulls = executorTreeRoot->null_mask();
        std::vector<std::string> columns;
        size_t column_index = 0;
        for (auto &col : executorTreeRoot->cols()) {
            std::string col_str;
            char *rec_buf = Tuple->data + col.offset;
            if (nulls != nullptr && column_index < nulls->size() && (*nulls)[column_index]) {
                col_str = "NULL";
            } else if (col.type == TYPE_INT) {
                col_str = std::to_string(*(int *)rec_buf);
            } else if (col.type == TYPE_FLOAT) {
                col_str = std::to_string(*(float *)rec_buf);
            } else if (col.type == TYPE_STRING) {
                col_str = std::string((char *)rec_buf, col.len);
                col_str.resize(strlen(col_str.c_str()));
            }
            columns.push_back(col_str);
            column_index++;
        }
        // print record into buffer
        rec_printer.print_record(columns, context);
        // print record into file
        if (outfile.is_open()) {
            outfile << "|";
            for(size_t i = 0; i < columns.size(); ++i) {
                outfile << " " << columns[i] << " |";
            }
            outfile << "\n";
        }
        num_rec++;
    }
    if (outfile.is_open()) outfile.close();
    // Print footer + record count into buffer
    rec_printer.print_separator(context);
    RecordPrinter::print_record_count(num_rec, context);
}

// 执行DML语句
void QlManager::run_dml(std::unique_ptr<AbstractExecutor> exec){
    exec->Next();
}

namespace {

struct CsvField {
    char *data;
    size_t size;
};

void split_csv_line(std::string &line, std::vector<CsvField> &fields) {
    fields.clear();
    const size_t size = line.size();
    line.push_back('\0');
    char *data = line.data();
    size_t begin = 0;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] != ',') continue;
        data[i] = '\0';
        fields.push_back({data + begin, i - begin});
        begin = i + 1;
    }
    fields.push_back({data + begin, size - begin});
}

void make_load_index_key(const IndexMeta &index, const char *record, char *key) {
    int offset = 0;
    for (const auto &col : index.cols) {
        memcpy(key + offset, record + col.offset, col.len);
        offset += col.len;
    }
}

void bulk_load_sorted_index(RmFileHandle *fh, const IndexMeta &index,
                            IxIndexHandle *ih, size_t count) {
    RmScan scan(fh);
    ih->bulk_load(static_cast<long>(count), [&](char *key, Rid *rid) {
        if (scan.is_end()) {
            throw InternalError("LOAD index build ended before expected row count");
        }
        make_load_index_key(index, scan.record_data(), key);
        *rid = scan.rid();
        scan.next();
    });
}

}  // namespace

void QlManager::run_load(const std::string &file_path, const std::string &tab_name, Context *context) {
    TabMeta tab = sm_manager_->db_.get_table(tab_name);
    RmFileHandle *fh = sm_manager_->fhs_.at(tab_name).get();
    std::ifstream infile(file_path);
    if (!infile.is_open()) {
        throw RMDBError("Cannot open load file: " + file_path + "\n");
    }
    const int rec_size = fh->get_file_hdr().record_size;

    // Finals create indexes before LOAD.  Incremental insertion of every CSV
    // row makes that path O(rows * tree height), even though generated data is
    // normally already in primary-key order.  On an empty table, defer each
    // index independently while its incoming keys remain strictly increasing;
    // such an index can be built bottom-up in one linear pass.  If an index is
    // not ordered (for example a secondary lookup index), materialize the
    // already sorted prefix and immediately fall back to ordinary inserts.
    struct LoadIndexState {
        const IndexMeta *meta = nullptr;
        IxIndexHandle *handle = nullptr;
        bool deferred = false;
        std::vector<ColType> types;
        std::vector<int> lens;
        std::vector<char> previous_key;
        std::vector<char> current_key;
    };
    bool table_empty = false;
    {
        RmScan probe(fh);
        table_empty = probe.is_end();
    }
    std::vector<LoadIndexState> indexes;
    indexes.reserve(tab.indexes.size());
    for (const auto &index : tab.indexes) {
        LoadIndexState state;
        state.meta = &index;
        state.handle = sm_manager_->ihs_.at(
            sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        state.deferred = table_empty;
        state.previous_key.resize(index.col_tot_len);
        state.current_key.resize(index.col_tot_len);
        for (const auto &col : index.cols) {
            state.types.push_back(col.type);
            state.lens.push_back(col.len);
        }
        indexes.push_back(std::move(state));
    }

    std::string line;
    std::vector<CsvField> fields;
    fields.reserve(tab.cols.size());
    RmRecord rec(rec_size);
    size_t loaded_rows = 0;
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        split_csv_line(line, fields);
        if (fields.size() != tab.cols.size()) {
            throw RMDBError("Column count mismatch in load file\n");
        }
        // CSV 首行常为列名表头（如 w_id,w_name,...），跳过以免多插入一行
        bool is_header = true;
        for (size_t i = 0; i < tab.cols.size(); ++i) {
            if (fields[i].size != tab.cols[i].name.size() ||
                memcmp(fields[i].data, tab.cols[i].name.data(), fields[i].size) != 0) {
                is_header = false;
                break;
            }
        }
        if (is_header) continue;
        memset(rec.data, 0, rec_size);
        for (size_t i = 0; i < tab.cols.size(); ++i) {
            auto &col = tab.cols[i];
            if (col.type == TYPE_INT) {
                *(int *)(rec.data + col.offset) = std::atoi(fields[i].data);
            } else if (col.type == TYPE_FLOAT) {
                *(float *)(rec.data + col.offset) = static_cast<float>(std::atof(fields[i].data));
            } else {
                size_t cpy = std::min(fields[i].size, static_cast<size_t>(col.len));
                memcpy(rec.data + col.offset, fields[i].data, cpy);
            }
        }

        for (auto &index : indexes) {
            make_load_index_key(*index.meta, rec.data, index.current_key.data());
            if (index.deferred && loaded_rows > 0 &&
                ix_compare(index.previous_key.data(), index.current_key.data(),
                           index.types, index.lens) >= 0) {
                // The current row is not in the heap yet, so the existing
                // prefix is still strictly ordered and can be bulk-built.
                bulk_load_sorted_index(fh, *index.meta, index.handle, loaded_rows);
                index.deferred = false;
            }
        }

        Rid rid;
        if (context && context->log_mgr_ && context->txn_) {
            // Reserve first so the WAL record can carry the final RID, append
            // WAL before publishing the heap slot, then make the row visible.
            // This closes the full-page unpin -> WAL append gap during LOAD.
            rid = fh->reserve_insert_slot();
            try {
                InsertLogRecord lr(context->txn_->get_transaction_id(), rec, rid, tab_name);
                context->txn_->set_prev_lsn(context->log_mgr_->add_log_to_buffer(&lr));
                fh->publish_insert_slot(rid, rec.data);
            } catch (...) {
                fh->cancel_insert_slot(rid);
                throw;
            }
        } else {
            rid = fh->insert_record(rec.data, context);
        }
        for (auto &index : indexes) {
            if (index.deferred) {
                index.previous_key.swap(index.current_key);
            } else {
                index.handle->insert_entry(index.current_key.data(), rid,
                                           context ? context->txn_ : nullptr);
            }
        }
        ++loaded_rows;
    }

    for (auto &index : indexes) {
        if (index.deferred && loaded_rows > 0) {
            bulk_load_sorted_index(fh, *index.meta, index.handle, loaded_rows);
            index.deferred = false;
        }
    }
    // 每表装载完成后（语句事务提交后）打检查点：装载数据全量落盘 + 推进 restart
    // 起点。否则崩后恢复须全量重放装载 WAL（W=50 ~5.9GB）+ 全量重建索引，远超
    // 90s 就绪预算；且堆头页/索引页从不主动落盘，两条腿都断则装载全丢。
    if (context) context->checkpoint_after_commit_ = true;
}
