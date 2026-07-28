#!/usr/bin/env python3
import socket
import subprocess
import time
import os
import sys

PORT = 8765
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD = os.path.join(ROOT, "build")
DB_DIR = os.path.join(BUILD, "mvcc_verify_db")

def send_sql(sock, sql):
    sock.sendall((sql + "\0").encode())
    data = b""
    while True:
        chunk = sock.recv(8192)
        if not chunk:
            break
        data += chunk
        if b"\0" in data:
            break
    return data.split(b"\0")[0].decode()

def run_snapshot_test():
    if os.path.exists(DB_DIR):
        import shutil
        shutil.rmtree(DB_DIR)
    os.makedirs(DB_DIR, exist_ok=True)
    proc = subprocess.Popen(
        ["./bin/rmdb", "mvcc_verify_db"],
        cwd=BUILD,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(1)
    try:
        s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s1.connect(("127.0.0.1", PORT))
        s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s2.connect(("127.0.0.1", PORT))

        send_sql(s1, "create table t (id int, v int);")
        send_sql(s1, "insert into t values (1, 100);")
        send_sql(s1, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send_sql(s1, "begin;")
        send_sql(s2, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send_sql(s2, "begin;")
        send_sql(s2, "update t set v = 200 where id = 1;")
        send_sql(s2, "commit;")
        out = send_sql(s1, "select v from t where id = 1;")
        send_sql(s1, "commit;")
        print("SNAPSHOT_TEST_OUTPUT:", repr(out))
        ok = "100" in out and "200" not in out
        print("SNAPSHOT_TEST_PASS:", ok)
        return ok
    finally:
        try:
            s1.close()
        except Exception:
            pass
        try:
            s2.close()
        except Exception:
            pass
        proc.terminate()
        proc.wait(timeout=3)

if __name__ == "__main__":
    ok = run_snapshot_test()
    sys.exit(0 if ok else 1)
