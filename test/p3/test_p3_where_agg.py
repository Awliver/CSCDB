#!/usr/bin/env python3
"""
WHERE中聚合函数的语法变体测试
如果test3是健壮性测试，需要确认所有变体都被正确拒绝
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_p3_wa_db"
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

send_sql(sock, "CREATE TABLE grade (course char(20),id int,score float);")
send_sql(sock, "INSERT INTO grade values('DataStructure',1,95);")
send_sql(sock, "INSERT INTO grade values('DataStructure',2,93.5);")
send_sql(sock, "INSERT INTO grade values('ComputerNetworks',1,99);")

print("="*70)
print("WHERE中聚合函数 - 语法变体测试")
print("="*70)

# 应该全部失败的查询
queries_should_fail = [
    # 原始测试用例
    "select id, MAX(score) as max_score from grade where MAX(score) > 90 group by id",
    # 变体1: 小写
    "select id, max(score) as max_score from grade where max(score) > 90 group by id",
    # 变体2: 无AS别名
    "select id, MAX(score) max_score from grade where MAX(score) > 90 group by id",
    # 变体3: 无别名
    "select id, MAX(score) from grade where MAX(score) > 90 group by id",
    # 变体4: 不同聚合函数
    "select id, COUNT(*) from grade where COUNT(*) > 1 group by id",
    "select id, SUM(score) from grade where SUM(score) > 100 group by id",
    "select id, AVG(score) from grade where AVG(score) > 90 group by id",
    "select id, MIN(score) from grade where MIN(score) > 90 group by id",
    # 变体5: 聚合在rhs
    "select id from grade where 90 < MAX(score) group by id",
    # 变体6: 多个WHERE条件，其中一个含聚合
    "select id from grade where id = 1 and MAX(score) > 90 group by id",
    # 变体7: 无GROUP BY的WHERE聚合
    "select COUNT(*) from grade where MAX(score) > 90",
    # 变体8: 聚合与列比较
    "select id from grade where MAX(score) > score group by id",
]

print("\n--- 应失败的查询 ---")
failures = []
for q in queries_should_fail:
    r = send_sql(sock, q)
    is_fail = 'Error' in r or 'failure' in r.lower() or r.strip() == ''
    if not is_fail:
        failures.append((q, r))
        print(f"[未拒绝] {q}")
        print(f"  输出: {r[:100]}")
    else:
        print(f"[已拒绝] {q[:60]}...")

# 应该成功的查询（HAVING中允许聚合）
queries_should_pass = [
    "select id, MAX(score) as max_score from grade group by id having MAX(score) > 90",
    "select id, COUNT(*) as cnt from grade group by id having COUNT(*) > 1",
]

print("\n--- 应成功的查询（HAVING） ---")
for q in queries_should_pass:
    r = send_sql(sock, q)
    is_fail = 'Error' in r or 'failure' in r.lower() or r.strip() == ''
    if is_fail:
        failures.append((q, r))
        print(f"[错误拒绝] {q}")
        print(f"  输出: {r[:100]}")
    else:
        print(f"[通过] {q[:60]}...")

send_sql(sock, "DROP TABLE grade;")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print("\n" + "="*70)
if failures:
    print(f"发现 {len(failures)} 个未被正确处理的查询:")
    for q, r in failures:
        print(f"  - {q}")
else:
    print("所有查询均被正确处理")
print("="*70)
