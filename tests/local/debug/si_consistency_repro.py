#!/usr/bin/env python3
"""SI concurrent TPC-C burst + strict consistency (OJ Phase 3 style)."""
import os
import random
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
BUILD = os.path.join(ROOT, "build")
sys.path.insert(0, os.path.join(ROOT, "tests", "local"))

from tpcc_common import bootstrap_tpcc, kill_rmdb, parse_count, RmdbClient
from tpcc_scale import ensure_full_data, loads_for_scale, scale_profile
from tpcc_transactions import pick_txn, run_txn, run_neworder
from tpcc_consistency import run_consistency_checks

DB = "si_consistency_db"


def main():
    subprocess.run(["pkill", "-9", "-f", "bin/rmdb"], stderr=subprocess.DEVNULL)
    time.sleep(0.5)
    scale = scale_profile("full")
    loads = list(loads_for_scale("full"))
    ensure_full_data("full")
    proc, cli = bootstrap_tpcc(DB, loads=loads)
    before = {
        t: parse_count(cli.query("select count(*) from %s;" % t))
        for t in ("orders", "order_line", "history")
    }
    cli.close()

    def worker(duration, seed, stop):
        rng = random.Random(seed)
        c = RmdbClient()
        c.query("set transaction isolation level snapshot isolation")
        end = time.perf_counter() + duration
        while time.perf_counter() < end and not stop.is_set():
            name, _ = pick_txn(rng)
            if name == "new_order":
                run_neworder(c, rng, scale)
            else:
                run_txn(c, rng, scale, name)
        c.close()

    stop = threading.Event()
    threads = [
        threading.Thread(target=worker, args=(90, i * 1009, stop), daemon=True)
        for i in range(8)
    ]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    alive = proc.poll() is None
    print("server_alive:", alive)
    if not alive:
        proc.kill()
        kill_rmdb()
        return 1

    c2 = RmdbClient()
    c2.query("set transaction isolation level snapshot isolation")
    ok = run_consistency_checks(
        c2, districts=scale["districts"], before_counts=before, strict=True
    )
    c2.close()
    proc.kill()
    kill_rmdb()
    print("STRICT_CONSISTENCY:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
