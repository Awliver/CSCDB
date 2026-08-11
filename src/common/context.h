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

#include <string>
#include <vector>

#include "defs.h"
#include "transaction/transaction.h"
#include "transaction/concurrency/lock_manager.h"
#include "recovery/log_manager.h"

class TransactionManager;

// used for data_send
static int const_offset = -1;

// 决赛 Wire Protocol v3：查询结果的一个单元格（脱耦 ColMeta，避免 context.h 依赖 sm_meta.h）
struct WireCell {
    ColType type = TYPE_INT;
    int int_val = 0;
    float float_val = 0.0f;
    std::string str_val;
    bool is_null = false;    // 聚合/外连接等产生 SQL NULL 时，wire 发 present=0
};

// 决赛 Wire Protocol v3：执行侧只感知本抽象接口，不感知具体 socket/帧编码；
// 模板方法：on_meta 记录“本次是查询结果”，供上层判断非查询语句是否已经产生过结构化结果。
class WireResultSink {
public:
    virtual ~WireResultSink() = default;

    void on_meta(const std::vector<std::pair<std::string, ColType>> &cols) {
        sent_result_ = true;
        emit_meta(cols);
    }
    void on_row(const std::vector<WireCell> &cells) { emit_row(cells); }
    void on_end(uint64_t row_count) { emit_end(row_count); }
    bool sent_result() const { return sent_result_; }
    bool failed() const { return failed_; }

protected:
    virtual void emit_meta(const std::vector<std::pair<std::string, ColType>> &cols) = 0;
    virtual void emit_row(const std::vector<WireCell> &cells) = 0;
    virtual void emit_end(uint64_t row_count) = 0;
    bool failed_ = false;   // 子类：socket 写失败时置位，供上层提前中断

private:
    bool sent_result_ = false;
};

class Context {
public:
    Context (LockManager *lock_mgr, LogManager *log_mgr, 
            Transaction *txn, char *data_send = nullptr, int *offset = &const_offset)
        : lock_mgr_(lock_mgr), log_mgr_(log_mgr), txn_(txn),
          data_send_(data_send), offset_(offset) {
            ellipsis_ = false;
          }

    TransactionManager *txn_mgr_ = nullptr;
    LockManager *lock_mgr_;
    LogManager *log_mgr_;
    Transaction *txn_;
    char *data_send_;
    int *offset_;
    bool ellipsis_;
    bool ser_in_select_ = false;   // 题9 SER：当前扫描是否属于 SELECT（仅 SELECT 记录读集）
    WireResultSink *wire_sink_ = nullptr;  // 决赛：非空时查询结果走 Wire v3 二进制帧，不走 RecordPrinter
    // LOAD 语句置位：语句事务提交后做一次检查点（装载耐久性不能指望"数据页恰好被
    // 淘汰刷盘 + WAL 全量重放"——W=50 全量重放 + 索引重建远超崩后 90s 就绪预算）
    bool checkpoint_after_commit_ = false;
};
