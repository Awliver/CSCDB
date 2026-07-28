#!/usr/bin/env python3
"""HAVING / ORDER BY 边角漏洞探测
已发现漏洞:
1. HAVING引用不在GROUP BY中的列 → 返回空结果(应报错)
2. ORDER BY引用不在SELECT中的列 → 报错(标准应允许)
3. LIMIT负数 → 返回所有行(应报错)
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_gap_ho"
PORT = 8765

def send_sql(sql):
    sql = sql.strip()
    if not sql.endswith(';'):
        sql += ';'
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect(("127.0.0.1", PORT))
    s.sendall(sql.encode() + b'\x00')
    data = b""
    while True:
        try:
            chunk = s.recv(8192)
            if not chunk:
                break
            data += chunk
            if b'\x00' in data:
                break
        except socket.timeout:
            break
    s.close()
    return data.split(b'\x00')[0].decode()

os.makedirs(os.path.join(BUILD_DIR, "test_dbs"), exist_ok=True)
proc = subprocess.Popen([SERVER_BIN, os.path.join("test_dbs", DB_NAME)], cwd=BUILD_DIR,
    stdout=open(os.devnull, "w"), stderr=open(os.devnull, "w"))
time.sleep(2)

send_sql("CREATE TABLE grade (course char(10), id int, score float);")
send_sql("INSERT INTO grade VALUES ('A', 1, 90);")
send_sql("INSERT INTO grade VALUES ('A', 2, 85);")
send_sql("INSERT INTO grade VALUES ('B', 3, 80);")

bugs = []

# Vuln 1: HAVING references column not in GROUP BY
r = send_sql("SELECT course, COUNT(*) FROM grade GROUP BY course HAVING score > 80;")
if "Error" not in r:
    bugs.append("VULN-1: HAVING引用非GROUP BY列未报错，返回: " + r.strip().replace('\n', ' '))

# Vuln 2: ORDER BY column not in SELECT
r = send_sql("SELECT course FROM grade ORDER BY score;")
if "Error" in r:
    bugs.append("VULN-2: ORDER BY引用不在SELECT中的列报错: " + r.strip().replace('\n', ' '))

# Vuln 3: LIMIT negative
r = send_sql("SELECT * FROM grade LIMIT -1;")
if "Error" not in r and "Total record(s): 3" in r:
    bugs.append("VULN-3: LIMIT -1未报错，返回所有行")

send_sql("DROP TABLE grade;")
proc.send_signal(signal.SIGINT)
proc.wait(timeout=5)

if bugs:
    print("发现漏洞:")
    for b in bugs:
        print(f"  {b}")
    sys.exit(0)
else:
    print("未发现已知漏洞")
