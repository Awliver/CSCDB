#!/usr/bin/env python3
"""Bootstrap DB then run benchmark while gdb-attached server runs."""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../.."))
BUILD = os.path.join(ROOT, "build")

from tpcc_common import kill_rmdb, RmdbClient, SCHEMA, INDEXES, RMDB
from tpcc_scale import loads_for_scale, tpcc_runtime_scale
from bench_tpcc import bench_round

DB = "tpcc_gdb"
LOG = os.path.join(BUILD, DB, "gdb.log")


def main():
    kill_rmdb()
    dbpath = os.path.join(BUILD, DB)
    if os.path.isdir(dbpath):
        import shutil
        shutil.rmtree(dbpath)
    os.makedirs(dbpath, exist_ok=True)

    gdb_cmds = (
        "set pagination off\n"
        "handle SIGPIPE nostop pass\n"
        "run %s\n"
        "thread apply all bt\n"
    ) % DB
    with open(os.path.join(BUILD, "gdb_cmds.txt"), "w") as f:
        f.write(gdb_cmds)

    logf = open(LOG, "w")
    proc = subprocess.Popen(
        ["gdb", "-batch", "-x", os.path.join(BUILD, "gdb_cmds.txt"), "--args", RMDB, DB],
        cwd=BUILD,
        stdout=logf,
        stderr=subprocess.STDOUT,
    )
    time.sleep(3)
    if proc.poll() is not None:
        logf.flush()
        print("server failed to start")
        print(open(LOG).read()[-2000:])
        return 1

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
    bench_round(60, 8, 2, scale, "measure")

    time.sleep(2)
    proc.poll()
    logf.flush()
    logf.close()
    kill_rmdb()

    text = open(LOG).read()
    idx = text.find("Program received signal")
    if idx >= 0:
        print(text[idx:idx + 4000])
    else:
        print("no signal in log; tail:")
        print(text[-3000:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
