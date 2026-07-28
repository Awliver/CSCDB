#!/usr/bin/env python3
"""
PostgreSQL aggregates.sql / groupingsets.sql 适配迁移测试
覆盖 RMDB 已实现的聚合函数与分组统计功能
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_pg_agg_db"
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

# ============================================================
# 辅助：发送并打印
# ============================================================
def run(sql):
    r = send_sql(sock, sql)
    print(f"SQL: {sql}")
    print(r)
    return r

# ============================================================
# 场景1: 基本聚合函数（对应 aggregates.sql 开头）
# ============================================================
print("="*70)
print("场景1: 基本聚合函数 AVG / SUM / MAX / MIN / COUNT")
print("="*70)

run("CREATE TABLE aggtest (a int, b float);")
run("INSERT INTO aggtest VALUES (1, 10.5);")
run("INSERT INTO aggtest VALUES (2, 20.5);")
run("INSERT INTO aggtest VALUES (3, 30.0);")
run("INSERT INTO aggtest VALUES (100, 100.0);")

run("SELECT AVG(a) AS avg_a FROM aggtest;")
run("SELECT AVG(b) AS avg_b FROM aggtest;")
run("SELECT SUM(a) AS sum_a FROM aggtest;")
run("SELECT SUM(b) AS sum_b FROM aggtest;")
run("SELECT MAX(a) AS max_a FROM aggtest;")
run("SELECT MAX(b) AS max_b FROM aggtest;")
run("SELECT MIN(a) AS min_a FROM aggtest;")
run("SELECT MIN(b) AS min_b FROM aggtest;")
run("SELECT COUNT(*) AS cnt_all FROM aggtest;")
run("SELECT COUNT(a) AS cnt_a FROM aggtest;")

# 空表聚合
create_empty = "CREATE TABLE agg_empty (a int, b float);"
run(create_empty)
run("SELECT COUNT(*) AS cnt FROM agg_empty;")
run("SELECT MAX(a) AS mx FROM agg_empty;")
run("SELECT MIN(a) AS mn FROM agg_empty;")
run("SELECT SUM(a) AS sm FROM agg_empty;")
run("SELECT AVG(a) AS av FROM agg_empty;")
run("DROP TABLE agg_empty;")

# 条件聚合
run("SELECT AVG(a) AS avg_cond FROM aggtest WHERE a < 100;")
run("SELECT SUM(a) AS sum_cond FROM aggtest WHERE a < 100;")
run("SELECT MAX(b) AS max_cond FROM aggtest WHERE b > 15.0;")

run("DROP TABLE aggtest;")

# ============================================================
# 场景2: GROUP BY 单表分组（对应 aggregates.sql ~L189）
# ============================================================
print("\n" + "="*70)
print("场景2: GROUP BY 单表分组")
print("="*70)

run("CREATE TABLE onek (unique1 int, unique2 int, two int, four int, ten int, hundred int, stringu1 char(20));")
# 构造小规模 onek 数据（four 取值 0~3, ten 取值 0~9）
vals = []
for i in range(20):
    u1 = i
    u2 = i * 7 % 20
    two = i % 2
    four = i % 4
    ten = i % 10
    hundred = i % 100
    s = f"'str{i:02d}'"
    vals.append(f"({u1}, {u2}, {two}, {four}, {ten}, {hundred}, {s})")

# 插入所有行
for v in vals:
    run(f"INSERT INTO onek VALUES {v};")

run("SELECT four, COUNT(*) AS cnt, SUM(ten) AS s_ten FROM onek GROUP BY four;")
run("SELECT ten, COUNT(*) AS cnt, SUM(four) AS s_four FROM onek GROUP BY ten;")
run("SELECT two, MAX(four) AS mx, MIN(four) AS mn FROM onek GROUP BY two;")
run("SELECT four, COUNT(*) AS cnt FROM onek GROUP BY four ORDER BY four;")
run("SELECT ten, COUNT(four) AS cnt_four, SUM(four) AS sum_four FROM onek GROUP BY ten ORDER BY ten;")

run("DROP TABLE onek;")

# ============================================================
# 场景3: GROUP BY + HAVING（对应 aggregates.sql 含 HAVING 的用例）
# ============================================================
print("\n" + "="*70)
print("场景3: GROUP BY + HAVING")
print("="*70)

run("CREATE TABLE sales (product char(20), region char(20), amount float);")
run("INSERT INTO sales VALUES ('Apple', 'East', 100.0);")
run("INSERT INTO sales VALUES ('Apple', 'West', 200.0);")
run("INSERT INTO sales VALUES ('Banana', 'East', 150.0);")
run("INSERT INTO sales VALUES ('Banana', 'West', 50.0);")
run("INSERT INTO sales VALUES ('Cherry', 'East', 300.0);")

run("SELECT product, SUM(amount) AS total FROM sales GROUP BY product HAVING SUM(amount) > 200;")
run("SELECT region, COUNT(*) AS cnt FROM sales GROUP BY region HAVING COUNT(*) > 2;")
run("SELECT product, AVG(amount) AS avg_amt FROM sales GROUP BY product HAVING AVG(amount) > 100;")

run("DROP TABLE sales;")

# ============================================================
# 场景4: 多列 GROUP BY（对应 groupingsets 中多列分组思想）
# ============================================================
print("\n" + "="*70)
print("场景4: 多列 GROUP BY")
print("="*70)

run("CREATE TABLE gstest (a int, b int, c int, v float);")
run("INSERT INTO gstest VALUES (1, 1, 1, 10.0);")
run("INSERT INTO gstest VALUES (1, 1, 2, 20.0);")
run("INSERT INTO gstest VALUES (1, 2, 1, 30.0);")
run("INSERT INTO gstest VALUES (2, 1, 1, 40.0);")
run("INSERT INTO gstest VALUES (2, 2, 2, 50.0);")

run("SELECT a, b, SUM(v) AS s, COUNT(*) AS cnt FROM gstest GROUP BY a, b;")
run("SELECT a, b, c, AVG(v) AS a_v, MAX(v) AS m_v FROM gstest GROUP BY a, b, c;")
run("SELECT a, COUNT(*) AS cnt FROM gstest GROUP BY a;")
run("SELECT b, SUM(v) AS s FROM gstest GROUP BY b;")

# HAVING 在多列分组上
run("SELECT a, b, SUM(v) AS s FROM gstest GROUP BY a, b HAVING SUM(v) > 25;")
run("SELECT a, b, c, COUNT(*) AS cnt FROM gstest GROUP BY a, b, c HAVING COUNT(*) > 1;")

run("DROP TABLE gstest;")

# ============================================================
# 场景5: JOIN + GROUP BY（对应 aggregates.sql JOIN 聚合）
# ============================================================
print("\n" + "="*70)
print("场景5: JOIN + GROUP BY")
print("="*70)

run("CREATE TABLE student (id int, name char(20));")
run("CREATE TABLE score (student_id int, course char(20), score float);")
run("INSERT INTO student VALUES (1, 'Alice');")
run("INSERT INTO student VALUES (2, 'Bob');")
run("INSERT INTO student VALUES (3, 'Charlie');")
run("INSERT INTO score VALUES (1, 'Math', 95.0);")
run("INSERT INTO score VALUES (1, 'Physics', 92.0);")
run("INSERT INTO score VALUES (2, 'Math', 88.0);")
run("INSERT INTO score VALUES (2, 'Physics', 85.0);")
run("INSERT INTO score VALUES (3, 'Math', 90.0);")

run("SELECT student.name, COUNT(*) AS cnt, AVG(score.score) AS avg_score FROM student, score WHERE student.id = score.student_id GROUP BY student.name;")
run("SELECT student.name, MAX(score.score) AS max_score FROM student JOIN score WHERE student.id = score.student_id GROUP BY student.name HAVING COUNT(*) > 1;")

run("DROP TABLE student;")
run("DROP TABLE score;")

# ============================================================
# 场景6: 字符串列 GROUP BY（对应 aggregates.sql 字符串分组）
# ============================================================
print("\n" + "="*70)
print("场景6: 字符串列 GROUP BY")
print("="*70)

run("CREATE TABLE strgrp (category char(10), val int);")
run("INSERT INTO strgrp VALUES ('A', 1);")
run("INSERT INTO strgrp VALUES ('A', 2);")
run("INSERT INTO strgrp VALUES ('B', 3);")
run("INSERT INTO strgrp VALUES ('B', 4);")
run("INSERT INTO strgrp VALUES ('C', 5);")

run("SELECT category, COUNT(*) AS cnt, SUM(val) AS s, AVG(val) AS a FROM strgrp GROUP BY category;")
run("SELECT category, MAX(val) AS mx, MIN(val) AS mn FROM strgrp GROUP BY category;")

run("DROP TABLE strgrp;")

# ============================================================
# 场景7: 空表 + GROUP BY（应返回0行，对应 aggregates.sql 空输入测试）
# ============================================================
print("\n" + "="*70)
print("场景7: 空表 + GROUP BY")
print("="*70)

run("CREATE TABLE empty_grp (g int, v float);")
run("SELECT g, COUNT(*) AS cnt FROM empty_grp GROUP BY g;")
run("SELECT g, SUM(v) AS s, AVG(v) AS a FROM empty_grp GROUP BY g;")
run("DROP TABLE empty_grp;")

# ============================================================
# 场景8: 聚合与非聚合列混合（有 GROUP BY vs 无 GROUP BY 的报错）
# ============================================================
print("\n" + "="*70)
print("场景8: 聚合与非聚合列混合")
print("="*70)

run("CREATE TABLE mix (grp int, val float, name char(5));")
run("INSERT INTO mix VALUES (1, 10.0, 'A');")
run("INSERT INTO mix VALUES (1, 20.0, 'B');")
run("INSERT INTO mix VALUES (2, 30.0, 'C');")

# 正确：所有非聚合列都在 GROUP BY 中
run("SELECT grp, name, MAX(val) AS mx FROM mix GROUP BY grp, name;")
# 错误：name 不在 GROUP BY 中（应返回 failure 或报错）
run("SELECT grp, name, MAX(val) AS mx FROM mix GROUP BY grp;")

run("DROP TABLE mix;")

# ============================================================
# 场景9: ORDER BY + 聚合（对应 aggregates.sql order by 测试）
# ============================================================
print("\n" + "="*70)
print("场景9: ORDER BY + GROUP BY")
print("="*70)

run("CREATE TABLE ord (g int, v float);")
run("INSERT INTO ord VALUES (3, 30.0);")
run("INSERT INTO ord VALUES (1, 10.0);")
run("INSERT INTO ord VALUES (2, 20.0);")
run("INSERT INTO ord VALUES (1, 15.0);")
run("INSERT INTO ord VALUES (2, 25.0);")
run("INSERT INTO ord VALUES (3, 35.0);")

run("SELECT g, SUM(v) AS s FROM ord GROUP BY g ORDER BY g;")
run("SELECT g, AVG(v) AS a FROM ord GROUP BY g ORDER BY g;")
run("SELECT g, COUNT(*) AS cnt FROM ord GROUP BY g ORDER BY g;")

run("DROP TABLE ord;")

# ============================================================
# 场景10: 浮点精度 + 大数聚合（对应 aggregates.sql 精度测试）
# ============================================================
print("\n" + "="*70)
print("场景10: 浮点精度与大数聚合")
print("="*70)

run("CREATE TABLE bigfloat (v float);")
run("INSERT INTO bigfloat VALUES (100000003.0);")
run("INSERT INTO bigfloat VALUES (100000004.0);")
run("INSERT INTO bigfloat VALUES (100000006.0);")
run("INSERT INTO bigfloat VALUES (100000007.0);")

run("SELECT AVG(v) AS a, SUM(v) AS s FROM bigfloat;")
run("DROP TABLE bigfloat;")

# ============================================================
# 清理与结束
# ============================================================
sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print("\n" + "="*70)
print("PostgreSQL 聚合迁移测试完成")
print("="*70)
