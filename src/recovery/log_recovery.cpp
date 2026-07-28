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
#include <cstdio>
#include <fstream>
#include <unistd.h>
#include "record/rm_defs.h"

/* 题10：恢复流程（静态检查点版 UNDO/REDO）
 * - restart 文件(db.restart)给出最近检查点后的扫描起点；缺失则从 0 开始（基础恢复）。
 * - analyze：单遍扫描——COMMIT 过的事务进 redo list；其余事务的操作暂存（undo 用），
 *   该事务一旦 COMMIT 即丢弃暂存（内存只保留崩溃时未完成事务的操作）。
 * - redo：第二遍流式扫描，按日志序重放 redo list 事务的操作（物理镜像，幂等）。
 * - undo：对未完成事务暂存操作逆序撤销。
 * - 末尾重建全部索引（崩溃后索引文件不可信），并做一次内部检查点，使再次重启零扫描。
 *
 * P1：日志按批帧落盘 [magic|len|crc|records...]。恢复外层按批推进，CRC 不过则整批丢弃；
 * 批内仍用 per-record 解析。预分配后文件大小 ≠ 逻辑终点，不能再靠文件大小挡残尾。 */

// 滚动缓冲:保证 [offset, offset+need) 可由连续内存返回;失败返回 nullptr
const char* RecoveryManager::ensure_bytes(long offset, int need) {
    if (offset < rdbuf_start_ || offset + need > rdbuf_start_ + rdbuf_len_) {
        size_t cap = rdbuf_.size();
        if (cap < (size_t)(1 << 20)) cap = (size_t)(1 << 20);
        if (cap < (size_t)need) cap = (size_t)need;
        rdbuf_.resize(cap);
        long take = log_end_ - offset;
        if (take > (long)rdbuf_.size()) take = (long)rdbuf_.size();
        if (take < need) return nullptr;
        int n = disk_manager_->read_log(rdbuf_.data(), (int)take, (int)offset);
        rdbuf_start_ = offset;
        rdbuf_len_ = n;
        if (n < need) return nullptr;
    }
    return rdbuf_.data() + (offset - rdbuf_start_);
}

int RecoveryManager::read_one(long offset, std::vector<char>& scratch) {
    if (offset + LOG_HEADER_SIZE > log_end_) return 0;
    const char* hdr = ensure_bytes(offset, LOG_HEADER_SIZE);
    if (hdr == nullptr) return 0;
    uint32_t tot = *reinterpret_cast<const uint32_t*>(hdr + OFFSET_LOG_TOT_LEN);
    if (tot < (uint32_t)LOG_HEADER_SIZE || offset + (long)tot > log_end_) return 0;  // 截断尾
    const char* rec = ensure_bytes(offset, (int)tot);
    if (rec == nullptr) return 0;
    scratch.assign(rec, rec + tot);
    return (int)tot;
}

int RecoveryManager::read_batch(long offset, long& body_off, uint32_t& body_len) {
    if (offset + WAL_BATCH_HDR_SIZE > log_end_) return 0;
    const char* hdr = ensure_bytes(offset, WAL_BATCH_HDR_SIZE);
    if (hdr == nullptr) return 0;
    uint32_t magic = *reinterpret_cast<const uint32_t*>(hdr + 0);
    uint32_t blen = *reinterpret_cast<const uint32_t*>(hdr + 4);
    uint32_t expect_crc = *reinterpret_cast<const uint32_t*>(hdr + 8);
    if (magic != WAL_BATCH_MAGIC || blen == 0) return 0;
    if (offset + WAL_BATCH_HDR_SIZE + (long)blen > log_end_) return 0;
    const char* body = ensure_bytes(offset + WAL_BATCH_HDR_SIZE, (int)blen);
    if (body == nullptr) return 0;
    if (wal_crc32(body, blen) != expect_crc) return 0;
    body_off = offset + WAL_BATCH_HDR_SIZE;
    body_len = blen;
    return WAL_BATCH_HDR_SIZE + (int)blen;
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
    use_batch_ = false;

    if (!disk_manager_->is_file(LOG_FILE_NAME)) {
        log_end_ = 0;
        return;
    }
    // 物理大小（含预分配零尾）；逻辑终点由扫描决定
    log_end_ = disk_manager_->get_file_size(LOG_FILE_NAME);

    start_offset_ = 0;
    std::ifstream rf("db.restart", std::ios::binary);
    if (rf) {
        long off = 0;
        rf.read(reinterpret_cast<char*>(&off), sizeof(off));
        if (rf.gcount() == sizeof(off) && off >= 0 && off <= log_end_) start_offset_ = off;
    }

    // 探测批帧格式；旧日志无 magic 则走遗留 per-record 路径
    if (start_offset_ + WAL_BATCH_HDR_SIZE <= log_end_) {
        const char* peek = ensure_bytes(start_offset_, WAL_BATCH_HDR_SIZE);
        if (peek != nullptr &&
            *reinterpret_cast<const uint32_t*>(peek) == WAL_BATCH_MAGIC) {
            use_batch_ = true;
        }
    }

    std::vector<char> rec;
    long pos = start_offset_;

    // 第一遍只收集事务终态（committed 集合），绝不缓存 op 数据——
    // load 单表是单事务（W=50 的 order_line 有 1500 万条 insert 日志，commit 在末尾），
    // 原实现"见 commit 前全量缓存 PendingOp"会吃掉数 GB 内存，重启恢复直接
    // bad_alloc/OOM → SIGABRT（OJ Phase3 实测）。未完成事务的 undo 镜像改由
    // collect_uncommitted() 第二遍按 committed_ 过滤收集（量级=崩溃时活跃事务数）。
    auto handle_rec = [&](const char* data) {
        LogType type = *reinterpret_cast<const LogType*>(data);
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(data + OFFSET_LOG_TID);
        if (type == LogType::commit) {
            committed_.insert(tid);
        }
    };

    if (use_batch_) {
        long body_off = 0;
        uint32_t body_len = 0;
        int span;
        long file_end = log_end_;
        while ((span = read_batch(pos, body_off, body_len)) > 0) {
            log_end_ = body_off + body_len;  // 限制 read_one 在批内
            long bpos = body_off;
            int len;
            while ((len = read_one(bpos, rec)) > 0) {
                handle_rec(rec.data());
                bpos += len;
            }
            log_end_ = file_end;
            pos += span;
        }
        log_end_ = pos;
    } else {
        int len;
        while ((len = read_one(pos, rec)) > 0) {
            handle_rec(rec.data());
            pos += len;
        }
        log_end_ = pos;
    }
}

