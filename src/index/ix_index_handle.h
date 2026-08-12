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

#include <functional>
#include <shared_mutex>

#include "common/fair_shared_mutex.h"
#include "ix_defs.h"
#include "transaction/transaction.h"

enum class Operation { FIND = 0, INSERT, DELETE };  // 三种操作：查找、插入、删除

static const bool binary_search = false;

inline int ix_compare(const char *a, const char *b, ColType type, int col_len) {
    switch (type) {
        case TYPE_INT: {
            int ia = 0;
            int ib = 0;
            memcpy(&ia, a, sizeof(ia));
            memcpy(&ib, b, sizeof(ib));
            return (ia < ib) ? -1 : ((ia > ib) ? 1 : 0);
        }
        case TYPE_FLOAT: {
            float fa = 0.0F;
            float fb = 0.0F;
            memcpy(&fa, a, sizeof(fa));
            memcpy(&fb, b, sizeof(fb));
            return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
        }
        case TYPE_STRING:
            return memcmp(a, b, col_len);
        default:
            throw InternalError("Unexpected data type");
    }
}

inline int ix_compare(const char* a, const char* b, const std::vector<ColType>& col_types, const std::vector<int>& col_lens) {
    int offset = 0;
    for(size_t i = 0; i < col_types.size(); ++i) {
        int res = ix_compare(a + offset, b + offset, col_types[i], col_lens[i]);
        if(res != 0) return res;
        offset += col_lens[i];
    }
    return 0;
}

/* 管理B+树中的每个节点 */
class IxNodeHandle {
    friend class IxIndexHandle;
    friend class IxScan;

   private:
    const IxFileHdr *file_hdr;      // 节点所在文件的头部信息
    Page *page;                     // 存储节点的页面
    IxPageHdr *page_hdr;            // page->data的第一部分，指针指向首地址，长度为sizeof(IxPageHdr)
    char *keys;                     // page->data的第二部分，指针指向首地址，长度为file_hdr->keys_size，每个key的长度为file_hdr->col_len
    Rid *rids;                      // page->data的第三部分，指针指向首地址

   public:
    IxNodeHandle() = default;

    IxNodeHandle(const IxFileHdr *file_hdr_, Page *page_) : file_hdr(file_hdr_), page(page_) {
        page_hdr = reinterpret_cast<IxPageHdr *>(page->get_data());
        keys = page->get_data() + sizeof(IxPageHdr);
        rids = reinterpret_cast<Rid *>(keys + file_hdr->keys_size_);
    }

    int get_size() { return page_hdr->num_key; }

    void set_size(int size) { page_hdr->num_key = size; }

    int get_max_size() { return file_hdr->btree_order_ + 1; }

    int get_min_size() { return get_max_size() / 2; }

    int key_at(int i) {
        int value = 0;
        memcpy(&value, get_key(i), sizeof(value));
        return value;
    }

    /* 得到第i个孩子结点的page_no */
    page_id_t value_at(int i) { return get_rid(i)->page_no; }

    page_id_t get_page_no() { return page->get_page_id().page_no; }

    PageId get_page_id() { return page->get_page_id(); }

    page_id_t get_next_leaf() { return page_hdr->next_leaf; }

    page_id_t get_prev_leaf() { return page_hdr->prev_leaf; }

    page_id_t get_parent_page_no() { return page_hdr->parent; }

    bool is_leaf_page() { return page_hdr->is_leaf; }

    bool is_root_page() { return get_parent_page_no() == INVALID_PAGE_ID; }

    void set_next_leaf(page_id_t page_no) { page_hdr->next_leaf = page_no; }

    void set_prev_leaf(page_id_t page_no) { page_hdr->prev_leaf = page_no; }

    void set_parent_page_no(page_id_t parent) { page_hdr->parent = parent; }

    char *get_key(int key_idx) const { return keys + key_idx * file_hdr->col_tot_len_; }

    Rid *get_rid(int rid_idx) const { return &rids[rid_idx]; }

