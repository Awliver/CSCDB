#!/usr/bin/env python3
"""
ORDER BY无AS别名 + JOIN聚合无AS别名 边界测试
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_oj_db"
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

send_sql(sock, "CREATE TABLE A (a_id int, x int);")
send_sql(sock, "CREATE TABLE B (b_id int, y int);")
send_sql(sock, "INSERT INTO A VALUES (1, 10);")
send_sql(sock, "INSERT INTO A VALUES (2, 20);")
send_sql(sock, "INSERT INTO B VALUES (1, 100);")
send_sql(sock, "INSERT INTO B VALUES (2, 200);")

print("="*70)
print("ORDER BY无AS别名测试")
print("="*70)

queries = [
    ("ORDER BY无AS别名(原始列)", "SELECT a_id i FROM A ORDER BY i", 2),
    ("ORDER BY无AS别名(聚合)", "SELECT a_id, COUNT(*) c FROM A GROUP BY a_id ORDER BY c", 2),
    ("ORDER BY有AS别名(对比)", "SELECT a_id i FROM A ORDER BY a_id", 2),
    ("多列ORDER BY无AS", "SELECT a_id i, x j FROM A ORDER BY i, j", 2),
    ("ORDER BY无AS+LIMIT", "SELECT a_id i FROM A ORDER BY i LIMIT 1", 1),
    ("ORDER BY无AS+DESC", "SELECT a_id i FROM A ORDER BY i DESC", 2),
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

print("\n" + "="*70)
print("JOIN聚合无AS别名测试")
print("="*70)

queries2 = [
    ("JOIN+COUNT无AS", "SELECT A.a_id, COUNT(*) c FROM A, B WHERE A.a_id = B.b_id GROUP BY A.a_id", 2),
    ("JOIN+SUM无AS", "SELECT A.a_id, SUM(B.y) s FROM A, B WHERE A.a_id = B.b_id GROUP BY A.a_id", 2),
    ("JOIN+AVG无AS", "SELECT A.a_id, AVG(B.y) a FROM A, B WHERE A.a_id = B.b_id GROUP BY A.a_id", 2),
    ("JOIN+MAX无AS", "SELECT A.a_id, MAX(B.y) m FROM A, B WHERE A.a_id = B.b_id GROUP BY A.a_id", 2),
    ("JOIN+多聚合无AS", "SELECT A.a_id, COUNT(*) c, SUM(B.y) s FROM A, B WHERE A.a_id = B.b_id GROUP BY A.a_id", 2),
    ("JOIN+聚合有AS(对比)", "SELECT A.a_id, COUNT(*) AS c FROM A, B WHERE A.a_id = B.b_id GROUP BY A.a_id", 2),
    ("JOIN+聚合无AS+ORDER BY", "SELECT A.a_id, COUNT(*) c FROM A, B WHERE A.a_id = B.b_id GROUP BY A.a_id ORDER BY c", 2),
    ("JOIN+聚合无AS+HAVING", "SELECT A.a_id, COUNT(*) c FROM A, B WHERE A.a_id = B.b_id GROUP BY A.a_id HAVING c > 0", 2),
]

for desc, sql, expected in queries2:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    print(f"\n[{status}] {desc}")
    print(f"  SQL: {sql}")
    print(f"  期望: {expected} 行, 实际: {actual} 行")
    if status == "FAIL":
        print(f"  输出:\n{r}")

send_sql(sock, "DROP TABLE A;")
send_sql(sock, "DROP TABLE B;")

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
