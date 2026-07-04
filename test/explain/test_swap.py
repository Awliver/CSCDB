#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_swap_db"
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
    "CREATE TABLE large (id int, name char(10));",
    "CREATE TABLE small (id int, val int);",
    "INSERT INTO large VALUES (1, 'large1');",
    "INSERT INTO large VALUES (2, 'large2');",
    "INSERT INTO large VALUES (3, 'large3');",
    "INSERT INTO large VALUES (4, 'large4');",
    "INSERT INTO small VALUES (1, 100);",
    "INSERT INTO small VALUES (2, 200);",
]

for cmd in commands:
    r = send_sql(sock, cmd)
    if r.strip() and ("failure" in r or "Error" in r):
        print(f"CMD FAILED: {cmd} -> {r.strip()[:200]}")

# Test: large on LHS in condition, but small should be outer (driver)
q1 = "SELECT * FROM large, small WHERE large.id = small.id;"
print(f"\n=== Normal SELECT: {q1} ===")
r1 = send_sql(sock, q1)
print(r1)

q2 = "EXPLAIN ANALYZE SELECT * FROM large, small WHERE large.id = small.id;"
print(f"\n=== EXPLAIN: {q2} ===")
r2 = send_sql(sock, q2)
print(r2)

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
