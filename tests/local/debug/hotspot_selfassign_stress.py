#!/usr/bin/env python3
"""热点自赋值压测：复现 OJ TPC-C measurement 的 UPDATE SET col=col ERROR。
32 线程 EXEC_BATCH，NewOrder 式事务：自赋值锁行(char/float/int) + 算术更新 + 提交，
少量热点行制造高冲突；统计 OK/ABORT，任何 ERROR 立即记录 diag。"""
import os, sys, subprocess, time, shutil, threading, random

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BUILD = os.environ.get("RMDB_BUILD", os.path.join(REPO, "build"))
DB = "hotspot_sa_db"
DBPATH = os.path.join(BUILD, DB)
LOG = os.path.join(BUILD, "hotspot_server.log")
DUR = float(sys.argv[1]) if len(sys.argv) > 1 else 90
THREADS = int(sys.argv[2]) if len(sys.argv) > 2 else 32
sys.path.insert(0, os.path.join(REPO, "tests", "local"))

subprocess.run(["pkill", "-x", "rmdb"], check=False)
time.sleep(0.5)
shutil.rmtree(DBPATH, ignore_errors=True)
logf = open(LOG, "w")
proc = subprocess.Popen([os.path.join(BUILD, "bin", "rmdb"), DB], cwd=BUILD,
                        stdout=logf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
time.sleep(2)

from wire_client import (WireClient, send_frame, recv_frame, TAG_EXEC_STREAM,
                         TAG_COMMAND_OK, TAG_ERROR, TAG_TRANSACTION_ABORT,
                         SQLTYPE_INT32 as I, SQLTYPE_FLOAT32 as F, SQLTYPE_CHAR as C)

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

boot = WireClient()
# stock 形态：多列含 char，(w,i) 组合索引
r = stream(boot, "create table st (s_w_id int, s_i_id int, s_quantity int, s_ytd float, s_cnt int, s_data char(16));")
assert r[0] == "OK", r
r = stream(boot, "create index st (s_w_id, s_i_id);")
assert r[0] == "OK", r
# district 形态
r = stream(boot, "create table di (d_w_id int, d_id int, d_next_o_id int, d_ytd float);")
assert r[0] == "OK", r
r = stream(boot, "create index di (d_w_id, d_id);")
assert r[0] == "OK", r
NW, NI = 4, 40
for w in range(1, NW + 1):
    for i in range(1, NI + 1):
        stream(boot, f"insert into st values ({w}, {i}, 50, 0.0, 0, 'dist{w:02d}x{i:04d}');")
    stream(boot, f"insert into di values ({w}, 1, 3001, 30000.0);")
boot.close()

STMTS = [
    (1, False, [], "begin"),
    (2, False, [], "commit"),
    (3, False, [], "abort"),
    # 决赛新增形态：自赋值锁行（三种列型 + 索引键列）
    (10, False, [I, I], "update st set s_data = s_data where s_w_id = $1 and s_i_id = $2"),
    (11, False, [I, I], "update st set s_ytd = s_ytd where s_w_id = $1 and s_i_id = $2"),
    (12, False, [I, I], "update st set s_quantity = s_quantity where s_w_id = $1 and s_i_id = $2"),
    (13, False, [I, I], "update di set d_next_o_id = d_next_o_id where d_w_id = $1 and d_id = $2"),
    (14, False, [I, I], "update st set s_i_id = s_i_id where s_w_id = $1 and s_i_id = $2"),
    # 常规排名语句
    (20, False, [I, I], "update di set d_next_o_id = d_next_o_id + 1 where d_w_id = $1 and d_id = $2"),
    (21, False, [I, F, I, I], "update st set s_quantity=$1, s_ytd=s_ytd+$2, s_cnt=s_cnt+1 where s_w_id=$3 and s_i_id=$4"),
    (22, True, [I, I], "select d_next_o_id from di where d_w_id = $1 and d_id = $2"),
    (23, True, [I, I], "select s_quantity, s_data from st where s_w_id = $1 and s_i_id = $2"),
    (24, True, [I], "select count(*) from st where s_w_id = $1"),
]

stop = False
stats = {"ok": 0, "abort": 0, "error": 0}
errors = []
lock = threading.Lock()

def worker(tid):
    rng = random.Random(tid)
    global stop
    while not stop:
        try:
            c = WireClient(timeout=30)
            stream(c, "set transaction isolation level snapshot isolation;")
            c.prepare_set(STMTS)
            n = 0
            while not stop:
                w = 1 if rng.random() < 0.65 else rng.randint(1, NW)   # 热点仓
                items = sorted({(1 if rng.random() < 0.5 else rng.randint(1, NI)) for _ in range(rng.randint(3, 8))})
                # batch1: begin + 锁 district + 锁 items（混用各型自赋值）
                ops = [(1, [])]
                ops.append((13, [w, 1]))
                for it in items:
                    ops.append((rng.choice([10, 11, 12, 14]), [w, it]))
                ops.append((22, [w, 1]))
                r1 = c.exec_batch(ops)
                if r1.error:
                    with lock:
                        stats["error"] += 1
                        errors.append(("batch1", r1.failed_op, r1.diagnostic))
                    continue
                if r1.aborted:
                    with lock: stats["abort"] += 1
                    continue
                # batch2: 真实写 + commit（10% 主动 abort，5% 直接断连）
                if rng.random() < 0.05:
                    c.close()
                    raise ConnectionError("deliberate drop")
                ops2 = [(20, [w, 1])]
                for it in items:
                    ops2.append((21, [rng.randint(10, 100), rng.uniform(1, 99), w, it]))
                ops2.append((24, [w]))
                ops2.append((3, []) if rng.random() < 0.10 else (2, []))
                r2 = c.exec_batch(ops2)
                if r2.error:
                    with lock:
                        stats["error"] += 1
                        errors.append(("batch2", r2.failed_op, r2.diagnostic))
                    continue
                if r2.aborted:
                    with lock: stats["abort"] += 1
                    continue
                with lock: stats["ok"] += 1
                n += 1
        except (ConnectionError, OSError):
            continue
        except Exception as e:
            with lock:
                errors.append(("client-exc", -1, repr(e)))
            continue

ts = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(THREADS)]
t0 = time.time()
for t in ts: t.start()
while time.time() - t0 < DUR:
    time.sleep(5)
    with lock:
        print(f"[{time.time()-t0:6.1f}s] ok={stats['ok']} abort={stats['abort']} error={stats['error']}", flush=True)
    if stats["error"]:
        break
stop = True
time.sleep(2)

print("\n=== STATS ===", stats)
print("=== first errors ===")
for e in errors[:10]:
    print(e)
logf.flush()
print("=== [sql-error] lines in server log ===")
subprocess.run(["grep", "-a", "sql-error", LOG], check=False)
proc.terminate()
try: proc.wait(timeout=10)
except Exception: proc.kill()
sys.exit(1 if stats["error"] or errors else 0)
