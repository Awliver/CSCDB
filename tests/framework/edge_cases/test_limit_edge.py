#!/usr/bin/env python3
"""
LIMIT + 聚合 边界测试
覆盖: 空表聚合+LIMIT, JOIN+聚合+LIMIT, NULL值, 重复行, 浮点精度
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_limit_edge_db"
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
        return data_lines[1:]  # 去掉表头
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

# === 测试1: 空表聚合 + LIMIT ===
print("\n" + "="*70)
print("测试1: 空表聚合 + LIMIT")
print("="*70)

send_sql(sock, "CREATE TABLE empty_t (id int, val float);")

tests_empty = [
    ("空表 COUNT(*) LIMIT 1", "SELECT COUNT(*) AS cnt FROM empty_t LIMIT 1", 1, ["| 0 |"]),
    ("空表 COUNT(*) LIMIT 0", "SELECT COUNT(*) AS cnt FROM empty_t LIMIT 0", 0, []),
    ("空表 MAX LIMIT 1", "SELECT MAX(val) AS mx FROM empty_t LIMIT 1", 1, ["| 0.000000 |"]),
    ("空表 SUM LIMIT 1", "SELECT SUM(val) AS sm FROM empty_t LIMIT 1", 1, ["| 0.000000 |"]),
    ("空表 AVG LIMIT 1", "SELECT AVG(val) AS av FROM empty_t LIMIT 1", 1, ["| 0.000000 |"]),
]

for desc, sql, expected_rows, expected_content in tests_empty:
    reply = send_sql(sock, sql)
    actual_rows = count_result_lines(reply)
    actual_content = get_result_lines(reply)
    status = "PASS" if actual_rows == expected_rows else "FAIL"
    if expected_content and actual_content != expected_content:
        status = "FAIL"
    if status == "FAIL":
        print(f"[{status}] {desc}")
        print(f"  SQL: {sql}")
        print(f"  期望行数: {expected_rows}, 实际: {actual_rows}")
        print(f"  期望内容: {expected_content}")
        print(f"  实际内容: {actual_content}")
        print(f"  原始输出:\n{reply}")
    else:
        print(f"[{status}] {desc}")

send_sql(sock, "DROP TABLE empty_t;")

# === 测试2: JOIN + 聚合 + LIMIT ===
print("\n" + "="*70)
print("测试2: JOIN + 聚合 + LIMIT")
print("="*70)

send_sql(sock, "CREATE TABLE s (id int, name char(10));")
send_sql(sock, "CREATE TABLE sc (sid int, score float);")
send_sql(sock, "INSERT INTO s VALUES (1, 'A');")
send_sql(sock, "INSERT INTO s VALUES (2, 'B');")
send_sql(sock, "INSERT INTO s VALUES (3, 'C');")
send_sql(sock, "INSERT INTO sc VALUES (1, 90.0);")
send_sql(sock, "INSERT INTO sc VALUES (1, 80.0);")
send_sql(sock, "INSERT INTO sc VALUES (2, 85.0);")
send_sql(sock, "INSERT INTO sc VALUES (2, 75.0);")
send_sql(sock, "INSERT INTO sc VALUES (3, 95.0);")

tests_join = [
    ("JOIN COUNT LIMIT 1", "SELECT s.name, COUNT(*) AS cnt FROM s, sc WHERE s.id = sc.sid GROUP BY s.name LIMIT 1", 1),
    ("JOIN COUNT LIMIT 2", "SELECT s.name, COUNT(*) AS cnt FROM s, sc WHERE s.id = sc.sid GROUP BY s.name LIMIT 2", 2),
    ("JOIN COUNT LIMIT 0", "SELECT s.name, COUNT(*) AS cnt FROM s, sc WHERE s.id = sc.sid GROUP BY s.name LIMIT 0", 0),
    ("JOIN AVG LIMIT 1", "SELECT s.name, AVG(sc.score) AS avg_s FROM s, sc WHERE s.id = sc.sid GROUP BY s.name LIMIT 1", 1),
    ("JOIN SUM LIMIT 3", "SELECT s.name, SUM(sc.score) AS total FROM s, sc WHERE s.id = sc.sid GROUP BY s.name LIMIT 3", 3),
    ("JOIN + WHERE + LIMIT", "SELECT s.name, COUNT(*) AS cnt FROM s, sc WHERE s.id = sc.sid AND sc.score > 80 GROUP BY s.name LIMIT 1", 1),
]

for desc, sql, expected_rows in tests_join:
    reply = send_sql(sock, sql)
    actual_rows = count_result_lines(reply)
    status = "PASS" if actual_rows == expected_rows else "FAIL"
    if status == "FAIL":
        print(f"[{status}] {desc}")
        print(f"  SQL: {sql}")
        print(f"  期望行数: {expected_rows}, 实际: {actual_rows}")
        print(f"  原始输出:\n{reply}")
    else:
        print(f"[{status}] {desc}")

send_sql(sock, "DROP TABLE s;")
send_sql(sock, "DROP TABLE sc;")

# === 测试3: 重复行 + LIMIT ===
print("\n" + "="*70)
print("测试3: 重复行 + LIMIT")
print("="*70)

send_sql(sock, "CREATE TABLE dup (val int);")
send_sql(sock, "INSERT INTO dup VALUES (1);")
send_sql(sock, "INSERT INTO dup VALUES (1);")
send_sql(sock, "INSERT INTO dup VALUES (1);")
send_sql(sock, "INSERT INTO dup VALUES (2);")
send_sql(sock, "INSERT INTO dup VALUES (2);")

tests_dup = [
    ("重复行 LIMIT 2", "SELECT val FROM dup LIMIT 2", 2),
    ("重复行 LIMIT 4", "SELECT val FROM dup LIMIT 4", 4),
    ("重复行 GROUP BY LIMIT 1", "SELECT val, COUNT(*) AS cnt FROM dup GROUP BY val LIMIT 1", 1),
    ("重复行 GROUP BY LIMIT 2", "SELECT val, COUNT(*) AS cnt FROM dup GROUP BY val LIMIT 2", 2),
]

for desc, sql, expected_rows in tests_dup:
    reply = send_sql(sock, sql)
    actual_rows = count_result_lines(reply)
    status = "PASS" if actual_rows == expected_rows else "FAIL"
    if status == "FAIL":
        print(f"[{status}] {desc}")
        print(f"  SQL: {sql}")
        print(f"  期望行数: {expected_rows}, 实际: {actual_rows}")
        print(f"  原始输出:\n{reply}")
    else:
        print(f"[{status}] {desc}")

send_sql(sock, "DROP TABLE dup;")

# === 测试4: 浮点精度 + LIMIT ===
print("\n" + "="*70)
print("测试4: 浮点精度 + LIMIT")
print("="*70)

send_sql(sock, "CREATE TABLE fp (val float);")
send_sql(sock, "INSERT INTO fp VALUES (1.5);")
send_sql(sock, "INSERT INTO fp VALUES (2.5);")
send_sql(sock, "INSERT INTO fp VALUES (3.0);")

reply = send_sql(sock, "SELECT AVG(val) AS av FROM fp LIMIT 1;")
print(f"AVG(1.5,2.5,3.0) = {get_result_lines(reply)}")

reply = send_sql(sock, "SELECT SUM(val) AS sm FROM fp LIMIT 1;")
print(f"SUM(1.5,2.5,3.0) = {get_result_lines(reply)}")

send_sql(sock, "DROP TABLE fp;")

# === 测试5: 大规模数据 + LIMIT ===
print("\n" + "="*70)
print("测试5: 大规模数据 + LIMIT")
print("="*70)

send_sql(sock, "CREATE TABLE big (id int, grp char(5));")
for i in range(1, 101):
    g = chr(ord('A') + (i % 5))
    send_sql(sock, f"INSERT INTO big VALUES ({i}, '{g}');")

reply = send_sql(sock, "SELECT grp, COUNT(*) AS cnt FROM big GROUP BY grp LIMIT 3;")
print(f"100行数据 GROUP BY LIMIT 3: {count_result_lines(reply)} 行")
print(f"内容: {get_result_lines(reply)}")

reply = send_sql(sock, "SELECT COUNT(*) AS total FROM big LIMIT 1;")
print(f"100行 COUNT(*) LIMIT 1: {get_result_lines(reply)}")

reply = send_sql(sock, "SELECT id FROM big ORDER BY id DESC LIMIT 5;")
print(f"100行 ORDER BY DESC LIMIT 5: {count_result_lines(reply)} 行")
print(f"内容: {get_result_lines(reply)}")

send_sql(sock, "DROP TABLE big;")

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
