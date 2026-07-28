#!/usr/bin/env python3
import socket, subprocess, time, os, shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PORT=8765; BUILD = os.path.join(ROOT, "build")
def send(sock, sql):
    sock.sendall((sql+"\0").encode()); data=b""
    while True:
        c=sock.recv(65536)
        if not c: break
        data+=c
        if b"\0" in data: break
    return data.split(b"\0")[0].decode()

path=os.path.join(BUILD,"vis_db")
if os.path.exists(path): shutil.rmtree(path)
proc=subprocess.Popen(["./bin/rmdb","vis_db"],cwd=BUILD,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(2)
s=socket.socket(); s.connect(("127.0.0.1",PORT))
print("create", send(s,"create table t (id int, v int);"))
print("insert", send(s,"insert into t values (1, 10);"))
print("set", send(s,"SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;"))
print("begin", send(s,"begin;"))
print("select", send(s,"select * from t;"))
print("sel id", send(s,"select * from t where id = 1;"))
proc.terminate()
