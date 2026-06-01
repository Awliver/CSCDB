/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "ix_index_handle.h"

#include "ix_scan.h"

/**
 * @brief 在当前node中查找第一个>=target的key_idx
 *
 * @return key_idx，范围为[0,num_key)，如果返回的key_idx=num_key，则表示target大于最后一个key
 * @note 返回key index（同时也是rid index），作为slot no
 */
int IxNodeHandle::lower_bound(const char *target) const {
    // 二分查找：第一个 key >= target 的位置
    int l = 0, r = page_hdr->num_key;
    while (l < r) {
        int mid = (l + r) >> 1;
        if (ix_compare(get_key(mid), target, file_hdr->col_types_, file_hdr->col_lens_) >= 0) {
            r = mid;
        } else {
            l = mid + 1;
        }
    }
    return l;
}

/**
 * @brief 在当前node中查找第一个>target的key_idx
 *
 * @return key_idx，范围为[1,num_key)，如果返回的key_idx=num_key，则表示target大于等于最后一个key
 * @note 注意此处的范围从1开始
 */
int IxNodeHandle::upper_bound(const char *target) const {
    // 二分查找：第一个 key > target 的位置（从 1 开始）
    int l = 1, r = page_hdr->num_key;
    while (l < r) {
        int mid = (l + r) >> 1;
        if (ix_compare(get_key(mid), target, file_hdr->col_types_, file_hdr->col_lens_) > 0) {
            r = mid;
        } else {
            l = mid + 1;
        }
    }
    return l;
}

/**
 * @brief 用于叶子结点根据key来查找该结点中的键值对
 * 值value作为传出参数，函数返回是否查找成功
 *
 * @param key 目标key
 * @param[out] value 传出参数，目标key对应的Rid
 * @return 目标key是否存在
 */
bool IxNodeHandle::leaf_lookup(const char *key, Rid **value) {
    int idx = lower_bound(key);
    if (idx >= page_hdr->num_key) return false;
    if (ix_compare(get_key(idx), key, file_hdr->col_types_, file_hdr->col_lens_) != 0) return false;
    *value = get_rid(idx);
    return true;
}

/**
 * 用于内部结点（非叶子节点）查找目标key所在的孩子结点（子树）
 * @param key 目标key
 * @return page_id_t 目标key所在的孩子节点（子树）的存储页面编号
 */
page_id_t IxNodeHandle::internal_lookup(const char *key) {
    // 内部节点：upper_bound 返回第一个 > key 的位置 i，则 key 落在 i-1 子树
    int idx = upper_bound(key);
    return value_at(idx - 1);
}

/**
 * @brief 在指定位置插入n个连续的键值对
 * 将key的前n位插入到原来keys中的pos位置；将rid的前n位插入到原来rids中的pos位置
 *
 * @param pos 要插入键值对的位置
 * @param (key, rid) 连续键值对的起始地址，也就是第一个键值对，可以通过(key, rid)来获取n个键值对
 * @param n 键值对数量
 * @note [0,pos)           [pos,num_key)
 *                            key_slot
 *                            /      \
 *                           /        \
 *       [0,pos)     [pos,pos+n)   [pos+n,num_key+n)
 *                      key           key_slot
 */
void IxNodeHandle::insert_pairs(int pos, const char *key, const Rid *rid, int n) {
    assert(pos >= 0 && pos <= page_hdr->num_key);
    int len = file_hdr->col_tot_len_;
    int old_num = page_hdr->num_key;
    // 把 [pos, old_num) 的 keys/rids 向后移 n 位
    memmove(keys + (pos + n) * len, keys + pos * len, (old_num - pos) * len);
    memmove(rids + pos + n, rids + pos, (old_num - pos) * sizeof(Rid));
    // 写入新数据
    memcpy(keys + pos * len, key, n * len);
    memcpy(rids + pos, rid, n * sizeof(Rid));
    page_hdr->num_key += n;
}

/**
 * @brief 用于在结点中插入单个键值对。
 * 函数返回插入后的键值对数量
 *
 * @param (key, value) 要插入的键值对
 * @return int 键值对数量
 */
