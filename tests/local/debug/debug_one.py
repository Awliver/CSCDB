#!/usr/bin/env python3
import socket, subprocess, time, os, shutil, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PORT = 8765
BUILD = os.path.join(ROOT, "build")

def send(sock, sql):
    sock.sendall((sql + "\0").encode())
    data = b""
    while True:
        c = sock.recv(65536)
        if not c:
            break
        data += c
        if b"\0" in data:
            break
    return data.split(b"\0")[0].decode()

path = os.path.join(BUILD, "dbg_db")
if os.path.exists(path):
    shutil.rmtree(path)
proc = subprocess.Popen(["./bin/rmdb", "dbg_db"], cwd=BUILD, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
time.sleep(2)
if proc.poll() is not None:
    print("SERVER DIED:", proc.stdout.read().decode())
    sys.exit(1)
try:
    s = socket.socket()
    s.connect(("127.0.0.1", PORT))
    print("create:", repr(send(s, "create table t (id int, v int);")))
    print("insert:", repr(send(s, "insert into t values (1, 10);")))
    print("set:", repr(send(s, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")))
    print("begin:", repr(send(s, "begin;")))
    print("update:", repr(send(s, "update t set v = 20 where id = 1;")))
    print("select:", send(s, "select v from t where id = 1;"))
    print("commit:", repr(send(s, "commit;")))
    print("final:", send(s, "select v from t where id = 1;"))
except Exception as e:
    print("ERROR:", e)
finally:
    proc.terminate()
    print("server tail:", proc.stdout.read()[-500:].decode(errors='replace'))
