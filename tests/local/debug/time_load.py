#!/usr/bin/env python3
"""Time the OJ-style load flow (schema -> 9 loads -> 8 indexes) against a given binary.
Usage: time_load.py <rmdb_binary> <db_dir_name> [data_dir]"""
import os
import shutil
import socket
import subprocess
import sys
import time

BIN = sys.argv[1]
DB = sys.argv[2]
DATA = sys.argv[3] if len(sys.argv) > 3 else \
    "/home/smart/workspace/2026/db2026/build/tpccbench_data/full_w1_seed42"
BUILD = os.path.dirname(os.path.dirname(BIN))          # .../build/bin/rmdb -> .../build

sys.path.insert(0, "/home/smart/workspace/2026/db2026/bench")
from tpccbench import schema  # noqa: E402

subprocess.run(["pkill", "-9", "-x", "rmdb"], capture_output=True)
time.sleep(0.5)
dbp = os.path.join(BUILD, DB)
if os.path.isdir(dbp):
    shutil.rmtree(dbp)
os.makedirs(dbp)
proc = subprocess.Popen([BIN, DB], cwd=BUILD,
                        stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
for _ in range(40):
    try:
        s = socket.socket(); s.settimeout(2); s.connect(("127.0.0.1", 8765)); break
    except OSError:
        time.sleep(0.3)
s.settimeout(1200)

def q(sql):
    s.sendall((sql + "\x00").encode())
    buf = b""
    while b"\x00" not in buf:
        chunk = s.recv(1 << 16)
        if not chunk:
            raise RuntimeError("conn closed on " + sql[:60])
        buf += chunk
    return buf.split(b"\x00")[0].decode(errors="replace")

t0 = time.time()
for _, ddl in schema.SCHEMA:
    q(ddl)
t_schema = time.time() - t0

t0 = time.time()
per_tab = []
for tab in schema.LOAD_ORDER:
    tt = time.time()
    r = q("load %s into %s;" % (os.path.join(DATA, tab + ".csv"), tab))
    per_tab.append((tab, time.time() - tt))
t_load = time.time() - t0

t0 = time.time()
per_ix = []
for ddl in schema.INDEXES:
    tt = time.time()
    q(ddl)
    per_ix.append((ddl.split("(")[0].split()[-1], time.time() - tt))
t_index = time.time() - t0

t0 = time.time()
n = q("select count(*) from order_line;")
t_count = time.time() - t0

print("binary: %s" % BIN)
print("schema: %.2fs  load: %.2fs  index: %.2fs  count(300k): %.2fs" %
      (t_schema, t_load, t_index, t_count))
print("  loads: " + " ".join("%s=%.1fs" % (t, d) for t, d in per_tab))
print("  index: " + " ".join("%s=%.1fs" % (t, d) for t, d in per_ix))
s.sendall(b"exit\x00")
proc.kill()