int IxNodeHandle::insert(const char *key, const Rid &value) {
    int pos = lower_bound(key);
    // 已存在则跳过插入（唯一索引语义）
    if (pos < page_hdr->num_key &&
        ix_compare(get_key(pos), key, file_hdr->col_types_, file_hdr->col_lens_) == 0) {
        return page_hdr->num_key;
    }
    insert_pair(pos, key, value);
    return page_hdr->num_key;
}

/**
 * @brief 用于在结点中的指定位置删除单个键值对
 *
 * @param pos 要删除键值对的位置
 */
void IxNodeHandle::erase_pair(int pos) {
    assert(pos >= 0 && pos < page_hdr->num_key);
    int len = file_hdr->col_tot_len_;
    int n = page_hdr->num_key;
    // 把 [pos+1, n) 的 keys/rids 向前移 1
    memmove(keys + pos * len, keys + (pos + 1) * len, (n - pos - 1) * len);
    memmove(rids + pos, rids + pos + 1, (n - pos - 1) * sizeof(Rid));
    page_hdr->num_key--;
}

/**
 * @brief 用于在结点中删除指定key的键值对。函数返回删除后的键值对数量
 *
 * @param key 要删除的键值对key值
 * @return 完成删除操作后的键值对数量
 */
int IxNodeHandle::remove(const char *key) {
    int pos = lower_bound(key);
    if (pos >= page_hdr->num_key) return page_hdr->num_key;
    if (ix_compare(get_key(pos), key, file_hdr->col_types_, file_hdr->col_lens_) != 0) {
        return page_hdr->num_key;  // 未找到
    }
    erase_pair(pos);
    return page_hdr->num_key;
}

IxIndexHandle::IxIndexHandle(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager, int fd)
    : disk_manager_(disk_manager), buffer_pool_manager_(buffer_pool_manager), fd_(fd) {
    // init file_hdr_
    disk_manager_->read_page(fd, IX_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(file_hdr_));
    char* buf = new char[PAGE_SIZE];
    memset(buf, 0, PAGE_SIZE);
    disk_manager_->read_page(fd, IX_FILE_HDR_PAGE, buf, PAGE_SIZE);
    file_hdr_ = new IxFileHdr();
    file_hdr_->deserialize(buf);
    delete[] buf;
    
    // disk_manager管理的fd对应的文件中，设置从file_hdr_->num_pages开始分配page_no
    int now_page_no = disk_manager_->get_fd2pageno(fd);
    disk_manager_->set_fd2pageno(fd, now_page_no + 1);
}

/**
 * @brief 用于查找指定键所在的叶子结点
 * @param key 要查找的目标key值
 * @param operation 查找到目标键值对后要进行的操作类型
 * @param transaction 事务参数，如果不需要则默认传入nullptr
 * @return [leaf node] and [root_is_latched] 返回目标叶子结点以及根结点是否加锁
 * @note need to Unlatch and unpin the leaf node outside!
 * 注意：用了FindLeafPage之后一定要unlatch叶结点，否则下次latch该结点会堵塞！
 */
std::pair<IxNodeHandle *, bool> IxIndexHandle::find_leaf_page(const char *key, Operation operation,
                                                            Transaction *transaction, bool find_first) {
    IxNodeHandle *node = fetch_node(file_hdr_->root_page_);
    while (!node->is_leaf_page()) {
        page_id_t child_page = find_first ? node->value_at(0) : node->internal_lookup(key);
        buffer_pool_manager_->unpin_page(node->get_page_id(), false);
        delete node;
        node = fetch_node(child_page);
    }
    return std::make_pair(node, false);
}

/**
 * @brief 用于查找指定键在叶子结点中的对应的值result
 *
 * @param key 查找的目标key值
 * @param result 用于存放结果的容器
 * @param transaction 事务指针
 * @return bool 返回目标键值对是否存在
 */
bool IxIndexHandle::get_value(const char *key, std::vector<Rid> *result, Transaction *transaction) {
    std::scoped_lock<std::mutex> lock(root_latch_);
    auto [leaf, _] = find_leaf_page(key, Operation::FIND, transaction);
    Rid *rid_ptr = nullptr;
    bool found = leaf->leaf_lookup(key, &rid_ptr);
    if (found && rid_ptr != nullptr) {
        result->push_back(*rid_ptr);
    }
    buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
    delete leaf;
    return found;
}

