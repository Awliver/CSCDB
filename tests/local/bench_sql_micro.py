#!/usr/bin/env python3
"""
SQL-level micro-benchmarks for RMDB performance tuning.

Unlike tpmC benches (CPU + concurrency dominated), these isolate single-statement
or small patterns to expose:
  - index point-read vs range-scan cost
  - update / insert / commit (WAL + fdatasync path)
  - aggregate + join (Stock-Level style)
  - cold-cache vs warm-cache gap (buffer pool / disk I/O)

Examples:
  # All categories, full data (default), warm cache
  python3 tests/local/bench_sql_micro.py

  # Quick iteration on mini CSV
  python3 tests/local/bench_sql_micro.py --scale mini --category stock_pk_read

  # Commit latency after hot-row update (WAL sensitive)
  python3 tests/local/bench_sql_micro.py --category commit_hot_update --iterations 50

  # Cold start: restart server before each iteration (slow, shows I/O)
  python3 tests/local/bench_sql_micro.py --cold --category district_pk_read --iterations 10

  # 8 threads hammering warehouse row (lock / MVCC contention)
  python3 tests/local/bench_sql_micro.py --category hot_row_contention --threads 8
"""

import argparse
import sys
import threading

from bench_timing import LatStats, print_stats_table, run_timed_loop
from tpcc_common import (
    ENTRY_D,
    RmdbClient,
    TPCC_SCALE,
    W_ID,
    bootstrap_tpcc,
    kill_rmdb,
)
from tpcc_scale import ensure_full_data, loads_for_scale, tpcc_runtime_scale

# ---------------------------------------------------------------------------
# SQL workload catalog
# ---------------------------------------------------------------------------

def _mini_params():
    return {"d_id": 1, "c_id": 1, "i_id": 1, "o_id": 1, "ol_no": 1}


def _full_params(scale):
  return {
      "d_id": 1,
      "c_id": 1,
      "i_id": scale["items"] // 2,
      "o_id": scale.get("orders_per_district", 3000) // 2,
      "ol_no": 5,
  }


