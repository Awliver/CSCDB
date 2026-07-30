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

#include <pthread.h>

/* 写者优先的读写锁。glibc 的 std::shared_mutex 默认读者优先：只要持续有新读者
 * 进入，等待的写者可以被无限期饿死。key 锚定索引扫描按行反复取 root_latch_
 * 共享锁，读流量常驻，insert_entry/delete_entry 的独占锁在饱和负载下等待秒级
 * （W=10×64 线程实测单行 INSERT 2~9s，超过评测语句预算）。
 * PREFER_WRITER_NONRECURSIVE_NP：有写者排队时新读者入队等待，写者优先获锁。
 * 约束：持共享锁的线程不得再次共享加锁同一把锁（写者夹在中间即自死锁）——
 * root_latch_ 的全部加锁点已审计为非递归（nolock 变体只由已持锁路径调用）。
 * 接口满足 SharedLockable，可与 std::shared_lock/std::unique_lock 配合。 */
class FairSharedMutex {
   public:
    FairSharedMutex() {
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
        pthread_rwlock_init(&rw_, &attr);
        pthread_rwlockattr_destroy(&attr);
    }
    ~FairSharedMutex() { pthread_rwlock_destroy(&rw_); }
    FairSharedMutex(const FairSharedMutex &) = delete;
    FairSharedMutex &operator=(const FairSharedMutex &) = delete;

    void lock() { pthread_rwlock_wrlock(&rw_); }
    bool try_lock() { return pthread_rwlock_trywrlock(&rw_) == 0; }
    void unlock() { pthread_rwlock_unlock(&rw_); }

    void lock_shared() { pthread_rwlock_rdlock(&rw_); }
    bool try_lock_shared() { return pthread_rwlock_tryrdlock(&rw_) == 0; }
    void unlock_shared() { pthread_rwlock_unlock(&rw_); }

   private:
    pthread_rwlock_t rw_;
};
