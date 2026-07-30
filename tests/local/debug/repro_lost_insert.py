#!/usr/bin/env python3
"""Minimal repro: sequential committed new_orders lose rows.
Usage: python3 repro_lost_insert.py [n_txns] [si|default] [mixed|pure]
Requires a loaded tpccbench db running is NOT needed - it bootstraps itself
via tpccbench load on scale mini into db 'repro_lost_db'.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bench"))

from tpccbench.db import Client, Server, parse_rows  # noqa: E402
from tpccbench.workload import Config, new_order, payment, delivery  # noqa: E402
from tpccbench.tpcrand import TpccRandom  # noqa: E402
from tpccbench import cli as tcli  # noqa: E402

N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
ISO = sys.argv[2] if len(sys.argv) > 2 else "si"
MODE = sys.argv[3] if len(sys.argv) > 3 else "pure"
DB = "repro_lost_db"

# fresh load (quiet)
args = type("A", (), {"scale": "mini", "warehouses": 1, "seed": 42,
                      "data": None, "db": DB, "isolation": ISO})()
print("== loading mini data into %s ==" % DB)
server = tcli.cmd_load(args, keep_server=True)

cli = Client(timeout=60)
if ISO == "si":
    cli.query("set transaction isolation level snapshot isolation")
cli.query("set output_file off")

cfg = Config(warehouses=1, items=1000, districts=3, customers=50)
r = TpccRandom(7)

def rows(sql):
    return parse_rows(cli.query(sql))

lost, committed = [], 0
for i in range(1, N + 1):
    before = int(float(rows("select d_next_o_id from district where d_w_id=1 and d_id=1;")[0][0]))
    st, det = new_order(cli, r, cfg, 1, d_id=1, force_rollback=False)
    if st != "commit":
        print("txn %d: %s %s" % (i, st, det))
        continue
    committed += 1
    o_id = before  # single client: this txn's o_id == counter before txn
    n = len(rows("select o_id from orders where o_w_id=1 and o_d_id=1 and o_id=%d;" % o_id))
    if n != 1:
        lost.append((i, o_id, n))
        print("txn %d: o_id %d -> %d rows IMMEDIATELY AFTER COMMIT" % (i, o_id, n))
    if MODE == "mixed":
        payment(cli, r, cfg, 1, d_id=1, c_id=1 + i % 50, amount=1.0)
        if i % 10 == 0:
            delivery(cli, r, cfg, 1)

# final re-scan: which of the committed o_ids are missing NOW (delayed loss?)
start = int(float(rows("select d_next_o_id from district where d_w_id=1 and d_id=1;")[0][0])) - committed
late = []
for o_id in range(start, start + committed):
    if not rows("select o_id from orders where o_w_id=1 and o_d_id=1 and o_id=%d;" % o_id):
        late.append(o_id)

print("\ncommitted=%d immediate-loss=%d delayed-loss=%d iso=%s mode=%s"
      % (committed, len(lost), len(late), ISO, MODE))
if late:
    print("missing o_ids now:", late[:20], "..." if len(late) > 20 else "")
cli.close()
server.stop()
