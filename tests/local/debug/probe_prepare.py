#!/usr/bin/env python3
import sys
import os
_HERE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, _HERE)
from wire_client import WireClient, SQLTYPE_INT32 as I
from tpcc_common import SCHEMA, INDEXES
from tpcc_batch import install_prepare

print("start", flush=True)
cli = WireClient(timeout=30)
print("connected", flush=True)
for s in SCHEMA:
    ok, t = cli.query_ok(s)
    if not ok:
        print("schema fail", s, t)
        sys.exit(1)
for s in INDEXES:
    cli.query(s)
cli.query("insert into warehouse values (1,'w','a','b','c','ST','123456789',0.1,0.0);")
cli.query("insert into district values (1,1,'d','a','b','c','ST','123456789',0.1,0.0,20);")
cli.query("insert into order_line values (1,1,1,1,1,1,'2023-07-22 20:50:31',1,12.5,'dist');")
ok, text = cli.query_ok(
    "select sum(ol_amount) from order_line where ol_w_id=1 and ol_d_id=1 and ol_o_id=1;"
)
print("STREAM", ok, repr(text[:120]), flush=True)

cases = [
    (1, True, [I, I, I], "select sum(ol_amount) from order_line where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3"),
    (1, True, [I, I, I], "select sum(ol_amount) as samt from order_line where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3"),
    (2, False, [I, I], "update district set d_next_o_id=d_next_o_id+1 where d_w_id=$1 and d_id=$2"),
    (
        3,
        True,
        [I, I, I, I, I, I],
        "select count(distinct (s_i_id)) from order_line, stock where ol_w_id=$1 and ol_d_id=$2 "
        "and ol_o_id>=$3 and ol_o_id<=$4 and s_w_id=$5 and s_i_id=ol_i_id and s_quantity<$6",
    ),
    (
        4,
        False,
        [I, I, I, I, I],
        "update stock set s_quantity=$1, s_ytd=s_ytd+$2, s_order_cnt=s_order_cnt+1, "
        "s_remote_cnt=s_remote_cnt+$3 where s_w_id=$4 and s_i_id=$5",
    ),
    (
        5,
        False,
        [I, I, I, I, I],
        "update customer set c_balance=c_balance-$1, c_ytd_payment=c_ytd_payment+$2, "
        "c_payment_cnt=c_payment_cnt+1 where c_w_id=$3 and c_d_id=$4 and c_id=$5",
    ),
]
for stmt in cases:
    try:
        cli.prepare_set([stmt])
        print("PREPARE OK", stmt[0], stmt[3][:50], flush=True)
    except Exception as e:
        print("PREPARE FAIL", stmt[0], e, flush=True)

try:
    install_prepare(cli)
    print("FULL PREPARE OK count=", len(cli.prepared), flush=True)
except Exception as e:
    print("FULL PREPARE FAIL", e, flush=True)

cli.close()
print("done", flush=True)
