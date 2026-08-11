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

#include <unistd.h>
#include <algorithm>
#include <map>
#include <set>

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
    
    // 分配计数器必须从【文件真实页数】起步（与 RmFileHandle 同约定）。
    // 旧实现是 get()+1：计数器初值 0 时被设成 1——凡本次打开不经 bulk_load
    // （bulk 末尾会纠正为 num_pages_，如检查点快速恢复路径），运行期首次分裂
    // create_node→allocate_page 就发出页 1（叶链哨兵）/页 2、3（现存叶）当"新页"，
    // 现存页被清空改装成分裂新叶 → 双亲引用、叶链孤儿段（树可达/链不可达，宽扫描
    // 系统性漏行）、幽灵项、prev/next 盖章式扩散。2026-07-31 paranoid 日志实锤：
    // `[ix-op] split node=882 new=2`。头页 num_pages_ 可能因崩溃未刷而陈旧，
    // 与磁盘实际文件大小取 max 兜底；计数器只上调不下调。
    int pages_on_disk = (int)(disk_manager_->get_file_size(disk_manager_->get_file_name(fd)) / PAGE_SIZE);
    int start = std::max(file_hdr_->num_pages_, pages_on_disk);
    if (disk_manager_->get_fd2pageno(fd) < start) {
        disk_manager_->set_fd2pageno(fd, start);
    }
}

IxIndexHandle::~IxIndexHandle() {
    release_pinned_leaf();
}

