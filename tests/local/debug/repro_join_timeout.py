#!/usr/bin/env python3
"""Reproduce OJ-like Join Test: large NLJ then INLJ. Fail if any query > 60s."""
import os, sys, time, socket, subprocess, shutil, signal

ROOT = "/home/neo/CSC_DB/db2026"
BUILD = os.path.join(ROOT, "build")
SERVER = os.path.join(BUILD, "bin", "rmdb")
DB_NAME = "repro_join_timeout"
DB_DIR = os.path.join(BUILD, "test_dbs", DB_NAME)
PORT = 8765
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

def kill_rmdb():
    subprocess.call("ps aux | grep '[b]in/rmdb' | awk '{print $2}' | xargs -r kill -9", shell=True)
    time.sleep(0.3)

def wait_port(timeout=8):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            s = socket.create_connection(("127.0.0.1", PORT), 0.3)
            s.close(); return True
        except Exception:
            time.sleep(0.1)
    return False

def send(sock, sql, timeout=120):
    if not sql.endswith(';'): sql += ';'
    sock.sendall(sql.encode() + b'\x00')
    data = b''; t0 = time.time()
    while time.time() - t0 < timeout:
        sock.settimeout(max(0.1, timeout - (time.time() - t0)))
        try:
            chunk = sock.recv(1 << 20)
            if not chunk: break
            data += chunk
            if b'\x00' in data: break
        except socket.timeout:
            continue
    return data.split(b'\x00')[0].decode('utf-8', 'replace')

kill_rmdb()
if os.path.isdir(DB_DIR): shutil.rmtree(DB_DIR)
os.makedirs(os.path.join(BUILD, "test_dbs"), exist_ok=True)
logf = open(os.path.join(BUILD, "test_dbs", "repro_join.log"), "w")
proc = subprocess.Popen([os.path.relpath(SERVER, BUILD), os.path.join("test_dbs", DB_NAME)],
                        cwd=BUILD, stdout=logf, stderr=subprocess.STDOUT)
if not wait_port():
    print("FAIL: server start"); sys.exit(1)

sock = socket.create_connection(("127.0.0.1", PORT), 5)
sock.settimeout(5)

def run(sql, label=None, timeout=120):
    label = label or sql[:60]
    t0 = time.time()
    r = send(sock, sql, timeout=timeout)
    dt = time.time() - t0
    err = r.startswith("Error") or "InternalError" in r or "buffer pool" in r
    print(f"[{dt:7.2f}s] {label}  err={err}  out_len={len(r)}")
    if err: print("  ", r[:200])
    if dt > 60: print("  !! SLOW >60s")
    return dt, r

run("create table t1(id int, v int);")
run("create table t2(id int, v int);")
print(f"loading {N} rows...")
t0 = time.time()
for i in range(N):
    # partial match: ~10% join hits
    v = i if (i % 10 == 0) else -i
    send(sock, f"insert into t1 values ({i}, {v});", timeout=30)
    send(sock, f"insert into t2 values ({i}, {i});", timeout=30)
    if i % 500 == 0 and i:
        print(f"  inserted {i}/{N}")
print(f"load done in {time.time()-t0:.1f}s")

# NLJ phase (no index)
dt, r = run(f"select t1.id, t2.id from t1 join t2 on t1.v = t2.v;", "NLJ select", timeout=180)
dt2, _ = run(f"explain analyze select t1.id, t2.id from t1 join t2 on t1.v = t2.v;", "NLJ explain", timeout=180)

# INLJ phase
run("create index t2(v);")
dt3, _ = run(f"select t1.id, t2.id from t1 join t2 on t1.v = t2.v;", "INLJ select", timeout=180)
dt4, out = run(f"explain analyze select t1.id, t2.id from t1 join t2 on t1.v = t2.v;", "INLJ explain", timeout=180)
print("--- explain ---")
print(out[:500])

send(sock, "exit;")
sock.close()
proc.wait(timeout=5)
kill_rmdb()
print("DONE", "PASS" if max(dt,dt2,dt3,dt4) < 60 else "TOO_SLOW")
