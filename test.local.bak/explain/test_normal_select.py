#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_normal_db"
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
    "CREATE TABLE orders (order_id int, customer_id int, order_date char(40), total_amount float);",
    "CREATE TABLE customers (customer_id int, name char(50), email char(100), address char(200));",
    "INSERT INTO customers VALUES (1, 'Alice', 'alice@example.com', 'A Street');",
    "INSERT INTO customers VALUES (2, 'Bob', 'bob@example.com', 'B Street');",
    "INSERT INTO customers VALUES (3, 'Carol', 'carol@example.com', 'C Street');",
    "INSERT INTO orders VALUES (101, 1, '2025-01-01', 500.0);",
    "INSERT INTO orders VALUES (102, 1, '2025-01-02', 1200.0);",
    "INSERT INTO orders VALUES (103, 2, '2025-01-03', 900.0);",
    "INSERT INTO orders VALUES (104, 2, '2025-01-04', 1500.0);",
    "INSERT INTO orders VALUES (105, 3, '2025-01-05', 700.0);",
]

for cmd in commands:
    r = send_sql(sock, cmd)
    if r.strip() and ("failure" in r or "Error" in r):
        print(f"CMD FAILED: {cmd} -> {r.strip()[:200]}")

queries = [
    "SELECT * FROM customers, orders WHERE customers.customer_id = orders.customer_id AND orders.total_amount > 1000;",
    "SELECT customers.name, orders.order_id FROM customers, orders WHERE customers.customer_id = orders.customer_id;",
]

for q in queries:
    print(f"\n=== Normal SELECT: {q} ===")
    r = send_sql(sock, q)
    print(r)

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
