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
#include "record_printer.h"
#include "analyze/analyze.h"
#include "explain_plan.h"

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
    }
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

    // 记录扫描前客户端缓冲位置：output.txt 写入与客户端完全一致的带框输出
    int buf_start = context && context->offset_ ? *context->offset_ : 0;

    // Print header into buffer
    RecordPrinter rec_printer(sel_cols.size());
    rec_printer.print_separator(context);
    rec_printer.print_record(captions, context);
    rec_printer.print_separator(context);

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
        num_rec++;
    }
    // Print footer + record count into buffer
    rec_printer.print_separator(context);
    RecordPrinter::print_record_count(num_rec, context);

    // 题9：扫描成功后，把本条 SELECT 的带框输出一次性写入 output.txt（与客户端一致）；
    // 若 SER 读侧在扫描中途中止则抛出，不会执行到此，output.txt 不产生残缺输出
    if (context && context->data_send_ && context->offset_ && *context->offset_ > buf_start) {
        std::fstream outfile;
        outfile.open("output.txt", std::ios::out | std::ios::app);
        outfile.write(context->data_send_ + buf_start, *context->offset_ - buf_start);
        outfile.close();
    }
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

    int buf_start = context && context->offset_ ? *context->offset_ : 0;
    RecordPrinter rec_printer(n);
    rec_printer.print_separator(context);
    rec_printer.print_record(captions, context);
    rec_printer.print_separator(context);
    rec_printer.print_record(row, context);
    rec_printer.print_separator(context);
    RecordPrinter::print_record_count(1, context);
    if (context && context->data_send_ && context->offset_ && *context->offset_ > buf_start) {
        std::fstream outfile;
        outfile.open("output.txt", std::ios::out | std::ios::app);
        outfile.write(context->data_send_ + buf_start, *context->offset_ - buf_start);
        outfile.close();
    }
}

// 执行DML语句
void QlManager::run_dml(std::unique_ptr<AbstractExecutor> exec){
    exec->Next();
}

// 题4：EXPLAIN ANALYZE —— 构建优化后计划树、计数执行、输出计划树（不输出结果集）
void QlManager::run_explain(std::shared_ptr<Query> query, Context *context) {
    std::string tree = explain::run(query.get(), sm_manager_, context);
    // 写 output.txt（评测产物）
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << tree;
    outfile.close();
    // 写客户端缓冲（不输出结果集，仅计划树）
    if (context->data_send_ && context->offset_) {
        size_t n = tree.size();
        memcpy(context->data_send_ + *context->offset_, tree.c_str(), n);
        *context->offset_ += (int)n;
        context->data_send_[*context->offset_] = '\0';
    }
}