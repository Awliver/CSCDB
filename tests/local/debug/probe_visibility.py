#!/usr/bin/env python3
"""Probe which access path loses a committed insert (bug #1 mechanism)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bench"))

from tpccbench.db import Client, parse_rows  # noqa: E402
from tpccbench.workload import Config, new_order  # noqa: E402
from tpccbench.tpcrand import TpccRandom  # noqa: E402
from tpccbench import cli as tcli  # noqa: E402

DB = "probe_db"
args = type("A", (), {"scale": "mini", "warehouses": 1, "seed": 42,
                      "data": None, "db": DB, "isolation": "si"})()
server = tcli.cmd_load(args, keep_server=True)

cli = Client(timeout=60)
cli.query("set transaction isolation level snapshot isolation")
cli.query("set output_file off")
cfg = Config(warehouses=1, items=1000, districts=3, customers=50)
r = TpccRandom(7)

def rows(sql):
    return parse_rows(cli.query(sql))

def probe(tag, o_id):
    ix = len(rows("select o_id from orders where o_w_id=1 and o_d_id=1 and o_id=%d;" % o_id))
    # o_id 单列不匹配复合索引前缀 → seq scan
    seq = len(rows("select o_id, o_d_id from orders where o_id=%d;" % o_id))
    cnt = int(float(rows("select count(*) from orders where o_w_id=1 and o_d_id=1;")[0][0]))
    no_ix = len(rows("select no_o_id from new_orders where no_w_id=1 and no_d_id=1 and no_o_id=%d;" % o_id))
    print("%-22s o_id=%d  index=%d seq=%d(全部区) count_d1=%d  no_index=%d"
          % (tag, o_id, ix, seq, cnt, no_ix))

def next_oid():
    return int(float(rows("select d_next_o_id from district where d_w_id=1 and d_id=1;")[0][0]))

# --- txn 1 ---
o1 = next_oid()
st, det = new_order(cli, r, cfg, 1, d_id=1, force_rollback=False)
print("txn1:", st, det)
probe("after txn1 commit", o1)

# --- txn 2 ---
o2 = next_oid()
st, det = new_order(cli, r, cfg, 1, d_id=1, force_rollback=False)
print("txn2:", st, det)
probe("after txn2: txn1 row", o1)
probe("after txn2: txn2 row", o2)

# --- 另一个连接的视角 ---
cli2 = Client(timeout=60)
cli2.query("set output_file off")
def rows2(sql):
    return parse_rows(cli2.query(sql))
ix2 = len(rows2("select o_id from orders where o_w_id=1 and o_d_id=1 and o_id=%d;" % o2))
seq2 = len(rows2("select o_id, o_d_id from orders where o_id=%d;" % o2))
print("fresh conn: txn2 row  index=%d seq=%d" % (ix2, seq2))
cli2.close()

cli.close()
server.stop()
