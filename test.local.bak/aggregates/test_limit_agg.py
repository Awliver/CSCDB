#!/usr/bin/env python3
"""
LIMIT + 聚合 穷举测试
覆盖: 简单LIMIT, 聚合+LIMIT, GROUP BY+LIMIT, ORDER BY+LIMIT, LIMIT 0, LIMIT超界
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_limit_db"
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
    """统计结果中的数据行数（以 | 开头的行）"""
    lines = reply.strip().splitlines()
    data_lines = [l for l in lines if l.strip().startswith('|')]
    # 去掉表头行（通常第一行是列名）
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

# 建表插数
setup = [
    "CREATE TABLE t (id int, grp char(10), val float);",
    "INSERT INTO t VALUES (1, 'A', 10.0);",
    "INSERT INTO t VALUES (2, 'A', 20.0);",
    "INSERT INTO t VALUES (3, 'B', 30.0);",
    "INSERT INTO t VALUES (4, 'B', 40.0);",
    "INSERT INTO t VALUES (5, 'C', 50.0);",
]
for sql in setup:
    send_sql(sock, sql)

# 测试用例: (描述, SQL, 期望行数)
tests = [
    # === 基础 LIMIT ===
    ("LIMIT 1 基础", "SELECT id FROM t LIMIT 1", 1),
    ("LIMIT 3 基础", "SELECT id FROM t LIMIT 3", 3),
    ("LIMIT 0", "SELECT id FROM t LIMIT 0", 0),
    ("LIMIT 超界", "SELECT id FROM t LIMIT 100", 5),
    
    # === 聚合 + LIMIT ===
    ("聚合无GROUP LIMIT 1", "SELECT COUNT(*) AS cnt FROM t LIMIT 1", 1),
    ("聚合无GROUP LIMIT 0", "SELECT COUNT(*) AS cnt FROM t LIMIT 0", 0),
    ("聚合无GROUP LIMIT 5", "SELECT COUNT(*) AS cnt FROM t LIMIT 5", 1),
    
    # === GROUP BY + LIMIT ===
    ("GROUP BY LIMIT 1", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp LIMIT 1", 1),
    ("GROUP BY LIMIT 2", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp LIMIT 2", 2),
    ("GROUP BY LIMIT 0", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp LIMIT 0", 0),
    ("GROUP BY LIMIT 超界", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp LIMIT 10", 3),
    
    # === ORDER BY + LIMIT ===
    ("ORDER BY LIMIT 2", "SELECT id FROM t ORDER BY id DESC LIMIT 2", 2),
    ("ORDER BY LIMIT 0", "SELECT id FROM t ORDER BY id DESC LIMIT 0", 0),
    
    # === ORDER BY + GROUP BY + LIMIT ===
    ("GROUP BY ORDER BY LIMIT 2", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp ORDER BY cnt DESC LIMIT 2", 2),
    
    # === HAVING + LIMIT ===
    ("HAVING LIMIT 1", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp HAVING COUNT(*) > 1 LIMIT 1", 1),
    ("HAVING LIMIT 超界", "SELECT grp, COUNT(*) AS cnt FROM t GROUP BY grp HAVING COUNT(*) > 1 LIMIT 10", 2),
    
    # === 多聚合 + LIMIT ===
    ("多聚合 LIMIT 1", "SELECT grp, MAX(val) AS mx, MIN(val) AS mn FROM t GROUP BY grp LIMIT 1", 1),
    
    # === WHERE + LIMIT ===
    ("WHERE LIMIT 1", "SELECT id FROM t WHERE val > 15 LIMIT 1", 1),
    ("WHERE LIMIT 超界", "SELECT id FROM t WHERE val > 15 LIMIT 10", 4),
]

print("\n" + "="*70)
print("LIMIT + 聚合 穷举测试")
print("="*70)

failures = []
for desc, sql, expected_rows in tests:
    reply = send_sql(sock, sql)
    actual_rows = count_result_lines(reply)
    status = "PASS" if actual_rows == expected_rows else "FAIL"
    if status == "FAIL":
        failures.append((desc, sql, expected_rows, actual_rows, reply))
    print(f"[{status}] {desc}")
    print(f"  SQL: {sql}")
    print(f"  期望行数: {expected_rows}, 实际行数: {actual_rows}")
    if status == "FAIL":
        print(f"  原始输出:\n{reply}")
    print()

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print("="*70)
if failures:
    print(f"失败 {len(failures)}/{len(tests)} 个测试:")
    for desc, sql, exp, act, reply in failures:
        print(f"  - {desc}: 期望{exp}行, 实际{act}行")
else:
    print(f"全部通过! {len(tests)}/{len(tests)}")