/**
 * @brief  将传入的一个node拆分(Split)成两个结点，在node的右边生成一个新结点new node
 * @param node 需要拆分的结点
 * @return 拆分得到的new_node
 * @note need to unpin the new node outside
 * 注意：本函数执行完毕后，原node和new node都需要在函数外面进行unpin
 */
IxNodeHandle *IxIndexHandle::split(IxNodeHandle *node) {
    IxNodeHandle *new_node = create_node();
    new_node->page_hdr->is_leaf = node->is_leaf_page();
    new_node->page_hdr->parent = node->get_parent_page_no();
    new_node->page_hdr->num_key = 0;
    new_node->page_hdr->next_free_page_no = IX_NO_PAGE;

    int old_size = node->get_size();
    int split_pos = old_size / 2;
    int move_count = old_size - split_pos;

    // 把 node 的右半 [split_pos, old_size) keys/rids 复制到 new_node
    new_node->insert_pairs(0, node->get_key(split_pos), node->get_rid(split_pos), move_count);
    node->set_size(split_pos);

    if (node->is_leaf_page()) {
        // 维护叶子双链：node -> new_node -> old_next
        page_id_t old_next = node->get_next_leaf();
        new_node->set_next_leaf(old_next);
        new_node->set_prev_leaf(node->get_page_no());
        node->set_next_leaf(new_node->get_page_no());

        // 更新原 next 节点的 prev_leaf（可能是普通 leaf 或 leaf_header）
        IxNodeHandle *next_node = fetch_node(old_next);
        next_node->set_prev_leaf(new_node->get_page_no());
        buffer_pool_manager_->unpin_page(next_node->get_page_id(), true);
        delete next_node;

        // 如果 node 之前是 last_leaf，更新 last_leaf
        if (node->get_page_no() == file_hdr_->last_leaf_) {
            file_hdr_->last_leaf_ = new_node->get_page_no();
        }
    } else {
        // 内部节点：new_node 接管了 node 右半的子树，更新这些子树的 parent
        for (int i = 0; i < new_node->get_size(); i++) {
            maintain_child(new_node, i);
        }
    }

    return new_node;
}

/**
 * @brief Insert key & value pair into internal page after split
 * 拆分(Split)后，向上找到old_node的父结点
 * 将new_node的第一个key插入到父结点，其位置在 父结点指向old_node的孩子指针 之后
 * 如果插入后>=maxsize，则必须继续拆分父结点，然后在其父结点的父结点再插入，即需要递归
 * 直到找到的old_node为根结点时，结束递归（此时将会新建一个根R，关键字为key，old_node和new_node为其孩子）
 *
 * @param (old_node, new_node) 原结点为old_node，old_node被分裂之后产生了新的右兄弟结点new_node
 * @param key 要插入parent的key
 * @note 一个结点插入了键值对之后需要分裂，分裂后左半部分的键值对保留在原结点，在参数中称为old_node，
 * 右半部分的键值对分裂为新的右兄弟节点，在参数中称为new_node（参考Split函数来理解old_node和new_node）
 * @note 本函数执行完毕后，new node和old node都需要在函数外面进行unpin
 */
