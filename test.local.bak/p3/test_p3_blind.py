#!/usr/bin/env python3
"""
basic_query_test3 盲区穷举
Expected 2 got 1 -> 某个查询期望输出2行实际1行
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_blind_db"
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

# 场景A: 两个连续的空表聚合（每个期望1行，共2行）
print("="*70)
print("场景A: 两个连续空表聚合")
print("="*70)
send_sql(sock, "CREATE TABLE ea (id int, val float);")
send_sql(sock, "CREATE TABLE eb (id int, val float);")

q1 = "SELECT COUNT(*) AS c1 FROM ea;"
q2 = "SELECT COUNT(*) AS c2 FROM eb;"
r1 = send_sql(sock, q1)
r2 = send_sql(sock, q2)
total = count_data_lines(r1) + count_data_lines(r2)
print(f"Q1: {count_data_lines(r1)} 行, Q2: {count_data_lines(r2)} 行, 总计: {total}")
if total != 2:
    print(f"!!! 期望2行，实际{total}行 !!!")
    print(r1)
    print(r2)

send_sql(sock, "DROP TABLE ea;")
send_sql(sock, "DROP TABLE eb;")

# 场景B: 空表多聚合 + 空表单聚合
print("\n" + "="*70)
print("场景B: 空表多聚合")
print("="*70)
send_sql(sock, "CREATE TABLE em (val float);")

q = "SELECT COUNT(*) AS cnt, MAX(val) AS mx, MIN(val) AS mn, SUM(val) AS sm, AVG(val) AS av FROM em;"
r = send_sql(sock, q)
print(f"多聚合空表: {count_data_lines(r)} 行")
if count_data_lines(r) != 1:
    print(f"!!! 期望1行，实际{count_data_lines(r)}行 !!!")
    print(r)

send_sql(sock, "DROP TABLE em;")

# 场景C: HAVING过滤所有组后 + 另一个查询
print("\n" + "="*70)
print("场景C: HAVING过滤所有组")
print("="*70)
send_sql(sock, "CREATE TABLE eh (grp int, val float);")
send_sql(sock, "INSERT INTO eh VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO eh VALUES (2, 20.0);")

q1 = "SELECT grp, COUNT(*) AS cnt FROM eh GROUP BY grp HAVING COUNT(*) > 10;"
q2 = "SELECT COUNT(*) AS total FROM eh;"
r1 = send_sql(sock, q1)
r2 = send_sql(sock, q2)
total = count_data_lines(r1) + count_data_lines(r2)
print(f"HAVING过滤: {count_data_lines(r1)} 行, COUNT(*): {count_data_lines(r2)} 行, 总计: {total}")
if total != 1:
    print(f"!!! 期望1行，实际{total}行 !!!")
    print(r1)
    print(r2)

send_sql(sock, "DROP TABLE eh;")

# 场景D: WHERE过滤所有行 + 聚合
print("\n" + "="*70)
print("场景D: WHERE过滤所有行 + 聚合")
print("="*70)
send_sql(sock, "CREATE TABLE ew (id int, val float);")
send_sql(sock, "INSERT INTO ew VALUES (1, 10.0);")

q = "SELECT COUNT(*) AS cnt FROM ew WHERE val > 100;"
r = send_sql(sock, q)
print(f"WHERE过滤所有: {count_data_lines(r)} 行")
if count_data_lines(r) != 1:
    print(f"!!! 期望1行，实际{count_data_lines(r)}行 !!!")
    print(r)

send_sql(sock, "DROP TABLE ew;")

# 场景E: 只有GROUP BY没有聚合函数
print("\n" + "="*70)
print("场景E: 纯GROUP BY无聚合")
print("="*70)
send_sql(sock, "CREATE TABLE eg (grp int, val float);")
send_sql(sock, "INSERT INTO eg VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO eg VALUES (2, 20.0);")

q = "SELECT grp FROM eg GROUP BY grp;"
r = send_sql(sock, q)
print(f"纯GROUP BY 2组: {count_data_lines(r)} 行")
if count_data_lines(r) != 2:
    print(f"!!! 期望2行，实际{count_data_lines(r)}行 !!!")
    print(r)

send_sql(sock, "DROP TABLE eg;")

# 场景F: 纯GROUP BY空表
print("\n" + "="*70)
print("场景F: 纯GROUP BY空表")
print("="*70)
send_sql(sock, "CREATE TABLE ege (grp int, val float);")

q = "SELECT grp FROM ege GROUP BY grp;"
r = send_sql(sock, q)
print(f"纯GROUP BY空表: {count_data_lines(r)} 行")
if count_data_lines(r) != 0:
    print(f"!!! 期望0行，实际{count_data_lines(r)}行 !!!")
    print(r)

send_sql(sock, "DROP TABLE ege;")

# 场景G: 聚合+LIMIT 0
print("\n" + "="*70)
print("场景G: 聚合+LIMIT 0")
print("="*70)
send_sql(sock, "CREATE TABLE el (val float);")
send_sql(sock, "INSERT INTO el VALUES (10.0);")

q = "SELECT COUNT(*) AS cnt FROM el LIMIT 0;"
r = send_sql(sock, q)
print(f"聚合+LIMIT 0: {count_data_lines(r)} 行")
if count_data_lines(r) != 0:
    print(f"!!! 期望0行，实际{count_data_lines(r)}行 !!!")
    print(r)

send_sql(sock, "DROP TABLE el;")

# 场景H: 两个相同聚合查询连续执行
print("\n" + "="*70)
print("场景H: 两个相同聚合查询")
print("="*70)
send_sql(sock, "CREATE TABLE et (val float);")
send_sql(sock, "INSERT INTO et VALUES (10.0);")

q1 = "SELECT COUNT(*) AS cnt FROM et;"
q2 = "SELECT COUNT(*) AS cnt FROM et;"
r1 = send_sql(sock, q1)
r2 = send_sql(sock, q2)
total = count_data_lines(r1) + count_data_lines(r2)
print(f"相同聚合x2: 总计 {total} 行")
if total != 2:
    print(f"!!! 期望2行，实际{total}行 !!!")
    print(r1)
    print(r2)

send_sql(sock, "DROP TABLE et;")

# 场景I: 混合聚合（COUNT+SUM+AVG+MAX+MIN同时）
print("\n" + "="*70)
print("场景I: 混合聚合")
print("="*70)
send_sql(sock, "CREATE TABLE emix (id int, val float);")
send_sql(sock, "INSERT INTO emix VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO emix VALUES (2, 20.0);")

q = "SELECT COUNT(*) AS cnt, SUM(val) AS sm, AVG(val) AS av, MAX(val) AS mx, MIN(val) AS mn FROM emix;"
r = send_sql(sock, q)
print(f"混合聚合: {count_data_lines(r)} 行")
if count_data_lines(r) != 1:
    print(f"!!! 期望1行，实际{count_data_lines(r)}行 !!!")
    print(r)

send_sql(sock, "DROP TABLE emix;")

# 场景J: GROUP BY + ORDER BY聚合别名
print("\n" + "="*70)
print("场景J: GROUP BY + ORDER BY聚合别名")
print("="*70)
send_sql(sock, "CREATE TABLE ej (grp int, val float);")
send_sql(sock, "INSERT INTO ej VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO ej VALUES (2, 20.0);")

q = "SELECT grp, COUNT(*) AS cnt FROM ej GROUP BY grp ORDER BY cnt;"
r = send_sql(sock, q)
print(f"GROUP BY+ORDER BY: {count_data_lines(r)} 行")
if count_data_lines(r) != 2:
    print(f"!!! 期望2行，实际{count_data_lines(r)}行 !!!")
    print(r)

send_sql(sock, "DROP TABLE ej;")

# 场景K: 1行数据的GROUP BY
print("\n" + "="*70)
print("场景K: 1行数据GROUP BY")
print("="*70)
send_sql(sock, "CREATE TABLE ek (grp int, val float);")
send_sql(sock, "INSERT INTO ek VALUES (1, 10.0);")

q = "SELECT grp, COUNT(*) AS cnt FROM ek GROUP BY grp;"
r = send_sql(sock, q)
print(f"1行GROUP BY: {count_data_lines(r)} 行")
if count_data_lines(r) != 1:
    print(f"!!! 期望1行，实际{count_data_lines(r)}行 !!!")
    print(r)

send_sql(sock, "DROP TABLE ek;")

# 场景L: 负数聚合
print("\n" + "="*70)
print("场景L: 负数聚合")
print("="*70)
send_sql(sock, "CREATE TABLE en (val int);")
send_sql(sock, "INSERT INTO en VALUES (-10);")
send_sql(sock, "INSERT INTO en VALUES (-20);")

q = "SELECT COUNT(*) AS cnt, SUM(val) AS sm, AVG(val) AS av, MAX(val) AS mx, MIN(val) AS mn FROM en;"
r = send_sql(sock, q)
print(f"负数聚合: {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE en;")

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
