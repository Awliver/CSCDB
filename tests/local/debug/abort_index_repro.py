#!/usr/bin/env python3
"""Reproduce Abort Index Test pattern: begin -> insert with index -> abort -> verify."""
import os, socket, subprocess, sys, time

PORT = 8765
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BUILD = os.path.join(ROOT, "build")
RMDB = os.path.join(BUILD, "bin/rmdb")
DB = os.path.join(BUILD, "abort_idx_db")


def q(sock, sql):
    sock.sendall((sql + "\0").encode())
    buf = b""
    while b"\0" not in buf:
        chunk = sock.recv(8192)
        if not chunk:
            break
        buf += chunk
    return buf.split(b"\0")[0].decode()


def main():
    subprocess.run(["pkill", "-9", "-f", "bin/rmdb"], stderr=subprocess.DEVNULL)
    time.sleep(0.3)
    if os.path.isdir(DB):
        import shutil
        shutil.rmtree(DB)
    proc = subprocess.Popen([RMDB, "abort_idx_db"], cwd=BUILD)
    time.sleep(2)
    s = socket.socket()
    s.connect(("127.0.0.1", PORT))
    results = {}
    try:
        q(s, "create table t (id int, v int);")
        q(s, "create index t (id);")
        q(s, "begin;")
        r1 = q(s, "insert into t values (1, 100);")
        q(s, "abort;")
        r2 = q(s, "insert into t values (1, 200);")
        r3 = q(s, "select * from t where id = 1;")
        results["abort_insert"] = "failure" not in r2.lower() and "error" not in r2.lower()
        results["index_scan"] = "200" in r3 and "100" not in r3
        results["insert_resp"] = r2[:120]
        results["select_resp"] = r3[:200]
        print("insert_after_abort:", results["abort_insert"], repr(r2[:80]))
        print("index_scan:", results["index_scan"], repr(r3[:120]))
        ok = results["abort_insert"] and results["index_scan"]
        print("OVERALL:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        s.close()
        proc.kill()


if __name__ == "__main__":
    sys.exit(main())
