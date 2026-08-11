#!/usr/bin/env python3
"""Compare two structured TPC-C result JSON files."""

import argparse
import json
import sys


COMPARABLE_KEYS = (
    "tier",
    "scale",
    "warehouses",
    "threads",
    "seed",
    "warmup_sec",
    "measure_sec",
    "rounds",
    "protocol",
    "mix",
    "use_batch",
    "routing",
)


def load_result(path):
    with open(path) as stream:
        return json.load(stream)


def percent_change(old, new):
    if old in (None, 0) or new is None:
        return None
    return (float(new) / float(old) - 1.0) * 100.0


def fmt_number(value, digits=2):
    if value is None:
        return "-"
    return ("%%.%df" % digits) % value


def print_counter_delta(title, baseline, candidate, limit=12):
    keys = sorted(
        set(baseline or {}) | set(candidate or {}),
        key=lambda key: (-(candidate or {}).get(key, 0), key),
    )
    if not keys:
        return
    print("\n%s" % title)
    print("  %-34s %10s %10s %10s" % ("category", "baseline", "candidate", "delta"))
    for key in keys[:limit]:
        old = (baseline or {}).get(key, 0)
        new = (candidate or {}).get(key, 0)
        print("  %-34s %10d %10d %+10d" % (key, old, new, new - old))


