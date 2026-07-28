#!/usr/bin/env python3
import socket
import subprocess
import threading
import time
import os
import shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.join(ROOT, "build/bin/rmdb")
class Client:
    def __init__(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.settimeout(10)
        self.s.connect(("127.0.0.1", 8765))

    def sql(self, q):
        self.s.sendall((q + "\0").encode())
        time.sleep(0.08)
        chunks = []
        while True:
            try:
                part = self.s.recv(65536)
                if not part:
                    break
                chunks.append(part)
                if b"\0" in part:
                    break
            except socket.timeout:
                break
        return b"".join(chunks).decode(errors="replace").replace("\0", "")

    def close(self):
        try:
            self.s.sendall(b"exit\0")
        except Exception:
            pass
        self.s.close()


def run_server(db_dir):
    os.makedirs(db_dir, exist_ok=True)
    return subprocess.Popen([BIN, db_dir], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def scenario_lost_update():
    db = os.path.join(ROOT, "build/p9_dbg_lost_update")
    if os.path.exists(db):
        shutil.rmtree(db)
    proc = run_server(db)
    time.sleep(0.8)
    try:
        boot = Client()
        boot.sql("set transaction isolation level snapshot isolation;")
        boot.sql("create table t (id int, v int);")
        boot.sql("insert into t values (1, 10);")
        boot.close()
        a, b = Client(), Client()
        a.sql("set transaction isolation level snapshot isolation;")
        b.sql("set transaction isolation level snapshot isolation;")
        a.sql("begin;")
        b.sql("begin;")
        a.sql("select * from t where id = 1;")
        b.sql("update t set v = 20 where id = 1;")
        b.sql("commit;")
        r = a.sql("update t set v = 30 where id = 1;")
        a.sql("commit;")
        final = Client().sql("select * from t;")
        print("LostUpdate:", "PASS" if "abort" in r and "20" in final else "FAIL", r[:80], final[:80])
    finally:
        proc.terminate()
        proc.wait(timeout=3)


def scenario_delete_insert_parallel():
    db = os.path.join(ROOT, "build/p9_dbg_del_ins")
    if os.path.exists(db):
        shutil.rmtree(db)
    proc = run_server(db)
    time.sleep(0.8)
    try:
        boot = Client()
        boot.sql("set transaction isolation level snapshot isolation;")
        boot.sql("create table t (id int, v int);")
        boot.sql("create index t(id);")
        boot.sql("insert into t values (1, 10);")
        boot.close()

        barrier = threading.Barrier(2)

        def client_a():
            c = Client()
            c.sql("set transaction isolation level snapshot isolation;")
            c.sql("begin;")
            barrier.wait()
            r1 = c.sql("delete from t where id = 1;")
            time.sleep(0.15)
            r2 = c.sql("commit;")
            print("A delete/commit:", repr(r1[:40]), repr(r2[:40]))
            c.close()

        def client_b():
            c = Client()
            c.sql("set transaction isolation level snapshot isolation;")
            c.sql("begin;")
            barrier.wait()
            time.sleep(0.05)
            r1 = c.sql("insert into t values (1, 20);")
            r2 = c.sql("commit;")
            print("B insert/commit:", repr(r1[:40]), repr(r2[:40]))
            c.close()

        t1 = threading.Thread(target=client_a)
        t2 = threading.Thread(target=client_b)
        t1.start()
        t2.start()
        t1.join()
        t2.join()
        out = Client().sql("select * from t;")
        print("DeleteInsert parallel:", out[:200])
        if os.path.exists(os.path.join(db, "output.txt")):
            print("output.txt:", open(os.path.join(db, "output.txt")).read()[-200:])
    finally:
        proc.terminate()
        proc.wait(timeout=3)


def scenario_serializable_where():
    db = os.path.join(ROOT, "build/p9_dbg_ser")
    if os.path.exists(db):
        shutil.rmtree(db)
    proc = run_server(db)
    time.sleep(0.8)
    try:
        boot = Client()
        boot.sql("set transaction isolation level serializable;")
        boot.sql("create table t (id int, v int);")
        boot.sql("insert into t values (1, 10);")
        boot.sql("insert into t values (2, 20);")
        boot.close()
        a, b = Client(), Client()
        a.sql("set transaction isolation level serializable;")
        b.sql("set transaction isolation level serializable;")
        a.sql("begin;")
        b.sql("begin;")
        a.sql("select * from t where v > 5;")
        b.sql("insert into t values (3, 30);")
        b.sql("commit;")
        r = a.sql("commit;")
        print("Serializable phantom:", "PASS" if "abort" in r else "FAIL", repr(r[:60]))
    finally:
        proc.terminate()
        proc.wait(timeout=3)


if __name__ == "__main__":
    os.system("pkill -f 'build/bin/rmdb' 2>/dev/null")
    time.sleep(0.3)
    scenario_lost_update()
    scenario_delete_insert_parallel()
    scenario_serializable_where()
