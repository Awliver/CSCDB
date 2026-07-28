#!/usr/bin/env python3
import os, socket, subprocess, time, shutil, signal, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
BUILD = os.path.join(ROOT, "build")
PORT = 8765
RMDB = os.path.join(BUILD, "bin/rmdb")

def kill_rmdb():
    subprocess.run(["pkill", "-9", "-f", RMDB], stderr=subprocess.DEVNULL)
    time.sleep(0.4)

def send_sql(sock, sql, timeout=30):
    sock.settimeout(timeout)
    sock.sendall((sql + "\0").encode())
    b = b""
    while b"\0" not in b:
        chunk = sock.recv(65536)
        if not chunk:
            break
        b += chunk
    return b

def recovery_test():
    kill_rmdb()
    db = "p3_recovery"
    dbpath = os.path.join(BUILD, db)
    if os.path.isdir(dbpath):
        shutil.rmtree(dbpath)
    proc = subprocess.Popen([RMDB, db], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    s = socket.socket()
    s.connect(("127.0.0.1", PORT))
    for sql in [
        "create table t (id int, v int);",
        "create index t(id);",
        "insert into t values (1,10);",
        "insert into t values (2,20);",
        "insert into t values (3,30);",
    ]:
        send_sql(s, sql)
    s.close()
    os.kill(proc.pid, signal.SIGKILL)
    proc.wait()
    time.sleep(0.5)
    proc2 = subprocess.Popen([RMDB, db], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    s2 = socket.socket()
    s2.connect(("127.0.0.1", PORT))
    send_sql(s2, "select * from t where id = 2;")
    s2.close()
    proc2.kill()
    proc2.wait()
    out = open(os.path.join(dbpath, "output.txt")).read()
    ok = "2" in out and "20" in out
    print("RECOVERY:", "PASS" if ok else "FAIL")
    if not ok:
        print(out)
    return ok

def perf_test(nrows=3000, nqueries=500):
    kill_rmdb()
    db = "p3_perf"
    dbpath = os.path.join(BUILD, db)
    if os.path.isdir(dbpath):
        shutil.rmtree(dbpath)
    proc = subprocess.Popen([RMDB, db], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    s = socket.socket()
    s.connect(("127.0.0.1", PORT))
    send_sql(s, "create table t (id int);")
    for i in range(nrows):
        send_sql(s, f"insert into t values ({i});")
    # 数千条查询：题三 OJ 性能测要求
    t0 = time.time()
    for i in range(nqueries):
        send_sql(s, f"select * from t where id = {i % nrows};")
    t_seq = time.time() - t0
    send_sql(s, "create index t(id);")
    # 预热索引页，避免冷启动影响
    for _ in range(10):
        send_sql(s, "select * from t where id = 0;")
    t1 = time.time()
    for i in range(nqueries):
        send_sql(s, f"select * from t where id = {i % nrows};")
    t_idx = time.time() - t1
    s.close()
    proc.kill()
    proc.wait()
    ratio = t_idx / t_seq if t_seq > 0 else 999
    ok = ratio < 0.7
    print(f"PERF: seq={t_seq:.3f}s idx={t_idx:.3f}s ratio={ratio:.3f} -> {'PASS' if ok else 'FAIL'}")
    return ok

if __name__ == "__main__":
    r = recovery_test()
    p = perf_test()
    sys.exit(0 if r and p else 1)
