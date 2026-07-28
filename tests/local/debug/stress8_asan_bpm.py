#!/usr/bin/env python3
"""Reproduce the 8-thread BPM crash against the ASAN build, mini scale."""
import os
import sys
import time

os.environ["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1:halt_on_error=1:print_stacktrace=1"
os.environ["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import tpcc_common
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
tpcc_common.RMDB = os.path.join(ROOT, "build_asan", "bin", "rmdb")

from tpcc_common import start_rmdb, kill_rmdb, RmdbClient, SCHEMA, INDEXES, BUILD
from tpcc_scale import loads_for_scale, tpcc_runtime_scale
from bench_tpcc import bench_round

LOG = os.path.join(BUILD, "tpcc_crash", "server.log")


def main():
    kill_rmdb()
    proc, _ = start_rmdb("tpcc_crash", log_path=LOG)
    print("pid", proc.pid, "binary", tpcc_common.RMDB, flush=True)
    cli = RmdbClient()
    for sql in SCHEMA:
        cli.query(sql)
    for tab, path, _ in loads_for_scale("mini"):
        cli.query("load %s into %s;" % (path, tab))
    for sql in INDEXES:
        cli.query(sql)
    cli.query("set transaction isolation level snapshot isolation")
    cli.query("set output_file off")
    cli.close()
    scale = tpcc_runtime_scale("mini")
    for r in range(1, 6):
        bench_round(12, 8, r, scale, "stress")
        alive = proc.poll()
        print("round %d done, server poll=%s" % (r, alive), flush=True)
        if alive is not None:
            print("SERVER DIED after round", r, flush=True)
            break
    time.sleep(0.5)
    print("=== server.log tail ===", flush=True)
    if os.path.isfile(LOG):
        with open(LOG, errors="replace") as f:
            data = f.read()
        print(data[-8000:], flush=True)


if __name__ == "__main__":
    main()
