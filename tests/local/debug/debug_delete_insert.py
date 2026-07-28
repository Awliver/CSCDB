#!/usr/bin/env python3
import socket, subprocess, time, os, shutil

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

path = os.path.join(BUILD, "di_db")
if os.path.exists(path):
    shutil.rmtree(path)
proc = subprocess.Popen(["./bin/rmdb", "di_db"], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
a = socket.socket()
b = socket.socket()
a.connect(("127.0.0.1", PORT))
b.connect(("127.0.0.1", PORT))
send(a, "create table t (id int, v int); create index t (id); insert into t values (1, 10);")
send(a, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
send(a, "begin;")
print("A delete:", repr(send(a, "delete from t where id = 1;")))
send(b, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
send(b, "begin;")
out = send(b, "insert into t values (1, 20);")
print("B insert:", repr(out.strip()))
send(a, "abort;")
send(b, "commit;")
final = send(a, "select * from t;")
print("final:", final)
print("PASS:", out.strip() == "abort" and "10" in final)
a.close()
b.close()
proc.terminate()
