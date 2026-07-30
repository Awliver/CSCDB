#!/usr/bin/env python3
"""Delivery 扫描回环定向复现器（对应 07-30 12:52 OJ 报告）。

new_orders 形态：索引 (w,d,o)，Delivery 线程 MIN+点查+DELETE 出队，
NewOrder 线程持续插入新 o 值——删除/合并使内部分隔键陈旧，
key 锚定重定位可能倒退。检测三种终态：
  1. 任何 ERROR 终结（OJ 判负形态）
  2. 点查返回 >1 行（游标弹回重复发行 = 回环缺陷本体）
  3. MIN 查询返回行数 >1（同上）
用法: RMDB_BUILD=<build目录> python3 delivery_scan_loop_repro.py [seconds] [threads]
"""
import os, sys, subprocess, time, shutil, threading, random

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BUILD = os.environ.get("RMDB_BUILD", os.path.join(REPO, "build"))
DB = "scanloop_db"
DBPATH = os.path.join(BUILD, DB)
LOG = os.path.join(BUILD, "scanloop_server.log")
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 120
THREADS = int(sys.argv[2]) if len(sys.argv) > 2 else 24
sys.path.insert(0, os.path.join(REPO, "tests", "local"))

subprocess.run(["pkill", "-x", "rmdb"], check=False)
time.sleep(0.5)
shutil.rmtree(DBPATH, ignore_errors=True)
logf = open(LOG, "w")
proc = subprocess.Popen([os.path.join(BUILD, "bin", "rmdb"), DB], cwd=BUILD,
                        stdout=logf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
time.sleep(2)

from wire_client import (WireClient, send_frame, recv_frame, TAG_EXEC_STREAM,
                         TAG_ERROR, TAG_TRANSACTION_ABORT,
                         SQLTYPE_INT32 as I)

def stream(c, sql):
    send_frame(c.sock, TAG_EXEC_STREAM, sql.encode())
    while True:
        tag, flags, payload = recv_frame(c.sock)
        if tag in (0x01, 0x02):
            continue
        if tag == TAG_ERROR:
            return ("ERROR", payload.decode(errors="replace"))
        if tag == TAG_TRANSACTION_ABORT:
            return ("ABORT", payload.decode(errors="replace"))
        return ("OK", "")

NW, ND, SEED_O = 4, 10, 3000
csv_path = os.path.join(BUILD, "scanloop_no.csv")
with open(csv_path, "w") as f:
    f.write("w,d,o\n")
    for w in range(1, NW + 1):
        for d in range(1, ND + 1):
            for o in range(1, SEED_O + 1):
                f.write(f"{w},{d},{o}\n")
boot = WireClient()
r = stream(boot, "create table no (w int, d int, o int);")
assert r[0] == "OK", r
r = stream(boot, "create index no (w, d, o);")
assert r[0] == "OK", r
r = stream(boot, f"load {csv_path} into no;")
assert r[0] == "OK", r
boot.close()

STMTS = [
    (1, False, [], "begin"),
    (2, False, [], "commit"),
    (3, False, [], "abort"),
    (10, True, [I, I], "select o from no where w = $1 and d = $2 order by o asc limit 1"),
    (11, True, [I, I, I], "select o from no where w = $1 and d = $2 and o = $3"),
    (12, False, [I, I, I], "delete from no where w = $1 and d = $2 and o = $3"),
    (13, False, [I, I, I], "insert into no values ($1, $2, $3)"),
    (14, True, [I, I], "select count(*) from no where w = $1 and d = $2"),
]

stop = False
stats = {"deliver": 0, "insert": 0, "abort": 0, "error": 0, "dup": 0}
errors = []
lock = threading.Lock()
next_o = [[SEED_O + 1] * (ND + 1) for _ in range(NW + 1)]
olock = threading.Lock()

def fail(kind, detail):
    with lock:
        stats[kind] += 1
        errors.append((kind, detail))

def worker(tid):
    rng = random.Random(tid)
    while not stop:
        try:
            c = WireClient(timeout=30)
            stream(c, "set transaction isolation level snapshot isolation;")
            c.prepare_set(STMTS)
            while not stop:
                w = rng.randint(1, NW)
                d = rng.randint(1, ND)
                if rng.random() < 0.5:
                    # Delivery：MIN → 点查 → DELETE → commit
                    r = c.exec_batch([(1, []), (10, [w, d])])
                    if r.error:
                        fail("error", ("min", r.diagnostic)); c.exec_batch([(3, [])]); continue
                    if r.aborted:
                        with lock: stats["abort"] += 1
                        continue
                    rows = r.results.get(1, [])
                    if len(rows) > 1:
                        fail("dup", ("min rows=%d" % len(rows), rows[:5]))
                    if not rows:
                        c.exec_batch([(3, [])]); continue
                    o = rows[0][0]
                    r2 = c.exec_batch([(11, [w, d, o]), (12, [w, d, o]), (2, [])])
                    if r2.error:
                        fail("error", ("point/del", r2.diagnostic)); continue
                    if r2.aborted:
                        with lock: stats["abort"] += 1
                        continue
                    prow = r2.results.get(0, [])
                    if len(prow) > 1:
                        fail("dup", ("point rows=%d w=%d d=%d o=%d" % (len(prow), w, d, o), prow[:5]))
                    with lock: stats["deliver"] += 1
                else:
                    # NewOrder 喂队列：插入新 o
                    with olock:
                        o = next_o[w][d]; next_o[w][d] += 1
                    r = c.exec_batch([(1, []), (13, [w, d, o]), (2, [])])
                    if r.error:
                        fail("error", ("insert", r.diagnostic)); continue
                    if r.aborted:
                        with lock: stats["abort"] += 1
                        continue
                    with lock: stats["insert"] += 1
        except (ConnectionError, OSError):
            continue
        except Exception as e:
            fail("error", ("client-exc", repr(e)))
            continue

ts = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(THREADS)]
t0 = time.time()
for t in ts: t.start()
while time.time() - t0 < DUR:
    time.sleep(5)
    with lock:
        print(f"[{time.time()-t0:6.1f}s] deliver={stats['deliver']} insert={stats['insert']} "
              f"abort={stats['abort']} error={stats['error']} dup={stats['dup']}", flush=True)
        if stats["error"] or stats["dup"]:
            break
stop = True
time.sleep(2)

print("\n=== STATS ===", stats)
for e in errors[:8]:
    print(e)
logf.flush()
print("=== server [sql-error]/[pressure-abort] ===")
subprocess.run(["grep", "-aE", "sql-error|pressure-abort", LOG], check=False)
proc.terminate()
try: proc.wait(timeout=10)
except Exception: proc.kill()
sys.exit(1 if stats["error"] or stats["dup"] else 0)
