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
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class DeleteExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Condition> conds_;  // delete的条件
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::vector<Rid> rids_;         // 需要删除的记录的位置
    std::string tab_name_;          // 表名称
    SmManager *sm_manager_;
    // 优化 5：跨 rid 复用 page handle
    int cached_page_no_ = -1;
    Page *cached_page_ = nullptr;
    RmPageHdr *cached_page_hdr_ = nullptr;
    char *cached_bitmap_ = nullptr;
    char *cached_slots_ = nullptr;     // 题3：读旧记录构造索引 key
    int record_size_ = 0;

   public:
    DeleteExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Condition> conds,
                   std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
        record_size_ = fh_->get_file_hdr().record_size;
    }

    void release_cached_page() {
        if (cached_page_) {
            sm_manager_->get_bpm()->unpin_page(cached_page_->get_page_id(), true);
            cached_page_ = nullptr;
            cached_page_no_ = -1;
            cached_page_hdr_ = nullptr;
            cached_bitmap_ = nullptr;
            cached_slots_ = nullptr;
        }
    }

    void cache_page(int page_no) {
        if (page_no != cached_page_no_) {
            release_cached_page();
            RmPageHandle handle = fh_->fetch_page_handle(page_no);
            cached_page_ = handle.page;
            cached_page_no_ = page_no;
            cached_page_hdr_ = handle.page_hdr;
            cached_bitmap_ = handle.bitmap;
            cached_slots_ = handle.slots;
        }
    }

    /**
     * @description: 遍历所有匹配的rid，逐条调用RmFileHandle::delete_record
     *               题3实现后还需要同步删除索引项
     */
    std::unique_ptr<RmRecord> Next() override {
        auto &file_hdr = fh_->get_file_hdr_mut();
        int num_per_page = file_hdr.num_records_per_page;
        // 题9：删除一律走逻辑删除（保留堆槽、登记删除版本），不走释放堆槽的快路径——
        // 物理删除会让堆槽被后续 insert 复用，使 select * 行序与标准(全程 MVCC 逻辑删除,
        // 重插行总在末尾)不一致。差分测试已证实该行序分歧。
        bool versioning = context_ && context_->txn_mgr_ && context_->txn_;
        for (const auto &rid : rids_) {
            cache_page(rid.page_no);
            char *slot = cached_slots_ + rid.slot_no * record_size_;

            // 题9 MVCC：逻辑删除——写写冲突检测 + 登记删除版本，保留堆槽与索引供快照读
            if (versioning) {
                if (!context_->txn_mgr_->mvcc_write(context_->txn_, tab_name_, rid,
                                                    slot, nullptr, record_size_, true)) {
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                    AbortReason::DEADLOCK_PREVENTION);
                }
                // 题9 SER：被删旧记录 vs 其他事务读 → rw 反依赖；成 SSI 危险结构则 abort
                if (context_->txn_mgr_->is_ser(context_->txn_) &&
                    context_->txn_mgr_->ser_write_check(context_->txn_, tab_name_, rid, slot)) {
                    throw TransactionAbortException(context_->txn_->get_transaction_id(),
                                                    AbortReason::DEADLOCK_PREVENTION);
                }
                continue;
            }

            // 题3：删数据前，先把这条记录从所有索引里删除
            for (auto &index : tab_.indexes) {
                std::vector<char> key(index.col_tot_len);
                int offset = 0;
                for (auto &idx_col : index.cols) {
                    memcpy(key.data() + offset, slot + idx_col.offset, idx_col.len);
                    offset += idx_col.len;
                }
                auto ih = sm_manager_->ihs_.at(
                    sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                ih->delete_entry(key.data(), context_ ? context_->txn_ : nullptr);
            }

            // 删数据：bitmap + 计数 + free-list 维护
            bool was_full = cached_page_hdr_->num_records == num_per_page;
            Bitmap::reset(cached_bitmap_, rid.slot_no);
            cached_page_hdr_->num_records--;
            if (was_full) {
                cached_page_hdr_->next_free_page_no = file_hdr.first_free_page_no;
                file_hdr.first_free_page_no = rid.page_no;
            }
        }
        release_cached_page();
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }

    ~DeleteExecutor() override { release_cached_page(); }
};