void IxIndexHandle::insert_into_parent(IxNodeHandle *old_node, const char *key, IxNodeHandle *new_node,
                                     Transaction *transaction) {
    if (old_node->is_root_page()) {
        // 1. old_node 是根：建新 root，把 old/new 作为新 root 的左右孩子
        IxNodeHandle *new_root = create_node();
        new_root->page_hdr->is_leaf = false;
        new_root->page_hdr->parent = IX_NO_PAGE;
        new_root->page_hdr->num_key = 0;
        new_root->page_hdr->next_free_page_no = IX_NO_PAGE;

        // 装两条内部条目
        new_root->insert_pair(0, old_node->get_key(0), Rid{old_node->get_page_no(), -1});
        new_root->insert_pair(1, key, Rid{new_node->get_page_no(), -1});

        // 更新 old/new 的 parent 指向新 root
        old_node->set_parent_page_no(new_root->get_page_no());
        new_node->set_parent_page_no(new_root->get_page_no());

        // 更新树的 root_page_
        file_hdr_->root_page_ = new_root->get_page_no();

        buffer_pool_manager_->unpin_page(new_root->get_page_id(), true);
        delete new_root;
        return;
    }

    // 2. 非根：找到 old_node 在 parent 中的位置，把 (key, new_node) 插在它后一格
    IxNodeHandle *parent = fetch_node(old_node->get_parent_page_no());
    int old_idx = parent->find_child(old_node);
    parent->insert_pair(old_idx + 1, key, Rid{new_node->get_page_no(), -1});
    new_node->set_parent_page_no(parent->get_page_no());

    // 3. 若 parent 也满了，递归 split
    if (parent->get_size() >= parent->get_max_size()) {
        IxNodeHandle *new_parent = split(parent);
        insert_into_parent(parent, new_parent->get_key(0), new_parent, transaction);
        buffer_pool_manager_->unpin_page(new_parent->get_page_id(), true);
        delete new_parent;
    }

    buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
    delete parent;
}

/**
 * @brief 将指定键值对插入到B+树中
 * @param (key, value) 要插入的键值对
 * @param transaction 事务指针
 * @return page_id_t 插入到的叶结点的page_no
 */
page_id_t IxIndexHandle::insert_entry(const char *key, const Rid &value, Transaction *transaction) {
    std::scoped_lock<std::mutex> lock(root_latch_);

    auto [leaf, _] = find_leaf_page(key, Operation::INSERT, transaction);
    int old_size = leaf->get_size();
    int new_size = leaf->insert(key, value);
    page_id_t leaf_page = leaf->get_page_no();

    if (new_size == old_size) {
        // 重复 key，未插入（唯一索引语义）
        buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
        delete leaf;
        return leaf_page;
    }

    // 插入可能改变了叶子的首 key，向上传播
    maintain_parent(leaf);

    // 满了则分裂并把新节点上插
    if (new_size >= leaf->get_max_size()) {
        IxNodeHandle *new_leaf = split(leaf);
        insert_into_parent(leaf, new_leaf->get_key(0), new_leaf, transaction);
        buffer_pool_manager_->unpin_page(new_leaf->get_page_id(), true);
        delete new_leaf;
    }

    buffer_pool_manager_->unpin_page(leaf->get_page_id(), true);
    delete leaf;
    return leaf_page;
}

/**
 * @brief 用于删除B+树中含有指定key的键值对
 * @param key 要删除的key值
 * @param transaction 事务指针
 */
bool IxIndexHandle::delete_entry(const char *key, Transaction *transaction) {
    std::scoped_lock<std::mutex> lock(root_latch_);

    auto [leaf, _] = find_leaf_page(key, Operation::DELETE, transaction);
    int old_size = leaf->get_size();
    int new_size = leaf->remove(key);

    if (new_size == old_size) {
        // 没找到 key
        buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
        delete leaf;
        return false;
    }

    // 删除后叶子可能首 key 变化，向上传播
    if (new_size > 0) {
        maintain_parent(leaf);
    }

    bool node_deleted = coalesce_or_redistribute(leaf, transaction);

    if (node_deleted) {
        // leaf 的页已被 coalesce 删除，只需释放 IxNodeHandle
        delete leaf;
    } else {
        buffer_pool_manager_->unpin_page(leaf->get_page_id(), true);
        delete leaf;
    }
    return true;
}

/**
 * @brief 用于处理合并和重分配的逻辑，用于删除键值对后调用
 *
 * @param node 执行完删除操作的结点
 * @param transaction 事务指针
 * @param root_is_latched 传出参数：根节点是否上锁，用于并发操作
 * @return 是否需要删除结点
 * @note User needs to first find the sibling of input page.
 * If sibling's size + input page's size >= 2 * page's minsize, then redistribute.
 * Otherwise, merge(Coalesce).
 */
