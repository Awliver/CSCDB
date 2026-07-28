#!/usr/bin/env python3
"""Reproduce 8-thread crash with ASAN server logging."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tpcc_common import start_rmdb, kill_rmdb, RmdbClient, SCHEMA, INDEXES, BUILD
from tpcc_scale import loads_for_scale, tpcc_runtime_scale
from bench_tpcc import bench_round

LOG = os.path.join(BUILD, "tpcc_crash", "server.log")


def main():
    kill_rmdb()
    proc, _ = start_rmdb("tpcc_crash", log_path=LOG)
    print("pid", proc.pid, flush=True)
    cli = RmdbClient()
    for sql in SCHEMA:
        cli.query(sql)
    for tab, path, _ in loads_for_scale("full"):
        cli.query("load %s into %s;" % (path, tab))
    for sql in INDEXES:
        cli.query(sql)
    cli.query("set transaction isolation level snapshot isolation")
    cli.query("set output_file off")
    cli.close()
    scale = tpcc_runtime_scale("full")
    bench_round(30, 8, 1, scale, "warmup")
    print("warmup done, server", proc.poll(), flush=True)
    bench_round(90, 8, 2, scale, "measure")
    print("measure done, server", proc.poll(), flush=True)


if __name__ == "__main__":
    main()