/**
 * @description: 第二遍扫描——只为【非已提交】事务缓存 undo 所需的 op 镜像。
 * 已提交事务（含 load 的千万行级大事务）零缓存，由第三遍流式 redo；
 * 未完成/中止事务的量级 = 崩溃时活跃连接数 × 每事务语句数，内存可忽略。
 * INSERT 的 undo 只需 rid（apply_delete），不缓存行数据。
 */
void RecoveryManager::collect_uncommitted() {
    if (!disk_manager_->is_file(LOG_FILE_NAME) || log_end_ <= start_offset_) return;
    std::vector<char> rec;
    long pos = start_offset_;

    auto handle_rec = [&](const char* data) {
        LogType type = *reinterpret_cast<const LogType*>(data);
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(data + OFFSET_LOG_TID);
        if (committed_.count(tid)) return;
        switch (type) {
            case LogType::INSERT: {
                InsertLogRecord lr;
                lr.deserialize(data);
                PendingOp op;
                op.type = LogType::INSERT;
                op.table.assign(lr.table_name_, lr.table_name_size_);
                op.rid = lr.rid_;
                uncommitted_[tid].push_back(std::move(op));
                delete[] lr.table_name_;
                break;
            }
            case LogType::DELETE: {
                DeleteLogRecord lr;
                lr.deserialize(data);
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
                lr.deserialize(data);
                PendingOp op;
                op.type = LogType::UPDATE;
                op.table.assign(lr.table_name_, lr.table_name_size_);
                op.rid = lr.rid_;
                op.old_data.assign(lr.old_value_.data, lr.old_value_.size);
                uncommitted_[tid].push_back(std::move(op));
                delete[] lr.table_name_;
                break;
            }
            case LogType::UPDATE_DELTA: {
                UpdateDeltaLogRecord lr;
                lr.deserialize(data);
                PendingOp op;
                op.type = LogType::UPDATE_DELTA;
                op.table.assign(lr.table_name_, lr.table_name_size_);
                op.rid = lr.rid_;
                for (int i = 0; i < lr.n_ranges_; i++) {
                    op.delta_ranges.push_back(lr.ranges_[i]);
                    op.delta_old.emplace_back(lr.old_ptrs_[i], lr.ranges_[i].len);
                    op.delta_new.emplace_back(lr.new_ptrs_[i], lr.ranges_[i].len);
                }
                uncommitted_[tid].push_back(std::move(op));
                break;
            }
            default:
                break;
        }
    };

    if (use_batch_) {
        long file_end = log_end_;
        long body_off = 0;
        uint32_t body_len = 0;
        int span;
        while (pos < file_end && (span = read_batch(pos, body_off, body_len)) > 0) {
            long saved = log_end_;
            log_end_ = body_off + body_len;
            long bpos = body_off;
            int len;
            while ((len = read_one(bpos, rec)) > 0) {
                handle_rec(rec.data());
                bpos += len;
            }
            log_end_ = saved;
            pos += span;
        }
    } else {
        int len;
        while (pos < log_end_ && (len = read_one(pos, rec)) > 0) {
            handle_rec(rec.data());
            pos += len;
        }
    }
}

