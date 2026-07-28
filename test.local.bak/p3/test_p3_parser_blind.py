#!/usr/bin/env python3
"""
Parser/Analyze 盲区测试
test3 在原始代码就失败 -> 根因可能在 parser/analyze 阶段
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_par_db"
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

send_sql(sock, "CREATE TABLE t (id int, val float);")

print("="*70)
print("Parser/Analyze 盲区测试")
print("="*70)

# 测试1: 大小写敏感的聚合函数名
tests_case = [
    ("大写 COUNT", "SELECT COUNT(*) FROM t", 1),
    ("小写 count", "SELECT count(*) FROM t", 1),
    ("大小写混合 Count", "SELECT Count(*) FROM t", 1),
    ("大写 MAX", "SELECT MAX(val) FROM t", 1),
    ("小写 max", "SELECT max(val) FROM t", 1),
    ("大写 MIN", "SELECT MIN(val) FROM t", 1),
    ("小写 min", "SELECT min(val) FROM t", 1),
    ("大写 SUM", "SELECT SUM(val) FROM t", 1),
    ("小写 sum", "SELECT sum(val) FROM t", 1),
    ("大写 AVG", "SELECT AVG(val) FROM t", 1),
    ("小写 avg", "SELECT avg(val) FROM t", 1),
]

print("\n--- 大小写测试 ---")
for desc, sql, expected in tests_case:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "FAIL":
        print(f"[{status}] {desc}: 期望{expected}行, 实际{actual}行")
        print(f"  {sql}")
        print(f"  {r}")
    else:
        print(f"[{status}] {desc}")

# 测试2: 空格变化
tests_space = [
    ("COUNT(*)无空格", "SELECT COUNT(*) FROM t", 1),
    ("COUNT( * )有空格", "SELECT COUNT( * ) FROM t", 1),
    ("COUNT(  *  )多空格", "SELECT COUNT(  *  ) FROM t", 1),
    ("MAX(val)无空格", "SELECT MAX(val) FROM t", 1),
    ("MAX( val )有空格", "SELECT MAX( val ) FROM t", 1),
]

print("\n--- 空格变化测试 ---")
for desc, sql, expected in tests_space:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "FAIL":
        print(f"[{status}] {desc}: 期望{expected}行, 实际{actual}行")
        print(f"  {sql}")
        print(f"  {r}")
    else:
        print(f"[{status}] {desc}")

# 测试3: AS别名变化
tests_alias = [
    ("AS别名", "SELECT COUNT(*) AS cnt FROM t", 1),
    ("无AS直接别名", "SELECT COUNT(*) cnt FROM t", 1),
    ("复杂别名", "SELECT COUNT(*) AS total_count FROM t", 1),
]

print("\n--- 别名测试 ---")
for desc, sql, expected in tests_alias:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "FAIL":
        print(f"[{status}] {desc}: 期望{expected}行, 实际{actual}行")
        print(f"  {sql}")
        print(f"  {r}")
    else:
        print(f"[{status}] {desc}")

# 测试4: 表名/列名大小写
tests_tabname = [
    ("表名大写", "SELECT COUNT(*) FROM T", 1),
    ("列名大写", "SELECT COUNT(ID) FROM t", 1),
    ("列名小写", "SELECT COUNT(id) FROM t", 1),
]

print("\n--- 表名列名大小写测试 ---")
for desc, sql, expected in tests_tabname:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "FAIL":
        print(f"[{status}] {desc}: 期望{expected}行, 实际{actual}行")
        print(f"  {sql}")
        print(f"  {r}")
    else:
        print(f"[{status}] {desc}")

# 测试5: 特殊查询结构
send_sql(sock, "INSERT INTO t VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO t VALUES (2, 20.0);")

tests_special = [
    ("SELECT * + 聚合", "SELECT *, COUNT(*) FROM t", 0),  # 应失败
    ("聚合在子查询位置", "SELECT (SELECT COUNT(*) FROM t) FROM t", 0),  # 可能失败
    ("多个COUNT", "SELECT COUNT(*), COUNT(id) FROM t", 1),
    ("COUNT+MAX+MIN", "SELECT COUNT(*), MAX(val), MIN(val) FROM t", 1),
]

print("\n--- 特殊结构测试 ---")
for desc, sql, expected in tests_special:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "FAIL":
        print(f"[{status}] {desc}: 期望{expected}行, 实际{actual}行")
        print(f"  {sql}")
        print(f"  {r}")
    else:
        print(f"[{status}] {desc}")

send_sql(sock, "DROP TABLE t;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print("\n" + "="*70)
print("测试完成")
print("="*70)
