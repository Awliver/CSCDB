#!/usr/bin/env python3
import socket, subprocess, time, os, shutil, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PORT = 8765
BUILD = os.path.join(ROOT, "build")

def send(sock, sql):
    sock.sendall((sql + "\0").encode())
    data = b""
    while True:
        chunk = sock.recv(65536)
        if not chunk:
            break
        data += chunk
        if b"\0" in data:
            break
    return data.split(b"\0")[0].decode()

path = os.path.join(BUILD, "upd_db")
if os.path.exists(path):
    shutil.rmtree(path)
log = open("/tmp/upd_rmdb.log", "w")
proc = subprocess.Popen(["./bin/rmdb", "upd_db"], cwd=BUILD, stdout=log, stderr=log)
time.sleep(3)
s = socket.socket()
try:
    s.connect(("127.0.0.1", PORT))
except Exception as e:
    print("connect failed:", e)
    proc.terminate()
    sys.exit(1)
print("create+insert:", repr(send(s, "create table t (id int, v int); insert into t values (1, 10);")))
print("select:", send(s, "select * from t;"))
send(s, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
send(s, "begin;")
print("update:", repr(send(s, "update t set v = 20 where id = 1;")))
mid = send(s, "select v from t where id = 1;")
send(s, "commit;")
final = send(s, "select v from t where id = 1;")
print("mid has 20:", "20" in mid)
print("final has 20:", "20" in final)
s.close()
proc.terminate()
