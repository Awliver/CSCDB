#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_official_exact_db"
DB_DIR = os.path.join(BUILD_DIR, "test_dbs", DB_NAME)
PORT = 8765  # use different port

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

# 3.1 Single table
commands_31 = [
    "CREATE TABLE t (a int, b int);",
    "INSERT INTO t VALUES (1, 5);",
    "INSERT INTO t VALUES (2, 8);",
    "INSERT INTO t VALUES (3, 12);",
    "INSERT INTO t VALUES (4, 6);",
    "INSERT INTO t VALUES (5, 20);",
]
for cmd in commands_31:
    r = send_sql(sock, cmd)
    if r.strip() and ("failure" in r or "Error" in r):
        print(f"CMD FAILED: {cmd} -> {r.strip()[:200]}")

q31 = "EXPLAIN ANALYZE SELECT a, b FROM t WHERE a > 1 AND b < 10;"
print(f"=== 3.1 Single Table ===")
r31 = send_sql(sock, q31)
expected31 = "Project(columns=[t.a, t.b], rows=2)\n\tFilter(condition=[t.a>1, t.b<10], rows=2)\n\t\tScan(table=t, type=SeqScan, rows=5)\n"
print("ACTUAL:")
print(repr(r31))
print("EXPECTED:")
print(repr(expected31))
print("MATCH:", r31 == expected31)

# Drop table for next test
send_sql(sock, "DROP TABLE t;")

# 3.2 Selection pushdown
commands_32 = [
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
for cmd in commands_32:
    r = send_sql(sock, cmd)
    if r.strip() and ("failure" in r or "Error" in r):
        print(f"CMD FAILED: {cmd} -> {r.strip()[:200]}")

# Test with comma join (no alias, no ON)
q32 = "EXPLAIN ANALYZE SELECT * FROM customers, orders WHERE customers.customer_id = orders.customer_id AND orders.total_amount > 1000;"
print(f"\n=== 3.2 Selection Pushdown (comma join) ===")
r32 = send_sql(sock, q32)
expected32 = "Project(columns=[*], rows=2)\n\tJoin(tables=[customers, orders],condition=[customers.customer_id=orders.customer_id], rows=2)\n\t\tFilter(condition=[orders.total_amount>1000.0], rows=6)\n\t\t\tScan(table=orders, type=SeqScan, rows=15)\n\t\tScan(table=customers, type=SeqScan, rows=3)\n"
print("ACTUAL:")
print(repr(r32))
print("EXPECTED:")
print(repr(expected32))
print("MATCH:", r32 == expected32)

# Test with JOIN ON (official example syntax)
q32_on = "EXPLAIN ANALYZE SELECT * FROM customers c JOIN orders o ON c.customer_id = o.customer_id WHERE o.total_amount > 1000;"
print(f"\n=== 3.2 Selection Pushdown (JOIN ON with alias) ===")
r32_on = send_sql(sock, q32_on)
print("ACTUAL:")
print(repr(r32_on))

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
