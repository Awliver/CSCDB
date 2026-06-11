/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "log_recovery.h"
#include <fstream>
#include <unistd.h>
#include "record/rm_defs.h"

/* 题10：恢复流程（静态检查点版 UNDO/REDO）
 * - restart 文件(db.restart)给出最近检查点后的扫描起点；缺失则从 0 开始（基础恢复）。
 * - analyze：单遍扫描——COMMIT 过的事务进 redo list；其余事务的操作暂存（undo 用），
 *   该事务一旦 COMMIT 即丢弃暂存（内存只保留崩溃时未完成事务的操作）。
 * - redo：第二遍流式扫描，按日志序重放 redo list 事务的操作（物理镜像，幂等）。
 * - undo：对未完成事务暂存操作逆序撤销。
 * - 末尾重建全部索引（崩溃后索引文件不可信），并做一次内部检查点，使再次重启零扫描。 */

int RecoveryManager::read_one(long offset, std::vector<char>& scratch) {
    if (offset + LOG_HEADER_SIZE > log_end_) return 0;
    char hdr[LOG_HEADER_SIZE];
    int n = disk_manager_->read_log(hdr, LOG_HEADER_SIZE, (int)offset);
    if (n < LOG_HEADER_SIZE) return 0;
    uint32_t tot = *reinterpret_cast<const uint32_t*>(hdr + OFFSET_LOG_TOT_LEN);
    if (tot < (uint32_t)LOG_HEADER_SIZE || offset + (long)tot > log_end_) return 0;  // 截断尾
    scratch.resize(tot);
    n = disk_manager_->read_log(scratch.data(), (int)tot, (int)offset);
    if (n < (int)tot) return 0;
    return (int)tot;
}

RmFileHandle* RecoveryManager::table_fh(const std::string& tab) {
    auto it = sm_manager_->fhs_.find(tab);
    if (it == sm_manager_->fhs_.end()) return nullptr;
    return it->second.get();
}

void RecoveryManager::ensure_pages(RmFileHandle* fh, int page_no) {
    while (fh->get_file_hdr().num_pages <= page_no) {
        RmPageHandle ph = fh->create_new_page_handle();
        buffer_pool_manager_->unpin_page(ph.page->get_page_id(), true);
    }
}

void RecoveryManager::apply_insert(RmFileHandle* fh, const Rid& rid, const char* data) {
    ensure_pages(fh, rid.page_no);
    if (fh->is_record(rid)) {
        fh->update_record(rid, const_cast<char*>(data), nullptr);   // 页已落盘过：覆写即可
    } else {
        fh->insert_record(rid, const_cast<char*>(data));
    }
}

void RecoveryManager::apply_update(RmFileHandle* fh, const Rid& rid, const char* data) {
    ensure_pages(fh, rid.page_no);
    if (fh->is_record(rid)) {
        fh->update_record(rid, const_cast<char*>(data), nullptr);
    } else {
        fh->insert_record(rid, const_cast<char*>(data));            // 页未落盘：按新值重建
    }
}

void RecoveryManager::apply_delete(RmFileHandle* fh, const Rid& rid) {
    ensure_pages(fh, rid.page_no);
    if (fh->is_record(rid)) {
        fh->delete_record(rid, nullptr);
    }
}

/**
 * @description: analyze阶段，需要获得脏页表（DPT）和未完成的事务列表（ATT）
 */
void RecoveryManager::analyze() {
    committed_.clear();
    uncommitted_.clear();
    touched_ = false;

    if (!disk_manager_->is_file(LOG_FILE_NAME)) {
        log_end_ = 0;
        return;
    }
    log_end_ = disk_manager_->get_file_size(LOG_FILE_NAME);

    // restart 文件：最近一次静态检查点记录之后的扫描起点
    start_offset_ = 0;
    std::ifstream rf("db.restart", std::ios::binary);
    if (rf) {
        long off = 0;
        rf.read(reinterpret_cast<char*>(&off), sizeof(off));
        if (rf.gcount() == sizeof(off) && off >= 0 && off <= log_end_) start_offset_ = off;
    }

    std::vector<char> rec;
    long pos = start_offset_;
    int len;
    while ((len = read_one(pos, rec)) > 0) {
        LogType type = *reinterpret_cast<const LogType*>(rec.data());
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(rec.data() + OFFSET_LOG_TID);
        switch (type) {
            case LogType::commit:
                committed_.insert(tid);
                uncommitted_.erase(tid);     // 其操作交给 redo 流式重放
                break;
            case LogType::ABORT:
                // 运行期已就地回滚；其插入留存堆中需 undo，操作保留在 uncommitted_
                break;
            case LogType::INSERT: {
                InsertLogRecord lr;
                lr.deserialize(rec.data());
                PendingOp op;
                op.type = LogType::INSERT;
                op.table.assign(lr.table_name_, lr.table_name_size_);
                op.rid = lr.rid_;
                op.new_data.assign(lr.insert_value_.data, lr.insert_value_.size);
                uncommitted_[tid].push_back(std::move(op));
                delete[] lr.table_name_;
                break;
            }
            case LogType::DELETE: {
                DeleteLogRecord lr;
                lr.deserialize(rec.data());
                PendingOp op;
                op.type = LogType::DELETE;
                op.table.assign(lr.table_name_, lr.table_name_size_);
                op.rid = lr.rid_;
                op.old_data.assign(lr.delete_value_.data, lr.delete_value_.size);
                uncommitted_[tid].push_back(std::move(op));
                delete[] lr.table_name_;
                break;
            }
            case LogType::UPDATE: {
                UpdateLogRecord lr;
                lr.deserialize(rec.data());
                PendingOp op;
                op.type = LogType::UPDATE;
                op.table.assign(lr.table_name_, lr.table_name_size_);
                op.rid = lr.rid_;
                op.old_data.assign(lr.old_value_.data, lr.old_value_.size);
                op.new_data.assign(lr.new_value_.data, lr.new_value_.size);
                uncommitted_[tid].push_back(std::move(op));
                delete[] lr.table_name_;
                break;
            }
            case LogType::begin:
            case LogType::CKPT:
            default:
                break;
        }
        pos += len;
    }
    log_end_ = pos;   // 有效日志终点（忽略截断尾）
}

