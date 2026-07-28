#!/usr/bin/env python3
"""
GROUP BY 表别名测试
验证: GROUP BY 使用表别名(s.name) vs 原始表名(student.name)
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_gb_alias_db"
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

send_sql(sock, "CREATE TABLE student (id int, name char(10));")
send_sql(sock, "CREATE TABLE score (student_id int, course char(10), score float);")
send_sql(sock, "INSERT INTO student VALUES (1, 'Alice');")
send_sql(sock, "INSERT INTO student VALUES (2, 'Bob');")
send_sql(sock, "INSERT INTO score VALUES (1, 'Math', 95.0);")
send_sql(sock, "INSERT INTO score VALUES (1, 'Physics', 92.0);")
send_sql(sock, "INSERT INTO score VALUES (2, 'Math', 88.0);")

print("="*70)
print("GROUP BY 表别名测试")
print("="*70)

queries = [
    # 使用原始表名
    ("GROUP BY原始表名", "SELECT student.name, COUNT(*) FROM student, score WHERE student.id = score.student_id GROUP BY student.name", 2),
    ("GROUP BY原始表名+聚合别名", "SELECT student.name, COUNT(*) cnt FROM student, score WHERE student.id = score.student_id GROUP BY student.name", 2),
    
    # 使用表别名
    ("GROUP BY表别名", "SELECT s.name, COUNT(*) FROM student s, score sc WHERE s.id = sc.student_id GROUP BY s.name", 2),
    ("GROUP BY表别名+聚合别名", "SELECT s.name, COUNT(*) cnt FROM student s, score sc WHERE s.id = sc.student_id GROUP BY s.name", 2),
    
    # 混合: SELECT用别名，GROUP BY用原始名
    ("SELECT别名GROUP BY原始名", "SELECT s.name, COUNT(*) cnt FROM student s, score sc WHERE s.id = sc.student_id GROUP BY student.name", 2),
    
    # 单表GROUP BY别名
    ("单表GROUP BY别名", "SELECT s.name, COUNT(*) cnt FROM student s GROUP BY s.name", 2),
    ("单表GROUP BY原始名", "SELECT student.name, COUNT(*) cnt FROM student GROUP BY student.name", 2),
    
    # ORDER BY表别名
    ("ORDER BY表别名", "SELECT s.name FROM student s ORDER BY s.name", 2),
    ("ORDER BY原始名", "SELECT student.name FROM student ORDER BY student.name", 2),
    
    # WHERE表别名
    ("WHERE表别名", "SELECT s.name FROM student s WHERE s.id = 1", 1),
    ("WHERE原始名", "SELECT student.name FROM student WHERE student.id = 1", 1),
]

for desc, sql, expected in queries:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    print(f"\n[{status}] {desc}")
    print(f"  SQL: {sql}")
    print(f"  期望: {expected} 行, 实际: {actual} 行")
    if status == "FAIL":
        print(f"  输出:\n{r}")

send_sql(sock, "DROP TABLE student;")
send_sql(sock, "DROP TABLE score;")

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