// 放掉顺序插入长期攥着的那个叶页 pin（如果有）。close_index 在此之前已经
// delete_all_pages 强制清空过这个 fd 的所有页，此时 unpin_page 找不到页会直接
// 返回 false，是安全的空操作。
void IxIndexHandle::release_pinned_leaf() {
    if (pinned_leaf_page_ != nullptr) {
        buffer_pool_manager_->unpin_page(pinned_leaf_page_->get_page_id(), true);
        pinned_leaf_page_ = nullptr;
        pinned_leaf_no_ = IX_NO_PAGE;
    }
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
    std::shared_lock<FairSharedMutex> lock(root_latch_);
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
    static const bool paranoid = std::getenv("RMDB_IX_PARANOID") != nullptr;
    if (paranoid) {
        fprintf(stderr, "[ix-op] split fd=%d node=%d new=%d node_parent=%d leaf=%d\n", fd_,
                node->get_page_no(), new_node->get_page_no(), node->get_parent_page_no(),
                (int)node->is_leaf_page());
    }
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
        if (old_next > IX_NO_PAGE) {
            IxNodeHandle *next_node = fetch_node(old_next);
            next_node->set_prev_leaf(new_node->get_page_no());
            buffer_pool_manager_->unpin_page(next_node->get_page_id(), true);
            delete next_node;
        }

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
    if (old_idx < 0) {
        // parent 指针陈旧/被踩且分裂已过半（new_node 已链入叶链）：不能中止，
        // 退而按 key 序找本 parent 内的插入位，至少保持该节点有序。misplaced
        // 分裂曾产生"链上可见、树下降不可达"的幽灵项（点查无、宽扫描有）。
        int pos = 0;
        while (pos < parent->get_size() &&
               ix_compare(parent->get_key(pos), key, file_hdr_->col_types_,
                          file_hdr_->col_lens_) <= 0)
            pos++;
        old_idx = pos - 1;
        fprintf(stderr, "[ix-guard] insert_into_parent: stale parent=%d for child=%d, "
                        "key-ordered fallback pos=%d\n",
                parent->get_page_no(), old_node->get_page_no(), pos);
    }
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
page_id_t IxIndexHandle::insert_entry(const char *key, const Rid &value, Transaction *transaction,
                                      bool *inserted) {
    std::unique_lock<FairSharedMutex> lock(root_latch_);
    if (inserted != nullptr) *inserted = false;

    // 顺序追加快路径：缓存的叶仍是最右叶且 key 不小于其首 key 时，落点必在此叶。
    // 若上次那页的 pin 还攥着（pinned_leaf_no_ 命中），直接用，连 fetch_node 都省了；
    // 否则退化成"只查一次页"（跳过从根逐层遍历），仍比整树查找快。
    IxNodeHandle *leaf = nullptr;
    bool leaf_pin_owned_by_cache = false;   // leaf 用的是 pinned_leaf_page_ 那份 pin，末尾不能常规 unpin
    if (cached_leaf_no_ != IX_NO_PAGE && cached_leaf_no_ == file_hdr_->last_leaf_) {
        if (pinned_leaf_no_ == cached_leaf_no_ && pinned_leaf_page_ != nullptr) {
            IxNodeHandle *c = new IxNodeHandle(file_hdr_, pinned_leaf_page_);
            if (c->is_leaf_page() && c->get_size() > 0 &&
                ix_compare(key, c->get_key(0), file_hdr_->col_types_, file_hdr_->col_lens_) >= 0) {
                leaf = c;
                leaf_pin_owned_by_cache = true;
            } else {
                delete c;   // 不 unpin：这页仍是 pinned_leaf_page_，pin 继续留着
            }
        } else {
            IxNodeHandle *c = fetch_node(cached_leaf_no_);
            if (c->is_leaf_page() && c->get_size() > 0 &&
                ix_compare(key, c->get_key(0), file_hdr_->col_types_, file_hdr_->col_lens_) >= 0) {
                leaf = c;
            } else {
                buffer_pool_manager_->unpin_page(c->get_page_id(), false);
                delete c;
            }
        }
    }
    if (leaf == nullptr) {
        leaf = find_leaf_page(key, Operation::INSERT, transaction).first;
    }
    int old_size = leaf->get_size();
    int new_size = leaf->insert(key, value);
    page_id_t leaf_page = leaf->get_page_no();

    if (new_size == old_size) {
        // 重复 key，未插入（唯一索引语义）；没碰这页，是长期 pin 就什么都不用做
        if (!leaf_pin_owned_by_cache) {
            buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
        }
        delete leaf;
        return leaf_page;
    }
    if (inserted != nullptr) *inserted = true;

    // 仅当插入成为叶子首 key 时才需向上传播分隔键；追加插入首 key 未变，省去父节点访问
    if (memcmp(leaf->get_key(0), key, file_hdr_->col_tot_len_) == 0) {
        maintain_parent(leaf);
    }

    // 满了则分裂并把新节点上插
    if (new_size >= leaf->get_max_size()) {
        // 分裂改变了旧叶结构，不能再长期攥着它的 pin：先清掉记录，底下的
        // unpin_page(leaf, ...) 会把这份 pin 正常还回去，不会重复 unpin。
        if (leaf_pin_owned_by_cache) {
            pinned_leaf_no_ = IX_NO_PAGE;
            pinned_leaf_page_ = nullptr;
        }
        IxNodeHandle *new_leaf = split(leaf);
        insert_into_parent(leaf, new_leaf->get_key(0), new_leaf, transaction);
        buffer_pool_manager_->unpin_page(new_leaf->get_page_id(), true);
        delete new_leaf;
        cached_leaf_no_ = file_hdr_->last_leaf_;   // 分裂改变了最右叶，缓存指向新的最右叶

        buffer_pool_manager_->unpin_page(leaf->get_page_id(), true);
        delete leaf;
    } else {
        cached_leaf_no_ = leaf_page;               // 记住本次落点叶，供下次顺序插入复用
        if (leaf_pin_owned_by_cache) {
            delete leaf;    // 同一份 pin 继续留着，只删这次用的包装对象
        } else {
            // 换到了新叶：先放掉旧的长期 pin，再把这次 fetch 到的 pin 转交出去（不 unpin）
            release_pinned_leaf();
            pinned_leaf_no_ = leaf_page;
            pinned_leaf_page_ = leaf->page;
            delete leaf;
        }
    }
    static const bool paranoid = std::getenv("RMDB_IX_PARANOID") != nullptr;
    if (paranoid) paranoid_verify(leaf_page, "insert");
    return leaf_page;
}

/**
 * @brief 用于删除B+树中含有指定key的键值对
 * @param key 要删除的key值
 * @param transaction 事务指针
 */
bool IxIndexHandle::delete_entry(const char *key, Transaction *transaction) {
    std::unique_lock<FairSharedMutex> lock(root_latch_);
    cached_leaf_no_ = IX_NO_PAGE;   // 删除可能合并/重分配改变叶结构，作废顺序插入缓存
    release_pinned_leaf();         // 同时放掉顺序插入长期攥着的 pin，避免和 coalesce/redistribute 冲突

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

    page_id_t leaf_page_for_verify = leaf->get_page_no();
    bool node_deleted = coalesce_or_redistribute(leaf, transaction);

    if (node_deleted) {
        // leaf 的页已被 coalesce 删除，只需释放 IxNodeHandle
        delete leaf;
    } else {
        buffer_pool_manager_->unpin_page(leaf->get_page_id(), true);
        delete leaf;
    }
    static const bool paranoid = std::getenv("RMDB_IX_PARANOID") != nullptr;
    if (paranoid && !node_deleted) paranoid_verify(leaf_page_for_verify, "delete");
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
    if (idx < 0) {
        // parent 指针陈旧/被踩：拒绝本次调整（欠填叶合法），绝不能拿垃圾 idx 选兄弟
        buffer_pool_manager_->unpin_page(parent->get_page_id(), false);
        delete parent;
        return false;
    }
    int sibling_idx = (idx == 0) ? 1 : idx - 1;
    if (sibling_idx >= parent->get_size()) {   // 单孩子 parent（瞬态/受损）：无兄弟可调
        buffer_pool_manager_->unpin_page(parent->get_page_id(), false);
        delete parent;
        return false;
    }
    IxNodeHandle *sibling = fetch_node(parent->value_at(sibling_idx));

    // 叶合并/重分配前的相邻性验证：tree 序上的兄弟必须同时是叶链上的直接邻居。
    // 不满足即结构已分叉（find_child 陈旧、页头被踩等），此时任何缝合都会把
    // 中间段摘出链——宁可留欠填叶也不动链。
    if (node->is_leaf_page()) {
        IxNodeHandle *l = (idx == 0) ? node : sibling;
        IxNodeHandle *r = (idx == 0) ? sibling : node;
        if (l->get_next_leaf() != r->get_page_no() ||
            r->get_prev_leaf() != l->get_page_no()) {
            fprintf(stderr,
                    "[ix-guard] leaf adjacency violation: left=%d(next=%d) right=%d(prev=%d), "
                    "refuse coalesce/redistribute\n",
                    l->get_page_no(), l->get_next_leaf(), r->get_page_no(), r->get_prev_leaf());
            buffer_pool_manager_->unpin_page(sibling->get_page_id(), false);
            delete sibling;
            buffer_pool_manager_->unpin_page(parent->get_page_id(), false);
            delete parent;
            return false;
        }
    }

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
    static const bool paranoid = std::getenv("RMDB_IX_PARANOID") != nullptr;
    if (paranoid) {
        fprintf(stderr, "[ix-op] redis fd=%d node=%d nb=%d parent=%d idx=%d leaf=%d\n", fd_,
                node->get_page_no(), neighbor_node->get_page_no(), parent->get_page_no(), index,
                (int)node->is_leaf_page());
    }
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
    static const bool paranoid = std::getenv("RMDB_IX_PARANOID") != nullptr;
    if (paranoid) {
        fprintf(stderr, "[ix-op] coalesce fd=%d left=%d right=%d parent=%d idx=%d leaf=%d\n", fd_,
                left->get_page_no(), right->get_page_no(), (*parent)->get_page_no(), index,
                (int)left->is_leaf_page());
    }

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
    std::shared_lock<FairSharedMutex> lock(root_latch_);  // 与写路径共享，分裂未完成前写方持独占锁
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
    std::shared_lock<FairSharedMutex> lock(root_latch_);
    return lower_bound_nolock(key);
}

/* 调用方须已持 root_latch_（shared 或 unique）——IxScan key 锚定模式在自己的
 * shared 锁内重定位用（std::shared_mutex 同线程重复加锁是 UB，不能复用公开版本） */
Iid IxIndexHandle::lower_bound_nolock(const char *key) const {
    auto [leaf, _] = const_cast<IxIndexHandle *>(this)->find_leaf_page(key, Operation::FIND, nullptr);
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
    std::shared_lock<FairSharedMutex> lock(root_latch_);
    return upper_bound_nolock(key);
}

/* 调用方须已持 root_latch_（同 lower_bound_nolock） */
Iid IxIndexHandle::upper_bound_nolock(const char *key) const {
    auto [leaf, _] = const_cast<IxIndexHandle *>(this)->find_leaf_page(key, Operation::FIND, nullptr);
    int slot = leaf->lower_bound(key);
    // 必须跳过【全部】等值项并且允许跨叶：同 key 多项是常态（删后重插同 key 的
    // 墓碑项未 drain、MVCC UPDATE 版本化项）。此前只跳一格，upper_bound(K) 会停在
    // 下一个等值项上（key 仍 == K）——key 锚定扫描以"返回 key 严格大于 anchor"为
    // 进度保证，等值返回使 anchor 永不前进，扫描原地死循环（TPC-C MIN 实测卡死
    // 20 分钟、CPU 100%，锚点被甩出范围后连带在全索引死项间乱撞）。
    while (true) {
        if (slot < leaf->get_size()) {
            if (ix_compare(leaf->get_key(slot), key,
                           file_hdr_->col_types_, file_hdr_->col_lens_) == 0) {
                slot++;
                continue;
            }
            break;                                            // 严格 > key
        }
        if (leaf->get_page_no() == file_hdr_->last_leaf_) break;   // 树尾（等价 leaf_end）
        page_id_t nxt = leaf->get_next_leaf();
        buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
        delete leaf;
        leaf = const_cast<IxIndexHandle *>(this)->fetch_node(nxt);
        slot = 0;
    }
    Iid iid = {leaf->get_page_no(), slot};
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
    std::shared_lock<FairSharedMutex> lock(root_latch_);  // last_leaf_ 随分裂变，与写方共享读
    IxNodeHandle *node = fetch_node(file_hdr_->last_leaf_);
    Iid iid = {.page_no = file_hdr_->last_leaf_, .slot_no = node->get_size()};
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);  // unpin it!
    delete node;  // 修复内存泄漏：fetch_node 返回 new IxNodeHandle
    return iid;
}