bool IxIndexHandle::coalesce_or_redistribute(IxNodeHandle *node, Transaction *transaction, bool *root_is_latched) {
    // 1. 根节点：交给 adjust_root
    if (node->is_root_page()) {
        return adjust_root(node);
    }

    // 2. 不需要调整
    if (node->get_size() >= node->get_min_size()) {
        return false;
    }

    // 3. 找 parent + sibling（优先前驱）
    IxNodeHandle *parent = fetch_node(node->get_parent_page_no());
    int idx = parent->find_child(node);
    int sibling_idx = (idx == 0) ? 1 : idx - 1;
    IxNodeHandle *sibling = fetch_node(parent->value_at(sibling_idx));

    // 4. 重分配 vs 合并
    if (node->get_size() + sibling->get_size() >= 2 * node->get_min_size()) {
        // 重分配
        redistribute(sibling, node, parent, idx);
        buffer_pool_manager_->unpin_page(sibling->get_page_id(), true);
        delete sibling;
        buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        delete parent;
        return false;  // node 保留
    }

    // 5. 合并：调用 coalesce（内部可能 swap sibling 与 node 局部变量）
    bool parent_died = coalesce(&sibling, &node, &parent, idx, transaction, root_is_latched);

    // parent 清理
    if (parent_died) {
        delete parent;  // 页已被递归删除
    } else {
        buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        delete parent;
    }

    // sibling vs node 的清理用 idx（捕获前）判断：
    if (idx == 0) {
        // coalesce 内 swap 了：现在 `sibling` 变量指向调用方 node（LEFT 保留），
        // `node` 变量指向原 sibling（RIGHT 已删页）
        delete node;  // 释放原 sibling 的 IxNodeHandle，页已删
        // 调用方 node 仍 pin 着，调用方负责 unpin + delete
        return false;
    } else {
        // 无 swap：`sibling` 是 LEFT 保留，`node` 是调用方 node 即 RIGHT 已删页
        buffer_pool_manager_->unpin_page(sibling->get_page_id(), true);
        delete sibling;
        // 调用方 node 的页已删，调用方释放 IxNodeHandle
        return true;
    }
}

/**
 * @brief 用于当根结点被删除了一个键值对之后的处理
 * @param old_root_node 原根节点
 * @return bool 根结点是否需要被删除
 * @note size of root page can be less than min size and this method is only called within coalesce_or_redistribute()
 */
bool IxIndexHandle::adjust_root(IxNodeHandle *old_root_node) {
    // 情况 1：内部根 + size==1 → 单一子节点上位为新根
    if (!old_root_node->is_leaf_page() && old_root_node->get_size() == 1) {
        page_id_t child_page = old_root_node->value_at(0);
        IxNodeHandle *child = fetch_node(child_page);
        child->set_parent_page_no(IX_NO_PAGE);
        file_hdr_->root_page_ = child_page;
        buffer_pool_manager_->unpin_page(child->get_page_id(), true);
        delete child;

        // 删除旧根的页
        page_id_t old_root_page = old_root_node->get_page_no();
        buffer_pool_manager_->unpin_page(old_root_node->get_page_id(), true);
        buffer_pool_manager_->delete_page(PageId{fd_, old_root_page});
        release_node_handle(*old_root_node);
        return true;  // 调用方释放 IxNodeHandle
    }
    // 情况 2 & 3：叶子根空 / 其他都不动，保留作为后续插入的入口
    return false;
}

/**
 * @brief 重新分配node和兄弟结点neighbor_node的键值对
 * Redistribute key & value pairs from one page to its sibling page. If index == 0, move sibling page's first key
 * & value pair into end of input "node", otherwise move sibling page's last key & value pair into head of input "node".
 *
 * @param neighbor_node sibling page of input "node"
 * @param node input from method coalesceOrRedistribute()
 * @param parent the parent of "node" and "neighbor_node"
 * @param index node在parent中的rid_idx
 * @note node是之前刚被删除过一个key的结点
 * index=0，则neighbor是node后继结点，表示：node(left)      neighbor(right)
 * index>0，则neighbor是node前驱结点，表示：neighbor(left)  node(right)
 * 注意更新parent结点的相关kv对
 */
