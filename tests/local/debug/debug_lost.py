#!/usr/bin/env python3
import socket, subprocess, time, os, shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PORT = 8765
BUILD = os.path.join(ROOT, "build")

def send(sock, sql):
    sock.sendall((sql + "\0").encode())
    data = b""
    while True:
        c = sock.recv(65536)
        if not c: break
        data += c
        if b"\0" in data: break
    return data.split(b"\0")[0].decode()

path = os.path.join(BUILD, "lost_db")
if os.path.exists(path): shutil.rmtree(path)
proc = subprocess.Popen(["./bin/rmdb", "lost_db"], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
a = socket.socket(); b = socket.socket()
a.connect(("127.0.0.1", PORT)); b.connect(("127.0.0.1", PORT))
send(a, "create table t (id int, v int); insert into t values (1, 10);")
for s in (a, b):
    send(s, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
    send(s, "begin;")
send(a, "select v from t where id = 1;")
send(b, "select v from t where id = 1;")
send(a, "update t set v = 20 where id = 1;")
send(a, "commit;")
out = send(b, "update t set v = 30 where id = 1;")
send(b, "commit;")
final = send(a, "select v from t where id = 1;")
print("B update:", repr(out.strip()))
print("final:", final)
print("PASS:", out.strip() == "abort" and "20" in final)
proc.terminate()
