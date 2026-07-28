#!/usr/bin/env python3
"""
对抗性模糊测试 - 用异常数据冲击聚合逻辑
覆盖: 0值、极大值、极小值、混合类型GROUP BY、单条数据、全相同数据
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_fuzz_db"
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

print("="*70)
print("对抗性模糊测试")
print("="*70)

# 测试1: 全0值表
send_sql(sock, "CREATE TABLE t1 (val int);")
send_sql(sock, "INSERT INTO t1 VALUES (0);")
send_sql(sock, "INSERT INTO t1 VALUES (0);")
send_sql(sock, "INSERT INTO t1 VALUES (0);")

q = "SELECT val, COUNT(*) AS cnt FROM t1 GROUP BY val;"
r = send_sql(sock, q)
print(f"\n[全0值GROUP BY] {count_data_lines(r)} 行")
print(r)

q = "SELECT MAX(val) AS mx, MIN(val) AS mn, AVG(val) AS av FROM t1;"
r = send_sql(sock, q)
print(f"[全0值聚合] {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE t1;")

# 测试2: 全相同字符串
send_sql(sock, "CREATE TABLE t2 (name char(5));")
send_sql(sock, "INSERT INTO t2 VALUES ('A');")
send_sql(sock, "INSERT INTO t2 VALUES ('A');")

q = "SELECT name, COUNT(*) AS cnt FROM t2 GROUP BY name;"
r = send_sql(sock, q)
print(f"\n[全相同字符串GROUP BY] {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE t2;")

# 测试3: 单条数据
send_sql(sock, "CREATE TABLE t3 (id int, val float);")
send_sql(sock, "INSERT INTO t3 VALUES (1, 1.0);")

queries = [
    "SELECT COUNT(*) AS cnt FROM t3;",
    "SELECT id, COUNT(*) AS cnt FROM t3 GROUP BY id;",
    "SELECT MAX(val) AS mx FROM t3;",
    "SELECT MIN(val) AS mn FROM t3;",
    "SELECT SUM(val) AS sm FROM t3;",
    "SELECT AVG(val) AS av FROM t3;",
]
for q in queries:
    r = send_sql(sock, q)
    print(f"\n[单条数据] {q} -> {count_data_lines(r)} 行")
    print(r)

send_sql(sock, "DROP TABLE t3;")

# 测试4: 极大值和极小值混合
send_sql(sock, "CREATE TABLE t4 (val int);")
send_sql(sock, "INSERT INTO t4 VALUES (2147483647);")
send_sql(sock, "INSERT INTO t4 VALUES (-2147483648);")

q = "SELECT COUNT(*) AS cnt, MAX(val) AS mx, MIN(val) AS mn, SUM(val) AS sm, AVG(val) AS av FROM t4;"
r = send_sql(sock, q)
print(f"\n[极大极小值] {count_data_lines(r)} 行")
print(r)

q = "SELECT val, COUNT(*) AS cnt FROM t4 GROUP BY val;"
r = send_sql(sock, q)
print(f"[极大极小值GROUP BY] {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE t4;")

# 测试5: 混合0和NULL(用0模拟)
send_sql(sock, "CREATE TABLE t5 (val int);")
send_sql(sock, "INSERT INTO t5 VALUES (0);")
send_sql(sock, "INSERT INTO t5 VALUES (1);")
send_sql(sock, "INSERT INTO t5 VALUES (0);")

q = "SELECT val, COUNT(*) AS cnt FROM t5 GROUP BY val;"
r = send_sql(sock, q)
print(f"\n[混合0值] {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE t5;")

# 测试6: 空字符串
send_sql(sock, "CREATE TABLE t6 (name char(5));")
send_sql(sock, "INSERT INTO t6 VALUES ('');")
send_sql(sock, "INSERT INTO t6 VALUES ('');")
send_sql(sock, "INSERT INTO t6 VALUES ('A');")

q = "SELECT name, COUNT(*) AS cnt FROM t6 GROUP BY name;"
r = send_sql(sock, q)
print(f"\n[空字符串GROUP BY] {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE t6;")

# 测试7: 多列GROUP BY，其中一列全相同
send_sql(sock, "CREATE TABLE t7 (a int, b int, val float);")
send_sql(sock, "INSERT INTO t7 VALUES (1, 1, 10.0);")
send_sql(sock, "INSERT INTO t7 VALUES (1, 2, 20.0);")
send_sql(sock, "INSERT INTO t7 VALUES (1, 1, 30.0);")

q = "SELECT a, b, COUNT(*) AS cnt FROM t7 GROUP BY a, b;"
r = send_sql(sock, q)
print(f"\n[多列GROUP BY(一列相同)] {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE t7;")

# 测试8: HAVING使用聚合别名 vs 聚合原名
send_sql(sock, "CREATE TABLE t8 (grp int, val float);")
send_sql(sock, "INSERT INTO t8 VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO t8 VALUES (1, 20.0);")
send_sql(sock, "INSERT INTO t8 VALUES (2, 30.0);")

q = "SELECT grp, COUNT(*) AS cnt FROM t8 GROUP BY grp HAVING cnt > 1;"
r = send_sql(sock, q)
print(f"\n[HAVING别名cnt>1] {count_data_lines(r)} 行")
print(r)

q = "SELECT grp, COUNT(*) AS cnt FROM t8 GROUP BY grp HAVING COUNT(*) > 1;"
r = send_sql(sock, q)
print(f"[HAVING原名COUNT(*)>1] {count_data_lines(r)} 行")
print(r)

q = "SELECT grp, COUNT(*) AS cnt FROM t8 GROUP BY grp HAVING cnt = 2;"
r = send_sql(sock, q)
print(f"[HAVING别名cnt=2] {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE t8;")

# 测试9: ORDER BY GROUP BY列 + LIMIT
send_sql(sock, "CREATE TABLE t9 (grp int, val float);")
for i in range(1, 6):
    send_sql(sock, f"INSERT INTO t9 VALUES ({i}, {i*10}.0);")

q = "SELECT grp, COUNT(*) AS cnt FROM t9 GROUP BY grp ORDER BY grp LIMIT 3;"
r = send_sql(sock, q)
print(f"\n[ORDER BY grp + LIMIT 3] {count_data_lines(r)} 行")
print(r)

q = "SELECT grp, COUNT(*) AS cnt FROM t9 GROUP BY grp ORDER BY grp DESC LIMIT 3;"
r = send_sql(sock, q)
print(f"[ORDER BY grp DESC + LIMIT 3] {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE t9;")

# 测试10: 先空表聚合，再插入数据聚合
send_sql(sock, "CREATE TABLE t10 (val int);")
q1 = "SELECT COUNT(*) AS cnt FROM t10;"
r1 = send_sql(sock, q1)
print(f"\n[空表] {count_data_lines(r1)} 行")

send_sql(sock, "INSERT INTO t10 VALUES (1);")
q2 = "SELECT COUNT(*) AS cnt FROM t10;"
r2 = send_sql(sock, q2)
print(f"[插入后] {count_data_lines(r2)} 行")

send_sql(sock, "DROP TABLE t10;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print("\n" + "="*70)
print("模糊测试完成")
print("="*70)