void IxIndexHandle::redistribute(IxNodeHandle *neighbor_node, IxNodeHandle *node, IxNodeHandle *parent, int index) {
    if (index == 0) {
        // node 在 parent 的索引 0，neighbor 在索引 1（neighbor 是后继）
        // 把 neighbor 第一个 (key, rid) 移到 node 末尾
        int node_size = node->get_size();
        node->insert_pair(node_size, neighbor_node->get_key(0), *neighbor_node->get_rid(0));
        if (!node->is_leaf_page()) {
            maintain_child(node, node_size);
        }
        neighbor_node->erase_pair(0);
        // neighbor 首 key 变化，更新 parent 中 neighbor 对应的 key
        memcpy(parent->get_key(1), neighbor_node->get_key(0), file_hdr_->col_tot_len_);
    } else {
        // neighbor 在前（前驱），index 是 node 在 parent 的位置
        // 把 neighbor 最后一个 (key, rid) 移到 node 开头
        int neighbor_size = neighbor_node->get_size();
        node->insert_pair(0, neighbor_node->get_key(neighbor_size - 1),
                          *neighbor_node->get_rid(neighbor_size - 1));
        if (!node->is_leaf_page()) {
            maintain_child(node, 0);
        }
        neighbor_node->erase_pair(neighbor_size - 1);
        // node 首 key 变化，更新 parent 中 node 对应的 key
        memcpy(parent->get_key(index), node->get_key(0), file_hdr_->col_tot_len_);
    }
}

/**
 * @brief 合并(Coalesce)函数是将node和其直接前驱进行合并，也就是和它左边的neighbor_node进行合并；
 * 假设node一定在右边。如果上层传入的index=0，说明node在左边，那么交换node和neighbor_node，保证node在右边；合并到左结点，实际上就是删除了右结点；
 * Move all the key & value pairs from one page to its sibling page, and notify buffer pool manager to delete this page.
 * Parent page must be adjusted to take info of deletion into account. Remember to deal with coalesce or redistribute
 * recursively if necessary.
 *
 * @param neighbor_node sibling page of input "node" (neighbor_node是node的前结点)
 * @param node input from method coalesceOrRedistribute() (node结点是需要被删除的)
 * @param parent parent page of input "node"
 * @param index node在parent中的rid_idx
 * @return true means parent node should be deleted, false means no deletion happend
 * @note Assume that *neighbor_node is the left sibling of *node (neighbor -> node)
 */
bool IxIndexHandle::coalesce(IxNodeHandle **neighbor_node, IxNodeHandle **node, IxNodeHandle **parent, int index,
                             Transaction *transaction, bool *root_is_latched) {
    // 1. 保证 *neighbor_node 是 LEFT，*node 是 RIGHT
    if (index == 0) {
        std::swap(*neighbor_node, *node);
        index = 1;
    }

    IxNodeHandle *left = *neighbor_node;
    IxNodeHandle *right = *node;
    int left_size = left->get_size();
    int right_size = right->get_size();

    // 2. 把 right 的所有 (key, rid) 追加到 left 末尾
    if (right_size > 0) {
        left->insert_pairs(left_size, right->get_key(0), right->get_rid(0), right_size);
    }

    // 3. 内部节点：刚迁过来的子节点 parent 需要更新
    if (!left->is_leaf_page()) {
        for (int i = 0; i < right_size; i++) {
            maintain_child(left, left_size + i);
        }
    }

    // 4. 叶子节点：修复双链
    if (right->is_leaf_page()) {
        page_id_t right_next = right->get_next_leaf();
        left->set_next_leaf(right_next);
        IxNodeHandle *next_node = fetch_node(right_next);
        next_node->set_prev_leaf(left->get_page_no());
        buffer_pool_manager_->unpin_page(next_node->get_page_id(), true);
        delete next_node;

        if (right->get_page_no() == file_hdr_->last_leaf_) {
            file_hdr_->last_leaf_ = left->get_page_no();
        }
    }

    // 5. parent 移除 right 的 entry
    page_id_t right_page = right->get_page_no();
    (*parent)->erase_pair(index);

    // 6. 释放并删除 right 的页
    buffer_pool_manager_->unpin_page(right->get_page_id(), true);
    buffer_pool_manager_->delete_page(PageId{fd_, right_page});
    release_node_handle(*right);

    // 7. 递归检查 parent 是否需要调整
    return coalesce_or_redistribute(*parent, transaction, root_is_latched);
}

