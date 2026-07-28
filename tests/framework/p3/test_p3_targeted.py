#!/usr/bin/env python3
"""
精准覆盖 basic_query_test3 可能场景
Expected 2 got 1 -> 某个查询期望2行输出实际1行
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_db"
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

# 场景1: 两个独立查询，每个期望1行输出（空表聚合 + 有数据聚合）
print("="*70)
print("场景1: 空表聚合默认值 + 有数据聚合")
print("="*70)

send_sql(sock, "CREATE TABLE t1 (id int, val float);")
send_sql(sock, "CREATE TABLE t2 (id int, val float);")
send_sql(sock, "INSERT INTO t2 VALUES (1, 10.0);")

# 两个查询，期望总共2行数据
q1 = "SELECT COUNT(*) AS cnt FROM t1;"
q2 = "SELECT COUNT(*) AS cnt FROM t2;"
r1 = send_sql(sock, q1)
r2 = send_sql(sock, q2)
print(f"Q1: {q1} -> {count_data_lines(r1)} 行")
print(r1)
print(f"Q2: {q2} -> {count_data_lines(r2)} 行")
print(r2)

send_sql(sock, "DROP TABLE t1;")
send_sql(sock, "DROP TABLE t2;")

# 场景2: GROUP BY 期望2个组
print("\n" + "="*70)
print("场景2: GROUP BY 2个不同值")
print("="*70)

send_sql(sock, "CREATE TABLE g (grp int, val float);")
send_sql(sock, "INSERT INTO g VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO g VALUES (2, 20.0);")

q = "SELECT grp, COUNT(*) AS cnt FROM g GROUP BY grp;"
r = send_sql(sock, q)
print(f"Q: {q} -> {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE g;")

# 场景3: GROUP BY 字符串列（可能有填充问题）
print("\n" + "="*70)
print("场景3: GROUP BY 字符串列（不同长度相同内容）")
print("="*70)

send_sql(sock, "CREATE TABLE gs (grp char(10), val int);")
send_sql(sock, "INSERT INTO gs VALUES ('A', 1);")
send_sql(sock, "INSERT INTO gs VALUES ('A', 2);")
send_sql(sock, "INSERT INTO gs VALUES ('B', 3);")

q = "SELECT grp, COUNT(*) AS cnt FROM gs GROUP BY grp;"
r = send_sql(sock, q)
print(f"Q: {q} -> {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE gs;")

# 场景4: GROUP BY + HAVING 过滤部分组
print("\n" + "="*70)
print("场景4: GROUP BY + HAVING（期望过滤后剩1组）")
print("="*70)

send_sql(sock, "CREATE TABLE gh (grp int, val float);")
send_sql(sock, "INSERT INTO gh VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO gh VALUES (1, 20.0);")
send_sql(sock, "INSERT INTO gh VALUES (2, 30.0);")

q = "SELECT grp, COUNT(*) AS cnt FROM gh GROUP BY grp HAVING COUNT(*) > 1;"
r = send_sql(sock, q)
print(f"Q: {q} -> {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE gh;")

# 场景5: 空表 + GROUP BY（期望0行）
print("\n" + "="*70)
print("场景5: 空表 + GROUP BY")
print("="*70)

send_sql(sock, "CREATE TABLE eg (grp int, val float);")

q = "SELECT grp, COUNT(*) AS cnt FROM eg GROUP BY grp;"
r = send_sql(sock, q)
print(f"Q: {q} -> {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE eg;")

# 场景6: 多个聚合函数 + 空表
print("\n" + "="*70)
print("场景6: 空表 + 多聚合（期望1行默认值）")
print("="*70)

send_sql(sock, "CREATE TABLE mt (val float);")

queries = [
    "SELECT COUNT(*) AS cnt, MAX(val) AS mx, MIN(val) AS mn, SUM(val) AS sm, AVG(val) AS av FROM mt;",
    "SELECT COUNT(*) AS cnt FROM mt WHERE val > 100;",
]
for q in queries:
    r = send_sql(sock, q)
    print(f"Q: {q} -> {count_data_lines(r)} 行")
    print(r)

send_sql(sock, "DROP TABLE mt;")

# 场景7: 重复行 + GROUP BY
print("\n" + "="*70)
print("场景7: 重复行聚合")
print("="*70)

send_sql(sock, "CREATE TABLE dup (val int);")
send_sql(sock, "INSERT INTO dup VALUES (1);")
send_sql(sock, "INSERT INTO dup VALUES (1);")
send_sql(sock, "INSERT INTO dup VALUES (2);")
send_sql(sock, "INSERT INTO dup VALUES (2);")

q = "SELECT val, COUNT(*) AS cnt FROM dup GROUP BY val;"
r = send_sql(sock, q)
print(f"Q: {q} -> {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE dup;")

# 场景8: JOIN + GROUP BY（可能2个组变1个组）
print("\n" + "="*70)
print("场景8: JOIN + GROUP BY")
print("="*70)

send_sql(sock, "CREATE TABLE a (id int, name char(5));")
send_sql(sock, "CREATE TABLE b (aid int, val int);")
send_sql(sock, "INSERT INTO a VALUES (1, 'A');")
send_sql(sock, "INSERT INTO a VALUES (2, 'B');")
send_sql(sock, "INSERT INTO b VALUES (1, 10);")
send_sql(sock, "INSERT INTO b VALUES (1, 20);")
send_sql(sock, "INSERT INTO b VALUES (2, 30);")

q = "SELECT a.name, COUNT(*) AS cnt FROM a, b WHERE a.id = b.aid GROUP BY a.name;"
r = send_sql(sock, q)
print(f"Q: {q} -> {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE a;")
send_sql(sock, "DROP TABLE b;")

# 场景9: 浮点数 GROUP BY（精度问题）
print("\n" + "="*70)
print("场景9: 浮点数 GROUP BY")
print("="*70)

send_sql(sock, "CREATE TABLE fp (val float);")
send_sql(sock, "INSERT INTO fp VALUES (1.5);")
send_sql(sock, "INSERT INTO fp VALUES (2.5);")
send_sql(sock, "INSERT INTO fp VALUES (1.5);")

q = "SELECT val, COUNT(*) AS cnt FROM fp GROUP BY val;"
r = send_sql(sock, q)
print(f"Q: {q} -> {count_data_lines(r)} 行")
print(r)

send_sql(sock, "DROP TABLE fp;")

# 场景10: 混合聚合和非聚合列（有GROUP BY）
print("\n" + "="*70)
print("场景10: 混合列（有GROUP BY）")
print("="*70)

send_sql(sock, "CREATE TABLE mix (grp int, val float, name char(5));")
send_sql(sock, "INSERT INTO mix VALUES (1, 10.0, 'A');")
send_sql(sock, "INSERT INTO mix VALUES (1, 20.0, 'B');")
send_sql(sock, "INSERT INTO mix VALUES (2, 30.0, 'C');")

q = "SELECT grp, MAX(val) AS mx, name FROM mix GROUP BY grp, name;"
r = send_sql(sock, q)
print(f"Q: {q} -> {count_data_lines(r)} 行")
print(r)

# 这个应该失败（name不在GROUP BY中）
q2 = "SELECT grp, MAX(val) AS mx, name FROM mix GROUP BY grp;"
r2 = send_sql(sock, q2)
print(f"Q: {q2} -> {count_data_lines(r2)} 行")
print(r2)

send_sql(sock, "DROP TABLE mix;")

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
