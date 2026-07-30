#!/usr/bin/env python3
"""Targeted stress for the rare C5 off-by-one (orders.carrier=0 without new_orders row).
Runs bursts of new_order+delivery+payment on district 1 with 4 threads; after each
burst checks pairing; on mismatch dumps the culprit order's full state."""
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bench"))

from tpccbench.db import Client, parse_rows  # noqa: E402
from tpccbench.workload import Config, new_order, delivery, payment  # noqa: E402
from tpccbench.tpcrand import TpccRandom  # noqa: E402
from tpccbench import cli as tcli  # noqa: E402

DB = "stress_c5_db"
args = type("A", (), {"scale": "mini", "warehouses": 1, "seed": 42,
                      "data": None, "db": DB, "isolation": "si"})()
server = tcli.cmd_load(args, keep_server=True)
cfg = Config(warehouses=1, items=1000, districts=3, customers=50)

chk = Client(timeout=60)
chk.query("set output_file off")
def rows(sql):
    return parse_rows(chk.query(sql))

def burst(seconds, nthreads=4):
    stop = threading.Event()
    def worker(i):
        c = Client(timeout=60)
        c.query("set transaction isolation level snapshot isolation")
        c.query("set output_file off")
        r = TpccRandom(i * 7919 + int(time.time()))
        while not stop.is_set():
            k = r.randint(1, 10)
            try:
                if k <= 5:
                    new_order(c, r, cfg, 1, d_id=1)
                elif k <= 8:
                    payment(c, r, cfg, 1, d_id=1)
                else:
                    delivery(c, r, cfg, 1)
            except Exception:
                try: c.rollback_quiet()
                except Exception: pass
        c.close()
    ths = [threading.Thread(target=worker, args=(i,)) for i in range(nthreads)]
    for t in ths: t.start()
    time.sleep(seconds)
    stop.set()
    for t in ths: t.join(timeout=10)

def check():
    a = int(float(rows("select count(*) from orders where o_w_id=1 and o_d_id=1 and o_carrier_id=0;")[0][0]))
    b = int(float(rows("select count(*) from new_orders where no_w_id=1 and no_d_id=1;")[0][0]))
    return a, b

for it in range(1, 41):
    burst(6)
    a, b = check()
    print("iter %d: carrier0=%d new_orders=%d %s" % (it, a, b, "OK" if a == b else "<<< MISMATCH"), flush=True)
    if a != b:
        # 找出问题 o_id：carrier=0 但无 new_orders 行
        next_o = int(float(rows("select d_next_o_id from district where d_w_id=1 and d_id=1;")[0][0]))
        for oid in range(1, next_o):
            has_o = rows("select o_carrier_id, o_ol_cnt from orders where o_w_id=1 and o_d_id=1 and o_id=%d;" % oid)
            if not has_o or int(float(has_o[0][0])) != 0:
                continue
            has_no = rows("select no_o_id from new_orders where no_w_id=1 and no_d_id=1 and no_o_id=%d;" % oid)
            if has_no:
                continue
            ol_cnt = int(float(has_o[0][1]))
            pend = int(float(rows("select count(*) from order_line where ol_w_id=1 and ol_d_id=1 "
                                  "and ol_o_id=%d and ol_delivery_d='PENDING';" % oid)[0][0]))
            tot = int(float(rows("select count(*) from order_line where ol_w_id=1 and ol_d_id=1 "
                                 "and ol_o_id=%d;" % oid)[0][0]))
            print("  culprit o_id=%d carrier=0 无new_orders行 ol_cnt=%d lines=%d pending_lines=%d" %
                  (oid, ol_cnt, tot, pend))
            print("  诊断: lines %s" % ("已配送日期(delivery部分生效!)" if pend == 0 and tot > 0
                                        else "仍PENDING(new_orders行丢失!)"))
        break

chk.close()
server.stop()