/**
 * @brief 这里把iid转换成了rid，即iid的slot_no作为node的rid_idx(key_idx)
 * node其实就是把slot_no作为键值对数组的下标
 * 换而言之，每个iid对应的索引槽存了一对(key,rid)，指向了(要建立索引的属性首地址,插入/删除记录的位置)
 *
 * @param iid
 * @return Rid
 * @note iid和rid存的不是一个东西，rid是上层传过来的记录位置，iid是索引内部生成的索引槽位置
 */
Rid IxIndexHandle::get_rid(const Iid &iid) const {
    IxNodeHandle *node = fetch_node(iid.page_no);
    if (iid.slot_no >= node->get_size()) {
        buffer_pool_manager_->unpin_page(node->get_page_id(), false);
        delete node;
        throw IndexEntryNotFoundError();
    }
    // 必须在 unpin 之前 copy 出 Rid，否则 unpin 后访问 page 是 UB
    Rid result = *node->get_rid(iid.slot_no);
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);
    delete node;  // 释放 IxNodeHandle，否则每次调用泄漏
    return result;
}

/**
 * @brief FindLeafPage + lower_bound
 *
 * @param key
 * @return Iid
 * @note 上层传入的key本来是int类型，通过(const char *)&key进行了转换
 * 可用*(int *)key转换回去
 */
Iid IxIndexHandle::lower_bound(const char *key) {
    std::scoped_lock<std::mutex> lock(root_latch_);
    auto [leaf, _] = find_leaf_page(key, Operation::FIND, nullptr);
    int slot = leaf->lower_bound(key);
    Iid iid;
    if (slot < leaf->get_size()) {
        iid = {leaf->get_page_no(), slot};
    } else if (leaf->get_page_no() == file_hdr_->last_leaf_) {
        // 已到全树末尾，等价于 leaf_end()
        iid = {leaf->get_page_no(), slot};
    } else {
        // 当前叶所有 key 都 < target，跳到下一叶首位
        iid = {leaf->get_next_leaf(), 0};
    }
    buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
    delete leaf;
    return iid;
}

/**
 * @brief FindLeafPage + upper_bound
 *
 * @param key
 * @return Iid
 */
Iid IxIndexHandle::upper_bound(const char *key) {
    std::scoped_lock<std::mutex> lock(root_latch_);
    auto [leaf, _] = find_leaf_page(key, Operation::FIND, nullptr);
    int slot = leaf->lower_bound(key);
    // 注意：IxNodeHandle::upper_bound 是为内部节点设计的（从 1 开始）
    // 这里要在叶子上找"严格 > target 的第一个"，所以用 lower_bound 后等值则跳一格
    if (slot < leaf->get_size() &&
        ix_compare(leaf->get_key(slot), key, file_hdr_->col_types_, file_hdr_->col_lens_) == 0) {
        slot++;
    }
    Iid iid;
    if (slot < leaf->get_size()) {
        iid = {leaf->get_page_no(), slot};
    } else if (leaf->get_page_no() == file_hdr_->last_leaf_) {
        iid = {leaf->get_page_no(), slot};
    } else {
        iid = {leaf->get_next_leaf(), 0};
    }
    buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
    delete leaf;
    return iid;
}

/**
 * @brief 指向最后一个叶子的最后一个结点的后一个
 * 用处在于可以作为IxScan的最后一个
 *
 * @return Iid
 */
Iid IxIndexHandle::leaf_end() const {
    IxNodeHandle *node = fetch_node(file_hdr_->last_leaf_);
    Iid iid = {.page_no = file_hdr_->last_leaf_, .slot_no = node->get_size()};
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);  // unpin it!
    return iid;
}

/**
 * @brief 指向第一个叶子的第一个结点
 * 用处在于可以作为IxScan的第一个
 *
 * @return Iid
 */
