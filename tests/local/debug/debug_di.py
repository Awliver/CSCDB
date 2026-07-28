#!/usr/bin/env python3
import socket, subprocess, time, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PORT=8765; BUILD = os.path.join(ROOT, "build")

def send(s, sql):
    s.sendall((sql+'\0').encode()); d=b''
    while True:
        c=s.recv(65536)
        if not c: break
        d+=c
        if b'\0' in d: break
    return d.split(b'\0')[0].decode()

os.system("pkill -f '[r]mdb' >/dev/null 2>&1")
time.sleep(1)
os.system("rm -rf " + BUILD + "/di2_db")
p=subprocess.Popen(["./bin/rmdb", "di2_db"], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
a=socket.socket(); a.connect(("127.0.0.1", PORT))
b=socket.socket(); b.connect(("127.0.0.1", PORT))
for q in ["create table d (id int, v int);", "create index d (id);", "insert into d values (1, 10);"]:
    print("SETUP", repr(send(a, q)[:100]))
send(a, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
send(a, "begin;")
print("DEL", repr(send(a, "delete from d where id = 1;")[:100]))
send(b, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
send(b, "begin;")
print("INS", repr(send(b, "insert into d values (1, 20);")[:100]))
p.terminate()
