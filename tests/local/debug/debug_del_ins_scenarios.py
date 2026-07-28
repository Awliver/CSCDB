#!/usr/bin/env python3
"""Reproduce WriteWriteConflictDeleteInsert variants."""
import socket, subprocess, time, os, shutil, threading

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.join(ROOT, "build/bin/rmdb")
PORT = 8765

def send(sock, sql, delay=0.05):
    sock.sendall((sql + "\0").encode())
    time.sleep(delay)
    data = b""
    while True:
        chunk = sock.recv(65536)
        if not chunk:
            break
        data += chunk
        if b"\0" in data:
            break
    return data.split(b"\0")[0].decode().strip()

def run_server(db):
    os.makedirs(db, exist_ok=True)
    return subprocess.Popen([BIN, db], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def boot_table_no_index(db):
    subprocess.run(["pkill", "-f", "[r]mdb"], stderr=subprocess.DEVNULL)
    time.sleep(0.2)
    if os.path.exists(db):
        shutil.rmtree(db)
    proc = run_server(db)
    time.sleep(1.2)
    s = socket.socket()
    s.connect(("127.0.0.1", PORT))
    send(s, "set transaction isolation level snapshot isolation;")
    send(s, "create table t (id int, v int);")
    send(s, "insert into t values (1, 10);")
    s.close()
    return proc

def boot_table(db):
    subprocess.run(["pkill", "-f", "[r]mdb"], stderr=subprocess.DEVNULL)
    time.sleep(0.2)
    if os.path.exists(db):
        shutil.rmtree(db)
    proc = run_server(db)
    time.sleep(1.2)
    s = socket.socket()
    s.connect(("127.0.0.1", PORT))
    send(s, "set transaction isolation level snapshot isolation;")
    send(s, "create table t (id int, v int);")
    send(s, "create index t(id);")
    send(s, "insert into t values (1, 10);")
    s.close()
    return proc

def case_seq_del_then_ins_abort():
    """B inserts while A delete uncommitted; A aborts."""
    proc = boot_table(os.path.join(ROOT, "build/di_seq1"))
    a = socket.socket(); b = socket.socket()
    a.connect(("127.0.0.1", PORT)); b.connect(("127.0.0.1", PORT))
    send(a, "set transaction isolation level snapshot isolation;")
    send(b, "set transaction isolation level snapshot isolation;")
    send(a, "begin;"); send(b, "begin;")
    send(a, "delete from t where id = 1;")
    ins = send(b, "insert into t values (1, 20);")
    send(a, "abort;"); send(b, "commit;")
    final = send(a, "select * from t;")
    a.close(); b.close(); proc.terminate(); proc.wait(3)
    return {"name": "seq_del_ins_a_abort", "insert": ins, "final": final}

def case_seq_del_commit_then_ins():
    """B began before delete commit; insert should abort."""
    proc = boot_table(os.path.join(ROOT, "build/di_seq2"))
    a = socket.socket(); b = socket.socket()
    a.connect(("127.0.0.1", PORT)); b.connect(("127.0.0.1", PORT))
    send(a, "set transaction isolation level snapshot isolation;")
    send(b, "set transaction isolation level snapshot isolation;")
    send(b, "begin;"); send(a, "begin;")
    send(a, "delete from t where id = 1;")
    send(a, "commit;")
    ins = send(b, "insert into t values (1, 20);")
    send(b, "commit;")
    final = send(a, "select * from t;")
    a.close(); b.close(); proc.terminate(); proc.wait(3)
    return {"name": "del_commit_then_ins", "insert": ins, "final": final}

def case_ins_after_del_commit():
    """B begins after delete committed; insert should succeed."""
    proc = boot_table(os.path.join(ROOT, "build/di_seq3"))
    a = socket.socket(); b = socket.socket()
    a.connect(("127.0.0.1", PORT)); b.connect(("127.0.0.1", PORT))
    send(a, "set transaction isolation level snapshot isolation;")
    send(a, "begin;")
    send(a, "delete from t where id = 1;")
    send(a, "commit;")
    send(b, "set transaction isolation level snapshot isolation;")
    send(b, "begin;")
    ins = send(b, "insert into t values (1, 20);")
    send(b, "commit;")
    final = send(a, "select * from t;")
    a.close(); b.close(); proc.terminate(); proc.wait(3)
    return {"name": "ins_after_del_commit", "insert": ins, "final": final}

def case_parallel():
    proc = boot_table(os.path.join(ROOT, "build/di_par"))
    barrier = threading.Barrier(2)
    results = {}

    def client_a():
        c = socket.socket(); c.connect(("127.0.0.1", PORT))
        send(c, "set transaction isolation level snapshot isolation;")
        send(c, "begin;")
        barrier.wait()
        r1 = send(c, "delete from t where id = 1;")
        time.sleep(0.15)
        r2 = send(c, "commit;")
        results["a"] = (r1, r2)
        c.close()

    def client_b():
        c = socket.socket(); c.connect(("127.0.0.1", PORT))
        send(c, "set transaction isolation level snapshot isolation;")
        send(c, "begin;")
        barrier.wait()
        time.sleep(0.05)
        r1 = send(c, "insert into t values (1, 20);")
        r2 = send(c, "commit;")
        results["b"] = (r1, r2)
        c.close()

    t1 = threading.Thread(target=client_a)
    t2 = threading.Thread(target=client_b)
    t1.start(); t2.start(); t1.join(); t2.join()
    s = socket.socket(); s.connect(("127.0.0.1", PORT))
    final = send(s, "select * from t;")
    s.close(); proc.terminate(); proc.wait(3)
    return {"name": "parallel", "results": results, "final": final}

def case_older_ins_younger_del_abort():
    """Older inserter waits; younger deleter aborts; insert should succeed."""
    proc = boot_table(os.path.join(ROOT, "build/di_older_abort"))
    barrier = threading.Barrier(2)
    results = {}

    def client_b():
        c = socket.socket(); c.connect(("127.0.0.1", PORT))
        send(c, "set transaction isolation level snapshot isolation;")
        send(c, "begin;")
        barrier.wait()
        time.sleep(0.05)
        results["ins"] = send(c, "insert into t values (1, 20);")
        results["bcommit"] = send(c, "commit;")
        c.close()

    def client_a():
        c = socket.socket(); c.connect(("127.0.0.1", PORT))
        send(c, "set transaction isolation level snapshot isolation;")
        time.sleep(0.02)
        send(c, "begin;")
        barrier.wait()
        send(c, "delete from t where id = 1;")
        time.sleep(0.1)
        results["aabort"] = send(c, "abort;")
        c.close()

    t1 = threading.Thread(target=client_b)
    t2 = threading.Thread(target=client_a)
    t1.start(); t2.start(); t1.join(20); t2.join(20)
    s = socket.socket(); s.connect(("127.0.0.1", PORT))
    final = send(s, "select * from t;")
    out = ""
    out_path = os.path.join(ROOT, "build/di_older_abort/output.txt")
    if os.path.exists(out_path):
        out = open(out_path).read()
    s.close(); proc.terminate(); proc.wait(3)
    return {"name": "older_ins_younger_del_abort", "results": results, "final": final, "output": out}

def case_older_ins_younger_del_commit():
    proc = boot_table(os.path.join(ROOT, "build/di_older_commit"))
    barrier = threading.Barrier(2)
    results = {}

    def client_b():
        c = socket.socket(); c.connect(("127.0.0.1", PORT))
        send(c, "set transaction isolation level snapshot isolation;")
        send(c, "begin;")
        barrier.wait()
        time.sleep(0.05)
        results["ins"] = send(c, "insert into t values (1, 20);")
        results["bcommit"] = send(c, "commit;")
        c.close()

    def client_a():
        c = socket.socket(); c.connect(("127.0.0.1", PORT))
        send(c, "set transaction isolation level snapshot isolation;")
        time.sleep(0.02)
        send(c, "begin;")
        barrier.wait()
        send(c, "delete from t where id = 1;")
        time.sleep(0.1)
        results["acommit"] = send(c, "commit;")
        c.close()

    t1 = threading.Thread(target=client_b)
    t2 = threading.Thread(target=client_a)
    t1.start(); t2.start(); t1.join(20); t2.join(20)
    s = socket.socket(); s.connect(("127.0.0.1", PORT))
    final = send(s, "select * from t;")
    out_path = os.path.join(ROOT, "build/di_older_commit/output.txt")
    out = open(out_path).read() if os.path.exists(out_path) else ""
    s.close(); proc.terminate(); proc.wait(3)
    return {"name": "older_ins_younger_del_commit", "results": results, "final": final, "output": out}

def case_no_index_del_ins():
    proc = boot_table_no_index(os.path.join(ROOT, "build/di_no_idx"))
    a = socket.socket(); b = socket.socket()
    a.connect(("127.0.0.1", PORT)); b.connect(("127.0.0.1", PORT))
    send(a, "set transaction isolation level snapshot isolation;")
    send(b, "set transaction isolation level snapshot isolation;")
    send(a, "begin;"); send(b, "begin;")
    send(a, "delete from t where id = 1;")
    ins = send(b, "insert into t values (1, 20);")
    send(a, "commit;"); send(b, "commit;")
    final = send(a, "select * from t;")
    out_path = os.path.join(ROOT, "build/di_no_idx/output.txt")
    out = open(out_path).read() if os.path.exists(out_path) else ""
    a.close(); b.close(); proc.terminate(); proc.wait(3)
    return {"name": "no_index_del_ins", "insert": ins, "final": final, "output": out}

def case_no_index():
    proc = boot_table_no_index(os.path.join(ROOT, "build/di_no_idx"))
    a = socket.socket(); b = socket.socket()
    a.connect(("127.0.0.1", PORT)); b.connect(("127.0.0.1", PORT))
    send(a, "set transaction isolation level snapshot isolation;")
    send(b, "set transaction isolation level snapshot isolation;")
    send(a, "begin;"); send(b, "begin;")
    send(a, "delete from t where id = 1;")
    ins = send(b, "insert into t values (1, 20);")
    send(a, "commit;"); send(b, "commit;")
    final = send(a, "select * from t;")
    out_path = os.path.join(ROOT, "build/di_no_idx/output.txt")
    out = open(out_path).read() if os.path.exists(out_path) else ""
    a.close(); b.close(); proc.terminate(); proc.wait(3)
    return {"name": "no_index", "insert": ins, "final": final, "output": out}

def boot_table_no_index(db):
    subprocess.run(["pkill", "-f", "[r]mdb"], stderr=subprocess.DEVNULL)
    time.sleep(0.2)
    if os.path.exists(db):
        shutil.rmtree(db)
    proc = run_server(db)
    time.sleep(1.2)
    s = socket.socket()
    s.connect(("127.0.0.1", PORT))
    send(s, "set transaction isolation level snapshot isolation;")
    send(s, "create table t (id int, v int);")
    send(s, "insert into t values (1, 10);")
    s.close()
    return proc

def case_insert_first():
    """B insert while row exists - should failure; then delete path."""
    proc = boot_table(os.path.join(ROOT, "build/di_ins_first"))
    a = socket.socket(); b = socket.socket()
    a.connect(("127.0.0.1", PORT)); b.connect(("127.0.0.1", PORT))
    send(a, "set transaction isolation level snapshot isolation;")
    send(b, "set transaction isolation level snapshot isolation;")
    send(b, "begin;"); send(a, "begin;")
    ins = send(b, "insert into t values (1, 20);")
    delr = send(a, "delete from t where id = 1;")
    send(a, "commit;"); send(b, "commit;")
    final = send(a, "select * from t;")
    a.close(); b.close(); proc.terminate(); proc.wait(3)
    return {"name": "insert_first", "insert": ins, "delete": delr, "final": final}

if __name__ == "__main__":
    subprocess.run(["pkill", "-f", "[r]mdb"], stderr=subprocess.DEVNULL)
    time.sleep(0.5)
    for fn in [case_seq_del_then_ins_abort, case_seq_del_commit_then_ins,
               case_ins_after_del_commit, case_parallel, case_older_ins_younger_del_abort,
               case_older_ins_younger_del_commit, case_no_index, case_insert_first]:
        subprocess.run(["pkill", "-f", "[r]mdb"], stderr=subprocess.DEVNULL)
        time.sleep(0.3)
        r = fn()
        print(r)
        subprocess.run(["pkill", "-f", "[r]mdb"], stderr=subprocess.DEVNULL)
        time.sleep(0.3)