Iid IxIndexHandle::leaf_begin() const {
    Iid iid = {.page_no = file_hdr_->first_leaf_, .slot_no = 0};
    return iid;
}

/**
 * @brief 获取一个指定结点
 *
 * @param page_no
 * @return IxNodeHandle*
 * @note pin the page, remember to unpin it outside!
 */
IxNodeHandle *IxIndexHandle::fetch_node(int page_no) const {
    Page *page = buffer_pool_manager_->fetch_page(PageId{fd_, page_no});
    IxNodeHandle *node = new IxNodeHandle(file_hdr_, page);
    
    return node;
}

/**
 * @brief 创建一个新结点
 *
 * @return IxNodeHandle*
 * @note pin the page, remember to unpin it outside!
 * 注意：对于Index的处理是，删除某个页面后，认为该被删除的页面是free_page
 * 而first_free_page实际上就是最新被删除的页面，初始为IX_NO_PAGE
 * 在最开始插入时，一直是create node，那么first_page_no一直没变，一直是IX_NO_PAGE
 * 与Record的处理不同，Record将未插入满的记录页认为是free_page
 */
IxNodeHandle *IxIndexHandle::create_node() {
    IxNodeHandle *node;
    file_hdr_->num_pages_++;

    PageId new_page_id = {.fd = fd_, .page_no = INVALID_PAGE_ID};
    // 从3开始分配page_no，第一次分配之后，new_page_id.page_no=3，file_hdr_.num_pages=4
    Page *page = buffer_pool_manager_->new_page(&new_page_id);
    node = new IxNodeHandle(file_hdr_, page);
    return node;
}

/**
 * @brief 从node开始更新其父节点的第一个key，一直向上更新直到根节点
 *
 * @param node
 */
void IxIndexHandle::maintain_parent(IxNodeHandle *node) {
    IxNodeHandle *curr = node;
    while (curr->get_parent_page_no() != IX_NO_PAGE) {
        // Load its parent
        IxNodeHandle *parent = fetch_node(curr->get_parent_page_no());
        int rank = parent->find_child(curr);
        char *parent_key = parent->get_key(rank);
        char *child_first_key = curr->get_key(0);
        if (memcmp(parent_key, child_first_key, file_hdr_->col_tot_len_) == 0) {
            bool ok1 = buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
            assert(ok1);
            break;
        }
        memcpy(parent_key, child_first_key, file_hdr_->col_tot_len_);  // 修改了parent node
        curr = parent;

        bool ok2 = buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        assert(ok2);
    }
}

/**
 * @brief 要删除leaf之前调用此函数，更新leaf前驱结点的next指针和后继结点的prev指针
 *
 * @param leaf 要删除的leaf
 */
void IxIndexHandle::erase_leaf(IxNodeHandle *leaf) {
    assert(leaf->is_leaf_page());

    IxNodeHandle *prev = fetch_node(leaf->get_prev_leaf());
    prev->set_next_leaf(leaf->get_next_leaf());
    buffer_pool_manager_->unpin_page(prev->get_page_id(), true);

    IxNodeHandle *next = fetch_node(leaf->get_next_leaf());
    next->set_prev_leaf(leaf->get_prev_leaf());  // 注意此处是SetPrevLeaf()
    buffer_pool_manager_->unpin_page(next->get_page_id(), true);
}

/**
 * @brief 删除node时，更新file_hdr_.num_pages
 *
 * @param node
 */
void IxIndexHandle::release_node_handle(IxNodeHandle &node) {
    file_hdr_->num_pages_--;
}

/**
 * @brief 将node的第child_idx个孩子结点的父节点置为node
 */
void IxIndexHandle::maintain_child(IxNodeHandle *node, int child_idx) {
    if (!node->is_leaf_page()) {
        //  Current node is inner node, load its child and set its parent to current node
        int child_page_no = node->value_at(child_idx);
        IxNodeHandle *child = fetch_node(child_page_no);
        child->set_parent_page_no(node->get_page_no());
        buffer_pool_manager_->unpin_page(child->get_page_id(), true);
    }
}