/**
 * @brief 指向第一个叶子的第一个结点
 * 用处在于可以作为IxScan的第一个
 *
 * @return Iid
 */
Iid IxIndexHandle::leaf_begin() const {
    std::shared_lock<FairSharedMutex> lock(root_latch_);  // first_leaf_ 与写方共享读
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
    if (page_no <= IX_NO_PAGE || page_no >= file_hdr_->num_pages_) {
        throw InternalError("IxIndexHandle::fetch_node: invalid page " + std::to_string(page_no));
    }
    Page *page = buffer_pool_manager_->fetch_page(PageId{fd_, page_no});
    if (page == nullptr) {
        // 瞬时帧耗尽（fetch_page ~2s 重试后放弃）：抛可重试压力异常，语句层转
        // TRANSACTION_ABORT；此前抛 InternalError 会被映射成 ERROR 终结直接判负
        throw BufferPoolPressureError("IxIndexHandle::fetch_node: buffer pool full for page " + std::to_string(page_no));
    }
    return new IxNodeHandle(file_hdr_, page);
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
    // 页号冲突纵深防线：分配计数器低于已知页数=即将把现存页当新页发出（构造函数
    // 已按文件大小初始化,此处兜住运行期任何路径把计数器改小的意外）
    if (disk_manager_->get_fd2pageno(fd_) < file_hdr_->num_pages_) {
        fprintf(stderr, "[ix-guard] fd=%d fd2pageno=%d < num_pages=%d, clamp (collision averted)\n",
                fd_, (int)disk_manager_->get_fd2pageno(fd_), file_hdr_->num_pages_);
        disk_manager_->set_fd2pageno(fd_, file_hdr_->num_pages_);
    }
    file_hdr_->num_pages_++;

    PageId new_page_id = {.fd = fd_, .page_no = INVALID_PAGE_ID};
    // 从3开始分配page_no，第一次分配之后，new_page_id.page_no=3，file_hdr_.num_pages=4
    Page *page = buffer_pool_manager_->new_page(&new_page_id);
    if (page == nullptr) {
        // 同 fetch_node：瞬时帧耗尽是可重试压力，不是逻辑错误
        throw BufferPoolPressureError("IxIndexHandle::create_node: buffer pool full");
    }
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
        if (rank < 0) {   // parent 指针陈旧/被踩：get_key(-1) 是越界写，立即止损
            buffer_pool_manager_->unpin_page(parent->get_page_id(), false);
            break;
        }
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

    page_id_t prev_no = leaf->get_prev_leaf();
    page_id_t next_no = leaf->get_next_leaf();
    if (prev_no > IX_NO_PAGE) {
        IxNodeHandle *prev = fetch_node(prev_no);
        prev->set_next_leaf(next_no);
        buffer_pool_manager_->unpin_page(prev->get_page_id(), true);
        delete prev;
    }
    if (next_no > IX_NO_PAGE) {
        IxNodeHandle *next = fetch_node(next_no);
        next->set_prev_leaf(prev_no);
        buffer_pool_manager_->unpin_page(next->get_page_id(), true);
        delete next;
    }
}

