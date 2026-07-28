#!/usr/bin/env python3
"""临时回归验证：Abort（含索引、显式多语句事务）+ Crash Recovery（kill -9）。
验证本轮修复（abort restore overlay / buffer pool WAL ordering）不破坏正确性。
该脚本位于 gitignored 的 debug 目录，验证通过后可删除。
"""
import os, sys, time, socket, signal, subprocess, shutil, re


def norm(s):
    return re.sub(r"\s+", " ", s)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
BUILD = os.path.join(ROOT, "build")
BIN = os.path.join(BUILD, "bin", "rmdb")
PORT = 8765


def port_open():
    try:
        s = socket.socket(); s.settimeout(0.3); s.connect(("127.0.0.1", PORT)); s.close(); return True
    except Exception:
        return False


def kill_residual():
    subprocess.run(["pkill", "-9", "-f", BIN], capture_output=True)
    for _ in range(25):
        if not port_open(): break
        time.sleep(0.2)


def start(db, clean):
    db_dir = os.path.join(BUILD, db)
    if clean and os.path.isdir(db_dir):
        shutil.rmtree(db_dir)
    kill_residual()
    p = subprocess.Popen([os.path.relpath(BIN, BUILD), db], cwd=BUILD,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    for _ in range(40):
        if port_open(): return p
        if p.poll() is not None:
            out, err = p.communicate()
            raise RuntimeError("server exited early:\n" + err.decode(errors="replace"))
        time.sleep(0.2)
    raise RuntimeError("server did not open port")


def sql(sock, q):
    q = q.strip()
    if not q.endswith(';'): q += ';'
    sock.sendall(q.encode() + b'\x00')
    data = b""
    sock.settimeout(5.0)
    while True:
        try:
            c = sock.recv(8192)
            if not c: break
            data += c
            if b'\x00' in data: break
        except socket.timeout:
            break
    return data.split(b'\x00')[0].decode(errors="replace")


def graceful_stop(p):
    try:
        s = socket.socket(); s.settimeout(2); s.connect(("127.0.0.1", PORT))
        s.sendall(b"exit\x00"); s.close()
    except Exception:
        pass
    try:
        p.wait(timeout=4)
    except subprocess.TimeoutExpired:
        p.kill(); p.wait()
    for _ in range(25):
        if not port_open(): break
        time.sleep(0.2)


def test_abort():
    print("\n=== Test A: Abort with index (explicit multi-stmt txn) ===")
    p = start("dbg_abort", clean=True)
    ok = True
    try:
        s = socket.socket(); s.connect(("127.0.0.1", PORT))
        sql(s, "create table t (id int, val int)")
        sql(s, "create index t (id)")
        sql(s, "insert into t values (1, 10)")
        sql(s, "insert into t values (2, 20)")
        sql(s, "begin")
        sql(s, "update t set val = 100 where id = 1")
        sql(s, "delete from t where id = 2")
        sql(s, "insert into t values (3, 30)")
        sql(s, "abort")
        r1 = norm(sql(s, "select * from t"))
        print("after abort, select * from t:\n" + r1)
        # 期望回滚到 (1,10),(2,20)
        if "| 1 | 100 |" in r1 or "| 3 | 30 |" in r1:
            print("[FAIL] rollback did not restore original data"); ok = False
        if "| 1 | 10 |" not in r1 or "| 2 | 20 |" not in r1:
            print("[FAIL] original rows missing after rollback"); ok = False
        # 索引一致性：用索引列查询
        r2 = norm(sql(s, "select * from t where id = 1"))
        print("select where id=1 (index path):\n" + r2)
        if "| 1 | 10 |" not in r2:
            print("[FAIL] index lookup inconsistent after rollback"); ok = False
        r3 = norm(sql(s, "select * from t where id = 2"))
        if "| 2 | 20 |" not in r3:
            print("[FAIL] index lookup id=2 inconsistent after rollback"); ok = False
        s.sendall(b"exit\x00"); s.close()
    except Exception as e:
        print("[FAIL] exception (server likely crashed):", e); ok = False
    finally:
        graceful_stop(p)
    print("[PASS] Abort test" if ok else "[FAIL] Abort test")
    return ok


def test_crash():
    print("\n=== Test B: Crash recovery (kill -9) ===")
    # phase 1: write committed + uncommitted, then kill -9
    p = start("dbg_crash", clean=True)
    try:
        s = socket.socket(); s.connect(("127.0.0.1", PORT))
        sql(s, "create table t (id int, val int)")
        sql(s, "insert into t values (1, 10)")     # autocommit
        sql(s, "begin")
        sql(s, "insert into t values (2, 20)")
        sql(s, "commit")
        sql(s, "begin")
        sql(s, "insert into t values (3, 30)")     # uncommitted
        s.close()
    except Exception as e:
        print("[FAIL] phase1 exception:", e); graceful_stop(p); return False
    # hard kill (simulate crash)
    p.kill(); p.wait()
    for _ in range(25):
        if not port_open(): break
        time.sleep(0.2)
    # phase 2: restart same db (no clean) -> recovery
    ok = True
    try:
        p2 = start("dbg_crash", clean=False)
    except Exception as e:
        print("[FAIL] recovery restart failed:", e); return False
    try:
        s = socket.socket(); s.connect(("127.0.0.1", PORT))
        r = norm(sql(s, "select * from t"))
        print("after recovery, select * from t:\n" + r)
        if "| 1 | 10 |" not in r:
            print("[FAIL] committed row (1,10) lost"); ok = False
        if "| 2 | 20 |" not in r:
            print("[FAIL] committed row (2,20) lost"); ok = False
        if "| 3 | 30 |" in r:
            print("[FAIL] uncommitted row (3,30) survived"); ok = False
        s.sendall(b"exit\x00"); s.close()
    except Exception as e:
        print("[FAIL] phase2 exception:", e); ok = False
    finally:
        graceful_stop(p2)
    print("[PASS] Crash recovery test" if ok else "[FAIL] Crash recovery test")
    return ok


if __name__ == "__main__":
    if not os.path.isfile(BIN):
        print("missing binary", BIN); sys.exit(1)
    a = test_abort()
    b = test_crash()
    print("\n==== SUMMARY ====")
    print("Abort:", "PASS" if a else "FAIL")
    print("Crash recovery:", "PASS" if b else "FAIL")
    sys.exit(0 if (a and b) else 1)
