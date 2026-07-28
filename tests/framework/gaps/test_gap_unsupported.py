#!/usr/bin/env python3
"""已知不支持的功能（记录盲区，非漏洞）
- DISTINCT
- 子查询
- 算术表达式
- INSERT NULL
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_gap_unsup"
PORT = 8765

def send_sql(sock, sql):
    sql = sql.strip()
    if not sql.endswith(';'):
        sql += ';'
    sock.sendall(sql.encode() + b'\x00')
    data = b""
    while True:
        try:
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

os.makedirs(os.path.join(BUILD_DIR, "test_dbs"), exist_ok=True)
proc = subprocess.Popen([SERVER_BIN, os.path.join("test_dbs", DB_NAME)], cwd=BUILD_DIR,
    stdout=open(os.devnull, "w"), stderr=open("/tmp/rmdb_gap_unsup_err.log", "w"))
time.sleep(2)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5)
sock.connect(("127.0.0.1", PORT))

known = []

send_sql(sock, "CREATE TABLE t (id int, name char(10), score float);")
send_sql(sock, "INSERT INTO t VALUES (1, 'a', 90);")
send_sql(sock, "INSERT INTO t VALUES (1, 'b', 80);")
send_sql(sock, "INSERT INTO t VALUES (2, 'c', 70);")

# 1. DISTINCT
r = send_sql(sock, "SELECT COUNT(DISTINCT id) FROM t;")
if "Error" in r or "Parser" in r or not r.strip():
    known.append("KNOWN: DISTINCT不支持")

# 2. Subquery
r = send_sql(sock, "SELECT * FROM (SELECT id FROM t) AS sub;")
if "Error" in r or "Parser" in r or not r.strip():
    known.append("KNOWN: 子查询不支持")

# 3. Expression in aggregate
r = send_sql(sock, "SELECT SUM(score + 10) FROM t;")
if "Error" in r or "Parser" in r or not r.strip():
    known.append("KNOWN: 聚合函数中算术表达式不支持")

# 4. INSERT NULL
r = send_sql(sock, "INSERT INTO t VALUES (3, NULL, 60);")
if "Error" in r or "Parser" in r or not r.strip():
    known.append("KNOWN: INSERT NULL不支持")

# 5. GROUP BY expression
r = send_sql(sock, "SELECT id FROM t GROUP BY id + 1;")
if "Error" in r or "Parser" in r or not r.strip():
    known.append("KNOWN: GROUP BY表达式不支持")

# 6. ORDER BY constant
r = send_sql(sock, "SELECT id FROM t ORDER BY 1;")
if "Error" in r or "Parser" in r or not r.strip():
    known.append("KNOWN: ORDER BY常量不支持")

send_sql(sock, "DROP TABLE t;")
sock.close()
proc.send_signal(signal.SIGINT)
proc.wait(timeout=5)

if known:
    print("已知不支持的功能:")
    for k in known:
        print(f"  {k}")
else:
    print("所有功能都已支持")
