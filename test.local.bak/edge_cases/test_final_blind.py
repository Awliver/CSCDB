#!/usr/bin/env python3
"""
最终盲区测试: SELECT*+GROUP BY, 关键字列名, HAVING非聚合列
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_fb_db"
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

send_sql(sock, "CREATE TABLE t (id int, count int, max int, val float);")
send_sql(sock, "INSERT INTO t VALUES (1, 10, 100, 95.0);")
send_sql(sock, "INSERT INTO t VALUES (2, 20, 200, 88.5);")

print("="*70)
print("最终盲区测试")
print("="*70)

queries = [
    ("SELECT*+GROUP BY(应失败)", "SELECT * FROM t GROUP BY id", 0),
    ("列名=关键字count", "SELECT count FROM t", 2),
    ("列名=关键字max", "SELECT max FROM t", 2),
    ("聚合列名=关键字", "SELECT MAX(count) AS cnt FROM t", 1),
    ("HAVING非聚合列(GROUP BY列)", "SELECT id, COUNT(*) AS cnt FROM t GROUP BY id HAVING id > 1", 1),
    ("HAVING非聚合列(非GROUP BY列,应失败)", "SELECT id, COUNT(*) AS cnt FROM t GROUP BY id HAVING val > 50", 0),
    ("WHERE+GROUP BY+聚合", "SELECT id, COUNT(*) AS cnt FROM t WHERE val > 50 GROUP BY id", 2),
    ("空字符串GROUP BY", "SELECT count, COUNT(*) AS cnt FROM t GROUP BY count", 2),
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

send_sql(sock, "DROP TABLE t;")

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
