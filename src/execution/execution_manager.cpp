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
#include "executor_index_scan.h"
#include "executor_insert.h"
#include "executor_nestedloop_join.h"
#include "executor_projection.h"
#include "executor_seq_scan.h"
#include "executor_update.h"
#include "index/ix.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "record_printer.h"
#include "analyze/analyze.h"
#include "common/output_control.h"
#include "explain_plan.h"
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

bool plan_has_join(const std::shared_ptr<Plan> &plan) {
    if (!plan) return false;
    if (std::dynamic_pointer_cast<JoinPlan>(plan)) return true;
    if (auto p = std::dynamic_pointer_cast<ProjectionPlan>(plan)) return plan_has_join(p->subplan_);
    if (auto s = std::dynamic_pointer_cast<SortPlan>(plan)) return plan_has_join(s->subplan_);
    if (auto l = std::dynamic_pointer_cast<LimitPlan>(plan)) return plan_has_join(l->subplan_);
    if (auto a = std::dynamic_pointer_cast<AggPlan>(plan)) return plan_has_join(a->subplan_);
    return false;
}

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

std::string cond_to_string(const Condition &cond) {
    return cond.lhs_col.tab_name + "." + cond.lhs_col.col_name + join_op_to_string(cond.op) +
           cond.rhs_col.tab_name + "." + cond.rhs_col.col_name;
}

size_t count_scan_rows(SmManager *sm_manager, const ScanPlan &scan) {
    size_t count = 0;
    SeqScanExecutor exec(sm_manager, scan.tab_name_, scan.conds_, nullptr);
    for (exec.beginTuple(); !exec.is_end(); exec.nextTuple()) count++;
    return count;
}

bool eval_explain_cond(const Condition &cond, const std::vector<ColMeta> &cols, const char *data) {
    auto lhs_it = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &c) {
        return c.name == cond.lhs_col.col_name &&
               (cond.lhs_col.tab_name.empty() || c.tab_name == cond.lhs_col.tab_name);
    });
    if (lhs_it == cols.end()) return false;
    const char *lhs = data + lhs_it->offset;
    const char *rhs = nullptr;
    if (cond.is_rhs_val) {
        rhs = cond.rhs_val.raw->data;
    } else {
        auto rhs_it = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &c) {
            return c.name == cond.rhs_col.col_name &&
                   (cond.rhs_col.tab_name.empty() || c.tab_name == cond.rhs_col.tab_name);
        });
        if (rhs_it == cols.end()) return false;
        rhs = data + rhs_it->offset;
    }
    return SeqScanExecutor::compare_value(lhs, rhs, lhs_it->len, lhs_it->type, cond.op);
}

void collect_plan_tables_conds(const std::shared_ptr<Plan> &plan, std::vector<std::string> &tables,
                               std::vector<Condition> &conds) {
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        tables.push_back(scan->tab_name_);
        conds.insert(conds.end(), scan->conds_.begin(), scan->conds_.end());
    } else if (auto join = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        collect_plan_tables_conds(join->left_, tables, conds);
        collect_plan_tables_conds(join->right_, tables, conds);
        conds.insert(conds.end(), join->conds_.begin(), join->conds_.end());
    } else if (auto proj = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        collect_plan_tables_conds(proj->subplan_, tables, conds);
    } else if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) {
        collect_plan_tables_conds(sort->subplan_, tables, conds);
    } else if (auto limit = std::dynamic_pointer_cast<LimitPlan>(plan)) {
        collect_plan_tables_conds(limit->subplan_, tables, conds);
    }
}

size_t product_count_rec(SmManager *sm_manager, const std::vector<std::string> &tables, size_t idx,
                         const std::vector<Condition> &conds, std::vector<char> &buf,
                         std::vector<ColMeta> &cols, size_t len) {
    if (idx == tables.size()) {
        for (auto &cond : conds) {
            if (!eval_explain_cond(cond, cols, buf.data())) return 0;
        }
        return 1;
    }
    const std::string &tab_name = tables[idx];
    auto fh = sm_manager->fhs_.at(tab_name).get();
    auto &tab = sm_manager->db_.get_table(tab_name);
    size_t tuple_len = tab.cols.back().offset + tab.cols.back().len;
    size_t saved_cols = cols.size();
    size_t count = 0;
    for (RmScan scan(fh); !scan.is_end(); scan.next()) {
        auto rec = fh->get_record(scan.rid(), nullptr);
        buf.resize(len + tuple_len);
        memcpy(buf.data() + len, rec->data, tuple_len);
        cols.resize(saved_cols);
        for (auto col : tab.cols) {
            col.offset += len;
            cols.push_back(col);
        }
        count += product_count_rec(sm_manager, tables, idx + 1, conds, buf, cols, len + tuple_len);
    }
    cols.resize(saved_cols);
    buf.resize(len);
    return count;
}

