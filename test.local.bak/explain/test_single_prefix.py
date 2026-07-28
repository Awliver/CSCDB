#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_single_prefix_db"
PORT = 8765

def wait_for_port(port, timeout=5):
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            s.connect(("127.0.0.1", port))
            s.close()
            return True
        except Exception:
            time.sleep(0.2)
    return False

def send_sql(sock, sql):
    sql = sql.strip()
    if not sql.endswith(';'):
        sql += ';'
    payload = sql.encode('utf-8') + b'\x00'
    sock.sendall(payload)
    data = b""
    while True:
        try:
            sock.settimeout(5.0)
            chunk = sock.recv(8192)
            if not chunk:
                break
            data += chunk
            if b'\x00' in data:
                break
        except socket.timeout:
            break
    parts = data.split(b'\x00')
    return parts[0].decode('utf-8', errors='replace') if parts else ""

# Clean up and kill any existing server
os.system("pkill -f 'rmdb.*test_single_prefix_db' 2>/dev/null")
import time
time.sleep(1)

if os.path.isdir(os.path.join(BUILD_DIR, "test_dbs", DB_NAME)):
    shutil.rmtree(os.path.join(BUILD_DIR, "test_dbs", DB_NAME))

os.makedirs(os.path.join(BUILD_DIR, "test_dbs"), exist_ok=True)
proc = subprocess.Popen(
    [os.path.relpath(SERVER_BIN, BUILD_DIR), os.path.join("test_dbs", DB_NAME)],
    cwd=BUILD_DIR,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
if not wait_for_port(PORT, timeout=6):
    out, err = proc.communicate(timeout=2)
    print("Server stdout:", out.decode())
    print("Server stderr:", err.decode())
    proc.kill()
    sys.exit(1)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5)
sock.connect(("127.0.0.1", PORT))

commands = [
    "CREATE TABLE t (a int, b int);",
    "INSERT INTO t VALUES (1, 5);",
    "INSERT INTO t VALUES (2, 8);",
    "INSERT INTO t VALUES (3, 12);",
    "INSERT INTO t VALUES (4, 6);",
    "INSERT INTO t VALUES (5, 20);",
]
for cmd in commands:
    r = send_sql(sock, cmd)
    if r.strip() and ("failure" in r or "Error" in r):
        print(f"CMD FAILED: {cmd} -> {r.strip()[:200]}")

# Test 1: Explicit table prefix in SELECT
q1 = "EXPLAIN ANALYZE SELECT t.a, t.b FROM t WHERE t.a > 1 AND t.b < 10;"
print(f"=== Test 1: SELECT t.a, t.b ===")
r1 = send_sql(sock, q1)
print(repr(r1))

# Test 2: No table prefix in SELECT
q2 = "EXPLAIN ANALYZE SELECT a, b FROM t WHERE a > 1 AND b < 10;"
print(f"=== Test 2: SELECT a, b ===")
r2 = send_sql(sock, q2)
print(repr(r2))

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
