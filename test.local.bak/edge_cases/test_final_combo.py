#!/usr/bin/env python3
"""
最终组合测试: 无AS列别名 + 表别名 + ORDER BY
验证 output.txt 的实际内容
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_final_db"
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
send_sql(sock, "INSERT INTO t VALUES (2, 20.0);")

# 运行几个关键查询，然后检查 output.txt
queries = [
    "SELECT id FROM t",
    "SELECT id i FROM t ORDER BY i",
    "SELECT a.id FROM t a",
    "SELECT a.id i FROM t a ORDER BY a.id",
]

for q in queries:
    r = send_sql(sock, q)
    print(f"\nQuery: {q}")
    print(f"Socket reply: {repr(r[:100])}")

sock.close()
proc.send_signal(signal.SIGINT)
try:
    proc.wait(timeout=3)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

# 检查 output.txt
output_path = os.path.join(DB_DIR, "output.txt")
if os.path.exists(output_path):
    with open(output_path, 'r') as f:
        content = f.read()
    print(f"\n{'='*70}")
    print(f"output.txt 内容:")
    print(f"{'='*70}")
    print(content)
    lines = [l for l in content.strip().splitlines() if l.strip()]
    print(f"\n非空行数: {len(lines)}")
else:
    print(f"\noutput.txt 不存在")
