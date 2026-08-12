/* REPRO-TRACE: 零扰动并发事件环（临时诊断设施，问题定位后整体移除）。
 * 关键点：不得引入任何跨线程共享写（共享原子计数会串行化竞争路径、掩盖竞态，
 * canary 实测 12/12 假阴性）——事件写入线程本地环，时间戳用 steady_clock（vDSO，
 * 无共享状态），倾倒时汇总各线程环按时间排序。RMDB_RING=1 启用。 */
#pragma once
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

struct ReproRing {
    enum EvType : uint8_t {
        ACCEPT = 0,     // index scan 接受行: a=项key末int b=负载int c=(page<<16|slot) d=flags
        FASTPATH = 1,   // index scan 非mvcc快路径接受
        TOMBSTONE = 2,  // commit 物化墓碑: a=oid(链无从取,填-1) b=cts c=rid d=wm
        DRAINCLEAN = 3, // drain 物化延迟删除: b=cts c=rid
        IXINS = 4,      // insert_entry(new_orders): a=oid b=d c=rid
        IXDEL = 5,      // delete_entry(new_orders): a=oid b=d
        HEAPINS = 6,    // insert_record(new_orders 堆): c=rid
        AGGOUT = 7,     // AggExecutor MIN 结果发出: a=int值 b=扫描行数
        SKIPVIS = 8,    // index scan 跳过行: a=项key末int b=原因(1=mvcc不可见 2=from_heap复查
                        //   3=key不一致 4=槽死 5=eval不匹配 6=前缀越界停) c=rid
        DRAINDROP = 9,  // drain 弃单: a=原因(1=链不在 2=状态不符 3=key_src空 4=is_record假) b=cts c=rid
        PRUNEERASE = 10,// prune 摘除墓碑结尾链（不应发生的金丝雀）: b=cts c=rid d=wm
        CHAINGONE = 11, // 其余摘链点: a=来源(1=sweep 2=干净链回收 3=drain成功) b=尾版本cts(负=墓碑) c=rid
    };
    struct Ev {
        uint64_t ts;
        int32_t a, b, c, d;
        EvType type;
    };
    static constexpr uint32_t N = 1 << 14;
    struct TR {
        Ev buf[N];
        uint32_t idx = 0;          // 仅本线程写
        uint64_t tid = 0;
    };
    static inline std::mutex reg_mtx;
    static inline std::vector<TR *> regs;
    static bool on() {
        static bool v = getenv("RMDB_RING") != nullptr;
        return v;
    }
    static TR *mine() {
        static thread_local TR *tr = [] {
            TR *t = new TR();
            t->tid = (uint64_t)syscall(SYS_gettid);
            std::scoped_lock<std::mutex> l(reg_mtx);
            regs.push_back(t);
            return t;
        }();
        return tr;
    }
    static void push(EvType ty, int32_t a, int32_t b, int32_t c, int32_t d) {
        TR *t = mine();
        Ev &e = t->buf[t->idx & (N - 1)];
        e.ts = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        e.type = ty; e.a = a; e.b = b; e.c = c; e.d = d;
        t->idx++;
    }
    static int32_t rid32(int page, int slot) { return (int32_t)((page << 16) | (slot & 0xFFFF)); }
    /* 汇总全部线程环、按 ts 排序、打印最近 last_n 条 */
    static void dump(FILE *f, uint32_t last_n) {
        static const char *names[] = {"ACCEPT", "FASTPATH", "TOMBSTONE", "DRAIN", "IXINS", "IXDEL", "HEAPINS", "AGGOUT", "SKIPVIS", "DRAINDROP", "PRUNEERASE", "CHAINGONE"};
        struct Row { uint64_t ts; uint16_t tid; Ev e; };
        std::vector<Row> all;
        {
            std::scoped_lock<std::mutex> l(reg_mtx);
            for (TR *t : regs) {
                uint32_t end = t->idx, cnt = end < N ? end : N;
                for (uint32_t i = end - cnt; i < end; i++) {
                    const Ev &e = t->buf[i & (N - 1)];
                    all.push_back(Row{e.ts, (uint16_t)t->tid, e});
                }
            }
        }
        std::sort(all.begin(), all.end(), [](const Row &x, const Row &y) { return x.ts < y.ts; });
        size_t start = all.size() > last_n ? all.size() - last_n : 0;
        uint64_t base = all.empty() ? 0 : all[start].ts;
        for (size_t i = start; i < all.size(); i++) {
            const Ev &e = all[i].e;
            fprintf(f, "[ring] +%8luus t%04x %-9s a=%d b=%d rid=(%d,%d) d=%d\n",
                    (unsigned long)((all[i].ts - base) / 1000), all[i].tid, names[e.type],
                    e.a, e.b, e.c >> 16, e.c & 0xFFFF, e.d);
        }
        fflush(f);
    }
};
