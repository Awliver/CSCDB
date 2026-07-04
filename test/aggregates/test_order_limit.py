#!/usr/bin/env python3
"""
ORDER BY + 聚合别名 + LIMIT 测试
覆盖: ORDER BY聚合别名, 多列排序, DESC/ASC混合, 排序+LIMIT
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_order_db"
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

send_sql(sock, "CREATE TABLE t (grp char(5), val int, name char(10));")
send_sql(sock, "INSERT INTO t VALUES ('A', 30, 'z');")
send_sql(sock, "INSERT INTO t VALUES ('A', 20, 'y');")
send_sql(sock, "INSERT INTO t VALUES ('B', 50, 'x');")
send_sql(sock, "INSERT INTO t VALUES ('B', 10, 'w');")
send_sql(sock, "INSERT INTO t VALUES ('C', 40, 'v');")

print("\n" + "="*70)
print("测试: ORDER BY 聚合别名 + LIMIT")
print("="*70)

queries = [
    ("ORDER BY COUNT DESC", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp ORDER BY cnt DESC LIMIT 3"),
    ("ORDER BY COUNT ASC", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp ORDER BY cnt ASC LIMIT 3"),
    ("ORDER BY SUM DESC", "SELECT grp, SUM(val) AS total FROM t GROUP BY grp ORDER BY total DESC LIMIT 2"),
    ("ORDER BY AVG", "SELECT grp, AVG(val) AS av FROM t GROUP BY grp ORDER BY av LIMIT 2"),
    ("ORDER BY MAX", "SELECT grp, MAX(val) AS mx FROM t GROUP BY grp ORDER BY mx DESC LIMIT 2"),
    ("ORDER BY MIN", "SELECT grp, MIN(val) AS mn FROM t GROUP BY grp ORDER BY mn LIMIT 2"),
]

for desc, sql in queries:
    reply = send_sql(sock, sql)
    rows = count_result_lines(reply)
    lines = get_result_lines(reply)
    print(f"\n[{desc}] {sql}")
    print(f"  行数: {rows}")
    for line in lines:
        print(f"  {line}")

print("\n" + "="*70)
print("测试: 多列排序 + LIMIT")
print("="*70)

queries2 = [
    ("多列 ORDER BY 原始列", "SELECT grp, val, name FROM t ORDER BY grp ASC, val DESC LIMIT 5"),
    ("ORDER BY GROUP BY列", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp ORDER BY grp DESC LIMIT 3"),
]

for desc, sql in queries2:
    reply = send_sql(sock, sql)
    rows = count_result_lines(reply)
    lines = get_result_lines(reply)
    print(f"\n[{desc}] {sql}")
    print(f"  行数: {rows}")
    for line in lines:
        print(f"  {line}")

print("\n" + "="*70)
print("测试: ORDER BY + HAVING + LIMIT")
print("="*70)

queries3 = [
    ("HAVING+ORDER BY+LIMIT", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp HAVING COUNT(*) > 1 ORDER BY cnt DESC LIMIT 2"),
    ("HAVING+ORDER BY别名", "SELECT grp, SUM(val) AS total FROM t GROUP BY grp HAVING total > 30 ORDER BY total DESC LIMIT 2"),
]

for desc, sql in queries3:
    reply = send_sql(sock, sql)
    rows = count_result_lines(reply)
    lines = get_result_lines(reply)
    print(f"\n[{desc}] {sql}")
    print(f"  行数: {rows}")
    for line in lines:
        print(f"  {line}")

# 测试字符串排序
print("\n" + "="*70)
print("测试: 字符串列 ORDER BY + LIMIT")
print("="*70)

queries4 = [
    ("字符串 ORDER BY", "SELECT name, val FROM t ORDER BY name LIMIT 3"),
    ("字符串 ORDER BY DESC", "SELECT name, val FROM t ORDER BY name DESC LIMIT 3"),
]

for desc, sql in queries4:
    reply = send_sql(sock, sql)
    rows = count_result_lines(reply)
    lines = get_result_lines(reply)
    print(f"\n[{desc}] {sql}")
    print(f"  行数: {rows}")
    for line in lines:
        print(f"  {line}")

send_sql(sock, "DROP TABLE t;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
