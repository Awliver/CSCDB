#!/usr/bin/env python3
"""Exact-workload statement bisect (two identical txns, per-statement probe)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bench"))

from tpccbench.db import Client, parse_rows  # noqa: E402
from tpccbench import cli as tcli  # noqa: E402

DB = "probe3_db"
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

def full_neworder(oid, items):
    """完整 workload 语句序列（含 w_tax/customer/district 读、重读、多列 stock update）"""
    stmts = [
        "begin;",
        "select w_tax from warehouse where w_id = 1;",
        "select c_discount, c_last, c_credit from customer where c_w_id = 1 and c_d_id = 1 and c_id = 1;",
        "select d_tax, d_next_o_id from district where d_w_id = 1 and d_id = 1;",
        "update district set d_next_o_id = %d where d_w_id = 1 and d_id = 1;" % (oid + 1),
        "select d_next_o_id from district where d_w_id = 1 and d_id = 1;",
        "insert into orders values (%d, 1, 1, 1, '2026-01-01 00:00:00', 0, %d, 1);" % (oid, len(items)),
        "insert into new_orders values (%d, 1, 1);" % oid,
    ]
    for n, i_id in enumerate(items, 1):
        stmts += [
            "select i_price from item where i_id = %d;" % i_id,
            "select s_quantity, s_dist_01, s_ytd, s_order_cnt, s_remote_cnt from stock "
            "where s_w_id = 1 and s_i_id = %d;" % i_id,
            "update stock set s_quantity = 42, s_ytd = 7.00, s_order_cnt = 3, "
            "s_remote_cnt = 0 where s_w_id = 1 and s_i_id = %d;" % i_id,
            "insert into order_line values (%d, 1, 1, %d, %d, 1, 'PENDING', 5, 10.00, 'x');"
            % (oid, n, i_id),
        ]
    stmts.append("commit;")
    return stmts

for tno in (1, 2, 3):
    oid = int(float(parse_rows(c1.query(
        "select d_next_o_id from district where d_w_id=1 and d_id=1;"))[0][0]))
    stmts = full_neworder(oid, [5, 6, 7])
    print("== txn%d (o_id=%d) ==" % (tno, oid))
    prev = None
    for i, sql in enumerate(stmts):
        c1.query(sql)
        v = probe(oid)
        mark = ""
        if prev == 1 and v == 0:
            mark = "  <<< 行在此句后消失"
        if prev == 0 and v == 1 and i > 7:
            mark = "  <<< 行在此句后出现"
        if mark or i in (6, len(stmts) - 1):
            print("  [%02d] vis=%d  %s%s" % (i, v, sql[:78], mark))
        prev = v
    print("  final vis=%d" % probe(oid))

c1.close(); c2.close(); server.stop()
