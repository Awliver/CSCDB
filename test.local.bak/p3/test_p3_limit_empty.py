#!/usr/bin/env python3
"""
空表聚合 + LIMIT 组合测试
Remote test3: Expected 2 got 1，可能涉及LIMIT+空表聚合
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_lim_db"
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

def count_data_lines(reply):
    lines = reply.strip().splitlines()
    data_lines = [l for l in lines if l.strip().startswith('|')]
    return max(0, len(data_lines) - 1) if data_lines else 0

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

send_sql(sock, "CREATE TABLE empty_t (id int, val float);")

print("="*70)
print("空表聚合 + LIMIT 穷举")
print("="*70)

tests = [
    ("空表 COUNT(*) LIMIT 1", "SELECT COUNT(*) AS cnt FROM empty_t LIMIT 1", 1),
    ("空表 COUNT(*) LIMIT 2", "SELECT COUNT(*) AS cnt FROM empty_t LIMIT 2", 1),
    ("空表 COUNT(*) LIMIT 0", "SELECT COUNT(*) AS cnt FROM empty_t LIMIT 0", 0),
    ("空表 MAX LIMIT 1", "SELECT MAX(val) AS mx FROM empty_t LIMIT 1", 1),
    ("空表 MIN LIMIT 1", "SELECT MIN(val) AS mn FROM empty_t LIMIT 1", 1),
    ("空表 SUM LIMIT 1", "SELECT SUM(val) AS sm FROM empty_t LIMIT 1", 1),
    ("空表 AVG LIMIT 1", "SELECT AVG(val) AS av FROM empty_t LIMIT 1", 1),
    ("空表 多聚合 LIMIT 1", "SELECT COUNT(*) AS cnt, MAX(val) AS mx FROM empty_t LIMIT 1", 1),
    ("空表 多聚合 LIMIT 0", "SELECT COUNT(*) AS cnt, MAX(val) AS mx FROM empty_t LIMIT 0", 0),
    ("空表 COUNT(*) 无LIMIT", "SELECT COUNT(*) AS cnt FROM empty_t", 1),
]

failures = []
for desc, sql, expected in tests:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "FAIL":
        failures.append((desc, expected, actual))
        print(f"[{status}] {desc}")
        print(f"  期望: {expected} 行, 实际: {actual} 行")
        print(f"  输出:\n{r}")
    else:
        print(f"[{status}] {desc} ({actual}行)")

print("\n" + "="*70)
print("非空表聚合 + LIMIT（对比）")
print("="*70)

send_sql(sock, "CREATE TABLE data_t (id int, val float);")
send_sql(sock, "INSERT INTO data_t VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO data_t VALUES (2, 20.0);")

tests2 = [
    ("有数据 COUNT(*) LIMIT 1", "SELECT COUNT(*) AS cnt FROM data_t LIMIT 1", 1),
    ("有数据 COUNT(*) LIMIT 0", "SELECT COUNT(*) AS cnt FROM data_t LIMIT 0", 0),
    ("有数据 GROUP BY LIMIT 1", "SELECT id, COUNT(*) AS cnt FROM data_t GROUP BY id LIMIT 1", 1),
    ("有数据 GROUP BY LIMIT 3", "SELECT id, COUNT(*) AS cnt FROM data_t GROUP BY id LIMIT 3", 2),
]

for desc, sql, expected in tests2:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "FAIL":
        failures.append((desc, expected, actual))
        print(f"[{status}] {desc}")
        print(f"  期望: {expected} 行, 实际: {actual} 行")
        print(f"  输出:\n{r}")
    else:
        print(f"[{status}] {desc} ({actual}行)")

send_sql(sock, "DROP TABLE empty_t;")
send_sql(sock, "DROP TABLE data_t;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print("\n" + "="*70)
if failures:
    print(f"失败 {len(failures)} 个:")
    for desc, exp, act in failures:
        print(f"  - {desc}: 期望{exp}, 实际{act}")
else:
    print("全部通过")
print("="*70)
