#!/usr/bin/env python3
import socket, subprocess, time, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PORT = 8765
BUILD = os.path.join(ROOT, "build")

def send(sock, sql):
    sock.sendall((sql + "\0").encode())
    data = b""
    while True:
        chunk = sock.recv(65536)
        if not chunk: break
        data += chunk
        if b"\0" in data: break
    return data.split(b"\0")[0].decode().strip()

def run_case(name, fn):
    db = f"case_{name}"
    path = os.path.join(BUILD, db)
    if os.path.exists(path):
        import shutil; shutil.rmtree(path)
    proc = subprocess.Popen(["./bin/rmdb", db], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    try:
        ok = fn()
        print(f"{name}: {'PASS' if ok else 'FAIL'}")
        return ok
    finally:
        proc.terminate(); proc.wait(timeout=3)

def lost_update():
    a = socket.socket(); a.connect(("127.0.0.1", PORT))
    b = socket.socket(); b.connect(("127.0.0.1", PORT))
    send(a, "create table t (id int, v int);")
    send(a, "insert into t values (1, 10);")
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
    a.close(); b.close()
    print("  abort=", repr(out), "final_has_20=", "20" in final)
    return out == "abort" and "20" in final

def delete_insert():
    a = socket.socket(); a.connect(("127.0.0.1", PORT))
    b = socket.socket(); b.connect(("127.0.0.1", PORT))
    send(a, "create table t (id int, v int);")
    send(a, "create index t (id);")
    send(a, "insert into t values (1, 10);")
    send(a, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
    send(a, "begin;")
    send(a, "delete from t where id = 1;")
    send(b, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
    send(b, "begin;")
    out = send(b, "insert into t values (1, 20);")
    send(a, "abort;")
    send(b, "commit;")
    final = send(a, "select v from t where id = 1;")
    a.close(); b.close()
    print("  insert_resp=", repr(out), "final=", repr(final))
    return out == "abort"

def update_self_visible():
    s = socket.socket(); s.connect(("127.0.0.1", PORT))
    send(s, "create table t (id int, v int);")
    send(s, "insert into t values (1, 1);")
    send(s, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
    send(s, "begin;")
    send(s, "update t set v = 2 where id = 1;")
    send(s, "update t set v = 3 where id = 1;")
    out = send(s, "select v from t where id = 1;")
    send(s, "commit;")
    s.close()
    print("  select=", repr(out))
    return "3" in out

if __name__ == "__main__":
    subprocess.run(["pkill", "-f", "[r]mdb"], stderr=subprocess.DEVNULL)
    time.sleep(1)
    ok = all([
        run_case("lost_update", lost_update),
        run_case("delete_insert", delete_insert),
        run_case("update_self", update_self_visible),
    ])
    sys.exit(0 if ok else 1)
