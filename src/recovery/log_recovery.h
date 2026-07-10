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

#include <map>
#include <unordered_map>
#include <unordered_set>
#include "log_manager.h"
#include "storage/disk_manager.h"
#include "system/sm_manager.h"

class RedoLogsInPage {
public:
    RedoLogsInPage() { table_file_ = nullptr; }
    RmFileHandle* table_file_;
    std::vector<lsn_t> redo_logs_;   // 在该page上需要redo的操作的lsn
};

/* 题10：未提交事务的一条已记录操作（undo 用） */
struct PendingOp {
    LogType type;
    std::string table;
    Rid rid;
    std::string old_data;   // DELETE/UPDATE 的旧值
    std::string new_data;   // INSERT/UPDATE 的新值
    // P2：UPDATE_DELTA 的差异段（redo 用 new，undo 用 old）
    std::vector<WalDiffRange> delta_ranges;
    std::vector<std::string> delta_old;
    std::vector<std::string> delta_new;
};

class RecoveryManager {
public:
    RecoveryManager(DiskManager* disk_manager, BufferPoolManager* buffer_pool_manager, SmManager* sm_manager) {
        disk_manager_ = disk_manager;
        buffer_pool_manager_ = buffer_pool_manager;
        sm_manager_ = sm_manager;
    }

    void set_log_manager(LogManager* log_manager) { log_manager_ = log_manager; }

    void analyze();
    void redo();
    void undo();
    void undo_pass();

private:
    // 从 offset 读出一条完整日志到 scratch；返回总长，0 表示到尾/截断
    int read_one(long offset, std::vector<char>& scratch);
    // P1：读并校验一批；成功返回 12+len，失败返回 0
    int read_batch(long offset, long& body_off, uint32_t& body_len);
    RmFileHandle* table_fh(const std::string& tab);
    void ensure_pages(RmFileHandle* fh, int page_no);
    void apply_insert(RmFileHandle* fh, const Rid& rid, const char* data);
    void apply_update(RmFileHandle* fh, const Rid& rid, const char* data);
    void apply_delete(RmFileHandle* fh, const Rid& rid);
    void rebuild_indexes();

    const char* ensure_bytes(long offset, int need);                // 滚动缓冲取数

    LogBuffer buffer_;                                              // 读入日志
    DiskManager* disk_manager_;
    BufferPoolManager* buffer_pool_manager_;
    SmManager* sm_manager_;
    LogManager* log_manager_ = nullptr;

    std::vector<char> rdbuf_;                                       // 日志滚动读缓冲
    long rdbuf_start_ = 0;
    int rdbuf_len_ = 0;

    long start_offset_ = 0;                                         // 扫描起点（restart 文件）
    long log_end_ = 0;                                              // 有效日志终点
    bool use_batch_ = false;                                        // P1：是否批帧格式
    std::unordered_set<txn_id_t> committed_;                        // redo list
    std::map<txn_id_t, std::vector<PendingOp>> uncommitted_;        // undo list（操作按记录序）
    bool touched_ = false;                                          // 本次恢复是否有重放动作
};
