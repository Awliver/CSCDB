#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
BUILD = os.path.join(ROOT, "build")
BIN = os.path.join(BUILD, "bin", "rmdb")
PORT = 8765

def port_open():
    try:
        s=socket.socket();s.settimeout(0.3);s.connect(("127.0.0.1",PORT));s.close();return True
    except Exception: return False

def kill_residual():
    subprocess.run(["pkill","-9","-f",BIN],capture_output=True)
    for _ in range(25):
        if not port_open(): break
        time.sleep(0.2)

def start(db, clean):
    d=os.path.join(BUILD,db)
    if clean and os.path.isdir(d): shutil.rmtree(d)
    kill_residual()
    p=subprocess.Popen([os.path.relpath(BIN,BUILD),db],cwd=BUILD,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    for _ in range(40):
        if port_open(): return p
        if p.poll() is not None:
            print("EARLY EXIT stderr:\n"+p.communicate()[1].decode(errors='replace')); sys.exit(1)
        time.sleep(0.2)
    raise RuntimeError("no port")

def sql(s,q):
    q=q.strip()+(';' if not q.strip().endswith(';') else '')
    s.sendall(q.encode()+b'\x00')
    d=b"";s.settimeout(5)
    while True:
        try:
            c=s.recv(8192)
            if not c: break
            d+=c
            if b'\x00' in d: break
        except socket.timeout: break
    return d.split(b'\x00')[0].decode(errors='replace')

def gstop(p):
    try:
        s=socket.socket();s.settimeout(2);s.connect(("127.0.0.1",PORT));s.sendall(b"exit\x00");s.close()
    except Exception: pass
    try: p.wait(timeout=5)
    except subprocess.TimeoutExpired: p.kill();p.wait()
    for _ in range(25):
        if not port_open(): break
        time.sleep(0.2)

DB="dbg_inspect"
# phase1
p=start(DB,clean=True)
s=socket.socket();s.connect(("127.0.0.1",PORT))
print(sql(s,"create table t (id int, val int)").strip()[:40])
print(sql(s,"insert into t values (1, 10)").strip()[:40])
print(sql(s,"begin").strip()[:40])
print(sql(s,"insert into t values (2, 20)").strip()[:40])
print(sql(s,"commit").strip()[:40])
print(sql(s,"begin").strip()[:40])
print(sql(s,"insert into t values (3, 30)").strip()[:40])
s.close()
d=os.path.join(BUILD,DB)
print("\n-- db dir files --")
for f in sorted(os.listdir(d)):
    fp=os.path.join(d,f)
    print(f, os.path.getsize(fp) if os.path.isfile(fp) else "<dir>")

print("\n[1] graceful exit then reopen (NOT crash):")
gstop(p)
p2=start(DB,clean=False)
s=socket.socket();s.connect(("127.0.0.1",PORT))
print("select after graceful reopen:\n"+sql(s,"select * from t"))
s.sendall(b"exit\x00");s.close()
gstop(p2)