def build_sql_catalog(scale):
    p = _full_params(scale) if scale.get("warehouses", 1) > 1 or scale["items"] > 100 else _mini_params()
    d_id, c_id, i_id, o_id = p["d_id"], p["c_id"], p["i_id"], p["o_id"]
    dist_col = "s_dist_%02d" % d_id
    amount = 12.34

    return {
        "district_pk_read": (
            "Point read district (PK index)",
            lambda cli: cli.query(
                "select d_tax, d_next_o_id from district where d_w_id = %d and d_id = %d;"
                % (W_ID, d_id)
            ),
        ),
        "warehouse_pk_read": (
            "Point read warehouse",
            lambda cli: cli.query("select w_tax, w_ytd from warehouse where w_id = %d;" % W_ID),
        ),
        "stock_pk_read": (
            "Point read stock (NewOrder hot path)",
            lambda cli: cli.query(
                "select s_quantity, %s, s_ytd, s_order_cnt, s_remote_cnt from stock "
                "where s_w_id = %d and s_i_id = %d;" % (dist_col, W_ID, i_id)
            ),
        ),
        "item_pk_read": (
            "Point read item",
            lambda cli: cli.query("select i_price from item where i_id = %d;" % i_id),
        ),
        "customer_pk_read": (
            "Point read customer (Payment)",
            lambda cli: cli.query(
                "select c_balance, c_ytd_payment, c_payment_cnt, c_data from customer "
                "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (W_ID, d_id, c_id)
            ),
        ),
        "orders_idx_scan": (
            "Index scan orders DESC LIMIT 1 (Order-Status)",
            lambda cli: cli.query(
                "select o_id, o_entry_d, o_carrier_id from orders "
                "where o_w_id = %d and o_d_id = %d and o_c_id = %d order by o_id desc limit 1;"
                % (W_ID, d_id, c_id)
            ),
        ),
        "order_line_idx_scan": (
            "Index range scan order_line by o_id",
            lambda cli: cli.query(
                "select ol_i_id, ol_quantity, ol_amount from order_line "
                "where ol_w_id = %d and ol_d_id = %d and ol_o_id = %d;" % (W_ID, d_id, o_id)
            ),
        ),
        "stock_level_join_agg": (
            "Join + aggregate (Stock-Level)",
            lambda cli: cli.query(
                "select count(*) from order_line, stock where ol_w_id = %d and ol_d_id = %d "
                "and ol_o_id >= 1 and ol_o_id <= %d and s_w_id = %d and s_i_id = ol_i_id "
                "and s_quantity < 20;" % (W_ID, d_id, min(o_id + 19, o_id), W_ID)
            ),
        ),
        "district_update_no_commit": (
            "Update district in txn, rollback (no WAL flush)",
            lambda cli: _txn_update_rollback(
                cli,
                "update district set d_next_o_id = d_next_o_id + 1 "
                "where d_w_id = %d and d_id = %d;" % (W_ID, d_id),
            ),
        ),
        "stock_update_no_commit": (
            "Update stock in txn, rollback",
            lambda cli: _txn_update_rollback(
                cli,
                "update stock set s_order_cnt = s_order_cnt + 1 "
                "where s_w_id = %d and s_i_id = %d;" % (W_ID, i_id),
            ),
        ),
        "commit_hot_update": (
            "begin + update warehouse + commit (WAL / group-commit path)",
            lambda cli: _txn_update_commit(
                cli,
                "update warehouse set w_ytd = w_ytd + %s where w_id = %d;" % (amount, W_ID),
            ),
        ),
        "commit_district_update": (
            "begin + update district + commit",
            lambda cli: _txn_update_commit(
                cli,
                "update district set d_ytd = d_ytd + %s where d_w_id = %d and d_id = %d;"
                % (amount, W_ID, d_id),
            ),
        ),
        "commit_stock_update": (
            "begin + update stock + commit (per-line NewOrder cost)",
            lambda cli: _txn_update_commit(
                cli,
                "update stock set s_order_cnt = s_order_cnt + 1 "
                "where s_w_id = %d and s_i_id = %d;" % (W_ID, i_id),
            ),
        ),
        "insert_history_commit": (
            "begin + insert history + commit (Payment tail)",
            lambda cli: _txn_insert_history_commit(cli, d_id, c_id),
        ),
        "stock_seq_scan": (
            "Full stock table scan (buffer pool cold indicator)",
            lambda cli: cli.query(
                "select count(*) from stock where s_w_id = %d and s_quantity < 50;" % W_ID
            ),
        ),
        "item_seq_scan": (
            "Full item table scan",
            lambda cli: cli.query("select count(*) from item where i_price > 1.0;"),
        ),
    }


def _txn_update_rollback(cli, update_sql):
    cli.query("begin;")
    cli.query(update_sql)
    cli.query("abort;")


def _txn_update_commit(cli, update_sql):
    cli.query("begin;")
    cli.query(update_sql)
    cli.query("commit;")


def _txn_insert_history_commit(cli, d_id, c_id):
    cli.query("begin;")
    cli.query(
        "insert into history values (%d, %d, %d, %d, %d, '%s', 1.0, 'bench');"
        % (c_id, d_id, W_ID, d_id, W_ID, ENTRY_D)
    )
    cli.query("commit;")


# ---------------------------------------------------------------------------
# Benchmark runners
# ---------------------------------------------------------------------------

def bench_category(cli, name, fn, warmup, iterations):
    stats = run_timed_loop(lambda: fn(cli), warmup, iterations, name)
    return stats


