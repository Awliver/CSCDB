#!/usr/bin/env python3
"""Reproduce warehouse.w_ytd vs sum(district.d_ytd) drift under concurrent Payment."""
import os
import random
import sys
import threading
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, ".."))

from tpcc_common import bootstrap_tpcc, kill_rmdb, parse_table_rows, RmdbClient, W_ID
from tpcc_scale import scale_profile, loads_for_scale
from tpcc_transactions import run_payment

N_THREADS = 8
N_EACH = 40


def worker(seed, errs):
    rng = random.Random(seed)
    cli = RmdbClient()
    cli.query("set transaction isolation level snapshot isolation")
    for _ in range(N_EACH):
        ok, msg = run_payment(cli, rng, scale_profile("mini"))
        if not ok:
            errs.append(msg)


def ytd_balance(cli):
    w_rows = parse_table_rows(cli.query("select w_ytd from warehouse where w_id = %d;" % W_ID))
    w_ytd = float(w_rows[0][0])
    d_sum = 0.0
    for row in parse_table_rows(cli.query("select d_ytd from district where d_w_id = %d;" % W_ID)):
        d_sum += float(row[0])
    return w_ytd, d_sum


def main():
    kill_rmdb()
    prof = scale_profile("mini")
    bootstrap_tpcc("ytd_race_db", loads=loads_for_scale("mini"))
    cli = RmdbClient()
    w0, d0 = ytd_balance(cli)
    print("initial: w_ytd=%.2f sum(d_ytd)=%.2f diff=%.4f" % (w0, d0, w0 - d0))

    errs = []
    threads = [threading.Thread(target=worker, args=(99 + i, errs)) for i in range(N_THREADS)]
    t0 = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.perf_counter() - t0

    w1, d1 = ytd_balance(cli)
    dw, dd = w1 - w0, d1 - d0
    print("after %d payments (%d threads, %.1fs): w_ytd=%.4f sum(d)=%.4f"
          % (N_THREADS * N_EACH, N_THREADS, elapsed, w1, d1))
    print("  warehouse +%.4f  district +%.4f  delta diff=%.4f" % (dw, dd, dw - dd))
    if errs:
        print("errors (%d):" % len(errs), errs[:5])
    if abs(dw - dd) > 1.0:
        print("FAIL: payment ytd delta mismatch")
        return 1
    print("PASS (payment deltas match; load baseline offset preserved)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
