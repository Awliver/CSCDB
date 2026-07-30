#!/usr/bin/env python3
"""题九 SSI 危险结构检测 —— 专门验证【IndexScan 路径】不漏检 rw 反依赖。

改动风险点：单表查询在 SER 下现在走 IndexScan（旧实现强制 SeqScan）。若 IndexScan
的 SSI 钩子(ser_record_read/ser_read_check/ser_read_pred_check)不完整，就会漏检危险
结构 → 本该 abort 的写偏斜被允许提交（题九失分）。本测试全部用【索引列点查/范围】
触发 IndexScan，且穿插 ser_ GC 阈值(>512)不会误清活跃状态。

场景：
  W1 经典写偏斜：T1 读 A 写 B、T2 读 B 写 A（均索引点查）→ SER 必须 abort ≥1
  W2 幻影：T1 谓词读区间、T2 插入落在区间内并提交、T1 基于旧读写入 → 必须 abort T1
  W3 无冲突对照：两事务读写不相交行 → 都应提交（不能误杀，验证没过度保守）
"""
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BUILD = os.path.join(ROOT, "build")
PORT = 8765


def send(sock, sql):
    sock.sendall((sql + "\0").encode())
    data = b""
    while b"\0" not in data:
        c = sock.recv(65536)
        if not c:
            break
        data += c
    return data.split(b"\0")[0].decode(errors="replace")


def status(resp):
    low = resp.lower()
    if "abort" in low:
        return "abort"
    if "error" in low or "failure" in low:
        return "error"
    return "ok"


def fresh_server(db):
    subprocess.run(["pkill", "-9", "-x", "rmdb"], capture_output=True)
    time.sleep(0.5)
    path = os.path.join(BUILD, db)
    if os.path.exists(path):
        import shutil
        shutil.rmtree(path)
    proc = subprocess.Popen(["./bin/rmdb", db], cwd=BUILD,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        try:
            s = socket.socket(); s.settimeout(2); s.connect(("127.0.0.1", PORT)); s.close(); break
        except OSError:
            time.sleep(0.3)
    return proc


results = []
def check(name, ok, detail=""):
    results.append((name, ok))
    print("  %-34s %s %s" % (name, "PASS" if ok else "FAIL", detail), flush=True)


def conn():
    s = socket.socket(); s.connect(("127.0.0.1", PORT))
    send(s, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;")  # 显式 SER（= 默认，双保险）
    send(s, "set output_file off")
    return s


proc = fresh_server("ssi_ix_db")
try:
    setup = socket.socket(); setup.connect(("127.0.0.1", PORT))
    send(setup, "create table t (id int, v int);")
    send(setup, "create index t (id);")            # id 建索引 → WHERE id=X 走 IndexScan
    for i in range(1, 21):
        send(setup, "insert into t values (%d, 100);" % i)
    setup.close()

    # ---- W1: 写偏斜（索引点查读 + 索引点查行的写）----
    t1, t2 = conn(), conn()
    send(t1, "begin;"); send(t2, "begin;")
    send(t1, "select v from t where id = 1;")       # T1 读 A(=1) via IndexScan
    send(t2, "select v from t where id = 2;")       # T2 读 B(=2) via IndexScan
    send(t1, "update t set v = 1 where id = 2;")    # T1 写 B → T2 的读被 T1 写(rw: T2->T1)
    send(t2, "update t set v = 2 where id = 1;")    # T2 写 A → T1 的读被 T2 写(rw: T1->T2)
    c1 = status(send(t1, "commit;"))
    c2 = status(send(t2, "commit;"))
    # 危险结构 T1<->T2，SER 必须至少 abort 一个
    check("W1 写偏斜被检出(索引读)", not (c1 == "ok" and c2 == "ok"),
          "t1=%s t2=%s" % (c1, c2))
    t1.close(); t2.close()

    # ---- W2: 幻影插入落入谓词区间 ----
    t1, t2 = conn(), conn()
    send(t1, "begin;")
    send(t1, "select v from t where id > 100 and id < 110;")   # 空区间谓词读
    send(t2, "begin;")
    ins = status(send(t2, "insert into t values (105, 500);"))  # 幻影落入 T1 谓词
    ic2 = status(send(t2, "commit;"))
    # T1 再基于旧快照写 → 应检出 T1 ->rw T2 危险结构
    send(t1, "update t set v = 999 where id = 3;")
    c1 = status(send(t1, "commit;"))
    check("W2 幻影插入被检出", c1 == "abort" or ic2 == "abort",
          "t2_ins=%s t2_commit=%s t1=%s" % (ins, ic2, c1))
    t1.close(); t2.close()

    # ---- W3: 无冲突对照(不能误杀) ----
    t1, t2 = conn(), conn()
    send(t1, "begin;"); send(t2, "begin;")
    send(t1, "select v from t where id = 8;")
    send(t2, "select v from t where id = 9;")
    send(t1, "update t set v = 11 where id = 8;")   # T1 只读写 id=8
    send(t2, "update t set v = 12 where id = 9;")   # T2 只读写 id=9，不相交
    c1 = status(send(t1, "commit;"))
    c2 = status(send(t2, "commit;"))
    check("W3 无冲突不误杀", c1 == "ok" and c2 == "ok", "t1=%s t2=%s" % (c1, c2))
    t1.close(); t2.close()

    # ---- 压 ser_ GC 阈值：制造 >512 个已提交 SER 事务后重跑 W1，确认 GC 不破坏检测 ----
    burn = conn()
    for i in range(600):
        send(burn, "begin;")
        send(burn, "select v from t where id = %d;" % ((i % 20) + 1))
        send(burn, "commit;")
    burn.close()
    t1, t2 = conn(), conn()
    send(t1, "begin;"); send(t2, "begin;")
    send(t1, "select v from t where id = 4;")
    send(t2, "select v from t where id = 5;")
    send(t1, "update t set v = 4 where id = 5;")
    send(t2, "update t set v = 5 where id = 4;")
    c1 = status(send(t1, "commit;"))
    c2 = status(send(t2, "commit;"))
    check("W1' GC后写偏斜仍检出", not (c1 == "ok" and c2 == "ok"),
          "t1=%s t2=%s" % (c1, c2))
    t1.close(); t2.close()
finally:
    proc.terminate()
    try: proc.wait(timeout=3)
    except Exception: proc.kill()

n_fail = sum(1 for _, ok in results if not ok)
print("SSI INDEXSCAN: %s (%d/%d)" % ("PASS" if n_fail == 0 else "FAIL",
                                     len(results) - n_fail, len(results)))
sys.exit(1 if n_fail else 0)
