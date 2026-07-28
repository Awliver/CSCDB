#!/usr/bin/env python3
"""
Local TPC-C full benchmark (OJ / finals performance style).

Default path aligns with finals ranking clients:
  - Wire PREPARE_SET + EXEC_BATCH (OJ hot path; --stream for A/B)
  - Mix 45/43/4/4/4 (--legacy-mix for初赛 10/23)
  - SI before PREPARE_SET

Tiers:
  --quick     smoke: 3s + 15s x 1
  --mid       trend: 30s + 60s x 3 (BATCH)
  --finals    OJ-shaped: 30s + 150s x 3, 32 clients (W=5 data still unless scale grows)
  --strict    full checks; window defaults to 150s x 3 when --finals, else 360s x 3 (legacy)
"""

import argparse
import os
import random
import statistics
import subprocess
import sys
import threading
import time

from tpcc_common import (
    BUILD,
    RMDB,
    RmdbClient,
    benchmark_client_timeout,
    bootstrap_tpcc,
    kill_rmdb,
    parse_count,
    verify_load_counts,
)
from tpcc_scale import ensure_full_data, loads_for_scale, scale_profile, tpcc_runtime_scale
from tpcc_transactions import (
    TXN_WEIGHTS,
    TXN_WEIGHTS_FINALS,
    TXN_WEIGHTS_LEGACY,
    make_txn_population,
    run_neworder,
    run_txn,
)
from tpcc_batch import install_prepare, run_txn_batch
from tpcc_consistency import run_consistency_checks, snapshot_bench_start_o_ids, snapshot_ytd_baseline
from tpcc_load_verify import verify_load_content
from p2_gate import run_p2_functional_tests
from stress_consistency import parse_seed_list, run_multi_seed_stress
from oj_fit import (
    DATA_SOURCE,
    print_oj_summary,
    require_release_build,
    tier_label,
    build_result_payload,
    save_bench_history,
    write_json_result,
)

# Active mix set in main() (finals 45/43/4/4/4 by default; --legacy-mix → 10/23)
MIX_WEIGHTS = {name: w for name, w, _ in TXN_WEIGHTS}
MIX_TOTAL = sum(MIX_WEIGHTS.values())
TXN_POPULATION = make_txn_population(TXN_WEIGHTS)
TXN_NAMES = [name for name, _, _ in TXN_WEIGHTS]


class TxnStats:
    def __init__(self):
        self.lock = threading.Lock()
        self.new_order_ok = 0
        self.new_order_fail = 0
        self.other_ok = 0
        self.other_fail = 0
        self.last_err = ""
        self.by_type = {name: {"ok": 0, "fail": 0} for name in TXN_NAMES}
        self.new_order_latencies = []

    def record(self, txn_type, ok, err="", latency=0.0):
        with self.lock:
            bucket = self.by_type.setdefault(txn_type, {"ok": 0, "fail": 0})
            if ok:
                bucket["ok"] += 1
            else:
                bucket["fail"] += 1
            if txn_type == "new_order":
                if ok:
                    self.new_order_ok += 1
                    self.new_order_latencies.append(latency)
                else:
                    self.new_order_fail += 1
            else:
                if ok:
                    self.other_ok += 1
                else:
                    self.other_fail += 1
            if not ok and err:
                self.last_err = err

    def snapshot(self):
        with self.lock:
            return (
                self.new_order_ok,
                self.new_order_fail,
                self.other_ok,
                self.other_fail,
                self.last_err,
                {k: dict(v) for k, v in self.by_type.items()},
                list(self.new_order_latencies),
            )


def worker_loop(
    duration_sec,
    rng_seed,
    scale,
    stats,
    stop_event,
    client_timeout=None,
    thread_idx=0,
    use_batch=True,
    population=None,
):
    rng = random.Random(rng_seed)
    worker_scale = dict(scale)
    worker_scale["w_id"] = 1 + (thread_idx % scale["warehouses"])
    pop = population or TXN_POPULATION
    try:
        cli = RmdbClient(timeout=client_timeout)
        # 决赛：SI 在 PREPARE_SET 之前设置
        cli.query("set transaction isolation level snapshot isolation")
        if use_batch:
            install_prepare(cli)
    except (ConnectionRefusedError, OSError, RuntimeError) as e:
        stats.record("new_order", False, "connect/prepare failed: %s" % e)
        return
    end = time.perf_counter() + duration_sec
    try:
        while time.perf_counter() < end and not stop_event.is_set():
            txn_name, _ = __import__("tpcc_transactions").pick_txn(rng, pop)
            t0 = time.perf_counter()
            try:
                if use_batch:
                    ok, err = run_txn_batch(cli, rng, worker_scale, txn_name)
                elif txn_name == "new_order":
                    ok, err = run_neworder(cli, rng, worker_scale)
                else:
                    ok, err = run_txn(cli, rng, worker_scale, txn_name)
            except (RuntimeError, ConnectionRefusedError, OSError) as e:
                ok, err = False, str(e)
            dt = time.perf_counter() - t0
            stats.record(txn_name, ok, err, dt)
    finally:
        cli.close()


