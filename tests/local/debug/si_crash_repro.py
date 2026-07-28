#!/usr/bin/env python3
import os
import random
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
BUILD = os.path.join(ROOT, "build")
sys.path.insert(0, os.path.join(ROOT, "tests", "local"))

from tpcc_common import INDEXES, RmdbClient, SCHEMA, kill_rmdb
from tpcc_scale import loads_for_scale
from tpcc_transactions import run_neworder

DB = "crash_db"
PORT = 8765


def main():
    subprocess.run(["pkill", "-9", "-f", "bin/rmdb"], stderr=subprocess.DEVNULL)
    time.sleep(0.5)
    log_path = os.path.join(BUILD, "crash_server.log")
    proc = subprocess.Popen(
        [os.path.join(BUILD, "bin/rmdb"), DB],
        cwd=BUILD,
        stdout=open(log_path, "w"),
        stderr=subprocess.STDOUT,
    )
    time.sleep(2)

    loads = list(loads_for_scale("mini"))
    cli = RmdbClient()
    cli.query("set transaction isolation level snapshot isolation")
    for sql in SCHEMA:
        cli.query(sql)
    for tab, path, _ in loads:
        cli.query("load %s into %s;" % (path, tab))
    for sql in INDEXES:
        cli.query(sql)
    cli.close()

    def worker(seed):
        rng = random.Random(seed)
        c = RmdbClient()
        c.query("set transaction isolation level snapshot isolation")
        scale = {
            "districts": 3,
            "customers_per_district": 10,
            "items": 10,
            "min_ol_cnt": 5,
            "max_ol_cnt": 15,
        }
        for _ in range(120):
            run_neworder(c, rng, scale)
        c.close()

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    alive = proc.poll() is None
    print("server_alive:", alive)
    if os.path.isfile(log_path):
        with open(log_path) as f:
            tail = f.readlines()[-40:]
        print("".join(tail))
    proc.kill()
    kill_rmdb()
    return 0 if alive else 1


if __name__ == "__main__":
    sys.exit(main())
