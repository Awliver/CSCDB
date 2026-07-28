#!/usr/bin/env python3
"""Client-only 8-thread stress against an already-running server on PORT 8765.
Used together with a gdb-wrapped server to catch the crash stack."""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tpcc_common import RmdbClient, SCHEMA, INDEXES
from tpcc_scale import loads_for_scale, tpcc_runtime_scale
from bench_tpcc import bench_round


def main():
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
    print("bootstrap done", flush=True)
    scale = tpcc_runtime_scale("mini")
    for r in range(1, 11):
        try:
            bench_round(8, 8, r, scale, "stress")
        except Exception as e:
            print("round %d EXC %r" % (r, e), flush=True)
            break
        print("round %d done" % r, flush=True)


if __name__ == "__main__":
    main()
