#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_debug_db8"
DB_DIR = os.path.join(BUILD_DIR, "test_dbs", DB_NAME)
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
            sock.settimeout(3.0)
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

if os.path.isdir(DB_DIR):
    shutil.rmtree(DB_DIR)

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
    "CREATE TABLE A (a_id int, x int);",
    "CREATE TABLE B (b_id int, x int);",
    "INSERT INTO A VALUES (1, 1);",
    "INSERT INTO A VALUES (2, 2);",
    "INSERT INTO B VALUES (1, 1);",
    "INSERT INTO B VALUES (2, 2);",
]

for cmd in commands:
    r = send_sql(sock, cmd)
    if r.strip():
        print(f"CMD: {cmd} -> {r.strip()[:80]}")

queries = [
    # test6-like: 单表查询，无 WHERE
    "EXPLAIN ANALYZE SELECT a_id FROM A;",
    # test7-like: 两表连接，只有 join 条件
    "EXPLAIN ANALYZE SELECT A.a_id, B.b_id FROM A, B WHERE A.x = B.x;",
    # test8-like: 投影下推，SELECT 列包含 join 条件列
    "EXPLAIN ANALYZE SELECT A.x, B.b_id FROM A, B WHERE A.x = B.x;",
    # 空结果集
    "EXPLAIN ANALYZE SELECT A.a_id, B.b_id FROM A, B WHERE A.x = B.x AND A.x > 100;",
    # 多表查询，某表无 join 条件
    "EXPLAIN ANALYZE SELECT A.a_id, B.b_id FROM A, B WHERE A.a_id = 1;",
]

for q in queries:
    print(f"\n=== Query: {q} ===")
    result = send_sql(sock, q)
    print(result)

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