    void set_key(int key_idx, const char *key) { memcpy(keys + key_idx * file_hdr->col_tot_len_, key, file_hdr->col_tot_len_); }

    void set_rid(int rid_idx, const Rid &rid) { rids[rid_idx] = rid; }

    int lower_bound(const char *target) const;

    int upper_bound(const char *target) const;

    void insert_pairs(int pos, const char *key, const Rid *rid, int n);

    page_id_t internal_lookup(const char *key);

    bool leaf_lookup(const char *key, Rid **value);

    int insert(const char *key, const Rid &value);

    // 用于在结点中的指定位置插入单个键值对
    void insert_pair(int pos, const char *key, const Rid &rid) { insert_pairs(pos, key, &rid, 1); }

    void erase_pair(int pos);

    int remove(const char *key);

    /**
     * @brief used in internal node to remove the last key in root node, and return the last child
     *
     * @return the last child
     */
    page_id_t remove_and_return_only_child() {
        assert(get_size() == 1);
        page_id_t child_page_no = value_at(0);
        erase_pair(0);
        assert(get_size() == 0);
        return child_page_no;
    }

    /**
     * @brief 由parent调用，寻找child，返回child在parent中的rid_idx∈[0,page_hdr->num_key)
     * @param child
     * @return int
     */
    int find_child(IxNodeHandle *child) {
        int rid_idx;
        for (rid_idx = 0; rid_idx < page_hdr->num_key; rid_idx++) {
            if (get_rid(rid_idx)->page_no == child->get_page_no()) {
                break;
            }
        }
        // Release 版 assert 是空操作——child 的 parent 指针陈旧/被踩时这里若返回
        // num_key，调用方 value_at(idx±1) 会取到【任意页】当兄弟，coalesce 把不相邻
        // 叶缝合 = 叶链整段摘除（2026-07-31 W=10 实测 2.5 万条目孤儿段，宽扫描漏行
        // 而点查可见）。返回 -1 让调用方拒绝本次结构调整（欠填叶合法，无正确性代价）。
        if (rid_idx >= page_hdr->num_key) {
            fprintf(stderr, "[ix-guard] find_child miss: child=%d not in parent=%d (num_key=%d)\n",
                    child->get_page_no(), get_page_no(), page_hdr->num_key);
            return -1;
        }
        return rid_idx;
    }
};

/* B+树 */
class IxIndexHandle {
    friend class IxScan;
    friend class IxManager;

   private:
    DiskManager *disk_manager_;
    BufferPoolManager *buffer_pool_manager_;
    int fd_;                                    // 存储B+树的文件
    IxFileHdr* file_hdr_;                       // 存了root_page，但其初始化为2（第0页存FILE_HDR_PAGE，第1页存LEAF_HEADER_PAGE）
    // 读 shared、写 unique，分裂/合并与扫描并发。写者优先（见 FairSharedMutex 注释）：
    // key 锚定扫描按行取共享锁形成常驻读流量，读者优先策略下写者会饿死到秒级
    mutable FairSharedMutex root_latch_;
    page_id_t cached_leaf_no_ = IX_NO_PAGE;     // 顺序追加插入缓存的最右叶页号，命中则跳过从根遍历

    // 顺序插入落在同一叶时，把上次的 pin 一直攥着不放：命中时连 fetch_node 都不用调，
    // 免了一次 BPM 往返。只要叶结构没变就一直有效；分裂/删除/析构都要放掉。
    page_id_t pinned_leaf_no_ = IX_NO_PAGE;
    Page *pinned_leaf_page_ = nullptr;
    void release_pinned_leaf();

   public:
    IxIndexHandle(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager, int fd);
    ~IxIndexHandle();

    // for search
    bool get_value(const char *key, std::vector<Rid> *result, Transaction *transaction);

    std::pair<IxNodeHandle *, bool> find_leaf_page(const char *key, Operation operation, Transaction *transaction,
                                                 bool find_first = false);

