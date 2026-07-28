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

path = os.path.join(BUILD, "delonly_db")
if os.path.exists(path):
    shutil.rmtree(path)
proc = subprocess.Popen(["./bin/rmdb", "delonly_db"], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
s = socket.socket()
s.connect(("127.0.0.1", PORT))
send(s, "create table t (id int, v int); create index t (id); insert into t values (1, 10);")
send(s, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
send(s, "begin;")
print("before delete:", send(s, "select * from t;"))
print("delete:", repr(send(s, "delete from t where id = 1;")))
print("after delete in txn:", send(s, "select * from t;"))
send(s, "abort;")
print("after abort:", send(s, "select * from t;"))
s.close()
proc.terminate()
