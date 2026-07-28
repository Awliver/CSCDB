#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_debug_db4"
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

# Clean up
if os.path.isdir(DB_DIR):
    shutil.rmtree(DB_DIR)

# Start server
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

# Connect
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5)
sock.connect(("127.0.0.1", PORT))

# Create tables with different sizes
commands = [
    "CREATE TABLE small (id int, v int);",
    "CREATE TABLE medium (id int, v int);",
    "CREATE TABLE large (id int, v int);",
    "INSERT INTO small VALUES (1, 1);",
    "INSERT INTO small VALUES (2, 2);",
    "INSERT INTO medium VALUES (1, 1);",
    "INSERT INTO medium VALUES (2, 2);",
    "INSERT INTO medium VALUES (3, 3);",
    "INSERT INTO medium VALUES (4, 4);",
    "INSERT INTO large VALUES (1, 1);",
    "INSERT INTO large VALUES (2, 2);",
    "INSERT INTO large VALUES (3, 3);",
    "INSERT INTO large VALUES (4, 4);",
    "INSERT INTO large VALUES (5, 5);",
    "INSERT INTO large VALUES (6, 6);",
]

for cmd in commands:
    r = send_sql(sock, cmd)
    if r.strip():
        print(f"CMD: {cmd} -> {r.strip()[:80]}")

# Test queries
queries = [
    # Different sizes - greedy should pick small-medium first
    "EXPLAIN ANALYZE SELECT * FROM small, medium, large WHERE small.v = medium.v AND medium.v = large.v;",
    # Cross-join condition
    "EXPLAIN ANALYZE SELECT * FROM small, medium, large WHERE small.v = medium.v AND small.v = large.v;",
    # 4-table join
    "CREATE TABLE tiny (id int, v int);",
    "INSERT INTO tiny VALUES (1, 1);",
    "EXPLAIN ANALYZE SELECT * FROM tiny, small, medium, large WHERE tiny.v = small.v AND small.v = medium.v AND medium.v = large.v;",
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
