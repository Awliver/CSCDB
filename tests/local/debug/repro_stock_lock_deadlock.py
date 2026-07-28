#!/usr/bin/env python3
"""
Reproduce concurrent NewOrder stock row-lock hang / deadlock.

Setup (mini CSV, 10 stock SKUs):
  - Tx A: district d_id=1, stock updates in order  i_id = 1,2,3,4,5
  - Tx B: district d_id=2, stock updates in order  i_id = 5,4,3,2,1

Both pass district update (different rows), then lock overlapping stock in
opposite order → classic wait cycle on LockManager::lock_exclusive_on_record.

Expected with 2 threads + SI (WFG active):
  - One txn commits, the other aborts on conflicting stock lock (~seconds, no hang)
  - Re-run with pre-WFG binary to see 600s client timeout + rmdb stuck in cv.wait

Usage:
  cd /path/to/db2026
  python3 tests/local/debug/repro_stock_lock_deadlock.py
  python3 tests/local/debug/repro_stock_lock_deadlock.py --timeout 15
  python3 tests/local/debug/repro_stock_lock_deadlock.py --control   # 1 thread, should PASS

GDB (while hung):
  gdb -p $(pgrep -f 'bin/rmdb')
  (gdb) thread apply all bt
  # look for LockManager::lock_exclusive_on_record → pthread_cond_wait
"""

import argparse
import os
import sys
import threading
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_LOCAL = os.path.abspath(os.path.join(_HERE, ".."))
if _LOCAL not in sys.path:
    sys.path.insert(0, _LOCAL)

from tpcc_common import (  # noqa: E402
    ENTRY_D,
    W_ID,
    RmdbClient,
    bootstrap_tpcc,
    kill_rmdb,
    parse_table_rows,
)

# Deterministic lock order: forward vs reverse on items 1..5
ITEMS_FWD = [1, 2, 3, 4, 5]
ITEMS_REV = [5, 4, 3, 2, 1]


def run_neworder_fixed_items(cli, d_id, c_id, item_ids, qty=5):
    """NewOrder with explicit per-line i_id (triggers row lock in lock order)."""
    ol_cnt = len(item_ids)
    dist_col = "s_dist_%02d" % d_id

    ok, r = cli.query_ok("begin;")
    if not ok:
        return False, "begin: " + r[:120], "begin"

    r = cli.query(
        "select d_tax, d_next_o_id from district where d_w_id = %d and d_id = %d;"
        % (W_ID, d_id)
    )
    rows = parse_table_rows(r)
    if not rows:
        cli.query("abort;")
        return False, "district select empty", "select_district"

    d_next_o_id = int(float(rows[0][1]))
    ok, r = cli.query_ok(
        "update district set d_next_o_id = %d where d_w_id = %d and d_id = %d;"
        % (d_next_o_id + 1, W_ID, d_id)
    )
    if not ok:
        cli.query("abort;")
        return False, "update district: " + r[:120], "update_district"

    r = cli.query(
        "select d_next_o_id from district where d_w_id = %d and d_id = %d;"
        % (W_ID, d_id)
    )
    rows = parse_table_rows(r)
    if not rows:
        cli.query("abort;")
        return False, "district re-read empty", "select_district"
    o_id = int(float(rows[0][0])) - 1

    ok, r = cli.query_ok(
        "insert into orders values (%d, %d, %d, %d, '%s', 0, %d, 1);"
        % (o_id, d_id, W_ID, c_id, ENTRY_D, ol_cnt)
    )
    if not ok:
        cli.query("abort;")
        return False, "insert orders: " + r[:120], "insert_orders"

    ok, r = cli.query_ok(
        "insert into new_orders values (%d, %d, %d);" % (o_id, d_id, W_ID)
    )
    if not ok:
        cli.query("abort;")
        return False, "insert new_orders: " + r[:120], "insert_new_orders"

    for ol_no, i_id in enumerate(item_ids, start=1):
        r = cli.query("select i_price from item where i_id = %d;" % i_id)
        rows = parse_table_rows(r)
        if not rows:
            cli.query("abort;")
            return False, "item %d missing" % i_id, "select_item"

        i_price = float(rows[0][0])
        r = cli.query(
            "select s_quantity, %s, s_ytd, s_order_cnt, s_remote_cnt from stock "
            "where s_w_id = %d and s_i_id = %d;" % (dist_col, W_ID, i_id)
        )
        rows = parse_table_rows(r)
        if not rows:
            cli.query("abort;")
            return False, "stock %d missing" % i_id, "select_stock"

        s_qty = int(float(rows[0][0]))
        dist_info = rows[0][1]
        s_ytd = float(rows[0][2])
        s_order_cnt = int(float(rows[0][3]))
        s_remote_cnt = int(float(rows[0][4]))

        new_qty = s_qty - qty if s_qty >= qty + 10 else s_qty - qty + 91
        new_ytd = s_ytd + qty * i_price
        new_order_cnt = s_order_cnt + 1
        ol_amount = qty * i_price

        ok, r = cli.query_ok(
            "update stock set s_quantity = %d, s_ytd = %s, s_order_cnt = %d, "
            "s_remote_cnt = %d where s_w_id = %d and s_i_id = %d;"
            % (new_qty, new_ytd, new_order_cnt, s_remote_cnt, W_ID, i_id)
        )
        if not ok:
            cli.query("abort;")
            return False, "update stock i=%d: " % i_id + r[:120], "update_stock"

        ok, r = cli.query_ok(
            "insert into order_line values (%d, %d, %d, %d, %d, %d, '%s', %d, %s, '%s');"
            % (o_id, d_id, W_ID, ol_no, i_id, W_ID, ENTRY_D, qty, ol_amount, dist_info)
        )
        if not ok:
            cli.query("abort;")
            return False, "insert order_line: " + r[:120], "insert_order_line"

    ok, r = cli.query_ok("commit;")
    if not ok:
        cli.query("abort;")
        return False, "commit: " + r[:120], "commit"
    return True, "", "done"