/**
 * @brief 删除node时，更新file_hdr_.num_pages
 *
 * @param node
 */
void IxIndexHandle::release_node_handle(IxNodeHandle &node) {
    // 不递减 num_pages_：DiskManager::deallocate_page 为空操作、allocate_page 单调自增(fd2pageno_)，
    // 页从不真正回收复用。若此处 num_pages_-- 会与分配计数漂移，导致后续 create_node 分配到的
    // 合法页号 >= num_pages_，被 fetch_node 的越界检查误判为 "invalid page" 而抛异常崩溃。
    // num_pages_ 必须保持单调，作为 fetch_node 的有效页上界。
    (void)node;
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
/* 批量装载（恢复期索引重建的快路径）：自底向上顺序构建整棵 B+ 树。
 * 布局：页 0 文件头、页 1 叶链哨兵、页 2 起按层连续排布（叶层在前，逐层向上，
 * 根页号最大）。所有页面镜像在内存拼好后经 disk_manager 直写（绕过 BPM），
 * 结束时 fdatasync。节点填充率 ~90%（≤ btree_order_，不触发后续首插分裂）。
 * 内部节点不变量与增量插入一致：keys[i] = 第 i 个子树的最小 key，
 * rids[i].page_no = 孩子页号（internal_lookup 用 upper_bound(key)-1 下降）。 */
void IxIndexHandle::bulk_load(long n, const std::function<void(char *key_out, Rid *rid_out)> &next) {
    std::unique_lock<FairSharedMutex> lock(root_latch_);
    release_pinned_leaf();
    cached_leaf_no_ = IX_NO_PAGE;
    // create/open 可能已在 BPM 缓存 0/1/2 页；直写前必须清空本 fd 的缓存页，
    // 否则后续 fetch 命中旧缓存读到构建前的空节点
    buffer_pool_manager_->flush_all_pages(fd_);
    buffer_pool_manager_->delete_all_pages(fd_);

    const int klen = file_hdr_->col_tot_len_;
    const int max_keys = file_hdr_->btree_order_ + 1;
    int fill = max_keys * 9 / 10;
    if (fill < 2) fill = 2;
    if (fill > max_keys - 1) fill = max_keys - 1;

    std::vector<char> pagebuf(PAGE_SIZE);
    char *kbase = pagebuf.data() + sizeof(IxPageHdr);
    Rid *rbase = reinterpret_cast<Rid *>(kbase + file_hdr_->keys_size_);

    long n_leaves = (n + fill - 1) / fill;
    if (n_leaves < 1) n_leaves = 1;

    std::vector<long> level_cnt{n_leaves};              // [0]=叶层
    while (level_cnt.back() > 1) {
        level_cnt.push_back((level_cnt.back() + fill - 1) / fill);
    }
    std::vector<long> level_start;
    long next_id = IX_INIT_ROOT_PAGE;
    for (long c : level_cnt) {
        level_start.push_back(next_id);
        next_id += c;
    }
    const long total_pages = next_id;
    const page_id_t root_pno = (page_id_t)(next_id - 1);

    auto parent_of = [&](size_t level, long idx) -> page_id_t {
        if (level + 1 >= level_cnt.size()) return IX_NO_PAGE;   // 根
        return (page_id_t)(level_start[level + 1] + idx / fill);
    };

    // 叶层：流式消费排序条目，同时收集每叶最小 key 供上层构建
    std::vector<char> minkeys((size_t)n_leaves * klen);
    long produced = 0;
    for (long li = 0; li < n_leaves; li++) {
        memset(pagebuf.data(), 0, PAGE_SIZE);
        IxPageHdr *ph = reinterpret_cast<IxPageHdr *>(pagebuf.data());
        page_id_t me = (page_id_t)(level_start[0] + li);
        long cnt = n - produced;
        if (cnt > fill) cnt = fill;
        if (cnt < 0) cnt = 0;
        ph->next_free_page_no = IX_NO_PAGE;
        ph->parent = parent_of(0, li);
        ph->num_key = (int)cnt;
        ph->is_leaf = true;
        ph->prev_leaf = (li == 0) ? IX_LEAF_HEADER_PAGE : me - 1;
        ph->next_leaf = (li == n_leaves - 1) ? IX_LEAF_HEADER_PAGE : me + 1;
        for (long i = 0; i < cnt; i++) {
            next(kbase + i * klen, rbase + i);
        }
        produced += cnt;
        if (cnt > 0) memcpy(minkeys.data() + (size_t)li * klen, kbase, klen);
        disk_manager_->write_page(fd_, me, pagebuf.data(), PAGE_SIZE);
    }

    // 内部层
    for (size_t lv = 1; lv < level_cnt.size(); lv++) {
        long n_children = level_cnt[lv - 1];
        std::vector<char> next_min((size_t)level_cnt[lv] * klen);
        for (long ni = 0; ni < level_cnt[lv]; ni++) {
            memset(pagebuf.data(), 0, PAGE_SIZE);
            IxPageHdr *ph = reinterpret_cast<IxPageHdr *>(pagebuf.data());
            long c0 = ni * fill;
            long cn = n_children - c0;
            if (cn > fill) cn = fill;
            ph->next_free_page_no = IX_NO_PAGE;
            ph->parent = parent_of(lv, ni);
            ph->num_key = (int)cn;
            ph->is_leaf = false;
            ph->prev_leaf = IX_NO_PAGE;
            ph->next_leaf = IX_NO_PAGE;
            for (long c = 0; c < cn; c++) {
                memcpy(kbase + c * klen, minkeys.data() + (size_t)(c0 + c) * klen, klen);
                rbase[c] = Rid{(int)(level_start[lv - 1] + c0 + c), 0};
            }
            memcpy(next_min.data() + (size_t)ni * klen, kbase, klen);
            disk_manager_->write_page(fd_, (page_id_t)(level_start[lv] + ni), pagebuf.data(), PAGE_SIZE);
        }
        minkeys.swap(next_min);
    }

    // 叶链哨兵（页 1）：环通过它闭合
    memset(pagebuf.data(), 0, PAGE_SIZE);
    {
        IxPageHdr *ph = reinterpret_cast<IxPageHdr *>(pagebuf.data());
        ph->next_free_page_no = IX_NO_PAGE;
        ph->parent = IX_NO_PAGE;
        ph->num_key = 0;
        ph->is_leaf = true;
        ph->prev_leaf = (page_id_t)(level_start[0] + n_leaves - 1);
        ph->next_leaf = (page_id_t)level_start[0];
    }
    disk_manager_->write_page(fd_, IX_LEAF_HEADER_PAGE, pagebuf.data(), PAGE_SIZE);

    // 文件头 + 分配计数（open 后约定 fd2pageno == num_pages_）
    file_hdr_->first_free_page_no_ = IX_NO_PAGE;
    file_hdr_->num_pages_ = (int)total_pages;
    file_hdr_->root_page_ = root_pno;
    file_hdr_->first_leaf_ = (page_id_t)level_start[0];
    file_hdr_->last_leaf_ = (page_id_t)(level_start[0] + n_leaves - 1);
    memset(pagebuf.data(), 0, PAGE_SIZE);
    file_hdr_->serialize(pagebuf.data());
    disk_manager_->write_page(fd_, IX_FILE_HDR_PAGE, pagebuf.data(), file_hdr_->tot_len_);
    disk_manager_->set_fd2pageno(fd_, file_hdr_->num_pages_);
    fdatasync(fd_);
}

void IxIndexHandle::debug_chain_walk(FILE *out) {
    std::shared_lock<FairSharedMutex> lock(root_latch_);
    fprintf(out, "[chainwalk] fd=%d first_leaf=%d last_leaf=%d num_pages=%d\n", fd_,
            file_hdr_->first_leaf_, file_hdr_->last_leaf_, file_hdr_->num_pages_);
    std::map<std::pair<int, int>, long> pref_cnt;
    std::vector<char> prev_last;
    int pg = file_hdr_->first_leaf_;
    long leaves = 0, entries = 0, violations = 0;
    while (pg != IX_LEAF_HEADER_PAGE && pg > IX_NO_PAGE && pg < file_hdr_->num_pages_ &&
           leaves <= (long)file_hdr_->num_pages_) {
        IxNodeHandle *n = fetch_node(pg);
        if (!n->is_leaf_page()) {
            fprintf(out, "[chainwalk] leaf=%d NOT-LEAF (chain corrupt)\n", pg);
            buffer_pool_manager_->unpin_page(n->get_page_id(), false);
            delete n;
            break;
        }
        int sz = n->get_size();
        for (int i = 0; i < sz; i++) {
            const char *k = n->get_key(i);
            if (file_hdr_->col_tot_len_ >= 8) {
                int a, b;
                memcpy(&a, k, 4);
                memcpy(&b, k + 4, 4);
                pref_cnt[{a, b}]++;
            }
        }
        if (sz > 0 && !prev_last.empty() &&
            ix_compare(n->get_key(0), prev_last.data(), file_hdr_->col_types_,
                       file_hdr_->col_lens_) < 0) {
            violations++;
            fprintf(out, "[chainwalk] ORDER-VIOLATION at leaf=%d (first key < prev leaf last)\n", pg);
        }
        if (sz > 0) prev_last.assign(n->get_key(sz - 1), n->get_key(sz - 1) + file_hdr_->col_tot_len_);
        leaves++;
        entries += sz;
        int next = n->get_next_leaf();
        buffer_pool_manager_->unpin_page(n->get_page_id(), false);
        delete n;
        pg = next;
    }
    fprintf(out, "[chainwalk] leaves=%ld entries=%ld violations=%ld end_pg=%d (expect %d)\n",
            leaves, entries, violations, pg, IX_LEAF_HEADER_PAGE);
    for (auto &kv : pref_cnt) {
        fprintf(out, "[chainwalk] pref=(%d,%d) n=%ld\n", kv.first.first, kv.first.second,
                kv.second);
    }
    fflush(out);
}

void IxIndexHandle::debug_tree_walk(FILE *out) {
    std::shared_lock<FairSharedMutex> lock(root_latch_);
    // 先收集链上叶集合
    std::set<int> on_chain;
    {
        int pg = file_hdr_->first_leaf_;
        long hops = 0;
        while (pg != IX_LEAF_HEADER_PAGE && pg > IX_NO_PAGE && pg < file_hdr_->num_pages_ &&
               hops++ <= (long)file_hdr_->num_pages_) {
            on_chain.insert(pg);
            IxNodeHandle *n = fetch_node(pg);
            int next = n->is_leaf_page() ? n->get_next_leaf() : IX_NO_PAGE;
            buffer_pool_manager_->unpin_page(n->get_page_id(), false);
            delete n;
            pg = next;
        }
    }
    // 树 DFS（显式栈）
    fprintf(out, "[treewalk] root=%d chain_leaves=%zu\n", file_hdr_->root_page_, on_chain.size());
    std::vector<int> stack{file_hdr_->root_page_};
    long tree_leaves = 0, orphans = 0, tree_entries = 0;
    int prev_leaf_seen = -1;
    while (!stack.empty()) {
        int pg = stack.back();
        stack.pop_back();
        if (pg <= IX_NO_PAGE || pg >= file_hdr_->num_pages_) continue;
        IxNodeHandle *n = fetch_node(pg);
        if (n->is_leaf_page()) {
            tree_leaves++;
            int sz = n->get_size();
            tree_entries += sz;
            bool orphan = on_chain.find(pg) == on_chain.end();
            if (orphan) orphans++;
            auto lastint = [&](int i) {
                int v;
                memcpy(&v, n->get_key(i) + file_hdr_->col_tot_len_ - 4, 4);
                return v;
            };
            auto prevint = [&](int i) { int v; memcpy(&v, n->get_key(i), 4); return v; };
            auto dint = [&](int i) { int v; memcpy(&v, n->get_key(i) + 4, 4); return v; };
            if (orphan) {
                fprintf(out,
                        "[treewalk] ORPHAN leaf=%d size=%d prev=%d next=%d "
                        "first=(%d,%d,%d) last=(%d,%d,%d) after_treeleaf=%d\n",
                        pg, sz, n->get_prev_leaf(), n->get_next_leaf(),
                        sz ? prevint(0) : -1, sz ? dint(0) : -1, sz ? lastint(0) : -1,
                        sz ? prevint(sz - 1) : -1, sz ? dint(sz - 1) : -1,
                        sz ? lastint(sz - 1) : -1, prev_leaf_seen);
            }
            prev_leaf_seen = pg;
        } else {
            // 逆序压栈保持树序
            for (int i = n->get_size() - 1; i >= 0; i--) stack.push_back(n->value_at(i));
        }
        buffer_pool_manager_->unpin_page(n->get_page_id(), false);
        delete n;
    }
    fprintf(out, "[treewalk] tree_leaves=%ld orphans=%ld tree_entries=%ld (chain had %zu)\n",
            tree_leaves, orphans, tree_entries, on_chain.size());
    fflush(out);
}

void IxIndexHandle::paranoid_verify(page_id_t leaf_page, const char *op) {
    // 调用方须持 root_latch_（结构写者排它下调用，树处于静止一致点）
    if (leaf_page <= IX_NO_PAGE || leaf_page >= file_hdr_->num_pages_) return;
    IxNodeHandle *cur = fetch_node(leaf_page);
    // 叶邻接一致性
    if (cur->is_leaf_page()) {
        page_id_t nx = cur->get_next_leaf();
        if (nx > IX_NO_PAGE && nx != IX_LEAF_HEADER_PAGE && nx < file_hdr_->num_pages_) {
            IxNodeHandle *n = fetch_node(nx);
            if (n->get_prev_leaf() != leaf_page) {
                fprintf(stderr, "[ix-paranoid] after %s: leaf=%d next=%d but next.prev=%d\n",
                        op, leaf_page, nx, n->get_prev_leaf());
            }
            buffer_pool_manager_->unpin_page(n->get_page_id(), false);
            delete n;
        }
    }
    // 父链完整性
    int depth = 0;
    while (cur->get_parent_page_no() != IX_NO_PAGE && depth++ < 12) {
        page_id_t pp = cur->get_parent_page_no();
        if (pp <= IX_NO_PAGE || pp >= file_hdr_->num_pages_) {
            fprintf(stderr, "[ix-paranoid] after %s: node=%d parent=%d OUT-OF-RANGE\n", op,
                    cur->get_page_no(), pp);
            break;
        }
        IxNodeHandle *par = fetch_node(pp);
        bool found = false;
        for (int i = 0; i < par->get_size(); i++) {
            if (par->value_at(i) == cur->get_page_no()) { found = true; break; }
        }
        if (!found) {
            fprintf(stderr, "[ix-paranoid] after %s: node=%d NOT-IN parent=%d (size=%d)\n", op,
                    cur->get_page_no(), pp, par->get_size());
            buffer_pool_manager_->unpin_page(par->get_page_id(), false);
            delete par;
            break;
        }
        buffer_pool_manager_->unpin_page(cur->get_page_id(), false);
        delete cur;
        cur = par;
    }
    buffer_pool_manager_->unpin_page(cur->get_page_id(), false);
    delete cur;
}