def bench_cold_category(db_name, name, fn, iterations, scale_name, with_indexes):
    """Restart server before each iteration to empty buffer pool."""
    stats = LatStats(name + "_cold")
    loads = loads_for_scale(scale_name) if scale_name == "full" else None
    for i in range(iterations):
        proc, cli = bootstrap_tpcc(
            db_name + "_cold_%d" % i,
            with_indexes=with_indexes,
            loads=loads,
            client_timeout=300,
        )
        try:
            cli.query("set transaction isolation level snapshot isolation")
            t0 = time.perf_counter()
            fn(cli)
            stats.add(time.perf_counter() - t0)
        finally:
            cli.close()
            proc.kill()
            kill_rmdb()
    return stats


def bench_warm_vs_cold(cli, db_name, name, fn, scale_name, with_indexes):
    """Compare first iteration (cold) vs mean of next N (warm) on same server."""
    cold = LatStats(name + "_1st_iter")
    warm = LatStats(name + "_warm_mean")
    t0 = time.perf_counter()
    fn(cli)
    cold.add(time.perf_counter() - t0)
    samples = []
    for _ in range(20):
        t0 = time.perf_counter()
        fn(cli)
        samples.append(time.perf_counter() - t0)
    for s in samples:
        warm.add(s)
    return cold, warm


def hot_row_contention(db_name, threads, iterations_per_thread, scale_name):
    """N connections each: begin / update warehouse / commit."""
    loads = loads_for_scale(scale_name) if scale_name == "full" else None
    proc, _ = bootstrap_tpcc(db_name, loads=loads, client_timeout=120)
    barrier = threading.Barrier(threads)
    results = []
    lock = threading.Lock()
    amount = 1.0

    def worker(tid):
        try:
            cli = RmdbClient(timeout=120)
            cli.query("set transaction isolation level snapshot isolation")
            local = LatStats("thread_%d" % tid)
            barrier.wait()
            for _ in range(iterations_per_thread):
                t0 = time.perf_counter()
                cli.query("begin;")
                cli.query(
                    "update warehouse set w_ytd = w_ytd + %s where w_id = %d;"
                    % (amount, W_ID)
                )
                cli.query("commit;")
                local.add(time.perf_counter() - t0)
            cli.close()
            with lock:
                results.append(local)
        except (RuntimeError, OSError) as e:
            with lock:
                results.append(LatStats("thread_%d_err" % tid))
                print("  thread %d error: %s" % (tid, e))

    try:
        ts = []
        for i in range(threads):
            t = threading.Thread(target=worker, args=(i,), daemon=True)
            t.start()
            ts.append(t)
        for t in ts:
            t.join()
    finally:
        proc.kill()
        kill_rmdb()

    merged = LatStats("hot_row_contention_total")
    for r in results:
        merged.merge(r)
    return results, merged


def evict_then_measure(cli, db_name, name, fn, scale):
    """Scan large tables to evict cache, then measure target query."""
    cold = LatStats(name + "_after_evict")
    # Evict: touch item + stock fully
    cli.query("select count(*) from item;")
    cli.query("select count(*) from stock;")
    cli.query("select count(*) from order_line;")
    t0 = time.perf_counter()
    fn(cli)
    cold.add(time.perf_counter() - t0)
    warm = LatStats(name + "_after_evict_warm")
    for _ in range(10):
        t0 = time.perf_counter()
        fn(cli)
        warm.add(time.perf_counter() - t0)
    return cold, warm


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

CATEGORY_GROUPS = {
    "read": [
        "district_pk_read",
        "warehouse_pk_read",
        "stock_pk_read",
        "item_pk_read",
        "customer_pk_read",
        "orders_idx_scan",
        "order_line_idx_scan",
    ],
    "write": [
        "district_update_no_commit",
        "stock_update_no_commit",
        "commit_hot_update",
        "commit_district_update",
        "commit_stock_update",
        "insert_history_commit",
    ],
    "scan": ["stock_seq_scan", "item_seq_scan", "stock_level_join_agg"],
    "io": ["commit_hot_update", "commit_stock_update", "stock_seq_scan"],
}


