#!/usr/bin/env python3
"""
NewOrder-only tpmC smoke test.

WARNING: OJ measures tpmC under FIVE-txn mix (Payment contends for DB).
This script is for quick NewOrder iteration ONLY — do NOT use for OJ ranking.

For OJ-fit testing use:
  python3 tests/local/run_oj_perf_test.py --strict
  python3 tests/local/run_oj_perf_test.py --mid
"""

import argparse
import random
import statistics
import sys
import time

from oj_fit import DATA_SOURCE, require_release_build
from tpcc_common import (
    RmdbClient,
    benchmark_client_timeout,
    bootstrap_tpcc,
    kill_rmdb,
    parse_count,
    verify_load_counts,
)
from tpcc_scale import ensure_full_data, loads_for_scale, scale_profile, tpcc_runtime_scale
from tpcc_transactions import run_neworder


def bench_round(cli, duration_sec, rng, scale, label):
    start = time.perf_counter()
    end = start + duration_sec
    ok_cnt = 0
    fail_cnt = 0
    latencies = []
    last_err = ""
    while time.perf_counter() < end:
        d_id = rng.randint(1, scale["districts"])
        c_id = rng.randint(1, scale["customers_per_district"])
        ol_cnt = rng.randint(scale["min_ol_cnt"], scale["max_ol_cnt"])
        t0 = time.perf_counter()
        ok, err = run_neworder(cli, rng, scale, d_id=d_id, c_id=c_id, ol_cnt=ol_cnt)
        dt = time.perf_counter() - t0
        if ok:
            ok_cnt += 1
            latencies.append(dt)
        else:
            fail_cnt += 1
            last_err = err
    elapsed = time.perf_counter() - start
    tpm = (ok_cnt / elapsed * 60.0) if elapsed > 0 else 0.0
    print(
        "  [%s] elapsed=%.1fs ok=%d fail=%d tpmC=%.2f"
        % (label, elapsed, ok_cnt, fail_cnt, tpm)
    )
    if latencies:
        latencies.sort()
        p50 = latencies[len(latencies) // 2]
        p99 = latencies[int(len(latencies) * 0.99)]
        print("    latency: p50=%.1fms p99=%.1fms" % (p50 * 1000, p99 * 1000))
    if fail_cnt:
        print("    last error:", last_err)
    return tpm, ok_cnt, fail_cnt


def sanity_check(cli, scale, orders_before, new_orders_before, order_line_before):
    orders_after = parse_count(cli.query("select count(*) from orders;"))
    new_orders_after = parse_count(cli.query("select count(*) from new_orders;"))
    order_line_after = parse_count(cli.query("select count(*) from order_line;"))
    for d_id in range(1, scale["districts"] + 1):
        r = cli.query(
            "select d_next_o_id from district where d_w_id = 1 and d_id = %d;" % d_id
        )
        from tpcc_common import parse_table_rows

        rows = parse_table_rows(r)
        if rows:
            nxt = int(float(rows[0][0]))
            print("  district %d d_next_o_id = %d" % (d_id, nxt))
    print("  orders: %d -> %d (+%d)" % (orders_before, orders_after, orders_after - orders_before))
    print("  new_orders: %d -> %d (+%d)" % (new_orders_before, new_orders_after, new_orders_after - new_orders_before))
    print("  order_line: %d -> %d (+%d)" % (order_line_before, order_line_after, order_line_after - order_line_before))
    return orders_after >= orders_before and new_orders_after >= new_orders_before


def main():
    ap = argparse.ArgumentParser(
        description="NewOrder-only smoke (NOT OJ mix — use run_oj_perf_test.py for ranking)"
    )
    ap.add_argument("--scale", choices=["mini", "full"], default="full")
    ap.add_argument("--quick", action="store_true", help="3s warmup + 15s measure")
    ap.add_argument("--warmup", type=float, default=None)
    ap.add_argument("--measure", type=float, default=None)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--client-timeout", type=float, default=None)
    args = ap.parse_args()

    print("\n" + "=" * 72)
    print("  WARNING: NewOrder-only — NOT comparable to OJ tpmC (no Payment mix).")
    print("=" * 72 + "\n")

    if args.quick:
        warmup = args.warmup if args.warmup is not None else 3.0
        measure = args.measure if args.measure is not None else 15.0
    else:
        warmup = args.warmup if args.warmup is not None else 30.0
        measure = args.measure if args.measure is not None else 360.0

    client_timeout = benchmark_client_timeout(measure, args.client_timeout)
    if args.scale == "full":
        ensure_full_data()
        loads = loads_for_scale("full")
        scale = tpcc_runtime_scale("full")
        db_name = "tpcc_neworder_full_db"
    else:
        loads = None
        scale = tpcc_runtime_scale("mini")
        db_name = "tpcc_neworder_mini_db"

    print("=== TPC-C NewOrder smoke (NOT OJ ranking) ===")
    print("  scale:", scale_profile(args.scale)["label"])
    if args.scale == "full":
        print("  data:", DATA_SOURCE)
    print("  warmup=%ss measure=%ss rounds=%d" % (warmup, measure, args.rounds))

    print("\n-- build check --")
    require_release_build(strict=False)

    proc, cli = bootstrap_tpcc(db_name, loads=loads, client_timeout=None)
    cli.close()
    cli = RmdbClient(timeout=client_timeout)
    cli.query("set transaction isolation level snapshot isolation")
    rng = random.Random(args.seed)
    try:
        print("\n-- load verify --")
        if not verify_load_counts(cli, loads):
            print("OVERALL: FAIL (load counts)")
            return 1

        orders_before = parse_count(cli.query("select count(*) from orders;"))
        new_orders_before = parse_count(cli.query("select count(*) from new_orders;"))
        order_line_before = parse_count(cli.query("select count(*) from order_line;"))

        print("\n-- single NewOrder smoke --")
        ok, err = run_neworder(cli, rng, scale, d_id=1, c_id=1, ol_cnt=5)
        print("  smoke:", "PASS" if ok else "FAIL " + err)
        if not ok:
            return 1

        print("\n-- warmup %.0fs --" % warmup)
        bench_round(cli, warmup, rng, scale, "warmup")

        tpms = []
        total_ok = total_fail = 0
        for rd in range(1, args.rounds + 1):
            print("\n-- round %d/%d measure %.0fs --" % (rd, args.rounds, measure))
            tpm, ok_cnt, fail_cnt = bench_round(cli, measure, rng, scale, "round%d" % rd)
            tpms.append(tpm)
            total_ok += ok_cnt
            total_fail += fail_cnt

        print("\n-- post-check --")
        sanity_check(cli, scale, orders_before, new_orders_before, order_line_before)

        median_tpm = statistics.median(tpms) if tpms else 0.0
        print("\n=== RESULT (NewOrder-only, NOT OJ) ===")
        print("  rounds tpmC:", [("%.2f" % x) for x in tpms])
        print("  median tpmC: %.2f" % median_tpm)
        print("  total ok/fail: %d/%d" % (total_ok, total_fail))
        if total_fail > 0:
            print("OVERALL: FAIL")
            return 1
        print("OVERALL: PASS")
        return 0
    finally:
        cli.close()
        proc.kill()
        kill_rmdb()


if __name__ == "__main__":
    sys.exit(main())