def bench_round(
    duration_sec,
    threads,
    seed,
    scale,
    label="round",
    client_timeout=None,
    use_batch=True,
    population=None,
):
    stats = TxnStats()
    stop = threading.Event()
    start = time.perf_counter()
    workers = []
    for i in range(threads):
        t = threading.Thread(
            target=worker_loop,
            args=(
                duration_sec,
                seed + i * 10007,
                scale,
                stats,
                stop,
                client_timeout,
                i,
                use_batch,
                population,
            ),
            daemon=True,
        )
        t.start()
        workers.append(t)
    for t in workers:
        t.join()
    elapsed = time.perf_counter() - start
    no_ok, no_fail, o_ok, o_fail, last_err, by_type, lats = stats.snapshot()
    tpm = (no_ok / elapsed * 60.0) if elapsed > 0 else 0.0
    print(
        "  [%s] elapsed=%.1fs threads=%d new_order ok=%d fail=%d other ok=%d fail=%d tpmC=%.2f"
        % (label, elapsed, threads, no_ok, no_fail, o_ok, o_fail, tpm)
    )
    if no_fail or o_fail:
        print("    last error:", last_err[:120])
    return {
        "tpm": tpm,
        "elapsed": elapsed,
        "new_order_ok": no_ok,
        "new_order_fail": no_fail,
        "other_ok": o_ok,
        "other_fail": o_fail,
        "by_type": by_type,
        "new_order_latencies": lats,
    }


def check_txn_mix(by_type, tolerance=0.15):
    """Measured txn mix should be close to active MIX_WEIGHTS."""
    total = sum(v["ok"] + v["fail"] for v in by_type.values())
    if total < 50:
        print("  SKIP: txn mix (too few txns: %d)" % total)
        return True
    ok = True
    print("  txn mix (ok+fail):")
    for name, target_w in MIX_WEIGHTS.items():
        cnt = by_type.get(name, {}).get("ok", 0) + by_type.get(name, {}).get("fail", 0)
        expected = target_w / MIX_TOTAL
        actual = cnt / total
        print("    %-14s actual=%5.1f%% expected=%5.1f%% (n=%d)" % (
            name, actual * 100, expected * 100, cnt))
        if abs(actual - expected) > tolerance:
            ok = False
    if ok:
        print("  PASS: txn mix within ±%.0f%%" % (tolerance * 100))
    else:
        print("  FAIL: txn mix out of tolerance ±%.0f%%" % (tolerance * 100))
    return ok


def check_abort_rate(round_results, max_rate):
    if max_rate <= 0:
        return True
    ok = True
    for i, r in enumerate(round_results, 1):
        ok_cnt = r["new_order_ok"]
        fail_cnt = r["new_order_fail"]
        total = ok_cnt + fail_cnt
        if total == 0:
            print("  FAIL: round %d had zero new_order attempts" % i)
            ok = False
            continue
        rate = fail_cnt / total
        print("  round %d new_order abort rate: %.2f%% (%d/%d)" % (
            i, rate * 100, fail_cnt, total))
        if rate > max_rate:
            ok = False
    if ok:
        print("  PASS: new_order abort rate <= %.2f%%" % (max_rate * 100))
    else:
        print("  FAIL: new_order abort rate > %.2f%%" % (max_rate * 100))
    return ok


def check_other_fail_rate(round_results, max_rate):
    """Strict: non-NewOrder txn failure rate during measure rounds."""
    if max_rate <= 0:
        return True
    ok_cnt = sum(r.get("other_ok", 0) for r in round_results)
    fail_cnt = sum(r.get("other_fail", 0) for r in round_results)
    total = ok_cnt + fail_cnt
    if total == 0:
        print("  SKIP: other txn fail rate (no samples)")
        return True
    rate = fail_cnt / total
    print("  other txn fail rate: %.2f%% (%d/%d)" % (rate * 100, fail_cnt, total))
    if rate > max_rate:
        print("  FAIL: other txn fail rate > %.2f%%" % (max_rate * 100))
        return False
    print("  PASS: other txn fail rate <= %.2f%%" % (max_rate * 100))
    return True


