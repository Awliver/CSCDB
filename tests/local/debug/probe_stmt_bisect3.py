#!/usr/bin/env python3
"""Monkeypatch Client.query to probe after every real workload statement."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bench"))

from tpccbench.db import Client, parse_rows  # noqa: E402
from tpccbench.workload import Config, new_order  # noqa: E402
from tpccbench.tpcrand import TpccRandom  # noqa: E402
from tpccbench import cli as tcli  # noqa: E402

DB = "probe4_db"
args = type("A", (), {"scale": "mini", "warehouses": 1, "seed": 42,
                      "data": None, "db": DB, "isolation": "si"})()
server = tcli.cmd_load(args, keep_server=True)

c1 = Client(timeout=60)
c2 = Client(timeout=60)
for c in (c1, c2):
    c.query("set transaction isolation level snapshot isolation")
    c.query("set output_file off")

def probe(o_id):
    return len(parse_rows(c2.query(
        "select o_id from orders where o_w_id=1 and o_d_id=1 and o_id=%d;" % o_id)))

watch = {"oid": None, "prev": None, "log": []}
orig_query = Client.query

def traced_query(self, sql):
    r = orig_query(self, sql)
    if self is c1 and watch["oid"] is not None:
        v = probe(watch["oid"])
        if watch["prev"] is not None and v != watch["prev"]:
            watch["log"].append("vis %d->%d after: %s" % (watch["prev"], v, sql[:100]))
        watch["prev"] = v
    return r

Client.query = traced_query

cfg = Config(warehouses=1, items=1000, districts=3, customers=50)
r = TpccRandom(7)   # 与 probe_visibility 相同种子

def next_oid():
    return int(float(parse_rows(orig_query(c1,
        "select d_next_o_id from district where d_w_id=1 and d_id=1;"))[0][0]))

for tno in (1, 2, 3, 4):
    oid = next_oid()
    watch.update(oid=oid, prev=None, log=[])
    st, det = new_order(c1, r, cfg, 1, d_id=1, force_rollback=False)
    final = probe(oid)
    print("txn%d o_id=%d -> %s, final vis=%d" % (tno, oid, st, final))
    for line in watch["log"]:
        print("   ", line)

c1.close(); c2.close(); server.stop()
