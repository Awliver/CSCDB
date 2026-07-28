#!/usr/bin/env python3
"""
输出格式测试
验证: 浮点精度、整数对齐、列名、空字符串显示
远程测试可能严格比较格式
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_fmt_db"
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

send_sql(sock, "CREATE TABLE t (id int, name char(10), score float);")
send_sql(sock, "INSERT INTO t VALUES (1, 'A', 95.0);")
send_sql(sock, "INSERT INTO t VALUES (2, 'B', 88.5);")
send_sql(sock, "INSERT INTO t VALUES (3, '', 0.0);")

print("="*70)
print("输出格式测试")
print("="*70)

queries = [
    "SELECT COUNT(*) AS cnt FROM t",
    "SELECT MAX(score) AS mx FROM t",
    "SELECT MIN(score) AS mn FROM t",
    "SELECT SUM(score) AS sm FROM t",
    "SELECT AVG(score) AS av FROM t",
    "SELECT name, COUNT(*) AS cnt FROM t GROUP BY name",
    "SELECT id, name, score FROM t ORDER BY id",
    "SELECT id, name, score FROM t WHERE score = 0.0",
    "SELECT id, name, score FROM t WHERE name = ''",
]

for q in queries:
    r = send_sql(sock, q)
    print(f"\nQuery: {q}")
    print(r)

send_sql(sock, "DROP TABLE t;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
