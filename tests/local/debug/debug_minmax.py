#!/usr/bin/env python3
import socket, subprocess, time, os

BUILD = os.path.join(os.path.dirname(__file__), "../../build")
subprocess.run(["pkill", "-9", "-f", "bin/rmdb"], stderr=subprocess.DEVNULL)
time.sleep(0.5)
subprocess.Popen([os.path.join(BUILD, "bin/rmdb"), "smoke_db"], cwd=BUILD)
time.sleep(2)

def q(sock, sql):
    sock.sendall(sql.encode() + b"\0")
    buf = b""
    while b"\0" not in buf:
        buf += sock.recv(8192)
    return buf.split(b"\0")[0].decode()

s = socket.socket()
s.connect(("127.0.0.1", 8765))
q(s, "create table t(a int,b char(8));")
q(s, "insert into t values (1,'hello');")
with open(os.path.join(BUILD, "smoke_db/data.csv"), "w") as f:
    f.write("2,world\n3,foo\n")
q(s, "load data.csv into t;")
for sql in ["select min(b) from t;", "select max(b) from t;", "select min(b), max(b) from t;"]:
    print(sql, "=>", repr(q(s, sql)[:200]))
s.close()
subprocess.run(["pkill", "-9", "-f", "bin/rmdb"], stderr=subprocess.DEVNULL)