size_t count_plan_output(SmManager *sm_manager, const std::shared_ptr<Plan> &plan) {
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) return count_scan_rows(sm_manager, *scan);
    if (auto join = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        std::vector<std::string> tables;
        std::vector<Condition> conds;
        collect_plan_tables_conds(plan, tables, conds);
        std::vector<char> buf;
        std::vector<ColMeta> cols;
        return product_count_rec(sm_manager, tables, 0, conds, buf, cols, 0);
    }
    if (auto proj = std::dynamic_pointer_cast<ProjectionPlan>(plan)) return count_plan_output(sm_manager, proj->subplan_);
    if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) return count_plan_output(sm_manager, sort->subplan_);
    if (auto limit = std::dynamic_pointer_cast<LimitPlan>(plan)) {
        return std::min(limit->limit_, count_plan_output(sm_manager, limit->subplan_));
    }
    return 0;
}

std::set<std::string> plan_table_set(const std::shared_ptr<Plan> &plan) {
    std::vector<std::string> tables;
    std::vector<Condition> conds;
    collect_plan_tables_conds(plan, tables, conds);
    return std::set<std::string>(tables.begin(), tables.end());
}

void collect_required_cols(const std::shared_ptr<Query> &query, std::map<std::string, std::set<std::string>> &required) {
    for (auto &col : query->cols) required[col.tab_name].insert(col.col_name);
    for (auto &cond : query->conds) {
        required[cond.lhs_col.tab_name].insert(cond.lhs_col.col_name);
        if (!cond.is_rhs_val) required[cond.rhs_col.tab_name].insert(cond.rhs_col.col_name);
    }
}

std::vector<std::string> sorted_project_cols_for_scan(SmManager *sm_manager, const std::string &tab_name,
                                                      const std::map<std::string, std::set<std::string>> &required) {
    std::vector<std::string> cols;
    auto it = required.find(tab_name);
    if (it != required.end() && !it->second.empty()) {
        for (auto &name : it->second) cols.push_back(tab_name + "." + name);
    } else {
        for (auto &col : sm_manager->db_.get_table(tab_name).cols) cols.push_back(tab_name + "." + col.name);
    }
    std::sort(cols.begin(), cols.end());
    return cols;
}

void render_explain_plan(SmManager *sm_manager, const std::shared_ptr<Plan> &plan,
                         const std::map<std::string, std::set<std::string>> &required,
                         int depth, size_t outer_rows, long long forced_rows,
                         std::vector<std::string> &lines) {
    if (auto proj = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        render_explain_plan(sm_manager, proj->subplan_, required, depth, outer_rows, forced_rows, lines);
        return;
    }
    if (auto sort = std::dynamic_pointer_cast<SortPlan>(plan)) {
        render_explain_plan(sm_manager, sort->subplan_, required, depth, outer_rows, forced_rows, lines);
        return;
    }
    if (auto limit = std::dynamic_pointer_cast<LimitPlan>(plan)) {
        render_explain_plan(sm_manager, limit->subplan_, required, depth, outer_rows, forced_rows, lines);
        return;
    }
    std::string indent(depth, '\t');
    if (auto scan = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        size_t rows = forced_rows >= 0 ? static_cast<size_t>(forced_rows) : count_scan_rows(sm_manager, *scan) * outer_rows;
        auto proj_cols = sorted_project_cols_for_scan(sm_manager, scan->tab_name_, required);
        lines.push_back(indent + "Project(columns=[" + join_strings_sorted(proj_cols) + "], rows=" + std::to_string(rows) + ")");
        std::string scan_line = std::string(depth + 1, '\t') + "Scan(table=" + scan->tab_name_ + ", type=" +
                                (scan->tag == T_IndexScan ? "IndexScan" : "SeqScan");
        if (scan->tag == T_IndexScan && !scan->index_col_names_.empty()) {
            scan_line += ", using_index=(" + join_strings_sorted(scan->index_col_names_) + ")";
        }
        scan_line += ", rows=" + std::to_string(rows) + ")";
        lines.push_back(scan_line);
        return;
    }
    if (auto join = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        size_t left_rows = count_plan_output(sm_manager, join->left_);
        size_t join_rows = count_plan_output(sm_manager, plan);
        auto tables = plan_table_set(plan);
        std::vector<std::string> table_names(tables.begin(), tables.end());
        std::vector<std::string> conds;
        for (auto &cond : join->conds_) conds.push_back(cond_to_string(cond));
        lines.push_back(indent + "Join(tables=[" + join_strings_sorted(table_names) + "], condition=[" +
                        join_strings_sorted(conds) + "], rows=" + std::to_string(join_rows) + ")");
        render_explain_plan(sm_manager, join->left_, required, depth + 1, 1, -1, lines);
        bool inlj = join->tag == T_IndexNestLoop;
        render_explain_plan(sm_manager, join->right_, required, depth + 1, left_rows,
                            inlj ? static_cast<long long>(join_rows) : -1, lines);
    }
}

}  // namespace