def compare(baseline, candidate, args):
    mismatches = []
    for key in COMPARABLE_KEYS:
        if baseline.get(key) != candidate.get(key):
            mismatches.append((key, baseline.get(key), candidate.get(key)))

    if mismatches:
        print("CONFIG MISMATCH:")
        for key, old, new in mismatches:
            print("  %-18s baseline=%r candidate=%r" % (key, old, new))
        if not args.allow_config_mismatch:
            print("Refusing to compare non-equivalent runs. Pass --allow-config-mismatch to report only.")
            return 2

    base_tpm = baseline.get("median_tpmc")
    cand_tpm = candidate.get("median_tpmc")
    gain = percent_change(base_tpm, cand_tpm)
    base_abort = baseline.get("new_order_abort_rate")
    cand_abort = candidate.get("new_order_abort_rate")
    abort_delta_pp = None
    if base_abort is not None and cand_abort is not None:
        abort_delta_pp = (cand_abort - base_abort) * 100.0
    base_latency = baseline.get("new_order_latency") or {}
    cand_latency = candidate.get("new_order_latency") or {}
    base_p50 = base_latency.get("p50_ms")
    cand_p50 = cand_latency.get("p50_ms")
    p50_change = percent_change(base_p50, cand_p50)
    base_p99 = base_latency.get("p99_ms")
    cand_p99 = cand_latency.get("p99_ms")
    p99_change = percent_change(base_p99, cand_p99)

    print("\n=== TPC-C A/B COMPARISON ===")
    print("  baseline:  %s  %s" % (baseline.get("git_rev", "?"), baseline.get("git_subject", "")))
    print("  candidate: %s  %s" % (candidate.get("git_rev", "?"), candidate.get("git_subject", "")))
    print("  config:    tier=%s W=%s threads=%s windows=%sx%s" % (
        candidate.get("tier"),
        candidate.get("warehouses"),
        candidate.get("threads"),
        candidate.get("measure_sec"),
        candidate.get("rounds"),
    ))
    print("\n  %-26s %14s %14s %14s" % ("metric", "baseline", "candidate", "change"))
    print("  %-26s %14s %14s %13s%%" % (
        "median tpmC", fmt_number(base_tpm), fmt_number(cand_tpm), fmt_number(gain)))
    print("  %-26s %13s%% %13s%% %12s pp" % (
        "NewOrder abort rate",
        fmt_number(base_abort * 100.0 if base_abort is not None else None),
        fmt_number(cand_abort * 100.0 if cand_abort is not None else None),
        fmt_number(abort_delta_pp),
    ))
    print("  %-26s %11s ms %11s ms %13s%%" % (
        "NewOrder p50", fmt_number(base_p50), fmt_number(cand_p50), fmt_number(p50_change)))
    print("  %-26s %11s ms %11s ms %13s%%" % (
        "NewOrder p99", fmt_number(base_p99), fmt_number(cand_p99), fmt_number(p99_change)))
    print("  %-26s %14s %14s" % (
        "round tpmC",
        ",".join("%.0f" % value for value in baseline.get("tpms", [])),
        ",".join("%.0f" % value for value in candidate.get("tpms", [])),
    ))

    print_counter_delta("Abort reasons", baseline.get("abort_reasons"), candidate.get("abort_reasons"))
    print_counter_delta("Failure stages", baseline.get("failure_stages"), candidate.get("failure_stages"))
    print_counter_delta(
        "Warehouse attempts",
        baseline.get("warehouse_attempts"),
        candidate.get("warehouse_attempts"),
    )

    base_diag = baseline.get("server_diagnostics") or {}
    cand_diag = candidate.get("server_diagnostics") or {}
    print_counter_delta(
        "Server diagnostic lines",
        base_diag.get("line_counts"),
        cand_diag.get("line_counts"),
    )
    print_counter_delta(
        "Server abort roots",
        ((base_diag.get("abort_stats") or {}).get("last") or {}),
        ((cand_diag.get("abort_stats") or {}).get("last") or {}),
    )

    base_wal = base_diag.get("wal_last") or {}
    cand_wal = cand_diag.get("wal_last") or {}
    if base_wal or cand_wal:
        print("\nWAL last sample")
        for key in ("fsync", "avg_us", "max_us", "bytes", "batches", "commit_waits", "waits/fsync"):
            if key in base_wal or key in cand_wal:
                print("  %-18s %12s -> %12s" % (key, base_wal.get(key, "-"), cand_wal.get(key, "-")))

    base_ops = ((base_diag.get("batch_ops") or {}).get("last") or {})
    cand_ops = ((cand_diag.get("batch_ops") or {}).get("last") or {})
    if base_ops or cand_ops:
        print("\nEXEC_BATCH statement timing (last cumulative sample)")
        print("  %-8s %-42s %14s %14s %14s %14s" % (
            "stmt_id", "statement", "base avg_us", "cand avg_us", "base max_us", "cand max_us"))
        ids = sorted(set(base_ops) | set(cand_ops), key=lambda value: int(value))
        statements = candidate.get("prepared_statements") or baseline.get("prepared_statements") or {}
        for statement_id in ids:
            old = base_ops.get(statement_id, {})
            new = cand_ops.get(statement_id, {})
            statement = " ".join((statements.get(statement_id) or "?").split())
            print("  %-8s %-42s %14s %14s %14s %14s" % (
                statement_id, statement[:42],
                fmt_number(old.get("avg_us")), fmt_number(new.get("avg_us")),
                fmt_number(old.get("max_us")), fmt_number(new.get("max_us")),
            ))

    failed = False
    if args.require_pass and (baseline.get("overall") != "PASS" or candidate.get("overall") != "PASS"):
        print("\nFAIL: --require-pass and at least one run did not PASS")
        failed = True
    if args.min_tpmc_gain_pct is not None and (gain is None or gain < args.min_tpmc_gain_pct):
        print("\nFAIL: tpmC gain %s%% < required %.2f%%" % (fmt_number(gain), args.min_tpmc_gain_pct))
        failed = True
    if (
        args.max_abort_rate_increase_pp is not None
        and (abort_delta_pp is None or abort_delta_pp > args.max_abort_rate_increase_pp)
    ):
        print("\nFAIL: abort-rate increase %s pp > allowed %.2f pp" % (
            fmt_number(abort_delta_pp), args.max_abort_rate_increase_pp))
        failed = True
    if (
        args.max_p50_increase_pct is not None
        and (p50_change is None or p50_change > args.max_p50_increase_pct)
    ):
        print("\nFAIL: p50 increase %s%% > allowed %.2f%%" % (
            fmt_number(p50_change), args.max_p50_increase_pct))
        failed = True
    if (
        args.max_p99_increase_pct is not None
        and (p99_change is None or p99_change > args.max_p99_increase_pct)
    ):
        print("\nFAIL: p99 increase %s%% > allowed %.2f%%" % (
            fmt_number(p99_change), args.max_p99_increase_pct))
        failed = True
    if args.max_p50_ms is not None and (cand_p50 is None or cand_p50 > args.max_p50_ms):
        print("\nFAIL: candidate p50 %s ms > target %.2f ms" % (
            fmt_number(cand_p50), args.max_p50_ms))
        failed = True
    if args.max_p99_ms is not None and (cand_p99 is None or cand_p99 > args.max_p99_ms):
        print("\nFAIL: candidate p99 %s ms > target %.2f ms" % (
            fmt_number(cand_p99), args.max_p99_ms))
        failed = True

    print("\nVERDICT:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description="Compare baseline and candidate TPC-C JSON")
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--allow-config-mismatch", action="store_true")
    parser.add_argument("--require-pass", action="store_true")
    parser.add_argument("--min-tpmc-gain-pct", type=float, default=None)
    parser.add_argument("--max-abort-rate-increase-pp", type=float, default=None)
    parser.add_argument("--max-p50-increase-pct", type=float, default=None)
    parser.add_argument("--max-p99-increase-pct", type=float, default=None)
    parser.add_argument("--max-p50-ms", type=float, default=None)
    parser.add_argument("--max-p99-ms", type=float, default=None)
    args = parser.parse_args()
    return compare(load_result(args.baseline), load_result(args.candidate), args)


if __name__ == "__main__":
    sys.exit(main())
