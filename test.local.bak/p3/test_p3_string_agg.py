#!/usr/bin/env python3
"""
STRING类型聚合函数测试
题目要求: MAX/MIN/SUM/AVG 仅支持 int、float
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_str_db"
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

def is_failure(reply):
    r = reply.strip().lower()
    return 'error' in r or 'failure' in r or 'exception' in r

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

send_sql(sock, "CREATE TABLE t (id int, name char(10), val float);")
send_sql(sock, "INSERT INTO t VALUES (1, 'Alice', 10.0);")
send_sql(sock, "INSERT INTO t VALUES (2, 'Bob', 20.0);")

print("="*70)
print("STRING类型聚合函数行为测试")
print("="*70)

tests = [
    # 应该成功的
    ("COUNT(*) int表", "SELECT COUNT(*) AS cnt FROM t", True, 1),
    ("COUNT(name) char列", "SELECT COUNT(name) AS cnt FROM t", True, 1),
    ("MAX(val) float列", "SELECT MAX(val) AS mx FROM t", True, 1),
    ("MIN(val) float列", "SELECT MIN(val) AS mn FROM t", True, 1),
    ("SUM(val) float列", "SELECT SUM(val) AS sm FROM t", True, 1),
    ("AVG(val) float列", "SELECT AVG(val) AS av FROM t", True, 1),
    ("MAX(id) int列", "SELECT MAX(id) AS mx FROM t", True, 1),
    ("MIN(id) int列", "SELECT MIN(id) AS mn FROM t", True, 1),
    ("SUM(id) int列", "SELECT SUM(id) AS sm FROM t", True, 1),
    ("AVG(id) int列", "SELECT AVG(id) AS av FROM t", True, 1),
    
    # 应该失败的（STRING类型的MAX/MIN/SUM/AVG）
    ("MAX(name) char列-应失败", "SELECT MAX(name) AS mx FROM t", False, 0),
    ("MIN(name) char列-应失败", "SELECT MIN(name) AS mn FROM t", False, 0),
    ("SUM(name) char列-应失败", "SELECT SUM(name) AS sm FROM t", False, 0),
    ("AVG(name) char列-应失败", "SELECT AVG(name) AS av FROM t", False, 0),
    
    # GROUP BY string
    ("GROUP BY char", "SELECT name, COUNT(*) AS cnt FROM t GROUP BY name", True, 2),
    ("GROUP BY char + ORDER BY", "SELECT name, COUNT(*) AS cnt FROM t GROUP BY name ORDER BY name", True, 2),
    
    # 混合: 正确聚合 + 错误聚合
    ("COUNT+MAX(char)-应失败", "SELECT COUNT(*), MAX(name) FROM t", False, 0),
]

failures = []
for desc, sql, should_succeed, expected_rows in tests:
    r = send_sql(sock, sql)
    actual_rows = count_data_lines(r)
    failed = is_failure(r)
    
    if should_succeed and failed:
        failures.append((desc, sql, "应成功但失败", r))
        print(f"[FAIL] {desc}: 应成功但失败")
        print(f"  {sql}")
        print(f"  输出: {r[:200]}")
    elif not should_succeed and not failed:
        failures.append((desc, sql, "应失败但成功", r))
        print(f"[FAIL] {desc}: 应失败但成功")
        print(f"  {sql}")
        print(f"  输出: {r[:200]}")
    elif should_succeed and actual_rows != expected_rows:
        failures.append((desc, sql, f"行数不匹配 期望{expected_rows}实际{actual_rows}", r))
        print(f"[FAIL] {desc}: 行数不匹配")
        print(f"  期望: {expected_rows}, 实际: {actual_rows}")
        print(f"  输出: {r[:200]}")
    else:
        print(f"[PASS] {desc}")

send_sql(sock, "DROP TABLE t;")

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
    for desc, sql, reason, r in failures:
        print(f"  - {desc}: {reason}")
else:
    print("全部通过")
print("="*70)