/**
 * @description: 重做所有未落盘的操作
 */
void RecoveryManager::redo() {
    // 先撤销后重放:本系统写写互斥(first-updater-wins)下,未完成事务对某记录的
    // 占有止于其 abort;此后已提交事务可改写同一记录。若先 redo 后 undo,undo 会用
    // 旧镜像回卷已提交效果。先把全部未完成事务回退到其改前值,再按日志序重放已提交
    // 事务,终态正确。
    collect_uncommitted();
    undo_pass();
    if (committed_.empty()) return;
    std::vector<char> rec;
    long pos = start_offset_;

    auto redo_one = [&](const char* data) {
        LogType type = *reinterpret_cast<const LogType*>(data);
        txn_id_t tid = *reinterpret_cast<const txn_id_t*>(data + OFFSET_LOG_TID);
        if (!committed_.count(tid)) return;
        switch (type) {
            case LogType::INSERT: {
                InsertLogRecord lr;
                lr.deserialize(data);
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
                lr.deserialize(data);
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
                lr.deserialize(data);
                std::string tab(lr.table_name_, lr.table_name_size_);
                delete[] lr.table_name_;
                if (RmFileHandle* fh = table_fh(tab)) {
                    apply_update(fh, lr.rid_, lr.new_value_.data);
                    touched_ = true;
                }
                break;
            }
            case LogType::UPDATE_DELTA: {
                UpdateDeltaLogRecord lr;
                lr.deserialize(data);
                std::string tab(lr.table_name_, lr.table_name_size_);
                if (RmFileHandle* fh = table_fh(tab)) {
                    ensure_pages(fh, lr.rid_.page_no);
                    if (fh->is_record(lr.rid_)) {
                        auto cur = fh->get_record(lr.rid_, nullptr);
                        std::vector<char> buf(cur->data, cur->data + cur->size);
                        for (int i = 0; i < lr.n_ranges_; i++) {
                            if ((int)lr.ranges_[i].off + (int)lr.ranges_[i].len > (int)buf.size()) continue;
                            memcpy(buf.data() + lr.ranges_[i].off, lr.new_ptrs_[i], lr.ranges_[i].len);
                        }
                        apply_update(fh, lr.rid_, buf.data());
                        touched_ = true;
                    } else {
                        fprintf(stderr, "[recovery] UPDATE_DELTA redo: row missing %s(%d,%d)\n",
                                tab.c_str(), lr.rid_.page_no, lr.rid_.slot_no);
                    }
                }
                break;
            }
            default:
                break;
        }
    };

    if (use_batch_) {
        long file_end = log_end_;
        // analyze 已把 log_end_ 收成逻辑终点；读批时需能读到该终点内的批头
        // 批扫描用「逻辑终点」作上限即可（残缺批已在 analyze 丢弃）
        long body_off = 0;
        uint32_t body_len = 0;
        int span;
        while (pos < file_end && (span = read_batch(pos, body_off, body_len)) > 0) {
            long saved = log_end_;
            log_end_ = body_off + body_len;
            long bpos = body_off;
            int len;
            while ((len = read_one(bpos, rec)) > 0) {
                redo_one(rec.data());
                bpos += len;
            }
            log_end_ = saved;
            pos += span;
        }
    } else {
        int len;
        while (pos < log_end_ && (len = read_one(pos, rec)) > 0) {
            redo_one(rec.data());
            pos += len;
        }
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
                case LogType::UPDATE_DELTA: {
                    ensure_pages(fh, it->rid.page_no);
                    if (fh->is_record(it->rid)) {
                        auto cur = fh->get_record(it->rid, nullptr);
                        std::vector<char> buf(cur->data, cur->data + cur->size);
                        for (size_t i = 0; i < it->delta_ranges.size(); i++) {
                            auto& rg = it->delta_ranges[i];
                            if ((int)rg.off + (int)rg.len > (int)buf.size()) continue;
                            memcpy(buf.data() + rg.off, it->delta_old[i].data(), rg.len);
                        }
                        apply_update(fh, it->rid, buf.data());
                    } else {
                        fprintf(stderr, "[recovery] UPDATE_DELTA undo: row missing %s(%d,%d)\n",
                                it->table.c_str(), it->rid.page_no, it->rid.slot_no);
                    }
                    break;
                }
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
        // 物理截断到有效终点：切除崩溃残尾 / 预分配零区；并重置预分配水位
        if (disk_manager_->is_file(LOG_FILE_NAME)) {
            disk_manager_->reset_log_prealloc(log_end_);
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
