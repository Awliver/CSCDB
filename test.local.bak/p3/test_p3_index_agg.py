#!/usr/bin/env python3
"""
索引扫描 + 聚合测试
覆盖: 带索引的表上的聚合查询
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_idx_db"
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

send_sql(sock, "CREATE TABLE t (id int, val float);")
send_sql(sock, "CREATE INDEX idx_id ON t(id);")
send_sql(sock, "INSERT INTO t VALUES (1, 10.0);")
send_sql(sock, "INSERT INTO t VALUES (2, 20.0);")
send_sql(sock, "INSERT INTO t VALUES (3, 30.0);")

print("="*70)
print("索引扫描 + 聚合测试")
print("="*70)

tests = [
    ("COUNT(*) 有索引", "SELECT COUNT(*) AS cnt FROM t", 1),
    ("COUNT(*) WHERE索引列", "SELECT COUNT(*) AS cnt FROM t WHERE id > 1", 1),
    ("MAX WHERE索引列", "SELECT MAX(val) AS mx FROM t WHERE id > 1", 1),
    ("GROUP BY索引列", "SELECT id, COUNT(*) AS cnt FROM t GROUP BY id", 3),
    ("SUM WHERE索引列", "SELECT SUM(val) AS sm FROM t WHERE id < 3", 1),
    ("AVG WHERE索引列", "SELECT AVG(val) AS av FROM t WHERE id < 3", 1),
    ("空结果聚合 WHERE索引列", "SELECT COUNT(*) AS cnt FROM t WHERE id > 100", 1),
]

failures = []
for desc, sql, expected in tests:
    r = send_sql(sock, sql)
    actual = count_data_lines(r)
    status = "PASS" if actual == expected else "FAIL"
    if status == "FAIL":
        failures.append((desc, expected, actual))
        print(f"[{status}] {desc}")
        print(f"  期望: {expected}, 实际: {actual}")
        print(f"  {r}")
    else:
        print(f"[{status}] {desc} ({actual}行)")

send_sql(sock, "DROP TABLE t;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print("\n" + "="*70)
if failures:
    print(f"失败 {len(failures)} 个:")
    for desc, exp, act in failures:
        print(f"  - {desc}: 期望{exp}, 实际{act}")
else:
    print("全部通过")
print("="*70)
