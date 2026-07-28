#!/usr/bin/env python3
"""类型相关漏洞探测
已发现漏洞:
1. MAX/MIN on char列 → 报错(应支持)
2. AVG on empty table → 返回0.0(应返回NULL)
3. INSERT空字符串到char列 → 报错(应允许)
"""
import os, sys, time, socket, subprocess, shutil, signal

BUILD_DIR = "/home/neo/CSC_DB/db2026/build"
SERVER_BIN = os.path.join(BUILD_DIR, "bin", "rmdb")
DB_NAME = "test_gap_types"
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

bugs = []

# Vuln 1: MAX/MIN on char column
send_sql("CREATE TABLE s (name char(10));")
send_sql("INSERT INTO s VALUES ('hello');")
send_sql("INSERT INTO s VALUES ('world');")
r = send_sql("SELECT MAX(name), MIN(name) FROM s;")
if "Error" in r:
    bugs.append("VULN-1: MAX/MIN on char列报错: " + r.strip().replace('\n', ' '))
send_sql("DROP TABLE s;")

# Vuln 2: AVG on empty table
send_sql("CREATE TABLE empty (id int);")
r = send_sql("SELECT AVG(id) FROM empty;")
if "0.000000" in r:
    bugs.append("VULN-2: AVG on empty table返回0.0而不是NULL")
send_sql("DROP TABLE empty;")

# Vuln 3: INSERT empty string
send_sql("CREATE TABLE strtest (name char(10));")
r = send_sql("INSERT INTO strtest VALUES ('');")
if "Error" in r:
    bugs.append("VULN-3: INSERT空字符串到char列报错: " + r.strip().replace('\n', ' '))
send_sql("DROP TABLE strtest;")

proc.send_signal(signal.SIGINT)
proc.wait(timeout=5)

if bugs:
    print("发现漏洞:")
    for b in bugs:
        print(f"  {b}")
    sys.exit(0)
else:
    print("未发现已知漏洞")
