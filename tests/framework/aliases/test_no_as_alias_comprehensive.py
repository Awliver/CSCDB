#!/usr/bin/env python3
"""
无AS别名全面测试集
覆盖: 基础聚合、GROUP BY、HAVING、ORDER BY、JOIN、空表、多聚合
每个测试标注: 当前行为 vs 期望行为
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_noas_db"
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

def has_error(reply):
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

# 建表插数
send_sql(sock, "CREATE TABLE grade (course char(20), id int, score float);")
send_sql(sock, "INSERT INTO grade VALUES ('DS', 1, 95.0);")
send_sql(sock, "INSERT INTO grade VALUES ('DS', 2, 93.5);")
send_sql(sock, "INSERT INTO grade VALUES ('CN', 1, 99.0);")
send_sql(sock, "INSERT INTO grade VALUES ('CN', 2, 88.5);")

send_sql(sock, "CREATE TABLE empty_grade (course char(20), id int, score float);")

print("="*80)
print("无AS别名全面测试集")
print("="*80)

# 测试结构: (描述, SQL, 期望数据行数, 备注)
tests = []

# === 类别1: 基础聚合无AS别名 ===
tests.extend([
    ("A1", "COUNT(*) 无AS", "SELECT COUNT(*) cnt FROM grade", 1, "应返回1行数据"),
    ("A2", "COUNT(*) 有AS(对比)", "SELECT COUNT(*) AS cnt FROM grade", 1, "应返回1行数据"),
    ("A3", "MAX 无AS", "SELECT MAX(score) mx FROM grade", 1, "应返回1行数据"),
    ("A4", "MIN 无AS", "SELECT MIN(score) mn FROM grade", 1, "应返回1行数据"),
    ("A5", "SUM 无AS", "SELECT SUM(score) sm FROM grade", 1, "应返回1行数据"),
    ("A6", "AVG 无AS", "SELECT AVG(score) av FROM grade", 1, "应返回1行数据"),
    ("A7", "多聚合无AS", "SELECT COUNT(*) cnt, MAX(score) mx, MIN(score) mn FROM grade", 1, "应返回1行数据"),
    ("A8", "多聚合混合(有AS+无AS)", "SELECT COUNT(*) AS cnt, MAX(score) mx FROM grade", 1, "应返回1行数据"),
])

# === 类别2: 空表聚合无AS别名 ===
tests.extend([
    ("B1", "空表COUNT无AS", "SELECT COUNT(*) cnt FROM empty_grade", 1, "空表应返回默认行"),
    ("B2", "空表MAX无AS", "SELECT MAX(score) mx FROM empty_grade", 1, "空表应返回默认行"),
    ("B3", "空表AVG无AS", "SELECT AVG(score) av FROM empty_grade", 1, "空表应返回默认行"),
    ("B4", "空表多聚合无AS", "SELECT COUNT(*) cnt, MAX(score) mx FROM empty_grade", 1, "空表应返回默认行"),
])

# === 类别3: GROUP BY + 无AS别名 ===
tests.extend([
    ("C1", "GROUP BY + COUNT无AS", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course", 2, "应返回2组"),
    ("C2", "GROUP BY + MAX无AS", "SELECT course, MAX(score) mx FROM grade GROUP BY course", 2, "应返回2组"),
    ("C3", "GROUP BY + 多聚合无AS", "SELECT course, COUNT(*) cnt, MAX(score) mx FROM grade GROUP BY course", 2, "应返回2组"),
    ("C4", "GROUP BY + 混合AS", "SELECT course, COUNT(*) AS cnt, MAX(score) mx FROM grade GROUP BY course", 2, "应返回2组"),
    ("C5", "多列GROUP BY + 无AS", "SELECT course, id, COUNT(*) cnt FROM grade GROUP BY course, id", 4, "应返回4组"),
])

# === 类别4: HAVING + 无AS别名 ===
tests.extend([
    ("D1", "HAVING聚合原名", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course HAVING COUNT(*) > 1", 2, "应返回2组"),
    ("D2", "HAVING聚合别名", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course HAVING cnt > 1", 2, "应返回2组(别名)"),
    ("D3", "HAVING过滤所有组", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course HAVING COUNT(*) > 10", 0, "应返回0组"),
])

# === 类别5: ORDER BY + 无AS别名 ===
tests.extend([
    ("E1", "ORDER BY聚合别名", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course ORDER BY cnt", 2, "应返回2组"),
    ("E2", "ORDER BY聚合别名DESC", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course ORDER BY cnt DESC", 2, "应返回2组"),
    ("E3", "ORDER BY+LIMIT", "SELECT course, COUNT(*) cnt FROM grade GROUP BY course ORDER BY cnt LIMIT 1", 1, "应返回1组"),
])

# === 类别6: 普通列无AS别名 ===
tests.extend([
    ("F1", "普通列无AS", "SELECT id i FROM grade", 4, "应返回4行"),
    ("F2", "普通列无AS+WHERE", "SELECT id i FROM grade WHERE id > 1", 2, "应返回2行"),
    ("F3", "普通列无AS+ORDER BY", "SELECT id i FROM grade ORDER BY i", 4, "应返回4行"),
])

# === 类别7: JOIN + 无AS别名聚合 ===
send_sql(sock, "CREATE TABLE student (id int, name char(10));")
send_sql(sock, "INSERT INTO student VALUES (1, 'A');")
send_sql(sock, "INSERT INTO student VALUES (2, 'B');")

tests.extend([
    ("G1", "JOIN+聚合无AS", "SELECT s.name, COUNT(*) cnt FROM grade g, student s WHERE g.id = s.id GROUP BY s.name", 2, "应返回2组"),
    ("G2", "JOIN+SUM无AS", "SELECT s.name, SUM(g.score) sm FROM grade g, student s WHERE g.id = s.id GROUP BY s.name", 2, "应返回2组"),
])

# 运行测试
print("\n" + "-"*80)
print(f"{'ID':<5} {'类别':<25} {'期望行':<8} {'实际行':<8} {'状态':<6} {'SQL'}")
print("-"*80)

passed = 0
failed = 0
for tid, desc, sql, expected, note in tests:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "PASS":
        passed += 1
    else:
        failed += 1
    marker = "  " if status == "PASS" else "<<"
    print(f"{marker} {tid:<4} {desc:<24} {expected:<8} {actual:<8} {status:<6} {sql[:45]}")
    if status == "FAIL":
        print(f"     备注: {note}")
        # 打印详细输出（截断）
        lines = r.strip().splitlines()
        for line in lines[:6]:
            print(f"     {line}")
        if len(lines) > 6:
            print(f"     ... ({len(lines)} lines total)")

send_sql(sock, "DROP TABLE grade;")
send_sql(sock, "DROP TABLE empty_grade;")
send_sql(sock, "DROP TABLE student;")

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

if failed > 0:
    print("\n[关键发现]")
    print("无AS别名语法('SELECT expr alias')在当前代码中不被支持，")
    print("导致聚合查询返回空结果。这可能是 remote test3 'Expected 2 got 1' 的根因。")
