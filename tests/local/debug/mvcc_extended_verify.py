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

class Server:
    def __init__(self, db):
        self.db = db
        path = os.path.join(BUILD, db)
        if os.path.exists(path):
            import shutil; shutil.rmtree(path)
        self.proc = subprocess.Popen(["./bin/rmdb", db], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1)
    def close(self):
        self.proc.terminate(); self.proc.wait(timeout=3)

def sock():
    s = socket.socket(); s.connect(("127.0.0.1", PORT)); return s

def test_update_visible_in_txn():
    srv = Server("upd_db")
    try:
        a = sock()
        send(a, "create table u (id int, v int);")
        send(a, "insert into u values (1, 1);")
        send(a, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send(a, "begin;")
        send(a, "update u set v = 2 where id = 1;")
        out = send(a, "select v from u where id = 1;")
        send(a, "commit;")
        ok = "2" in out
        print("UPDATE_IN_TXN:", ok, repr(out[:80]))
        return ok
    finally:
        a.close(); srv.close()

def test_delete_insert_conflict():
    srv = Server("di_db")
    try:
        a, b = sock(), sock()
        send(a, "create table d (id int, v int);")
        send(a, "create index d (id);")
        send(a, "insert into d values (1, 10);")
        send(a, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send(a, "begin;")
        send(a, "delete from d where id = 1;")
        send(b, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send(b, "begin;")
        out = send(b, "insert into d values (1, 20);")
        send(a, "commit;")
        send(b, "commit;")
        c = sock()
        final = send(c, "select v from d where id = 1;")
        ok = out == "abort" and "20" not in final
        print("DELETE_INSERT:", ok, "b_out=", repr(out), "final=", repr(final[:60]))
        return ok
    finally:
        a.close(); b.close(); c.close(); srv.close()

def test_lost_update():
    srv = Server("lu_db")
    try:
        a, b = sock(), sock()
        send(a, "create table lu (id int, v int);")
        send(a, "insert into lu values (1, 100);")
        send(a, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send(a, "begin;")
        send(b, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send(b, "begin;")
        send(a, "select v from lu where id = 1;")
        send(b, "select v from lu where id = 1;")
        send(a, "update lu set v = 110 where id = 1;")
        send(a, "commit;")
        out = send(b, "update lu set v = 90 where id = 1;")
        send(b, "commit;")
        c = sock()
        final = send(c, "select v from lu where id = 1;")
        ok = out == "abort" and "110" in final
        print("LOST_UPDATE:", ok, "b_out=", repr(out), "final=", repr(final[:60]))
        return ok
    finally:
        a.close(); b.close(); c.close(); srv.close()

def test_phantom_writes_abort():
    srv = Server("pw_db")
    try:
        s = [sock() for _ in range(4)]
        send(s[0], "create table p (id int, name char(8), score float);")
        send(s[0], "create index p (id);")
        for row in ["(1,'a',90.0)", "(2,'b',95.0)", "(4,'c',88.5)", "(7,'d',91.0)",
                    "(10,'e',92.0)", "(8,'f',93.0)", "(100,'g',94.0)", "(201,'h',95.0)"]:
            send(s[0], f"insert into p values {row};")
        for x in s:
            send(x, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;")
        steps = [
            (0,"begin;"),(1,"begin;"),(2,"begin;"),(3,"begin;"),
            (0,"select * from p where id > 2 and id < 10;"),
            (1,"delete from p where id = 7;"),
            (0,"select * from p where id > 4 and id < 20;"),
            (2,"insert into p values (11, 'z', 99.0);"),
            (0,"select * from p where id > 9 and id < 200;"),
            (3,"update p set id = 13 where name = 'f';"),
            (0,"select * from p where id > 9 and id < 100;"),
        ]
        results = []
        for sess, sql in steps:
            results.append((sql, send(s[sess], sql)))
        for i, (sql, out) in enumerate(results):
            if sql.startswith("delete") or sql.startswith("insert") or sql.startswith("update"):
                print(f"  WRITE {sql[:40]!r} -> {out!r}")
        ok = all("abort" == results[i][1] for i in [5,7,9] if i < len(results))
        print("PHANTOM_WRITES_ABORT:", ok)
        return ok
    finally:
        for x in s: x.close(); srv.close()

if __name__ == "__main__":
    pkill = os.system("pkill -f '[r]mdb' >/dev/null 2>&1")
    time.sleep(0.5)
    tests = [test_update_visible_in_txn, test_delete_insert_conflict, test_lost_update, test_phantom_writes_abort]
    ok = all(t() for t in tests)
    sys.exit(0 if ok else 1)
