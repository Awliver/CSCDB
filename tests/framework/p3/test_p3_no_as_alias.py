#!/usr/bin/env python3
"""
无AS别名聚合测试
发现: SELECT COUNT(*) cnt FROM t 返回0行
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_alias_db"
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

send_sql(sock, "CREATE TABLE t (id int, val float);")
send_sql(sock, "INSERT INTO t VALUES (1, 10.0);")

print("="*70)
print("无AS别名聚合测试")
print("="*70)

queries = [
    "SELECT COUNT(*) AS cnt FROM t",
    "SELECT COUNT(*) cnt FROM t",
    "SELECT MAX(val) AS mx FROM t",
    "SELECT MAX(val) mx FROM t",
    "SELECT COUNT(*) FROM t",
    "SELECT COUNT(*) c FROM t",
    "SELECT SUM(val) s FROM t",
    "SELECT AVG(val) a FROM t",
    "SELECT MIN(val) m FROM t",
    # 非聚合列无AS别名（应该正常）
    "SELECT id i FROM t",
    # GROUP BY + 无AS别名
    "SELECT id, COUNT(*) cnt FROM t GROUP BY id",
    # ORDER BY + 无AS别名聚合
    "SELECT id, COUNT(*) cnt FROM t GROUP BY id ORDER BY cnt",
]

for q in queries:
    r = send_sql(sock, q)
    print(f"\nQuery: {q}")
    print(f"Reply:\n{r}")

send_sql(sock, "DROP TABLE t;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()