def check_round_stability(tpms, max_spread=0.50):
    """Report (max-min)/median tpmC; warn if over max_spread but do not fail."""
    if len(tpms) < 2:
        return True
    med = statistics.median(tpms)
    if med <= 0:
        print("  FAIL: median tpmC is zero")
        return False
    spread = (max(tpms) - min(tpms)) / med
    print("  tpmC spread (max-min)/median: %.2f (warn above %.2f)" % (spread, max_spread))
    if spread > max_spread:
        print("  WARN: round tpmC unstable (informational only)")
    else:
        print("  PASS: round tpmC stability")
    return True


def check_neworder_latency(latencies, p99_limit_ms=5000):
    if not latencies:
        print("  SKIP: new_order latency (no samples)")
        return True
    latencies = sorted(latencies)
    p50 = latencies[len(latencies) // 2]
    p99 = latencies[int(len(latencies) * 0.99)]
    print("  new_order latency p50=%.0fms p99=%.0fms (limit p99=%dms)" % (
        p50 * 1000, p99 * 1000, p99_limit_ms))
    if p99 * 1000 > p99_limit_ms:
        print("  FAIL: new_order p99 latency")
        return False
    print("  PASS: new_order latency")
    return True


def verify_indexes(cli):
    """Strict: indexed point lookups must work after bootstrap."""
    probes = [
        ("warehouse", "select w_id from warehouse where w_id = 1;"),
        ("district", "select d_id from district where d_w_id = 1 and d_id = 1;"),
        ("customer", "select c_id from customer where c_w_id = 1 and c_d_id = 1 and c_id = 1;"),
        ("orders", "select o_id from orders where o_w_id = 1 and o_d_id = 1 and o_id = 1;"),
        ("stock", "select s_quantity from stock where s_w_id = 1 and s_i_id = 1;"),
        ("item", "select i_price from item where i_id = 1;"),
    ]
    ok = True
    for name, sql in probes:
        r = cli.query(sql)
        low = r.lower()
        if "error" in low or "failure" in low:
            ok = False
            print("  FAIL: index probe %s: %s" % (name, r[:80]))
    if ok:
        print("  PASS: indexed point lookups")
    return ok


def verify_output_file_off(db_name):
    """Strict: output.txt should stay small after set output_file off."""
    path = os.path.join(BUILD, db_name, "output.txt")
    if not os.path.isfile(path):
        print("  PASS: output.txt absent (off)")
        return True
    size = os.path.getsize(path)
    if size > 4096:
        print("  FAIL: output.txt grew to %d bytes after set output_file off" % size)
        return False
    print("  PASS: output.txt size %d bytes" % size)
    return True


def wait_for_server(proc, log_path, timeout=60):
    for _ in range(timeout):
        time.sleep(1)
        if proc.poll() is not None:
            print("  FAIL: rmdb exited (see %s)" % log_path)
            return None
        try:
            return RmdbClient(timeout=30)
        except (ConnectionRefusedError, OSError):
            pass
    print("  FAIL: server not ready in %ds" % timeout)
    return None


def crash_recovery_post_benchmark(proc, db_name, districts, before_counts, strict=False,
                                bench_start_o_ids=None, ytd_baseline=None, warehouses=1):
    """Kill -9 after benchmark DB state; restart same files (stricter than OJ)."""
    print("\n-- crash recovery check (post-benchmark DB) --")
    cli = RmdbClient()
    try:
        orders_before = parse_count(cli.query("select count(*) from orders;"))
        ol_before = parse_count(cli.query("select count(*) from order_line;"))
        hist_before = parse_count(cli.query("select count(*) from history;"))
        print("  orders before kill:", orders_before)
    finally:
        cli.close()

    if proc and proc.poll() is None:
        proc.kill()
    kill_rmdb()
    time.sleep(0.5)

    dbpath = os.path.join(BUILD, db_name)
    log_path = os.path.join(dbpath, "recovery.log")
    with open(log_path, "w") as logf:
        proc2 = subprocess.Popen(
            [RMDB, db_name], cwd=BUILD, stdout=logf, stderr=subprocess.STDOUT,
        )
    cli2 = wait_for_server(proc2, log_path)
    if cli2 is None:
        proc2.kill()
        kill_rmdb()
        return False, None
    try:
        orders_after = parse_count(cli2.query("select count(*) from orders;"))
        ol_after = parse_count(cli2.query("select count(*) from order_line;"))
        hist_after = parse_count(cli2.query("select count(*) from history;"))
        print("  orders after restart:", orders_after)
        ok = True
        if orders_after < orders_before:
            print("  FAIL: lost orders %d -> %d" % (orders_before, orders_after))
            ok = False
        if ol_after < ol_before:
            print("  FAIL: lost order_line %d -> %d" % (ol_before, ol_after))
            ok = False
        if hist_after < hist_before:
            print("  FAIL: lost history %d -> %d" % (hist_before, hist_after))
            ok = False
        if ok:
            ok = run_consistency_checks(
                cli2, districts=districts, warehouses=warehouses,
                before_counts=before_counts, strict=strict,
                bench_start_o_ids=bench_start_o_ids, ytd_baseline=ytd_baseline,
            )
        print("CRASH RECOVERY:", "PASS" if ok else "FAIL")
        return ok, proc2
    finally:
        cli2.close()


def crash_recovery_fresh_load(db_name, scale, loads, districts, strict=False):
    """Additional strict check: recovery after fresh load + txn burst."""
    print("\n-- crash recovery check (fresh load + txn burst) --")
    warehouses = scale.get("warehouses", 1)
    kill_rmdb()
    proc = None
    cli = None
    proc2 = None
    cli2 = None
    try:
        proc, cli = bootstrap_tpcc(db_name + "_crash", loads=loads)
        # Snapshot before txn burst so post-recovery checks only cover new rows / deltas.
        bench_start_o_ids = snapshot_bench_start_o_ids(cli, districts, warehouses)
        ytd_baseline = snapshot_ytd_baseline(cli, warehouses)
        rng = random.Random(99)
        worker_scale = dict(scale)
        worker_scale["w_id"] = 1
        for i in range(50):
            ok, err = run_neworder(cli, rng, worker_scale, d_id=1, c_id=1 + (i % 10), ol_cnt=5)
            if not ok:
                print("  FAIL: fresh-load NewOrder burst: " + err[:120])
                return False
        before_counts = {}
        for tab in ("orders", "order_line", "history", "new_orders", "warehouse"):
            before_counts[tab] = parse_count(cli.query("select count(*) from %s;" % tab))
        orders_before = before_counts["orders"]
        print("  orders before kill:", orders_before)
        cli.close()
        cli = None
        proc.kill()
        proc = None
        kill_rmdb()
        time.sleep(0.5)

        dbpath = os.path.join(BUILD, db_name + "_crash")
        log_path = os.path.join(dbpath, "recovery.log")
        with open(log_path, "w") as logf:
            proc2 = subprocess.Popen(
                [RMDB, db_name + "_crash"], cwd=BUILD, stdout=logf, stderr=subprocess.STDOUT,
            )
        cli2 = wait_for_server(proc2, log_path, timeout=120)
        if cli2 is None:
            print("  FAIL: server not ready after crash restart (see %s)" % log_path)
            return False
        orders_after = parse_count(cli2.query("select count(*) from orders;"))
        print("  orders after restart:", orders_after)
        if orders_after < orders_before:
            print("  FAIL: lost committed orders")
            return False
        ok = run_consistency_checks(
            cli2,
            districts=districts,
            warehouses=warehouses,
            before_counts=before_counts,
            strict=strict,
            bench_start_o_ids=bench_start_o_ids,
            ytd_baseline=ytd_baseline,
        )
        print("CRASH RECOVERY (fresh):", "PASS" if ok else "FAIL")
        return ok
    except Exception as exc:
        print("  FAIL: fresh-load crash recovery raised: %s" % exc)
        return False
    finally:
        if cli is not None:
            try:
                cli.close()
            except Exception:
                pass
        if cli2 is not None:
            try:
                cli2.close()
            except Exception:
                pass
        if proc is not None and proc.poll() is None:
            proc.kill()
        if proc2 is not None and proc2.poll() is None:
            proc2.kill()
        kill_rmdb()


def check_measure_elapsed(round_results, measure_sec, min_ratio=0.85):
    """Fail if workers finished far before measure window (timeout / early exit)."""
    ok = True
    for i, r in enumerate(round_results, 1):
        elapsed = r.get("elapsed", 0)
        if elapsed < measure_sec * min_ratio:
            print(
                "  FAIL: round %d elapsed=%.1fs < %.0f%% of measure=%.0fs"
                % (i, elapsed, min_ratio * 100, measure_sec)
            )
            ok = False
    if ok:
        print("  PASS: measure rounds completed full window (elapsed >= %.0f%% of measure)" % (
            min_ratio * 100))
    return ok


def main():
    ap = argparse.ArgumentParser(description="Local OJ-style TPC-C performance benchmark")
    ap.add_argument("--scale", choices=["mini", "full"], default="full")
    ap.add_argument("--strict", action="store_true",
                    help="stricter than OJ: full consistency, abort rate, mix, crash on real DB")
    ap.add_argument("--quick", action="store_true",
                    help="smoke tier: 3s warmup + 15s measure, 1 round (NOT OJ window)")
    ap.add_argument("--mid", action="store_true",
                    help="trend tier: 30s warmup + 60s measure x 3 rounds, median tpmC")
    ap.add_argument("--finals", action="store_true",
                    help="OJ-shaped: 30s + 150s x 3, default 32 clients, BATCH + 45/43/4/4/4")
    ap.add_argument("--stream", action="store_true",
                    help="use EXEC_STREAM per SQL (A/B); default is PREPARE_SET+EXEC_BATCH")
    ap.add_argument("--legacy-mix", action="store_true",
                    help="use初赛 10/23 mix instead of finals 45/43/4/4/4")
    ap.add_argument("--warmup", type=float, default=None)
    ap.add_argument("--measure", type=float, default=None)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--threads", type=int, default=None,
                    help="concurrent clients (max 32; default 16, or 32 with --finals)")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--stress-seeds", default=None,
                    help="comma-separated seeds for post-benchmark stress trials "
                         "(strict default: 99,123,456 full / 99,123 quick)")
    ap.add_argument("--stress-seconds", type=float, default=None,
                    help="seconds per stress trial (strict default: 60 full / 15 quick)")
    ap.add_argument("--skip-stress", action="store_true",
                    help="skip multi-seed consistency stress")
    ap.add_argument("--max-other-fail-rate", type=float, default=None,
                    help="strict: max non-NewOrder fail rate (default 0.05)")
    ap.add_argument("--max-burst-fail-rate", type=float, default=0.25,
                    help="warn if per-trial fail rate exceeds this during stress (does not fail)")
    ap.add_argument("--skip-crash", action="store_true")
    ap.add_argument("--skip-consistency", action="store_true")
    ap.add_argument("--skip-p2", action="store_true", help="skip P2 functional gate")
    ap.add_argument("--skip-load-content", action="store_true", help="skip CSV content verify")
    ap.add_argument("--no-generate", action="store_true")
    ap.add_argument("--max-abort-rate", type=float, default=None,
                    help="max NewOrder abort rate in measure rounds (default: 0.01 strict, off otherwise)")
    ap.add_argument("--mix-tolerance", type=float, default=0.15,
                    help="strict: allowed deviation from active mix (default 0.15)")
    ap.add_argument("--max-round-spread", type=float, default=0.50,
                    help="strict: warn if (max-min)/median tpmC exceeds this (does not fail)")
    ap.add_argument("--p99-latency-ms", type=float, default=5000,
                    help="strict: max new_order p99 latency in ms")
    ap.add_argument("--client-timeout", type=float, default=None,
                    help="per-SQL socket timeout in seconds (0=unlimited; default 2×measure, min 600)")
    ap.add_argument("--json", metavar="PATH", default=None,
                    help="also write result JSON to PATH (history is always saved unless --no-save-history)")
    ap.add_argument("--no-save-history", action="store_true",
                    help="do not auto-save under build/bench_history/")
    args = ap.parse_args()

    tier_flags = sum(bool(x) for x in (args.quick, args.mid, args.finals))
    if tier_flags > 1:
        print("ERROR: --quick / --mid / --finals are mutually exclusive")
        return 2

    global MIX_WEIGHTS, MIX_TOTAL, TXN_POPULATION, TXN_NAMES
    active_weights = TXN_WEIGHTS_LEGACY if args.legacy_mix else TXN_WEIGHTS_FINALS
    MIX_WEIGHTS = {name: w for name, w, _ in active_weights}
    MIX_TOTAL = sum(MIX_WEIGHTS.values())
    TXN_POPULATION = make_txn_population(active_weights)
    TXN_NAMES = [name for name, _, _ in active_weights]
    use_batch = not args.stream

    if args.strict:
        if args.skip_crash or args.skip_consistency or args.skip_p2 or args.skip_load_content:
            print("ERROR: --strict disallows --skip-crash / --skip-consistency / "
                  "--skip-p2 / --skip-load-content")
            return 2
        if args.max_abort_rate is None:
            args.max_abort_rate = 0.01
        if args.max_other_fail_rate is None:
            args.max_other_fail_rate = 0.05

    if args.stress_seeds is None:
        if args.strict and not args.skip_stress:
            args.stress_seeds = "99,123" if args.quick else "99,123,456"
        else:
            args.stress_seeds = ""
    if args.stress_seconds is None:
        args.stress_seconds = 15.0 if args.quick else 60.0
    stress_seed_list = parse_seed_list(args.stress_seeds)

    if args.quick:
        warmup = args.warmup if args.warmup is not None else 3.0
        measure = args.measure if args.measure is not None else 15.0
        rounds = 1
        default_threads = 16
    elif args.mid:
        warmup = args.warmup if args.warmup is not None else 30.0
        measure = args.measure if args.measure is not None else 60.0
        rounds = args.rounds if args.rounds != 3 else 3
        default_threads = 16
    elif args.finals:
        warmup = args.warmup if args.warmup is not None else 30.0
        measure = args.measure if args.measure is not None else 150.0
        rounds = args.rounds if args.rounds != 3 else 3
        default_threads = 32
    else:
        # bare / --strict without tier: finals-shaped window (150s) for OJ closeness
        warmup = args.warmup if args.warmup is not None else 30.0
        measure = args.measure if args.measure is not None else (150.0 if args.strict else 360.0)
        rounds = args.rounds
        default_threads = 32 if args.strict else 16

    threads = min(max(1, args.threads if args.threads is not None else default_threads), 32)
    if args.scale == "mini" and (args.quick or args.mid) and not args.strict:
        threads = min(threads, 2)

    tier = tier_label(
        quick=args.quick,
        mid=args.mid,
        finals=args.finals,
        strict=args.strict and not args.quick and not args.mid and not args.finals,
    )

    client_timeout = benchmark_client_timeout(measure, args.client_timeout)
    profile = scale_profile(args.scale)
    scale = tpcc_runtime_scale(args.scale)

    if args.scale == "full":
        if not ensure_full_data(generate=not args.no_generate):
            print("Full data not ready. Run: python3 tests/local/generate_tpcc_data.py --scale full")
            return 1
    loads = loads_for_scale(args.scale)
    db_name = "tpcc_perf_db"

    mix_desc = (
        "legacy 10/23 NewOrder+Payment"
        if args.legacy_mix
        else "finals 45/43/4/4/4 NewOrder/Payment/OS/Del/SL"
    )
    proto_desc = "PREPARE_SET+EXEC_BATCH" if use_batch else "EXEC_STREAM"

    print("=== TPC-C %sperformance benchmark ===" % ("STRICT " if args.strict else ""))
    print("  tier:", tier, "(finals=150x3×32 | mid=60x3 trend | quick=smoke)")
    print("  scale:", profile["label"])
    print("  data: ", DATA_SOURCE)
    print("  protocol:", proto_desc)
    print("  mix:", mix_desc)
    print("  warmup=%ss measure=%ss rounds=%d threads=%d" % (warmup, measure, rounds, threads))
    if client_timeout is None:
        print("  client-timeout=unlimited")
    else:
        print("  client-timeout=%ss" % client_timeout)
    if args.strict:
        print("  strict: abort<=%.2f%% other-fail<=%.2f%% mix±%.0f%% spread-warn<=%.0f%% p99<=%.0fms" % (
            (args.max_abort_rate or 0) * 100, (args.max_other_fail_rate or 0) * 100,
            args.mix_tolerance * 100, args.max_round_spread * 100, args.p99_latency_ms))
        if stress_seed_list and not args.skip_stress:
            print("  stress: %d trials x %.0fs seeds=%s" % (
                len(stress_seed_list), args.stress_seconds, args.stress_seeds))

    proc = None
    cli = None
    proc2 = None
    consistency_ok = None
    crash_ok = None
    round_results = []
    tpms = []
    median_tpm = None
    fail_stage = None
    history_saved = False

    def persist(overall_status, stage=None):
        nonlocal history_saved
        if history_saved:
            return
        payload = build_result_payload(
            tier=tier,
            scale=args.scale,
            warehouses=scale["warehouses"],
            threads=threads,
            seed=args.seed,
            warmup=warmup,
            measure=measure,
            rounds=rounds,
            tpms=tpms,
            median_tpm=median_tpm,
            round_results=round_results,
            overall=overall_status,
            fail_stage=stage,
            consistency_ok=consistency_ok,
            crash_ok=crash_ok,
            extra={
                "protocol": proto_desc,
                "mix": mix_desc,
                "use_batch": use_batch,
            },
        )
        if not args.no_save_history:
            save_bench_history(payload, also_path=args.json)
        elif args.json:
            write_json_result(args.json, payload)
        history_saved = True

    try:
        print("\n-- build check --")
        if not require_release_build(strict=args.strict):
            fail_stage = "build"
            persist("FAIL", fail_stage)
            return 1

        if args.strict and not args.skip_p2:
            kill_rmdb()
            if not run_p2_functional_tests():
                print("OVERALL: FAIL (P2 functional gate)")
                fail_stage = "p2"
                persist("FAIL", fail_stage)
                return 1

        proc, cli = bootstrap_tpcc(db_name, loads=loads, client_timeout=None)

        if args.strict:
            print("\n-- index verify --")
            if not verify_indexes(cli):
                print("OVERALL: FAIL (indexes)")
                fail_stage = "indexes"
                persist("FAIL", fail_stage)
                return 1
            if not verify_output_file_off(db_name):
                print("OVERALL: FAIL (output_file off)")
                fail_stage = "output_file"
                persist("FAIL", fail_stage)
                return 1

        print("\n-- load verify (row counts) --")
        if not verify_load_counts(cli, loads):
            print("OVERALL: FAIL (load counts)")
            fail_stage = "load_counts"
            persist("FAIL", fail_stage)
            return 1

        if args.strict or not args.skip_load_content:
            if not verify_load_content(cli, args.scale, loads, strict=args.strict, db_name=db_name):
                print("OVERALL: FAIL (load content)")
                fail_stage = "load_content"
                persist("FAIL", fail_stage)
                return 1

        before_counts = {}
        for tab, _, _ in loads:
            before_counts[tab] = parse_count(cli.query("select count(*) from %s;" % tab))

        bench_start_o_ids = snapshot_bench_start_o_ids(cli, scale["districts"], scale["warehouses"])
        ytd_baseline = snapshot_ytd_baseline(cli, scale["warehouses"])

        print("\n-- smoke tests (one per txn type) --")
        rng = random.Random(args.seed)
        cli.query("set transaction isolation level snapshot isolation")
        if use_batch:
            install_prepare(cli)
        for txn in ("new_order", "payment", "order_status", "delivery", "stock_level"):
            if use_batch:
                ok, err = run_txn_batch(cli, rng, scale, txn)
            else:
                ok, err = run_txn(cli, rng, scale, txn)
            print("  %s: %s" % (txn, "PASS" if ok else "FAIL " + err))
            if not ok:
                print("OVERALL: FAIL (smoke)")
                fail_stage = "smoke"
                persist("FAIL", fail_stage)
                return 1

        print("\n-- warmup %.0fs --" % warmup)
        bench_round(
            warmup, threads, args.seed, scale, label="warmup",
            client_timeout=client_timeout, use_batch=use_batch, population=TXN_POPULATION,
        )

        for rd in range(1, rounds + 1):
            print("\n-- round %d/%d measure %.0fs --" % (rd, rounds, measure))
            r = bench_round(
                measure, threads, args.seed + rd * 1000, scale, label="round%d" % rd,
                client_timeout=client_timeout, use_batch=use_batch, population=TXN_POPULATION,
            )
            round_results.append(r)

        tpms = [r["tpm"] for r in round_results]
        median_tpm = statistics.median(tpms) if tpms else 0.0
        print("\n=== RESULT ===")
        print("  rounds tpmC:", [("%.2f" % x) for x in tpms])
        print("  median tpmC (OJ metric): %.2f" % median_tpm)

        if median_tpm <= 0:
            print("OVERALL: FAIL (zero tpmC)")
            fail_stage = "zero_tpmc"
            persist("FAIL", fail_stage)
            return 1

        measure_ok = True
        if args.strict or args.mid or args.finals:
            print("\n-- measure window check --")
            measure_ok = check_measure_elapsed(round_results, measure)

        if args.strict:
            print("\n-- strict measure checks --")
            strict_ok = measure_ok
            combined_by_type = {name: {"ok": 0, "fail": 0} for name in TXN_NAMES}
            all_lats = []
            for r in round_results:
                for name, counts in r["by_type"].items():
                    combined_by_type[name]["ok"] += counts["ok"]
                    combined_by_type[name]["fail"] += counts["fail"]
                all_lats.extend(r["new_order_latencies"])
            strict_ok = check_abort_rate(round_results, args.max_abort_rate or 0) and strict_ok
            strict_ok = check_other_fail_rate(round_results, args.max_other_fail_rate or 0) and strict_ok
            strict_ok = check_txn_mix(combined_by_type, args.mix_tolerance) and strict_ok
            if rounds >= 2:
                strict_ok = check_round_stability(tpms, args.max_round_spread) and strict_ok
            strict_ok = check_neworder_latency(all_lats, args.p99_latency_ms) and strict_ok
            if proc.poll() is not None:
                print("  FAIL: rmdb process died during benchmark")
                strict_ok = False
            if not strict_ok:
                print("OVERALL: FAIL (strict measure checks)")
                fail_stage = "strict_measure"
                persist("FAIL", fail_stage)
                return 1

        if proc.poll() is not None:
            print("OVERALL: FAIL (server crashed during benchmark)")
            fail_stage = "server_crash"
            persist("FAIL", fail_stage)
            return 1

        cli.close()
        cli = None

        if not args.skip_consistency:
            cli = RmdbClient(timeout=client_timeout)
            consistency_ok = run_consistency_checks(
                cli, districts=scale["districts"], warehouses=scale["warehouses"],
                before_counts=before_counts, strict=args.strict,
                bench_start_o_ids=bench_start_o_ids, ytd_baseline=ytd_baseline,
            )
            if not consistency_ok:
                print("OVERALL: FAIL (consistency)")
                fail_stage = "consistency"
                persist("FAIL", fail_stage)
                return 1
            cli.close()
            cli = None

        if stress_seed_list and not args.skip_stress and proc.poll() is None:
            if not run_multi_seed_stress(
                seeds=stress_seed_list,
                threads=threads,
                duration_sec=args.stress_seconds,
                scale=scale,
                districts=scale["districts"],
                before_counts=before_counts,
                bench_start_o_ids=bench_start_o_ids,
                strict=args.strict,
                client_timeout=client_timeout,
                max_burst_fail_rate=args.max_burst_fail_rate,
                ytd_baseline=ytd_baseline,
            ):
                print("OVERALL: FAIL (multi-seed stress)")
                fail_stage = "stress"
                persist("FAIL", fail_stage)
                return 1

        if not args.skip_crash and not args.quick and not args.mid and not args.finals:
            ok_crash, proc2 = crash_recovery_post_benchmark(
                proc, db_name, scale["districts"], before_counts, strict=args.strict,
                bench_start_o_ids=bench_start_o_ids, ytd_baseline=ytd_baseline,
                warehouses=scale["warehouses"],
            )
            crash_ok = ok_crash
            proc = None
            if not ok_crash:
                print("OVERALL: FAIL (crash recovery)")
                fail_stage = "crash_recovery"
                persist("FAIL", fail_stage)
                return 1
            if args.strict:
                if not crash_recovery_fresh_load(
                    db_name, scale, loads, scale["districts"], strict=True,
                ):
                    print("OVERALL: FAIL (crash recovery fresh)")
                    fail_stage = "crash_recovery_fresh"
                    persist("FAIL", fail_stage)
                    return 1

        print_oj_summary(
            tier=tier,
            scale=args.scale,
            threads=threads,
            seed=args.seed,
            warmup=warmup,
            measure=measure,
            rounds=rounds,
            tpms=tpms,
            median_tpm=median_tpm,
            round_results=round_results,
            consistency_ok=consistency_ok,
            crash_ok=crash_ok,
            overall_pass=True,
            mix_desc=mix_desc,
            protocol=proto_desc,
        )

        persist("PASS")
        print("OVERALL: PASS")
        return 0
    finally:
        if cli:
            cli.close()
        if proc and proc.poll() is None:
            proc.kill()
        if proc2 and proc2.poll() is None:
            proc2.kill()
        kill_rmdb()


if __name__ == "__main__":
    sys.exit(main())