def resolve_categories(catalog, category_arg):
    if category_arg == "all":
        return list(catalog.keys())
    if category_arg in CATEGORY_GROUPS:
        return [c for c in CATEGORY_GROUPS[category_arg] if c in catalog]
    if category_arg in catalog:
        return [category_arg]
    raise SystemExit(
        "unknown --category %r; choose from: all, read, write, scan, io, hot_row_contention, %s"
        % (category_arg, ", ".join(sorted(catalog.keys())))
    )


def main():
    ap = argparse.ArgumentParser(description="SQL micro-benchmarks for RMDB")
    ap.add_argument("--scale", choices=["mini", "full"], default="full")
    ap.add_argument(
        "--category",
        default="all",
        help="all | read | write | scan | io | <name>",
    )
    ap.add_argument("--warmup", type=int, default=30, help="warmup iterations")
    ap.add_argument("--iterations", type=int, default=100, help="measured iterations")
    ap.add_argument(
        "--cold",
        action="store_true",
        help="restart DB before each iteration (buffer pool cold)",
    )
    ap.add_argument(
        "--warm-cold-compare",
        action="store_true",
        help="report 1st iteration vs warm mean on same server",
    )
    ap.add_argument(
        "--evict-then-run",
        action="store_true",
        help="full-scan evict before measuring (simulates cache miss)",
    )
    ap.add_argument(
        "--threads",
        type=int,
        default=1,
        help="for hot_row_contention category",
    )
    args = ap.parse_args()

    if args.scale == "full":
        ensure_full_data()
        loads = loads_for_scale("full")
        scale = tpcc_runtime_scale("full")
        db_name = "sql_micro_full_db"
    else:
        loads = None
        scale = dict(TPCC_SCALE)
        db_name = "sql_micro_mini_db"

    print("=== SQL micro-benchmark ===")
    print("  scale=%s warmup=%d iterations=%d" % (args.scale, args.warmup, args.iterations))
    catalog = build_sql_catalog(scale)

    if args.category == "hot_row_contention":
        print("  mode=hot_row_contention threads=%d" % args.threads)
        per_thread = max(1, args.iterations // args.threads)
        per_thread_stats, merged = hot_row_contention(
            db_name, args.threads, per_thread, args.scale
        )
        print_stats_table(
            "hot_row_contention (begin+update warehouse+commit)",
            [merged] + per_thread_stats,
        )
        return 0

    cats = resolve_categories(catalog, args.category)

    proc, cli = bootstrap_tpcc(db_name, loads=loads, client_timeout=300)
    cli.query("set transaction isolation level snapshot isolation")

    all_stats = []
    try:
        for cat in cats:
            title, fn = catalog[cat]
            print("\n-- %s (%s) --" % (cat, title))
            if args.cold:
                st = bench_cold_category(
                    db_name, cat, fn, args.iterations, args.scale, True
                )
                all_stats.append(st)
                print(st.format_line())
            elif args.warm_cold_compare:
                c, w = bench_warm_vs_cold(cli, db_name, cat, fn, args.scale, True)
                all_stats.extend([c, w])
                print(c.format_line())
                print(w.format_line())
                cs, ws = c.summary_ms(), w.summary_ms()
                if ws["mean"] > 0:
                    ratio = cs["mean"] / ws["mean"]
                    print("  cold/warm mean ratio: %.2fx" % ratio)
            elif args.evict_then_run:
                c, w = evict_then_measure(cli, db_name, cat, fn, scale)
                all_stats.extend([c, w])
                print(c.format_line())
                print(w.format_line())
            else:
                st = bench_category(cli, cat, fn, args.warmup, args.iterations)
                all_stats.append(st)
                print(st.format_line())

        if len(all_stats) > 1 and not args.cold:
            print_stats_table("summary (%s)" % args.category, all_stats)

        print("\nOVERALL: PASS")
        return 0
    except Exception as e:
        print("OVERALL: FAIL —", e)
        return 1
    finally:
        cli.close()
        proc.kill()
        kill_rmdb()


if __name__ == "__main__":
    sys.exit(main())
