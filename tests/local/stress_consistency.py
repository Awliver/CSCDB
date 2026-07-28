#!/usr/bin/env python3
"""Multi-seed concurrent TPC-C bursts + consistency checks for flaky bug detection."""

import argparse
import os
import random
import subprocess
import sys
import threading
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from tpcc_common import bootstrap_tpcc, kill_rmdb, parse_count, RmdbClient
from tpcc_scale import ensure_full_data, loads_for_scale, scale_profile, tpcc_runtime_scale
from tpcc_transactions import pick_txn, run_neworder, run_txn
from tpcc_consistency import run_consistency_checks, snapshot_bench_start_o_ids


def parse_seed_list(seed_str):
    if not seed_str or not seed_str.strip():
        return []
    return [int(x.strip()) for x in seed_str.split(",") if x.strip()]


def stress_worker(duration_sec, seed, scale, stats, stop_event, client_timeout=None, thread_idx=0):
    rng = random.Random(seed)
    worker_scale = dict(scale)
    worker_scale["w_id"] = 1 + (thread_idx % scale["warehouses"])
    try:
        cli = RmdbClient(timeout=client_timeout)
        cli.query("set transaction isolation level snapshot isolation")
    except (ConnectionRefusedError, OSError) as exc:
        stats.record(False, "connect failed: " + str(exc))
        return
    end = time.perf_counter() + duration_sec
    try:
        while time.perf_counter() < end and not stop_event.is_set():
            txn_name, _ = pick_txn(rng)
            try:
                if txn_name == "new_order":
                    ok, err = run_neworder(cli, rng, worker_scale)
                else:
                    ok, err = run_txn(cli, rng, worker_scale, txn_name)
            except (RuntimeError, ConnectionRefusedError, OSError) as exc:
                ok, err = False, str(exc)
            stats.record(ok, err)
    finally:
        cli.close()


class StressStats:
    def __init__(self):
        self.lock = threading.Lock()
        self.ok = 0
        self.fail = 0
        self.last_err = ""

    def record(self, ok, err=""):
        with self.lock:
            if ok:
                self.ok += 1
            else:
                self.fail += 1
            if not ok and err:
                self.last_err = err

    def snapshot(self):
        with self.lock:
            return self.ok, self.fail, self.last_err


def run_stress_burst(duration_sec, threads, seed, scale, label="", client_timeout=None):
    stats = StressStats()
    stop = threading.Event()
    workers = []
    for i in range(threads):
        t = threading.Thread(
            target=stress_worker,
            args=(duration_sec, seed + i * 10007, scale, stats, stop, client_timeout, i),
            daemon=True,
        )
        workers.append(t)
    start = time.perf_counter()
    for t in workers:
        t.start()
    for t in workers:
        t.join()
    elapsed = time.perf_counter() - start
    ok_cnt, fail_cnt, last_err = stats.snapshot()
    total = ok_cnt + fail_cnt
    rate = (fail_cnt / total * 100) if total else 0.0
    tag = label or ("seed%d" % seed)
    print(
        "  [%s] elapsed=%.1fs ok=%d fail=%d fail_rate=%.2f%%"
        % (tag, elapsed, ok_cnt, fail_cnt, rate)
    )
    if fail_cnt and last_err:
        print("    last error:", last_err[:120])
    return ok_cnt, fail_cnt


def run_multi_seed_stress(
    seeds,
    threads,
    duration_sec,
    scale,
    districts,
    before_counts,
    bench_start_o_ids,
    strict,
    client_timeout=None,
    max_burst_fail_rate=0.10,
    ytd_baseline=None,
):
    """Run concurrent bursts with different seeds; consistency check after each."""
    if not seeds:
        return True
    print("\n-- multi-seed consistency stress (%d trials, %.0fs each) --" % (
        len(seeds), duration_sec))
    ok = True
    for i, seed in enumerate(seeds, 1):
        print("\n  trial %d/%d seed=%d" % (i, len(seeds), seed))
        burst_ok, burst_fail = run_stress_burst(
            duration_sec, threads, seed, scale,
            label="stress-seed%d" % seed, client_timeout=client_timeout,
        )
        total = burst_ok + burst_fail
        if total > 0:
            fail_rate = burst_fail / total
            if fail_rate > max_burst_fail_rate:
                print(
                    "  WARN: burst fail rate %.2f%% > %.2f%% (informational; aborts may be normal under SI)"
                    % (fail_rate * 100, max_burst_fail_rate * 100)
                )
        try:
            cli = RmdbClient(timeout=client_timeout)
            cli.query("set transaction isolation level snapshot isolation")
        except (ConnectionRefusedError, OSError) as exc:
            print("  FAIL: cannot connect for consistency:", exc)
            return False
        try:
            trial_ok = run_consistency_checks(
                cli,
                districts=districts,
                warehouses=scale.get("warehouses", 1),
                before_counts=before_counts,
                strict=strict,
                bench_start_o_ids=bench_start_o_ids,
                ytd_baseline=ytd_baseline,
            )
            if not trial_ok:
                print("  FAIL: consistency after stress seed %d" % seed)
                ok = False
        finally:
            cli.close()
    print("MULTI-SEED STRESS:", "PASS" if ok else "FAIL")
    return ok


def main():
    ap = argparse.ArgumentParser(description="Multi-seed TPC-C consistency stress")
    ap.add_argument("--scale", choices=["mini", "full"], default="full")
    ap.add_argument("--threads", type=int, default=16)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--seeds", default="42,99,123,456")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--max-burst-fail-rate", type=float, default=0.25,
                    help="warn if per-trial fail rate exceeds this (does not fail)")
    args = ap.parse_args()

    if args.scale == "full":
        if not ensure_full_data():
            return 1
    loads = loads_for_scale(args.scale)
    scale = tpcc_runtime_scale(args.scale)
    seeds = parse_seed_list(args.seeds)
    if not seeds:
        print("ERROR: no seeds")
        return 2

    kill_rmdb()
    proc, cli = bootstrap_tpcc("stress_consistency_db", loads=loads)
    try:
        before_counts = {
            tab: parse_count(cli.query("select count(*) from %s;" % tab))
            for tab, _, _ in loads
        }
        bench_start_o_ids = snapshot_bench_start_o_ids(cli, scale["districts"], scale["warehouses"])
    finally:
        cli.close()

    ok = run_multi_seed_stress(
        seeds=seeds,
        threads=min(max(1, args.threads), 32),
        duration_sec=args.seconds,
        scale=scale,
        districts=scale["districts"],
        before_counts=before_counts,
        bench_start_o_ids=bench_start_o_ids,
        strict=args.strict,
        max_burst_fail_rate=args.max_burst_fail_rate,
    )
    if proc.poll() is None:
        proc.kill()
    kill_rmdb()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
