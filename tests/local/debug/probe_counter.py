#!/usr/bin/env python3
"""Trace district counter across 3 workload txns, printing every read."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bench"))

from tpccbench.db import Client, parse_rows  # noqa: E402
from tpccbench.workload import Config, new_order  # noqa: E402
from tpccbench.tpcrand import TpccRandom  # noqa: E402
from tpccbench import cli as tcli  # noqa: E402

DB = "probe5_db"
args = type("A", (), {"scale": "mini", "warehouses": 1, "seed": 42,
                      "data": None, "db": DB, "isolation": "si"})()
server = tcli.cmd_load(args, keep_server=True)

cli = Client(timeout=60)
cli.query("set transaction isolation level snapshot isolation")
cli.query("set output_file off")
cfg = Config(warehouses=1, items=1000, districts=3, customers=50)
r = TpccRandom(7)

def counter():
    return int(float(parse_rows(cli.query(
        "select d_next_o_id from district where d_w_id=1 and d_id=1;"))[0][0]))

for t in (1, 2, 3):
    print("before txn%d: counter=%d" % (t, counter()))
    st, det = new_order(cli, r, cfg, 1, d_id=1, force_rollback=False)
    print("txn%d -> %s %s" % (t, st, det))
    print("after txn%d: counter=%d" % (t, counter()))

cli.close()
server.stop()