void QlManager::explain_query_plan(std::shared_ptr<ExplainPlan> plan, Context *context) {
    bool has_user_alias = false;
    for (auto &kv : plan->query_->real2alias) {
        if (kv.first != kv.second) {
            has_user_alias = true;
            break;
        }
    }
    if (!plan_has_join(plan->select_plan_) || has_user_alias) {
        run_explain(plan->query_, context);
        return;
    }

    std::map<std::string, std::set<std::string>> required;
    collect_required_cols(plan->query_, required);
    size_t rows = count_plan_output(sm_manager_, plan->select_plan_);

    auto select_ast = std::dynamic_pointer_cast<ast::SelectStmt>(plan->query_->parse);
    std::vector<std::string> root_cols;
    if (select_ast != nullptr && select_ast->cols.empty() && select_ast->aggs.empty()) {
        root_cols.push_back("*");
    } else {
        for (auto &col : plan->query_->cols) root_cols.push_back(col.tab_name + "." + col.col_name);
    }

    std::vector<std::string> lines;
    lines.push_back("Project(columns=[" + join_strings_sorted(root_cols) + "], rows=" + std::to_string(rows) + ")");
    render_explain_plan(sm_manager_, plan->select_plan_, required, 1, 1, -1, lines);

    std::ostringstream os;
    for (auto &line : lines) os << line << "\n";
    std::string out = os.str();
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

    // 题10：聚合查询（一致性检测 SQL：COUNT/MAX/MIN/SUM，单表）
    if (!sel_cols.empty() && sel_cols[0].agg_type != 0) {
        select_agg(std::move(executorTreeRoot), sel_cols, context);
        return;
    }
    std::vector<std::string> captions;
    captions.reserve(sel_cols.size());
    for (auto &sel_col : sel_cols) {
        captions.push_back(sel_col.col_name);
    }
    // Union / 部分聚合路径无 ProjectionPlan，从执行器输出列推断表头
    if (captions.empty()) {
        for (auto &col : executorTreeRoot->cols()) {
            captions.push_back(col.name);
        }
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
        std::vector<std::string> columns;
        for (auto &col : executorTreeRoot->cols()) {
            std::string col_str;
            char *rec_buf = Tuple->data + col.offset;
            if (col.type == TYPE_INT) {
                col_str = std::to_string(*(int *)rec_buf);
            } else if (col.type == TYPE_FLOAT) {
                col_str = std::to_string(*(float *)rec_buf);
            } else if (col.type == TYPE_STRING) {
                col_str = std::string((char *)rec_buf, col.len);
                col_str.resize(strlen(col_str.c_str()));
            }
            columns.push_back(col_str);
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

// 题10：聚合执行——单遍扫描折叠，输出单行带框结果（表头=AS 别名）
void QlManager::select_agg(std::unique_ptr<AbstractExecutor> executorTreeRoot,
                           std::vector<TabCol> &sel_cols, Context *context) {
    size_t n = sel_cols.size();
    std::vector<std::string> captions(n);
    for (size_t i = 0; i < n; i++) {
        // 无别名表头=裸列名（COUNT(*) 在 analyze 阶段已设别名 count(*)），与参考实现一致
        captions[i] = !sel_cols[i].alias.empty() ? sel_cols[i].alias : sel_cols[i].col_name;
    }
    std::vector<long long> cnt(n, 0);
    std::vector<long long> isum(n, 0);
    std::vector<double> fsum(n, 0.0);
    std::vector<int> ival(n, 0);
    std::vector<float> fval(n, 0.0f);
    std::vector<std::string> sval(n);
    std::vector<ColMeta> metas(n);
    for (size_t i = 0; i < n; i++) {
        for (auto &cm : executorTreeRoot->cols()) {
            if (cm.name == sel_cols[i].col_name) { metas[i] = cm; break; }
        }
    }

    for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
        auto Tuple = executorTreeRoot->Next();
        for (size_t i = 0; i < n; i++) {
            const ColMeta &cm = metas[i];
            char *p = Tuple->data + cm.offset;
            bool first = (cnt[i] == 0);
            cnt[i]++;
            if (sel_cols[i].agg_type == 1) continue;             // COUNT
            if (cm.type == TYPE_INT) {
                int v = *(int *)p;
                isum[i] += v;
                if (first || (sel_cols[i].agg_type == 2 && v > ival[i]) ||
                    (sel_cols[i].agg_type == 3 && v < ival[i])) ival[i] = v;
            } else if (cm.type == TYPE_FLOAT) {
                float v = *(float *)p;
                fsum[i] += v;
                if (first || (sel_cols[i].agg_type == 2 && v > fval[i]) ||
                    (sel_cols[i].agg_type == 3 && v < fval[i])) fval[i] = v;
            } else {
                std::string v((char *)p, cm.len);
                v.resize(strlen(v.c_str()));
                if (first || (sel_cols[i].agg_type == 2 && v > sval[i]) ||
                    (sel_cols[i].agg_type == 3 && v < sval[i])) sval[i] = v;
            }
        }
    }

    std::vector<std::string> row(n);
    for (size_t i = 0; i < n; i++) {
        int at = sel_cols[i].agg_type;
        const ColMeta &cm = metas[i];
        if (at == 1) {
            row[i] = std::to_string(cnt[i]);
        } else if (cnt[i] == 0) {
            row[i] = (metas[i].type == TYPE_FLOAT) ? "0.000000" : "0";   // 空集按列型零值，与参考实现一致
        } else if (cm.type == TYPE_INT) {
            row[i] = (at == 4) ? std::to_string(isum[i]) : std::to_string(ival[i]);
        } else if (cm.type == TYPE_FLOAT) {
            row[i] = (at == 4) ? std::to_string((float)fsum[i]) : std::to_string(fval[i]);
        } else {
            row[i] = sval[i];
        }
    }

    RecordPrinter rec_printer(n);
    rec_printer.print_separator(context);
    rec_printer.print_record(captions, context);
    rec_printer.print_separator(context);
    rec_printer.print_record(row, context);
    rec_printer.print_separator(context);
    RecordPrinter::print_record_count(1, context);
    if (output_file_enabled()) {
        std::ostringstream os;
        os << "|";
        for (size_t i = 0; i < n; ++i) os << " " << captions[i] << " |";
        os << "\n|";
        for (size_t i = 0; i < n; ++i) os << " " << row[i] << " |";
        os << "\n";
        append_output_file(os.str());
    }
}

// 执行DML语句
void QlManager::run_dml(std::unique_ptr<AbstractExecutor> exec){
    exec->Next();
}

namespace {

std::vector<std::string> split_csv_line(const std::string &line) {
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
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
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto fields = split_csv_line(line);
        if (fields.size() != tab.cols.size()) {
            throw RMDBError("Column count mismatch in load file\n");
        }
        // CSV 首行常为列名表头（如 w_id,w_name,...），跳过以免多插入一行
        bool is_header = true;
        for (size_t i = 0; i < tab.cols.size(); ++i) {
            if (fields[i] != tab.cols[i].name) {
                is_header = false;
                break;
            }
        }
        if (is_header) continue;
        RmRecord rec(rec_size);
        memset(rec.data, 0, rec_size);
        for (size_t i = 0; i < tab.cols.size(); ++i) {
            auto &col = tab.cols[i];
            if (col.type == TYPE_INT) {
                *(int *)(rec.data + col.offset) = std::atoi(fields[i].c_str());
            } else if (col.type == TYPE_FLOAT) {
                *(float *)(rec.data + col.offset) = static_cast<float>(std::atof(fields[i].c_str()));
            } else {
                size_t cpy = std::min(fields[i].size(), static_cast<size_t>(col.len));
                memcpy(rec.data + col.offset, fields[i].c_str(), cpy);
            }
        }
        Rid rid = fh->insert_record(rec.data, context);
        if (context && context->log_mgr_ && context->txn_) {
            InsertLogRecord lr(context->txn_->get_transaction_id(), rec, rid, tab_name);
            context->txn_->set_prev_lsn(context->log_mgr_->add_log_to_buffer(&lr));
        }
        for (auto &index : tab.indexes) {
            auto ih = sm_manager_->ihs_.at(
                sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
            std::vector<char> key(index.col_tot_len);
            int off = 0;
            for (auto &idx_col : index.cols) {
                memcpy(key.data() + off, rec.data + idx_col.offset, idx_col.len);
                off += idx_col.len;
            }
            ih->insert_entry(key.data(), rid, context ? context->txn_ : nullptr);
        }
    }
}

// 题4：EXPLAIN ANALYZE —— 构建优化后计划树、计数执行、输出计划树（不输出结果集）
void QlManager::run_explain(std::shared_ptr<Query> query, Context *context) {
    std::string tree = explain::run(query.get(), sm_manager_, context);
    append_output_file(tree);
    // 写客户端缓冲（不输出结果集，仅计划树）
    if (context->data_send_ && context->offset_) {
        size_t n = tree.size();
        memcpy(context->data_send_ + *context->offset_, tree.c_str(), n);
        *context->offset_ += (int)n;
        context->data_send_[*context->offset_] = '\0';
    }
}