    // for insert
    // inserted 非空时返回本次是否真正插入；重复键仍保持原有返回页号，便于调用方
    // 在不改变唯一 B+ 树结构的前提下识别唯一性冲突。
    page_id_t insert_entry(const char *key, const Rid &value, Transaction *transaction,
                           bool *inserted = nullptr);

    /* 批量装载：next(key_out, rid_out) 按 key 升序（ix_compare 序）逐条产出 n 条
     * 键值对，自底向上顺序构建整棵树并直接写盘（绕过逐条 insert_entry 的全树下降
     * 与分裂）。恢复期全量索引重建 W=50 从 30+ 分钟降到秒级的关键路径。
     * 要求：索引当前为空（刚 create）；调用期间无并发访问。 */
    void bulk_load(long n, const std::function<void(char *key_out, Rid *rid_out)> &next);

    IxNodeHandle *split(IxNodeHandle *node);

    void insert_into_parent(IxNodeHandle *old_node, const char *key, IxNodeHandle *new_node, Transaction *transaction);

    // for delete
    bool delete_entry(const char *key, Transaction *transaction);

    bool coalesce_or_redistribute(IxNodeHandle *node, Transaction *transaction = nullptr,
                                bool *root_is_latched = nullptr);
    bool adjust_root(IxNodeHandle *old_root_node);

    void redistribute(IxNodeHandle *neighbor_node, IxNodeHandle *node, IxNodeHandle *parent, int index);

    bool coalesce(IxNodeHandle **neighbor_node, IxNodeHandle **node, IxNodeHandle **parent, int index,
                  Transaction *transaction, bool *root_is_latched);

    Iid lower_bound(const char *key);

    Iid upper_bound(const char *key);

    Iid leaf_end() const;

    Iid leaf_begin() const;

    /* 索引 key 总字节数（IxScan::rid_and_key 拷贝 key 用） */
    int get_fhdr_col_tot_len() const { return file_hdr_->col_tot_len_; }
    /* nolock 变体：调用方须已持 root_latch_（IxScan key 锚定模式的锁内重定位） */
    Iid lower_bound_nolock(const char *key) const;
    Iid upper_bound_nolock(const char *key) const;
    const IxFileHdr *get_fhdr() const { return file_hdr_; }

    /* 调试取证（CHAINWALK 命令）：沿叶链走全程，按 key 前 8 字节（两 int 前缀，
     * 适配 orders/new_orders 的 (w,d) 形态）聚合条目数并报告顺序违例/坏链。
     * 与逐 (w,d) 树下降点查计数对比，可裁决"叶链跳段"型结构损伤。 */
    void debug_chain_walk(FILE *out);
    /* 调试取证（TREEWALK 命令）：从根 DFS 按树序枚举全部叶，打印每叶 page/prev/next/
     * size/首尾 key 末 int，并与链序对比标记"树内但不在链上"的孤儿叶。 */
    void debug_tree_walk(FILE *out);
    /* 偏执校验（RMDB_IX_PARANOID=1）：结构操作后验证 page 的父链（每级 parent 确实
     * 含 child）与叶邻接（next 的 prev 回指）。violation 处打印 op 上下文。 */
    void paranoid_verify(page_id_t leaf_page, const char *op);

   private:
    // 辅助函数
    void update_root_page_no(page_id_t root) { file_hdr_->root_page_ = root; }

    bool is_empty() const { return file_hdr_->root_page_ == IX_NO_PAGE; }

    // for get/create node
    IxNodeHandle *fetch_node(int page_no) const;

    IxNodeHandle *create_node();

    // for maintain data structure
    void maintain_parent(IxNodeHandle *node);

    void erase_leaf(IxNodeHandle *leaf);

    void release_node_handle(IxNodeHandle &node);

    void maintain_child(IxNodeHandle *node, int child_idx);

    // for index test
    Rid get_rid(const Iid &iid) const;
};
