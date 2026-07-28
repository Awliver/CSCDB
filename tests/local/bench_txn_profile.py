#!/usr/bin/env python3
"""
Transaction step profiler: per-SQL latency inside TPC-C transactions.

Breaks down NewOrder / Payment / Delivery / Stock-Level into step-level p50/p95
so optimizations can target commit, stock update, district counter, etc.

Unlike bench_tpcc*.py (tpmC only), this answers:
  - Which SQL step dominates each transaction type?
  - How does commit latency scale with concurrency?
  - Payment vs NewOrder write amplification

Examples:
  # NewOrder breakdown (ol_cnt=10), full data (default)
  python3 tests/local/bench_txn_profile.py --txn new_order

  # All five TPC-C txn types
  python3 tests/local/bench_txn_profile.py --txn all --iterations 50

  # Payment under 8-way concurrency (hot warehouse/district rows)
  python3 tests/local/bench_txn_profile.py --txn payment --threads 8 --iterations 200

  # Quick smoke on mini CSV
  python3 tests/local/bench_txn_profile.py --scale mini --txn stock_level
"""

import argparse
import random
import sys
import threading
import time

from bench_timing import InstrumentedClient, LatStats, StepAggregator, print_stats_table
from tpcc_common import RmdbClient, TPCC_SCALE, bootstrap_tpcc, kill_rmdb
from tpcc_scale import ensure_full_data, loads_for_scale, tpcc_runtime_scale
from tpcc_transactions import (
    run_delivery,
    run_neworder,
    run_order_status,
    run_payment,
    run_stock_level,
)

TXN_RUNNERS = {
    "new_order": ("NewOrder", run_neworder),
    "payment": ("Payment", run_payment),
    "order_status": ("Order-Status", run_order_status),
    "delivery": ("Delivery", run_delivery),
    "stock_level": ("Stock-Level", run_stock_level),
}


def profile_one_txn(iclient, rng, scale, txn_name, ol_cnt=None):
    """Run one instrumented transaction; return step list."""
    iclient.clear_steps()
    label, fn = TXN_RUNNERS[txn_name]
    if txn_name == "new_order":
        ok, err = fn(
            iclient,
            rng,
            scale,
            d_id=rng.randint(1, scale["districts"]),
            c_id=rng.randint(1, scale["customers_per_district"]),
            ol_cnt=ol_cnt or rng.randint(scale["min_ol_cnt"], scale["max_ol_cnt"]),
        )
    else:
        ok, err = fn(iclient, rng, scale)
    steps = iclient.take_txn_steps()
    return ok, err, steps


def profile_serial(cli, rng, scale, txn_name, warmup, iterations, ol_cnt):
    iclient = InstrumentedClient(cli)
    agg = StepAggregator()
    fail = 0
    last_err = ""
    for _ in range(warmup):
        profile_one_txn(iclient, rng, scale, txn_name, ol_cnt)
    for _ in range(iterations):
        ok, err, steps = profile_one_txn(iclient, rng, scale, txn_name, ol_cnt)
        if ok:
            agg.add_txn(steps)
        else:
            fail += 1
            last_err = err
    return agg, fail, last_err


