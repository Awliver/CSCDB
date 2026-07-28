#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_hybrid_db"
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
    print("Server failed:", err.decode())
    sys.exit(1)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5)
sock.connect(("127.0.0.1", PORT))

commands = [
    "CREATE TABLE A (a_id int, x int);",
    "CREATE TABLE B (b_id int, y int);",
    "CREATE TABLE C (c_id int, z int);",
    "INSERT INTO A VALUES (1, 1);",
    "INSERT INTO A VALUES (2, 2);",
    "INSERT INTO B VALUES (1, 1);",
    "INSERT INTO B VALUES (2, 2);",
    "INSERT INTO C VALUES (1, 1);",
    "INSERT INTO C VALUES (2, 2);",
]

for cmd in commands:
    send_sql(sock, cmd)

queries = [
    # 2-table join: should use greedy (same as SQL order)
    "EXPLAIN ANALYZE SELECT * FROM A, B WHERE A.x = B.y;",
    # 3-table join: should use Plan tree (make_one_rel) with swapped children
    "EXPLAIN ANALYZE SELECT * FROM A, B, C WHERE A.x = B.y AND B.y = C.z;",
    # 3-table with cross condition
    "EXPLAIN ANALYZE SELECT * FROM A, B, C WHERE A.x = B.y AND A.x = C.z;",
]

for q in queries:
    print(f"\n=== {q} ===")
    print(send_sql(sock, q))

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