class WorkerResult:
    __slots__ = ("name", "ok", "err", "step", "elapsed", "exc")

    def __init__(self, name):
        self.name = name
        self.ok = False
        self.err = ""
        self.step = ""
        self.elapsed = 0.0
        self.exc = None


def worker(name, d_id, item_ids, timeout, barrier, result):
    cli = RmdbClient(timeout=timeout)
    try:
        barrier.wait(timeout=timeout)
        t0 = time.perf_counter()
        ok, err, step = run_neworder_fixed_items(cli, d_id=d_id, c_id=1, item_ids=item_ids)
        result.elapsed = time.perf_counter() - t0
        result.ok = ok
        result.err = err
        result.step = step
    except Exception as e:
        result.exc = e
        result.err = str(e)
    finally:
        cli.close()


def main():
    ap = argparse.ArgumentParser(description="Reproduce NewOrder stock row-lock deadlock")
    ap.add_argument("--timeout", type=float, default=30.0,
                    help="per-client socket timeout seconds (default 30)")
    ap.add_argument("--control", action="store_true",
                    help="single-thread control (should finish quickly)")
    ap.add_argument("--keep-server", action="store_true",
                    help="leave rmdb running on hang for GDB")
    args = ap.parse_args()

    db_name = "repro_deadlock_db"
    print("=== stock row-lock deadlock repro ===")
    print("  mini data, SI, 2 concurrent NewOrders")
    print("  Tx A: district 1, stock i_id %s" % ITEMS_FWD)
    print("  Tx B: district 2, stock i_id %s" % ITEMS_REV)
    print("  client timeout=%.0fs" % args.timeout)

    proc = None
    try:
        proc, cli = bootstrap_tpcc(db_name, client_timeout=int(args.timeout) + 5)
        cli.close()

        if args.control:
            print("\n-- control (1 thread) --")
            cli = RmdbClient(timeout=args.timeout)
            t0 = time.perf_counter()
            ok, err, step = run_neworder_fixed_items(cli, d_id=1, c_id=1, item_ids=ITEMS_FWD)
            elapsed = time.perf_counter() - t0
            cli.close()
            if ok:
                print("  PASS in %.2fs (step=%s)" % (elapsed, step))
                return 0
            print("  FAIL at %s: %s" % (step, err))
            return 1

        barrier = threading.Barrier(2)
        ra, rb = WorkerResult("TxA"), WorkerResult("TxB")
        ta = threading.Thread(
            target=worker,
            args=("TxA", 1, ITEMS_FWD, args.timeout, barrier, ra),
            name="TxA",
        )
        tb = threading.Thread(
            target=worker,
            args=("TxB", 2, ITEMS_REV, args.timeout, barrier, rb),
            name="TxB",
        )
        print("\n-- launching 2 workers (barrier-synced begin) --")
        t0 = time.perf_counter()
        ta.start()
        tb.start()
        ta.join(timeout=args.timeout + 10)
        tb.join(timeout=args.timeout + 10)
        wall = time.perf_counter() - t0

        def report(r):
            if r.exc:
                kind = type(r.exc).__name__
                if "timed out" in r.err.lower() or kind == "TimeoutError":
                    return "TIMEOUT/HANG at ~%s" % (r.step or "?")
                return "ERROR (%s): %s" % (kind, r.err[:100])
            if r.ok:
                return "OK in %.2fs" % r.elapsed
            return "FAIL at %s: %s" % (r.step, r.err[:100])

        print("\n-- results (wall %.1fs) --" % wall)
        print("  TxA:", report(ra))
        print("  TxB:", report(rb))

        server_alive = proc.poll() is None
        both_hung = (
            (ra.exc and "timed out" in str(ra.exc).lower())
            or (rb.exc and "timed out" in str(rb.exc).lower())
            or (not ra.ok and not rb.ok and wall >= args.timeout * 0.9)
        )

        if both_hung and server_alive:
            print("\n>>> LIKELY DEADLOCK / INDEFINITE LOCK WAIT (WFG not active?)")
            print("    rmdb still alive (pid %s)" % proc.pid)
            print("    LockManager WFG inactive; threads may wait forever in cv.wait")
            print("\n    GDB:")
            print("      gdb -p $(pgrep -f 'bin/rmdb')")
            print("      (gdb) thread apply all bt")
            print("      # expect lock_exclusive_on_record → pthread_cond_wait")
            if not args.keep_server:
                print("\n    (use --keep-server to leave rmdb up for inspection)")
            return 2

        if ra.ok ^ rb.ok:
            print("\n>>> WFG: one commit, one abort (expected)")
            return 0

        if ra.ok and rb.ok:
            print("\n>>> UNEXPECTED: both committed (no hang this run)")
            print("    retry or check if row locks disabled (single active txn?)")
            return 1

        print("\n>>> inconclusive (partial failure, not classic dual-timeout hang)")
        return 1

    finally:
        if proc is not None and proc.poll() is None and not args.keep_server:
            proc.kill()
            kill_rmdb()


if __name__ == "__main__":
    sys.exit(main())
