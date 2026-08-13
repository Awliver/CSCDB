#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

enum class AbortStatReason : size_t {
    LOCK_ACTIVE_WRITER = 0,
    LOCK_DEADLOCK_SELF,
    LOCK_DEADLOCK_VICTIM,
    MVCC_PENDING_WRITER,
    MVCC_ACTIVE_WRITER,
    MVCC_STALE_SNAPSHOT,
    KEY_ACTIVE_DELETE,
    KEY_STALE_DELETE,
    COUNT,
};

inline std::array<std::atomic<uint64_t>, static_cast<size_t>(AbortStatReason::COUNT)> abort_stats{};

inline bool abort_stats_enabled() {
    static const bool enabled = std::getenv("RMDB_ABORT_STATS") != nullptr;
    return enabled;
}

inline void record_abort_stat(AbortStatReason reason) {
    if (!abort_stats_enabled()) return;
    abort_stats[static_cast<size_t>(reason)].fetch_add(1, std::memory_order_relaxed);
}

inline uint64_t read_abort_stat(AbortStatReason reason) {
    return abort_stats[static_cast<size_t>(reason)].load(std::memory_order_relaxed);
}

inline void report_abort_stats(const char *tag) {
    if (!abort_stats_enabled()) return;
    std::fprintf(
        stderr,
        "[abort-stats %s] lock_active_writer=%llu lock_deadlock_self=%llu "
        "lock_deadlock_victim=%llu mvcc_pending_writer=%llu mvcc_active_writer=%llu "
        "mvcc_stale_snapshot=%llu key_active_delete=%llu key_stale_delete=%llu\n",
        tag,
        static_cast<unsigned long long>(read_abort_stat(AbortStatReason::LOCK_ACTIVE_WRITER)),
        static_cast<unsigned long long>(read_abort_stat(AbortStatReason::LOCK_DEADLOCK_SELF)),
        static_cast<unsigned long long>(read_abort_stat(AbortStatReason::LOCK_DEADLOCK_VICTIM)),
        static_cast<unsigned long long>(read_abort_stat(AbortStatReason::MVCC_PENDING_WRITER)),
        static_cast<unsigned long long>(read_abort_stat(AbortStatReason::MVCC_ACTIVE_WRITER)),
        static_cast<unsigned long long>(read_abort_stat(AbortStatReason::MVCC_STALE_SNAPSHOT)),
        static_cast<unsigned long long>(read_abort_stat(AbortStatReason::KEY_ACTIVE_DELETE)),
        static_cast<unsigned long long>(read_abort_stat(AbortStatReason::KEY_STALE_DELETE)));
}

inline void maybe_report_abort_stats() {
    if (!abort_stats_enabled()) return;
    static std::atomic<uint64_t> batches{0};
    const uint64_t count = batches.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((count & 1023U) == 0) report_abort_stats("periodic");
}