/**
 * @description: 重做所有未落盘的操作
 */
void RecoveryManager::redo() {
    // 先撤销后重放:本系统写写互斥(first-updater-wins)下,未完成事务对某记录的
    // 占有止于其 abort;此后已提交事务可改写同一记录。若先 redo 后 undo,undo 会用
    // 旧镜像回卷已提交效果。先把全部未完成事务回退到其改前值,再按日志序重放已提交
    // 事务,终态正确。
    undo_pass();
    if (committed_.empty()) return;
    std::vector<char> rec;
    long pos = start_offset_;
    int len;
    while (pos < log_end_ && (len = read_one(pos, rec)) > 0) {
        LogType type = *reinterpret_cast<const LogType*>(rec.data());
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(rec.data() + OFFSET_LOG_TID);
        if (committed_.count(tid)) {
            switch (type) {
                case LogType::INSERT: {
                    InsertLogRecord lr;
                    lr.deserialize(rec.data());
                    std::string tab(lr.table_name_, lr.table_name_size_);
                    delete[] lr.table_name_;
                    if (RmFileHandle* fh = table_fh(tab)) {
                        apply_insert(fh, lr.rid_, lr.insert_value_.data);
                        touched_ = true;
                    }
                    break;
                }
                case LogType::DELETE: {
                    DeleteLogRecord lr;
                    lr.deserialize(rec.data());
                    std::string tab(lr.table_name_, lr.table_name_size_);
                    delete[] lr.table_name_;
                    if (RmFileHandle* fh = table_fh(tab)) {
                        apply_delete(fh, lr.rid_);
                        touched_ = true;
                    }
                    break;
                }
                case LogType::UPDATE: {
                    UpdateLogRecord lr;
                    lr.deserialize(rec.data());
                    std::string tab(lr.table_name_, lr.table_name_size_);
                    delete[] lr.table_name_;
                    if (RmFileHandle* fh = table_fh(tab)) {
                        apply_update(fh, lr.rid_, lr.new_value_.data);
                        touched_ = true;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        pos += len;
    }
}

/**
 * @description: 回滚未完成的事务
 */
void RecoveryManager::undo_pass() {
    for (auto& kv : uncommitted_) {
        auto& ops = kv.second;
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            RmFileHandle* fh = table_fh(it->table);
            if (fh == nullptr) continue;
            switch (it->type) {
                case LogType::INSERT:
                    apply_delete(fh, it->rid);
                    break;
                case LogType::UPDATE:
                    apply_update(fh, it->rid, it->old_data.data());
                    break;
                case LogType::DELETE:
                    // 题9 删除为纯逻辑（堆未动）；若曾被物理删且落盘，则重插旧值
                    ensure_pages(fh, it->rid.page_no);
                    if (!fh->is_record(it->rid)) {
                        fh->insert_record(it->rid, const_cast<char*>(it->old_data.data()));
                    }
                    break;
                default:
                    break;
            }
            touched_ = true;
        }
    }
    uncommitted_.clear();
}

void RecoveryManager::undo() {
    if (touched_) {
        rebuild_indexes();
    }

    // 内部检查点：恢复完成的状态全量落盘并推进 restart 起点，使重复重启零扫描、幂等
    if (log_manager_ != nullptr) {
        // 物理截断到有效终点：残留半条记录/垃圾尾不切除的话，
        // 追加会把残桩夹在日志中间，未来全量扫描在此失步丢事务
        if (disk_manager_->is_file(LOG_FILE_NAME) &&
            disk_manager_->get_file_size(LOG_FILE_NAME) > log_end_) {
            truncate(LOG_FILE_NAME.c_str(), (off_t)log_end_);
        }
        log_manager_->init_offset(log_end_);
        sm_manager_->do_checkpoint(log_manager_);
    }
}

/* 崩溃后索引文件不可信：删掉重建（元数据经 drop_index/create_index 原样恢复） */
void RecoveryManager::rebuild_indexes() {
    for (auto& fh_entry : sm_manager_->fhs_) {
        if (!sm_manager_->db_.is_table(fh_entry.first)) continue;
        TabMeta& tab = sm_manager_->db_.get_table(fh_entry.first);
        if (tab.indexes.empty()) continue;
        std::vector<std::vector<std::string>> index_cols;
        for (auto& index : tab.indexes) {
            std::vector<std::string> names;
            for (auto& col : index.cols) names.push_back(col.name);
            index_cols.push_back(std::move(names));
        }
        for (auto& names : index_cols) {
            sm_manager_->drop_index(tab.name, names, nullptr);
            sm_manager_->create_index(tab.name, names, nullptr);
        }
    }
}
