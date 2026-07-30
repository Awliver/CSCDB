#!/usr/bin/env python3
"""Time single-statement INSERT throughput (with indexes present), autocommit.
Usage: time_insert.py <rmdb_binary> <db_dir> [n_rows]"""
import os
import shutil
import socket
import subprocess
import sys
import time

BIN, DB = sys.argv[1], sys.argv[2]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 10000
BUILD = os.path.dirname(os.path.dirname(BIN))

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
s.settimeout(600)

def q(sql):
    s.sendall((sql + "\x00").encode())
    buf = b""
    while b"\x00" not in buf:
        c = s.recv(1 << 16)
        if not c:
            raise RuntimeError("closed")
        buf += c
    return buf.split(b"\x00")[0].decode(errors="replace")

# OJ 风格：建表 → 建索引 → 逐条 INSERT（自动提交）
q("create table t_ins (id int, w int, d int, val float, name char(24));")
q("create index t_ins (w, d, id);")
q("set output_file off")

t0 = time.time()
for i in range(1, N + 1):
    q("insert into t_ins values (%d, 1, %d, %.2f, 'row%08d');" % (i, (i % 10) + 1, i * 1.5, i))
dt = time.time() - t0
cnt = q("select count(*) from t_ins;")
print("binary: %s" % BIN)
print("%d inserts (indexed, autocommit): %.2fs = %.0f/s" % (N, dt, N / dt))

# 显式事务批量（每 100 条一个事务）
t0 = time.time()
for b in range(20):
    q("begin;")
    for i in range(100):
        rid = N + b * 100 + i + 1
        q("insert into t_ins values (%d, 2, %d, 1.0, 'tx%08d');" % (rid, (rid % 10) + 1, rid))
    q("commit;")
dt2 = time.time() - t0
print("2000 inserts (explicit txn x20): %.2fs = %.0f/s" % (dt2, 2000 / dt2))
s.sendall(b"exit\x00")
proc.kill()
