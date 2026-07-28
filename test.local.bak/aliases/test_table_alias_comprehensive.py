#!/usr/bin/env python3
"""
表别名全面测试集
覆盖: SELECT/WHERE/GROUP BY/ORDER BY/HAVING/JOIN 中表别名的使用
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_ta_db"
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

# 建表
send_sql(sock, "CREATE TABLE t1 (id int, name char(10), val int);")
send_sql(sock, "CREATE TABLE t2 (id int, score float);")
send_sql(sock, "INSERT INTO t1 VALUES (1, 'A', 10);")
send_sql(sock, "INSERT INTO t1 VALUES (2, 'B', 20);")
send_sql(sock, "INSERT INTO t2 VALUES (1, 95.0);")
send_sql(sock, "INSERT INTO t2 VALUES (2, 88.0);")

print("="*80)
print("表别名全面测试集")
print("="*80)

# 测试结构: (ID, 描述, SQL, 期望数据行)
tests = []

# === 类别A: 单表查询 ===
tests.extend([
    ("A1", "单表SELECT别名", "SELECT a.id FROM t1 a", 2),
    ("A2", "单表WHERE别名", "SELECT a.id FROM t1 a WHERE a.id = 1", 1),
    ("A3", "单表ORDER BY别名", "SELECT a.id FROM t1 a ORDER BY a.id", 2),
    ("A4", "单表GROUP BY别名", "SELECT a.name, COUNT(*) FROM t1 a GROUP BY a.name", 2),
    ("A5", "单表多条件别名", "SELECT a.id FROM t1 a WHERE a.id > 0 AND a.val > 5", 2),
])

# === 类别B: 两表JOIN ===
tests.extend([
    ("B1", "JOIN SELECT别名", "SELECT a.id, b.score FROM t1 a, t2 b WHERE a.id = b.id", 2),
    ("B2", "JOIN WHERE别名", "SELECT a.id FROM t1 a, t2 b WHERE a.id = b.id AND b.score > 90", 1),
    ("B3", "JOIN ORDER BY别名", "SELECT a.id FROM t1 a, t2 b WHERE a.id = b.id ORDER BY a.id", 2),
    ("B4", "JOIN GROUP BY别名", "SELECT a.name, COUNT(*) FROM t1 a, t2 b WHERE a.id = b.id GROUP BY a.name", 2),
    ("B5", "JOIN聚合+别名", "SELECT a.name, SUM(b.score) FROM t1 a, t2 b WHERE a.id = b.id GROUP BY a.name", 2),
    ("B6", "JOIN聚合别名+ORDER BY别名", "SELECT a.name, SUM(b.score) s FROM t1 a, t2 b WHERE a.id = b.id GROUP BY a.name ORDER BY s", 2),
])

# === 类别C: 三表JOIN ===
send_sql(sock, "CREATE TABLE t3 (id int, grade char(5));")
send_sql(sock, "INSERT INTO t3 VALUES (1, 'A');")
send_sql(sock, "INSERT INTO t3 VALUES (2, 'B');")

tests.extend([
    ("C1", "三表JOIN SELECT别名", "SELECT a.id, b.score, c.grade FROM t1 a, t2 b, t3 c WHERE a.id = b.id AND b.id = c.id", 2),
    ("C2", "三表JOIN聚合别名", "SELECT a.name, SUM(b.score), COUNT(*) FROM t1 a, t2 b, t3 c WHERE a.id = b.id AND b.id = c.id GROUP BY a.name", 2),
])

# === 类别D: 原始表名对比(应全部通过) ===
tests.extend([
    ("D1", "单表SELECT原始名", "SELECT t1.id FROM t1", 2),
    ("D2", "单表WHERE原始名", "SELECT t1.id FROM t1 WHERE t1.id = 1", 1),
    ("D3", "JOIN聚合原始名", "SELECT t1.name, SUM(t2.score) FROM t1, t2 WHERE t1.id = t2.id GROUP BY t1.name", 2),
    ("D4", "三表JOIN原始名", "SELECT t1.id, t2.score, t3.grade FROM t1, t2, t3 WHERE t1.id = t2.id AND t2.id = t3.id", 2),
])

# 运行测试
print(f"\n{'ID':<5} {'类别':<30} {'期望':<6} {'实际':<6} {'状态':<6} {'SQL'}")
print("-"*80)

passed = 0
failed = 0
failures = []

for tid, desc, sql, expected in tests:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "PASS":
        passed += 1
    else:
        failed += 1
        failures.append((tid, desc, sql, expected, actual, r))
    marker = "  " if status == "PASS" else "<<"
    print(f"{marker} {tid:<4} {desc:<29} {expected:<6} {actual:<6} {status:<6} {sql[:40]}")

send_sql(sock, "DROP TABLE t1;")
send_sql(sock, "DROP TABLE t2;")
send_sql(sock, "DROP TABLE t3;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print("-"*80)
print(f"总计: {passed}/{len(tests)} 通过, {failed}/{len(tests)} 失败")
print("="*80)

if failures:
    print("\n[失败详情]")
    for tid, desc, sql, exp, act, r in failures:
        print(f"\n{tid} {desc}")
        print(f"  SQL: {sql}")
        print(f"  期望: {exp} 行, 实际: {act} 行")
        lines = r.strip().splitlines()
        for line in lines[:5]:
            print(f"  {line}")
        if len(lines) > 5:
            print(f"  ... ({len(lines)} lines)")
    
    print("\n[关键结论]")
    print("表别名('FROM table alias')在当前代码中不被支持。")
    print("所有使用别名引用列的查询(a.id, a.name等)均返回空结果。")
    print("这可能是 remote test3 'Expected 2 got 1' 的根因。")
