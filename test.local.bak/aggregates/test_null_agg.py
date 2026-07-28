#!/usr/bin/env python3
"""
NULL 值 + 聚合 + LIMIT 测试
覆盖: NULL在聚合中的行为, COUNT(*) vs COUNT(col), 空表默认值
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_null_db"
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

def count_result_lines(reply):
    lines = reply.strip().splitlines()
    data_lines = [l for l in lines if l.strip().startswith('|')]
    return max(0, len(data_lines) - 1) if data_lines else 0

def get_result_lines(reply):
    lines = reply.strip().splitlines()
    data_lines = [l.strip() for l in lines if l.strip().startswith('|')]
    if len(data_lines) > 1:
        return data_lines[1:]
    return []

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

send_sql(sock, "CREATE TABLE t (id int, val float);")
# 插入含 NULL 的数据 (通过不插入某些列来模拟)
# 注意: RMDB 可能不支持显式 NULL，我们先测试框架是否支持
# 这里我们用不同方式: 插入 val 为 0 和正常值的混合
send_sql(sock, "INSERT INTO t VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO t VALUES (2, 20.0);")
send_sql(sock, "INSERT INTO t VALUES (3, 30.0);")
send_sql(sock, "INSERT INTO t VALUES (4, 0.0);")
send_sql(sock, "INSERT INTO t VALUES (5, 0.0);")

print("\n" + "="*70)
print("测试: 0值在聚合中的行为 (模拟NULL边界)")
print("="*70)

queries = [
    ("COUNT(*)", "SELECT COUNT(*) AS cnt FROM t LIMIT 1"),
    ("COUNT(val)", "SELECT COUNT(val) AS cnt FROM t LIMIT 1"),
    ("SUM(val)", "SELECT SUM(val) AS sm FROM t LIMIT 1"),
    ("AVG(val)", "SELECT AVG(val) AS av FROM t LIMIT 1"),
    ("MAX(val)", "SELECT MAX(val) AS mx FROM t LIMIT 1"),
    ("MIN(val)", "SELECT MIN(val) AS mn FROM t LIMIT 1"),
    ("GROUP BY + COUNT", "SELECT val, COUNT(*) AS cnt FROM t GROUP BY val LIMIT 10"),
    ("GROUP BY + SUM", "SELECT val, SUM(id) AS sm FROM t GROUP BY val LIMIT 10"),
    ("HAVING 0值", "SELECT val, COUNT(*) AS cnt FROM t GROUP BY val HAVING val = 0 LIMIT 10"),
]

for desc, sql in queries:
    reply = send_sql(sock, sql)
    rows = count_result_lines(reply)
    lines = get_result_lines(reply)
    print(f"\n[{desc}] {sql}")
    print(f"  行数: {rows}")
    for line in lines:
        print(f"  {line}")

# 测试空表默认值
send_sql(sock, "CREATE TABLE empty_t (id int, val float);")
print("\n" + "="*70)
print("空表默认值测试")
print("="*70)

empty_queries = [
    ("空表 COUNT(*)", "SELECT COUNT(*) FROM empty_t LIMIT 1"),
    ("空表 MAX", "SELECT MAX(val) FROM empty_t LIMIT 1"),
    ("空表 MIN", "SELECT MIN(val) FROM empty_t LIMIT 1"),
    ("空表 SUM", "SELECT SUM(val) FROM empty_t LIMIT 1"),
    ("空表 AVG", "SELECT AVG(val) FROM empty_t LIMIT 1"),
]

for desc, sql in empty_queries:
    reply = send_sql(sock, sql)
    rows = count_result_lines(reply)
    lines = get_result_lines(reply)
    print(f"\n[{desc}]")
    print(f"  行数: {rows}")
    for line in lines:
        print(f"  {line}")

send_sql(sock, "DROP TABLE t;")
send_sql(sock, "DROP TABLE empty_t;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
