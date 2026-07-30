#!/usr/bin/env python3
"""Statement-level bisect: which statement inside txn2 erases txn2's orders row."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bench"))

from tpccbench.db import Client, parse_rows  # noqa: E402
from tpccbench import cli as tcli  # noqa: E402

DB = "probe2_db"
args = type("A", (), {"scale": "mini", "warehouses": 1, "seed": 42,
                      "data": None, "db": DB, "isolation": "si"})()
server = tcli.cmd_load(args, keep_server=True)

c1 = Client(timeout=60)   # 事务连接
c2 = Client(timeout=60)   # 探测连接
for c in (c1, c2):
    c.query("set transaction isolation level snapshot isolation")
    c.query("set output_file off")

def probe(o_id):
    r = parse_rows(c2.query(
        "select o_id from orders where o_w_id=1 and o_d_id=1 and o_id=%d;" % o_id))
    return len(r)

# ---- txn1: 一笔完整 new_order（fast path 参照）----
def txn(stmts, label, watch_oid):
    print("== %s (watch o_id=%d) ==" % (label, watch_oid))
    for i, sql in enumerate(stmts):
        r = c1.query(sql)
        state = probe(watch_oid)
        flag = ""
        if i > 2 and state == 0:
            flag = "   <<< 行消失于此句后"
        print("  [%02d] vis=%d  %s%s" % (i, state, sql[:70], flag))

oid = int(float(parse_rows(c1.query(
    "select d_next_o_id from district where d_w_id=1 and d_id=1;"))[0][0]))

stmts1 = [
    "begin;",
    "update district set d_next_o_id = %d where d_w_id = 1 and d_id = 1;" % (oid + 1),
    "insert into orders values (%d, 1, 1, 1, '2026-01-01 00:00:00', 0, 2, 1);" % oid,
    "insert into new_orders values (%d, 1, 1);" % oid,
    "select i_price from item where i_id = 5;",
    "select s_quantity from stock where s_w_id = 1 and s_i_id = 5;",
    "update stock set s_quantity = 50 where s_w_id = 1 and s_i_id = 5;",
    "insert into order_line values (%d, 1, 1, 1, 5, 1, 'PENDING', 5, 10.0, 'x');" % oid,
    "insert into order_line values (%d, 1, 1, 2, 6, 1, 'PENDING', 5, 10.0, 'x');" % oid,
    "commit;",
]
txn(stmts1, "txn1", oid)
print("txn1 committed, final vis=%d" % probe(oid))

oid2 = oid + 1
stmts2 = [s.replace(str(oid), str(oid2)) if ("orders values" in s or "order_line values" in s or "new_orders values" in s) else s
          for s in stmts1]
stmts2[1] = "update district set d_next_o_id = %d where d_w_id = 1 and d_id = 1;" % (oid2 + 1)
txn(stmts2, "txn2", oid2)
print("txn2 committed, final vis=%d" % probe(oid2))

# txn3 之后 txn2 的行回来了吗？
c1.query("begin;")
c1.query("update district set d_next_o_id = %d where d_w_id = 1 and d_id = 1;" % (oid2 + 2))
c1.query("insert into orders values (%d, 1, 1, 1, '2026-01-01 00:00:00', 0, 1, 1);" % (oid2 + 1))
c1.query("commit;")
print("after txn3: txn2 row vis=%d, txn3 row vis=%d" % (probe(oid2), probe(oid2 + 1)))

c1.close(); c2.close(); server.stop()