def profile_concurrent(scale, txn_name, threads, iterations_total, seed, ol_cnt, client_timeout):
    """Each thread runs iterations_total // threads transactions."""
    per_thread = max(1, iterations_total // threads)
    aggs = []
    fails = []
    errors = []
    lock = threading.Lock()
    barrier = threading.Barrier(threads)

    def worker(tid):
        rng = random.Random(seed + tid * 10007)
        try:
            cli = RmdbClient(timeout=client_timeout)
            cli.query("set transaction isolation level snapshot isolation")
            iclient = InstrumentedClient(cli)
            local = StepAggregator()
            local_fail = 0
            local_err = ""
            barrier.wait()
            for _ in range(per_thread):
                ok, err, steps = profile_one_txn(iclient, rng, scale, txn_name, ol_cnt)
                if ok:
                    local.add_txn(steps)
                else:
                    local_fail += 1
                    local_err = err
            cli.close()
            with lock:
                aggs.append(local)
                fails.append(local_fail)
                if local_err:
                    errors.append(local_err)
        except (RuntimeError, OSError) as e:
            with lock:
                fails.append(per_thread)
                errors.append(str(e))

    ts = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(threads)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()

    total_fail = sum(fails)
    merged = StepAggregator()
    for a in aggs:
        for name, st in a.by_step.items():
            bucket = merged.by_step.setdefault(name, LatStats(name))
            bucket.merge(st)
        merged.total.merge(a.total)
    return merged, total_fail, errors[-1] if errors else ""


def compare_ol_cnt(cli, rng, scale, ol_counts, iterations):
    """Show how NewOrder total latency grows with order-line count."""
    print("\n=== NewOrder ol_cnt sweep ===")
    print("%6s  %8s  %8s  %8s  %8s" % ("ol_cnt", "mean", "p50", "p95", "commit_p50"))
    for ol in ol_counts:
        agg, fail, _ = profile_serial(cli, rng, scale, "new_order", 5, iterations, ol_cnt=ol)
        if fail:
            print("  ol_cnt=%d: %d failures" % (ol, fail))
            continue
        sm = agg.total.summary_ms()
        commit = agg.by_step.get("commit", LatStats("commit"))
        cm = commit.summary_ms()
        print(
            "%6d  %8.2f  %8.2f  %8.2f  %8.2f"
            % (ol, sm["mean"], sm["p50"], sm["p95"], cm["p50"])
        )


def main():
    ap = argparse.ArgumentParser(description="TPC-C transaction step profiler")
    ap.add_argument(
        "--txn",
        default="new_order",
        choices=list(TXN_RUNNERS.keys()) + ["all"],
    )
    ap.add_argument("--scale", choices=["mini", "full"], default="full")
    ap.add_argument("--warmup", type=int, default=10)
    ap.add_argument("--iterations", type=int, default=50)
    ap.add_argument("--threads", type=int, default=1)
    ap.add_argument("--ol-cnt", type=int, default=None, help="fixed NewOrder line count")
    ap.add_argument(
        "--ol-sweep",
        action="store_true",
        help="NewOrder only: sweep ol_cnt 5,10,15",
    )
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--client-timeout", type=float, default=120)
    args = ap.parse_args()

    if args.scale == "full":
        ensure_full_data()
        loads = loads_for_scale("full")
        scale = tpcc_runtime_scale("full")
        db_name = "txn_profile_full_db"
    else:
        loads = None
        scale = dict(TPCC_SCALE)
        db_name = "txn_profile_mini_db"

    print("=== TPC-C transaction step profiler ===")
    print(
        "  scale=%s txn=%s warmup=%d iterations=%d threads=%d"
        % (args.scale, args.txn, args.warmup, args.iterations, args.threads)
    )

    proc, cli = bootstrap_tpcc(db_name, loads=loads, client_timeout=args.client_timeout)
    cli.query("set transaction isolation level snapshot isolation")
    rng = random.Random(args.seed)

    txn_list = list(TXN_RUNNERS.keys()) if args.txn == "all" else [args.txn]
    exit_code = 0

    try:
        if args.ol_sweep:
            if args.txn not in ("new_order", "all"):
                print("--ol-sweep only applies to new_order")
                return 1
            compare_ol_cnt(cli, rng, scale, [5, 10, 15], max(20, args.iterations // 2))

        for txn_name in txn_list:
            label = TXN_RUNNERS[txn_name][0]
            print("\n######## %s ########" % label)
            t0 = time.perf_counter()
            if args.threads > 1:
                agg, fail, err = profile_concurrent(
                    scale,
                    txn_name,
                    args.threads,
                    args.iterations,
                    args.seed,
                    args.ol_cnt,
                    args.client_timeout,
                )
            else:
                agg, fail, err = profile_serial(
                    cli, rng, scale, txn_name, args.warmup, args.iterations, args.ol_cnt
                )
            elapsed = time.perf_counter() - t0
            attempted = args.iterations if args.threads > 1 else args.iterations
            ok_cnt = attempted - fail
            tps = (ok_cnt / elapsed) if elapsed > 0 else 0.0
            print(
                "  ok=%d fail=%d elapsed=%.1fs throughput=%.1f txn/s"
                % (ok_cnt, fail, elapsed, tps)
            )
            if fail:
                print("  last error:", err[:120])
                exit_code = 1
            agg.print_breakdown("%s step latency (ms)" % label)

        print("\nOVERALL: %s" % ("PASS" if exit_code == 0 else "FAIL"))
        return exit_code
    finally:
        cli.close()
        proc.kill()
        kill_rmdb()


if __name__ == "__main__":
    sys.exit(main())
