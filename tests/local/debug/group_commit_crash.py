#!/usr/bin/env python3
"""组提交持久性定向测试：并发提交中途 kill -9，已 ACK 的 commit 必须全部存活。

4 个客户端并发执行 begin; insert; commit;（每次记录"收到 commit 成功响应"的 id），
运行中随机时刻 kill -9 服务器，重启恢复后核对：每个已 ACK 的 id 都必须存在。
重复 ROUNDS 轮。这是组提交实现的红线——commit ACK 前 fsync 必须已覆盖其 LSN。
"""
import os
import random
import shutil
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BUILD = os.path.join(ROOT, "build")
PORT = 8765
ROUNDS = 4


def send(sock, sql):
    sock.sendall((sql + "\0").encode())
    d = b""
    while b"\0" not in d:
        c = sock.recv(65536)
        if not c:
            raise ConnectionError("closed")
        d += c
    return d.split(b"\0")[0].decode(errors="replace")


def ok(resp):
    l = resp.lower()
    return "abort" not in l and "error" not in l and "failure" not in l


def start(db, fresh):
    subprocess.run(["pkill", "-9", "-x", "rmdb"], capture_output=True)
    time.sleep(0.4)
    p = os.path.join(BUILD, db)
    if fresh and os.path.exists(p):
        shutil.rmtree(p)
    os.makedirs(p, exist_ok=True)
    proc = subprocess.Popen(["./bin/rmdb", db], cwd=BUILD,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(80):
        try:
            s = socket.socket(); s.settimeout(2); s.connect(("127.0.0.1", PORT)); s.close(); return proc
        except OSError:
            time.sleep(0.3)
    raise RuntimeError("server not up")


db = "gc_crash_db"
proc = start(db, fresh=True)
s0 = socket.socket(); s0.connect(("127.0.0.1", PORT))
send(s0, "create table t (id int, v int);")
send(s0, "create index t (id);")
s0.close()

all_fail = 0
next_id = [1]
for rnd in range(1, ROUNDS + 1):
    acked = [[] for _ in range(4)]
    stop = threading.Event()

    def worker(wi):
        try:
            s = socket.socket(); s.settimeout(20); s.connect(("127.0.0.1", PORT))
            send(s, "set output_file off")
            while not stop.is_set():
                with lock:
                    my = next_id[0]; next_id[0] += 1
                try:
                    if not ok(send(s, "begin;")):
                        continue
                    if not ok(send(s, "insert into t values (%d, %d);" % (my, wi))):
                        send(s, "abort;"); continue
                    if ok(send(s, "commit;")):
                        acked[wi].append(my)       # 收到 ACK 才记
                except (ConnectionError, OSError):
                    return                          # 服务器被 kill
        except OSError:
            return

    lock = threading.Lock()
    ths = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
    for t in ths: t.start()
    time.sleep(random.uniform(1.5, 3.5))            # 随机运行片刻
    proc.kill()                                     # kill -9
    stop.set()
    for t in ths: t.join(timeout=5)

    proc = start(db, fresh=False)                   # 重启走恢复
    s = socket.socket(); s.settimeout(60); s.connect(("127.0.0.1", PORT))
    send(s, "set output_file off")
    missing = []
    total = 0
    for wi in range(4):
        for my in acked[wi]:
            total += 1
            r = send(s, "select id from t where id = %d;" % my)
            if ("| %18d |" % my) not in r and ("%d" % my) not in r.replace(" ", "").replace("|", ""):
                missing.append(my)
    s.close()
    status = "PASS" if not missing else "FAIL"
    if missing:
        all_fail += 1
    print("round %d: acked=%d missing=%d %s %s"
          % (rnd, total, len(missing), status, missing[:10] if missing else ""), flush=True)

proc.kill()
print("GROUP COMMIT CRASH: %s" % ("PASS" if all_fail == 0 else "FAIL (%d rounds)" % all_fail))
sys.exit(1 if all_fail else 0)
