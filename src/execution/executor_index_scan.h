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

#include <cfloat>
#include <climits>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"
#include "common/repro_ring.h"

class IndexScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;                      // 物理表名
    std::string binding_name_;                  // SQL 中的关系实例名
    TabMeta tab_;                               // 表的元数据
    // predicate_ 是查询语义的唯一来源；access_conditions_ 只能用来
    // 收紧 B+树访问边界，不能代替逐行复核。
    ConditionExprPtr predicate_;
    std::vector<Condition> access_conditions_;
    RmFileHandle *fh_;                          // 表的数据文件句柄
    std::vector<ColMeta> cols_;                 // 需要读取的字段
    size_t len_;                                // 选取出来的一条记录的长度

    std::vector<std::string> index_col_names_;  // index scan涉及到的索引包含的字段
    IndexMeta index_meta_;                      // index scan涉及到的索引元数据

    Rid rid_;
    std::unique_ptr<IxScan> scan_;   // 具体类型：需要 rid_and_key（MVCC key 一致性过滤）

    SmManager *sm_manager_;

    // 题3 批 8：最左匹配支持
    int eq_match_count_ = 0;            // 前 N 个索引列有 EQ 条件
    std::vector<char> eq_prefix_data_;  // EQ 前缀的拼接字节（按索引列顺序）
    bool range_exhausted_ = false;      // 标记"EQ 前缀已被超出"
    // 当前是否定位在一条已通过全部过滤的行上。key 锚定模式下 scan_->is_end() 每次
    // 调用都是一趟锁内全树重定位——is_end/nextTuple/循环条件若直接问 scan_，每行
    // 要付 3 次重定位与 3 次 root_latch 共享锁（饱和负载下读流量饿死写者的主源）。
    // position_to_match 返回后此标志即为真值来源，问它零成本。
    bool positioned_ = false;

    // 决赛 index skip scan：索引首列无条件、第 2 列起有连续 EQ（Delivery 的
    // sum(ol_amount) where ol_o_id=? and ol_d_id=? 形态）——枚举首列 distinct 值，
    // 对每个值以 [v|EQ链] 为前缀做子范围扫描，避免全表扫（OJ 1500 万行必超时）
    bool skip_mode_ = false;
    int skip_eq_count_ = 0;             // 第 2 列起连续 EQ 数
    std::vector<char> skip_eq_data_;    // 上述 EQ 值拼接（不含首列）
    std::vector<char> skip_cur_first_;  // 当前枚举中的首列值
    bool skip_done_ = false;
    IxIndexHandle *ih_ = nullptr;

    // 题3 批 9：表数据页缓存（仿 SeqScan 批 2），避免每条记录都 BPM 往返 + alloc
    int cached_table_page_no_ = -1;
    Page *cached_table_page_ = nullptr;
    char *cached_table_slots_ = nullptr;
    char *cached_table_bitmap_ = nullptr;   // 槽位存活性校验（陈旧索引项防护）
    std::vector<char> cur_key_buf_;         // 当前索引项 key（MVCC key 一致性过滤用）
    int table_record_size_ = 0;

    // 题3 批 11：预编译条件 + 跳过冗余检查
    struct CompiledCond {
        int lhs_offset;
        int lhs_len;
        ColType lhs_type;
        CompOp op;
        bool is_rhs_val;
        const char *rhs_val_data;
        int rhs_offset;
    };
    BoolExprPtr<CompiledCond> compiled_predicate_;
    bool need_eval_ = true;             // 完整树始终残余复核；仅无谓词时为 false
    bool need_prefix_check_ = true;     // false = hi 是精确的（EQ 全匹配 或 range 上界），EQ 前缀检查冗余

    // 题9 MVCC：索引扫描与 SeqScan 同等对待——表进入 MVCC 脏态后，堆上的裸记录可能
    // 包含未提交写或缺少本事务自己的链上写(未提交版本仅存链/overlay，commit 才物化到堆)。
    // 不做可见性重建会导致：事务读不到自己的未提交更新 → 计数器错位 → 丢单/孤儿行。
    bool mvcc_on_ = false;
    std::string mvcc_buf_;              // 当前 rid 的可见版本字节（mvcc_on_ 时有效）
    // 题9 SER：与 SeqScan 相同的 SSI 读跟踪（谓词读 + 逐行读检查）。有了这套钩子，
    // SER 下单表点查可走索引而不必强制全表扫（旧实现强制 SeqScan 是 OJ 性能塌方主因）。
    bool ser_on_ = false;

   public:
    static ConditionExprPtr conditions_to_expr(std::vector<Condition> conditions) {
        ConditionExprPtr result;
        for (auto &condition : conditions) {
            auto atom = make_bool_atom<Condition>(std::move(condition));
            result = result == nullptr
                         ? std::move(atom)
                         : make_bool_binary<Condition>(BoolExprType::AND,
                                                       std::move(result), std::move(atom));
        }
        return result;
    }

    IndexScanExecutor(SmManager *sm_manager, std::string tab_name,
                      ConditionExprPtr predicate, std::vector<Condition> access_conditions,
                      std::vector<std::string> index_col_names, Context *context)
        : IndexScanExecutor(sm_manager, tab_name, tab_name, std::move(predicate),
                            std::move(access_conditions), std::move(index_col_names), context) {}

    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::string binding_name,
                      ConditionExprPtr predicate, std::vector<Condition> access_conditions,
                      std::vector<std::string> index_col_names, Context *context) {
        sm_manager_ = sm_manager;
        context_ = context;
        tab_name_ = std::move(tab_name);
        binding_name_ = std::move(binding_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        predicate_ = std::move(predicate);
        access_conditions_ = std::move(access_conditions);
        // index_no_ = index_no;
        index_col_names_ = index_col_names;
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab_.cols;
        for (auto &col : cols_) col.tab_name = binding_name_;
        len_ = cols_.back().offset + cols_.back().len;
        std::map<CompOp, CompOp> swap_op = {
            {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
        };

        for (auto &cond : access_conditions_) {
            if (!cond.lhs_col.tab_name.empty() && cond.lhs_col.tab_name != binding_name_) {
                // lhs is on other table, now rhs must be on this table
                if (cond.is_rhs_val || cond.rhs_col.tab_name != binding_name_) {
                    throw InternalError("Index access condition does not reference scan binding");
                }
                // swap lhs and rhs
                std::swap(cond.lhs_col, cond.rhs_col);
                cond.op = swap_op.at(cond.op);
            }
        }
        table_record_size_ = fh_->get_file_hdr().record_size;
    }

    // 兼容旧的 AND-vector 调用点。适配后仍立即构造完整树，
    // 不在执行路径中保留第二套语义。
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name,
                      std::vector<Condition> conds,
                      std::vector<std::string> index_col_names, Context *context)
        : IndexScanExecutor(sm_manager, std::move(tab_name),
                            conditions_to_expr(conds), conds,
                            std::move(index_col_names), context) {}

    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::string binding_name,
                      std::vector<Condition> conds,
                      std::vector<std::string> index_col_names, Context *context)
        : IndexScanExecutor(sm_manager, std::move(tab_name), std::move(binding_name),
                            conditions_to_expr(conds), conds,
                            std::move(index_col_names), context) {}

    ~IndexScanExecutor() override { release_table_page(); }

    void release_table_page() {
        if (cached_table_page_) {
            sm_manager_->get_bpm()->unpin_page(cached_table_page_->get_page_id(), false);
            cached_table_page_ = nullptr;
            cached_table_page_no_ = -1;
            cached_table_slots_ = nullptr;
            cached_table_bitmap_ = nullptr;
        }
    }

    const char *get_table_slot(const Rid &rid) {
        if (rid.page_no != cached_table_page_no_) {
            release_table_page();
            RmPageHandle handle = fh_->fetch_page_handle(rid.page_no);
            cached_table_page_ = handle.page;
            cached_table_page_no_ = rid.page_no;
            cached_table_slots_ = handle.slots;
            cached_table_bitmap_ = handle.bitmap;
        }
        return cached_table_slots_ + rid.slot_no * table_record_size_;
    }

    /* 当前缓存页上该槽位是否存活（bitmap 置位）。须在 get_table_slot(rid) 之后调用 */
    bool table_slot_live(const Rid &rid) const {
        return cached_table_bitmap_ != nullptr && Bitmap::is_set(cached_table_bitmap_, rid.slot_no);
    }

    /**
     * 字节级比较两段数据（与 SeqScanExecutor 同一套逻辑）
     */
    static bool cmp_bytes(const char *a, const char *b, int len, ColType type, CompOp op) {
        if (op == OP_LIKE) {
            return type == TYPE_STRING && sql_like_match(a, len, b, len);
        }
        int cmp;
        if (type == TYPE_INT) {
            int ia = load_unaligned<int>(a);
            int ib = load_unaligned<int>(b);
            cmp = (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
        } else if (type == TYPE_FLOAT) {
            float fa = load_unaligned<float>(a);
            float fb = load_unaligned<float>(b);
            cmp = (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
        } else {
            cmp = memcmp(a, b, len);
        }
        switch (op) {
            case OP_EQ: return cmp == 0;
            case OP_NE: return cmp != 0;
            case OP_LT: return cmp < 0;
            case OP_GT: return cmp > 0;
            case OP_LE: return cmp <= 0;
            case OP_GE: return cmp >= 0;
            case OP_LIKE: return false;
        }
        return false;
    }

    /**
     * 按索引列顺序分析 access_conditions_：找出前缀有多少 EQ 条件，并构造 EQ 前缀字节
     */
    void analyze_conditions() {
        eq_match_count_ = 0;
        eq_prefix_data_.clear();
        for (const auto &col : index_meta_.cols) {
            bool found_eq = false;
            for (const auto &cond : access_conditions_) {
                if (cond.is_rhs_val && cond.op == OP_EQ &&
                    cond.lhs_col.tab_name == binding_name_ &&
                    cond.lhs_col.col_name == col.name) {
                    eq_prefix_data_.insert(eq_prefix_data_.end(),
                                           cond.rhs_val.raw->data,
                                           cond.rhs_val.raw->data + col.len);
                    found_eq = true;
                    break;
                }
            }
            if (found_eq) eq_match_count_++;
            else break;
        }
        // skip scan 形态检测：首列无 EQ、第 2 列起有连续 EQ 且首列无任何条件
        skip_mode_ = false;
        skip_eq_count_ = 0;
        skip_eq_data_.clear();
        if (eq_match_count_ == 0 && index_meta_.cols.size() >= 2) {
            bool first_has_cond = false;
            for (const auto &cond : access_conditions_) {
                if (cond.is_rhs_val && cond.lhs_col.tab_name == binding_name_ &&
                    cond.lhs_col.col_name == index_meta_.cols[0].name) {
                    first_has_cond = true;
                    break;
                }
            }
            if (!first_has_cond) {
                for (size_t ci = 1; ci < index_meta_.cols.size(); ci++) {
                    const auto &col = index_meta_.cols[ci];
                    bool found_eq = false;
                    for (const auto &cond : access_conditions_) {
                        if (cond.is_rhs_val && cond.op == OP_EQ &&
                            cond.lhs_col.tab_name == binding_name_ &&
                            cond.lhs_col.col_name == col.name) {
                            skip_eq_data_.insert(skip_eq_data_.end(),
                                                 cond.rhs_val.raw->data,
                                                 cond.rhs_val.raw->data + col.len);
                            found_eq = true;
                            break;
                        }
                    }
                    if (!found_eq) break;
                    skip_eq_count_++;
                }
                skip_mode_ = (skip_eq_count_ >= 1);
            }
        }
    }

    /* skip scan：为首列值 v 打开子范围 [v|EQ链|min, v|EQ链|max]，并把
     * eq 前缀切换为 v|EQ链（prefix 早停与 key 一致性过滤随之生效） */
    void skip_open_for_value(const char *v) {
        const auto &fcol = index_meta_.cols[0];
        skip_cur_first_.assign(v, v + fcol.len);
        eq_match_count_ = 1 + skip_eq_count_;
        eq_prefix_data_.assign(v, v + fcol.len);
        eq_prefix_data_.insert(eq_prefix_data_.end(), skip_eq_data_.begin(), skip_eq_data_.end());

        std::vector<char> start_key(index_meta_.col_tot_len, 0);
        std::vector<char> end_key(index_meta_.col_tot_len, 0);
        memcpy(start_key.data(), eq_prefix_data_.data(), eq_prefix_data_.size());
        memcpy(end_key.data(), eq_prefix_data_.data(), eq_prefix_data_.size());
        fill_extreme_from(start_key.data(), eq_match_count_, false);
        fill_extreme_from(end_key.data(), eq_match_count_, true);
        range_exhausted_ = false;
        scan_ = std::make_unique<IxScan>(ih_, start_key.data(), end_key.data(), true,
                                         sm_manager_->get_bpm());
    }

    /* skip scan：跳到比 cur_first 更大的下一个首列值；无则 skip_done_ */
    bool skip_advance_first_col() {
        std::vector<char> probe(index_meta_.col_tot_len, 0);
        memcpy(probe.data(), skip_cur_first_.data(), skip_cur_first_.size());
        fill_extreme_from(probe.data(), 1, true);         // [cur|max...] 之后即下一首列值
        Iid nxt = ih_->upper_bound(probe.data());
        IxScan peek(ih_, nxt, ih_->leaf_end(), sm_manager_->get_bpm());
        if ((int)cur_key_buf_.size() < index_meta_.col_tot_len) {
            cur_key_buf_.resize(index_meta_.col_tot_len);
        }
        Rid r = peek.rid_and_key(cur_key_buf_.data());
        if (r.page_no < 0) {
            skip_done_ = true;
            return false;
        }
        skip_open_for_value(cur_key_buf_.data());
        return true;
    }

    /* skip scan：当前子范围耗尽时推进到下一首列值，直到定位有效行或全部枚举完 */
    void skip_fill_valid() {
        while (skip_mode_ && !skip_done_ &&
               (range_exhausted_ || scan_ == nullptr || !positioned_)) {
            if (!skip_advance_first_col()) return;
            position_to_match();
        }
    }

    /**
     * 检查当前 rec 是否仍在 EQ 前缀范围内（用于范围扫描早期终止）
     */
    bool eq_prefix_matches(const RmRecord *rec) const {
        int offset = 0;
        for (int i = 0; i < eq_match_count_; i++) {
            const auto &col = index_meta_.cols[i];
            if (memcmp(rec->data + col.offset,
                       eq_prefix_data_.data() + offset,
                       col.len) != 0) {
                return false;
            }
            offset += col.len;
        }
        return true;
    }

    /* REPRO-TRACE 辅助：项 key 末 int（new_orders 索引 (w,d,o) 的 o_id）*/
    int32_t ring_key_oid_() const {
        if ((int)cur_key_buf_.size() < index_meta_.col_tot_len || index_meta_.col_tot_len < 4) return -1;
        int32_t v; memcpy(&v, cur_key_buf_.data() + index_meta_.col_tot_len - 4, 4); return v;
    }
    void position_to_match() {
        const bool ring_on = ReproRing::on() && tab_name_ == "new_orders";
        positioned_ = false;
        // 终止判定用 rid 哨兵（{-1,-1}），不问 scan_->is_end()：key 模式下那是
        // 一趟额外的锁内全树重定位，rid_and_key/rid 本身已含同样的定位结果
        while (!range_exhausted_) {
            // 与 SeqScan 一致：扫描中途表变脏时打开 MVCC 读
            if (!mvcc_on_ && context_ && context_->txn_mgr_ && context_->txn_) {
                mvcc_on_ = context_->txn_mgr_->table_is_dirty(tab_name_);
            }
            if (mvcc_on_ || need_prefix_check_) {
                // MVCC 下同一 rid 可能有多个索引项（未提交 UPDATE 保留旧项+插入新项）：
                // 同锁取出本项 key，稍后与可见版本的 key 比对，非一致项跳过（防重复行）。
                // 前缀早停也必须用项 key（见下），故 need_prefix_check_ 时同样取 key。
                if ((int)cur_key_buf_.size() < index_meta_.col_tot_len) {
                    cur_key_buf_.resize(index_meta_.col_tot_len);
                }
                rid_ = scan_->rid_and_key(cur_key_buf_.data());
            } else {
                rid_ = scan_->rid();
            }
            if (rid_.page_no < 0 || rid_.slot_no < 0) {
                break;
            }

            // EQ 前缀早停必须先于可见性判定：不可见行（墓碑/回滚残留/他事务新项）若
            // 先走可见性 skip，就绕过了边界检查——扫描会以"每步两次全树下降"的代价
            // 爬过范围之外的整片死项区（TPC-C MIN 实测分钟级假死；OJ 热点分区的
            // 墓碑积压随轮内时间放大此代价）。项 key 与可见性无关，可最先判。
            if (need_prefix_check_) {
                int offset = 0;
                bool match = true;
                for (int i = 0; i < eq_match_count_; i++) {
                    const auto &col = index_meta_.cols[i];
                    if (memcmp(cur_key_buf_.data() + offset,
                               eq_prefix_data_.data() + offset, col.len) != 0) {
                        match = false;
                        break;
                    }
                    offset += col.len;
                }
                if (!match) {
                    if (ring_on) ReproRing::push(ReproRing::SKIPVIS, mvcc_on_ ? ring_key_oid_() : -1, 6, ReproRing::rid32(rid_.page_no, rid_.slot_no), 0);
                    range_exhausted_ = true;
                    return;
                }
            }

            // Fast path：range 已精确，且无残余 cond，直接返回匹配（无需 MVCC 重建/SER 跟踪时）
            if (!mvcc_on_ && !ser_on_ && !need_eval_ && !need_prefix_check_) {
                if (ring_on) ReproRing::push(ReproRing::FASTPATH, -1, 0, ReproRing::rid32(rid_.page_no, rid_.slot_no), 0);
                positioned_ = true;
                return;
            }

            const char *slot = get_table_slot(rid_);  // 0-alloc 直接读 slot
            const char *rec_data = slot;
            // 陈旧索引项防护：已提交删除会释放堆槽，而快照读期间索引项可能尚在。
            // 槽位不存活时，无版本链兜底的记录必须判为不可见，绝不能返回残留字节。
            const bool slot_live = table_slot_live(rid_);

            bool from_heap = false;
            if (mvcc_on_) {
                // 题9：按本事务快照重建可见版本；不可见/已删则跳过（与 SeqScan 一致）
                if (!context_->txn_mgr_->mvcc_read(context_->txn_, tab_name_, rid_,
                                                   slot, table_record_size_, mvcc_buf_, slot_live,
                                                   &from_heap)) {
                    if (ring_on) ReproRing::push(ReproRing::SKIPVIS, ring_key_oid_(), 1, ReproRing::rid32(rid_.page_no, rid_.slot_no), 0);
                    scan_->next();
                    continue;
                }
                // 关闭 drain 竞态窗口：slot_live 采样早于 mvcc_read，若期间
                // drain_deferred_deletes 完成"摘链+清堆"，链缺失的堆回退会拿陈旧采样
                // 误判可见（已删行瞬态复活，Delivery MIN canary 实测）。mvcc_read 与
                // drain 都持分片锁互斥，读后复查 bitmap 必然看到 drain 的结果。
                // 仅堆回退需要复查——链数据的可见性与堆槽无关（checkpoint 清堆场景合法）。
                if (from_heap && !table_slot_live(rid_)) {
                    if (ring_on) ReproRing::push(ReproRing::SKIPVIS, ring_key_oid_(), 2, ReproRing::rid32(rid_.page_no, rid_.slot_no), 0);
                    scan_->next();
                    continue;
                }
                rec_data = mvcc_buf_.data();
                // key 一致性：可见版本按索引列重建的 key 必须等于本索引项的 key，
                // 否则本项是"其他版本的 key"（如未提交 UPDATE 的新 key 项对旧快照）——
                // 跳过，行只经与其可见 key 一致的项输出一次
                {
                    int koff = 0;
                    bool key_same = true;
                    for (const auto &icol : index_meta_.cols) {
                        if (memcmp(rec_data + icol.offset, cur_key_buf_.data() + koff, icol.len) != 0) {
                            key_same = false;
                            break;
                        }
                        koff += icol.len;
                    }
                    if (!key_same) {
                        if (ring_on) ReproRing::push(ReproRing::SKIPVIS, ring_key_oid_(), 3, ReproRing::rid32(rid_.page_no, rid_.slot_no), 0);
                        scan_->next();
                        continue;
                    }
                }
            } else if (!slot_live) {
                if (ring_on) ReproRing::push(ReproRing::SKIPVIS, -1, 4, ReproRing::rid32(rid_.page_no, rid_.slot_no), 0);
                scan_->next();
                continue;
            }

            if (need_eval_) {
                if (!eval_compiled(rec_data)) {
                    if (ring_on) ReproRing::push(ReproRing::SKIPVIS, mvcc_on_ ? ring_key_oid_() : -1, 5, ReproRing::rid32(rid_.page_no, rid_.slot_no), 0);
                    scan_->next();
                    continue;
                }
            }
            if (ser_on_) {
                // 题9 SER：记录读集 + 读侧 rw 反依赖检查（与 SeqScan 一致）
                context_->txn_mgr_->ser_record_read(context_->txn_, tab_name_, rid_);
                if (context_->txn_mgr_->ser_read_check(context_->txn_, tab_name_, rid_))
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                    AbortReason::SSI_DANGEROUS_STRUCTURE);
            }
            if (ring_on) {
                int32_t payload = -1;
                if (table_record_size_ >= 4) memcpy(&payload, rec_data, 4);
                ReproRing::push(ReproRing::ACCEPT, mvcc_on_ ? ring_key_oid_() : payload, payload,
                                ReproRing::rid32(rid_.page_no, rid_.slot_no),
                                (mvcc_on_ ? 1 : 0) | (from_heap ? 2 : 0));
            }
            positioned_ = true;
            return;
        }
    }

    CompiledCond compile_atom(const Condition &cond) const {
        auto lhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
            return c.name == cond.lhs_col.col_name &&
                   (cond.lhs_col.tab_name.empty() || c.tab_name == cond.lhs_col.tab_name);
        });
        if (lhs_it == cols_.end()) throw ColumnNotFoundError(cond.lhs_col.col_name);

        CompiledCond cc{};
        cc.lhs_offset = lhs_it->offset;
        cc.lhs_len = lhs_it->len;
        cc.lhs_type = lhs_it->type;
        cc.op = cond.op;
        cc.is_rhs_val = cond.is_rhs_val;
        if (cond.is_rhs_val) {
            if (cond.rhs_val.raw == nullptr) {
                throw InternalError("Index scan predicate literal has no raw value");
            }
            cc.rhs_val_data = cond.rhs_val.raw->data;
            cc.rhs_offset = -1;
        } else {
            auto rhs_it = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &c) {
                return c.name == cond.rhs_col.col_name &&
                       (cond.rhs_col.tab_name.empty() || c.tab_name == cond.rhs_col.tab_name);
            });
            if (rhs_it == cols_.end()) throw ColumnNotFoundError(cond.rhs_col.col_name);
            cc.rhs_val_data = nullptr;
            cc.rhs_offset = rhs_it->offset;
        }
        return cc;
    }

    BoolExprPtr<CompiledCond> compile_expr(const ConditionExprPtr &expr) const {
        if (expr == nullptr) return nullptr;
        switch (expr->type) {
            case BoolExprType::CONSTANT:
                return make_bool_constant<CompiledCond>(expr->constant);
            case BoolExprType::ATOM:
                return make_bool_atom<CompiledCond>(compile_atom(expr->atom));
            case BoolExprType::NOT:
                return make_bool_not<CompiledCond>(compile_expr(expr->left));
            case BoolExprType::AND:
            case BoolExprType::OR:
                return make_bool_binary<CompiledCond>(expr->type, compile_expr(expr->left),
                                                       compile_expr(expr->right));
        }
        throw InternalError("Unknown index scan boolean expression node");
    }

    bool eval_compiled(const char *slot) const {
        const TruthValue result = evaluate_bool_expr(
            compiled_predicate_, [&](const CompiledCond &cc) {
            const char *lhs = slot + cc.lhs_offset;
            const char *rhs = cc.is_rhs_val ? cc.rhs_val_data : slot + cc.rhs_offset;
            return cmp_bytes(lhs, rhs, cc.lhs_len, cc.lhs_type, cc.op)
                       ? TruthValue::TRUE_VALUE
                       : TruthValue::FALSE_VALUE;
        });
        return result == TruthValue::TRUE_VALUE;
    }

    /**
     * 把 index key 中第 from_col 列及之后的所有列填成该列类型的极值。
     * 多列索引前缀范围查询时，range 列之后的 suffix 列必须填类型极值（而非字节0）
     * 才能正确界定 B+ 树扫描边界——否则如 (id,name,score) 上 `id>1`，零填充会让
     * upper_bound([1,0,0]) 落到第一个 id=1 的键（因 name 字节 > 0），错误地把 id=1 扫进来。
     * want_max=true 填类型最大值（INT_MAX / FLT_MAX / 0xFF），否则填最小值。
     */
    void fill_extreme_from(char *key, int from_col, bool want_max) const {
        int off = 0;
        for (int i = 0; i < (int)index_meta_.cols.size(); ++i) {
            const auto &c = index_meta_.cols[i];
            if (i >= from_col) {
                if (c.type == TYPE_INT) {
                    int v = want_max ? INT_MAX : INT_MIN;
                    memcpy(key + off, &v, sizeof(int));
                } else if (c.type == TYPE_FLOAT) {
                    float v = want_max ? FLT_MAX : -FLT_MAX;
                    memcpy(key + off, &v, sizeof(float));
                } else {  // TYPE_STRING：按字节比较，0xFF 最大 / 0x00 最小
                    memset(key + off, want_max ? 0xFF : 0x00, c.len);
                }
            }
            off += c.len;
        }
    }

    /* MIN 早停资格（须在 beginTuple 之后询问——eq_match_count_/skip_mode_/ser_on_
     * 都在那里才定型）：非 skip 模式下输出严格按索引 key 序，EQ 前缀各列是常量、
     * cols[eq_match_count_] 是范围内第一个自由列，两者皆升序。MVCC key 一致性过滤
     * 保证每个可见行恰在其快照 key 位置产出，序不被陈旧索引项破坏。SER 下关闭：
     * SSI 的逐行读跟踪依赖真实触行，提前停读会缩小读集。 */
    bool sorted_asc_on(const TabCol &col) const override {
        if (skip_mode_ || ser_on_) return false;
        if (!col.tab_name.empty() && col.tab_name != binding_name_) return false;
        for (int i = 0; i <= eq_match_count_ && i < (int)index_meta_.cols.size(); ++i) {
            if (index_meta_.cols[i].name == col.col_name) return true;
        }
        return false;
    }

    void beginTuple() override {
        // 题9：与 SeqScan 相同的 MVCC 开关——表被 MVCC 写过后必须按快照重建可见版本
        mvcc_on_ = context_ && context_->txn_mgr_ && context_->txn_ &&
                   context_->txn_mgr_->table_is_dirty(tab_name_);
        // 题9 SER：与 SeqScan 相同的 SSI 读跟踪（仅 SELECT 记录读集）
        ser_on_ = context_ && context_->txn_mgr_ && context_->txn_ && context_->ser_in_select_ &&
                  context_->txn_mgr_->is_ser(context_->txn_);
        if (ser_on_) {
            auto physical_predicate = map_bool_atoms<Condition>(predicate_, [&](const Condition &input) {
                Condition cond = input;
                if (cond.lhs_col.tab_name == binding_name_) cond.lhs_col.tab_name = tab_name_;
                if (!cond.is_rhs_val && cond.rhs_col.tab_name == binding_name_) cond.rhs_col.tab_name = tab_name_;
                return cond;
            });
            context_->txn_mgr_->ser_record_pred(context_->txn_, tab_name_, physical_predicate);
            // 读侧(谓词)：匹配本谓词但快照不可见的他事务写(幻影插入) → rw 反依赖；危险结构则 abort
            if (context_->txn_mgr_->ser_read_pred_check(context_->txn_, tab_name_, physical_predicate))
                throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                AbortReason::SSI_DANGEROUS_STRUCTURE);
        }
        auto ih = sm_manager_->ihs_.at(
            sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_col_names_)).get();

        analyze_conditions();
        compiled_predicate_ = compile_expr(predicate_);
        range_exhausted_ = false;
        positioned_ = false;   // 嵌套循环 join 会反复 beginTuple，须清上轮定位态
        ih_ = ih;

        if (skip_mode_) {
            // index skip scan：从索引最小首列值起逐值枚举
            skip_done_ = false;
            need_eval_ = predicate_ != nullptr;
            need_prefix_check_ = true;
            if ((int)cur_key_buf_.size() < index_meta_.col_tot_len) {
                cur_key_buf_.resize(index_meta_.col_tot_len);
            }
            Iid first = ih->leaf_begin();
            IxScan peek(ih, first, ih->leaf_end(), sm_manager_->get_bpm());
            Rid r = peek.rid_and_key(cur_key_buf_.data());
            if (r.page_no < 0) {
                skip_done_ = true;
                range_exhausted_ = true;
                scan_ = std::make_unique<IxScan>(ih, first, first, sm_manager_->get_bpm());
                return;
            }
            skip_open_for_value(cur_key_buf_.data());
            position_to_match();
            skip_fill_valid();
            return;
        }

        // 构造 start_key / end_key：默认 EQ 前缀 + 零填充
        int eq_len = (int)eq_prefix_data_.size();
        std::vector<char> start_key(index_meta_.col_tot_len, 0);
        std::vector<char> end_key(index_meta_.col_tot_len, 0);
        if (eq_len > 0) {
            memcpy(start_key.data(), eq_prefix_data_.data(), eq_len);
            memcpy(end_key.data(), eq_prefix_data_.data(), eq_len);
        }

        // 找 EQ 前缀之后第一个索引列的范围条件，取最紧的上下界
        bool has_lower = false, has_upper = false;
        bool lower_inclusive = false, upper_inclusive = false;
        if (eq_match_count_ < (int)index_meta_.cols.size()) {
            const auto &range_col = index_meta_.cols[eq_match_count_];
            for (const auto &cond : access_conditions_) {
                if (!cond.is_rhs_val) continue;
                if (cond.lhs_col.tab_name != binding_name_) continue;
                if (cond.lhs_col.col_name != range_col.name) continue;

                const char *cv = cond.rhs_val.raw->data;
                if (cond.op == OP_GT || cond.op == OP_GE) {
                    if (!has_lower ||
                        ix_compare(cv, start_key.data() + eq_len, range_col.type, range_col.len) > 0) {
                        memcpy(start_key.data() + eq_len, cv, range_col.len);
                        has_lower = true;
                        lower_inclusive = (cond.op == OP_GE);
                    }
                } else if (cond.op == OP_LT || cond.op == OP_LE) {
                    if (!has_upper ||
                        ix_compare(cv, end_key.data() + eq_len, range_col.type, range_col.len) < 0) {
                        memcpy(end_key.data() + eq_len, cv, range_col.len);
                        has_upper = true;
                        upper_inclusive = (cond.op == OP_LE);
                    }
                }
            }
        }

        // 计算 start_key（key 锚定模式：定位式 lo/hi 在并发 delete_entry 下会失效——
        // 条目左移/搬走后跳行、空扫，Delivery MIN canary 实测；改为把上下界都表达成
        // 完整 key 字节，IxScan 每次访问在锁内实时重定位）
        //   >=val(含)：suffix 填 min（含 val 的所有后缀）
        //   >val (排)：suffix 填 max（真实 key 不会等于极值填充，lower_bound 即跳过 val）
        if (has_lower) {
            fill_extreme_from(start_key.data(), eq_match_count_ + 1, !lower_inclusive);
        } else {
            // 纯 EQ 前缀或无条件：range 列及之后填 min
            fill_extreme_from(start_key.data(), eq_match_count_, false);
        }

        // 计算 end_key + 含端标志
        bool full_eq = (eq_match_count_ == (int)index_meta_.cols.size());
        bool end_incl = true;
        if (has_upper) {
            // <=val(含)：suffix 填 max、含端；<val(排)：suffix 填 min、排端
            fill_extreme_from(end_key.data(), eq_match_count_ + 1, upper_inclusive);
            end_incl = upper_inclusive;
        } else if (full_eq) {
            end_incl = true;   // end_key == 全 EQ 前缀本身
        } else {
            // 部分 EQ 或纯前缀：上界为前缀的最大后缀（prefix check 亦兜底早停）
            fill_extreme_from(end_key.data(), eq_match_count_, true);
        }

        // 逐行防线不变：所有值条件始终 eval；EQ 前缀检查始终开启（早期硬停）。
        need_eval_ = predicate_ != nullptr;
        need_prefix_check_ = (eq_match_count_ > 0);

        scan_ = std::make_unique<IxScan>(ih, start_key.data(), end_key.data(), end_incl,
                                         sm_manager_->get_bpm());
        position_to_match();
    }

    void nextTuple() override {
        if (range_exhausted_ || !positioned_) {
            if (skip_mode_) skip_fill_valid();
            return;
        }
        scan_->next();
        position_to_match();
        if (skip_mode_) skip_fill_valid();
    }

    bool is_end() const override { return !positioned_; }

    std::unique_ptr<RmRecord> Next() override {
        auto rec = std::make_unique<RmRecord>(table_record_size_);
        if (mvcc_on_) {
            // 题9：position_to_match 已为当前 rid_ 重建可见版本，直接物化
            memcpy(rec->data, mvcc_buf_.data(), table_record_size_);
        } else {
            // 从缓存的 slot 复制到新 RmRecord（仅匹配时调一次）
            const char *slot = get_table_slot(rid_);
            memcpy(rec->data, slot, table_record_size_);
        }
        return rec;
    }

    size_t tupleLen() const override { return len_; }
    const std::vector<ColMeta> &cols() const override { return cols_; }

    Rid &rid() override { return rid_; }
};
