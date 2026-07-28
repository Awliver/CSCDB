#!/usr/bin/env python3
import socket, subprocess, time, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PORT = 8765
BUILD = os.path.join(ROOT, "build")

def send(sock, sql):
    sock.sendall((sql + "\0").encode())
    data = b""
    while True:
        chunk = sock.recv(8192)
        if not chunk: break
        data += chunk
        if b"\0" in data: break
    return data.split(b"\0")[0].decode()

def main():
    db = "ww_verify_db"
    db_path = os.path.join(BUILD, db)
    if os.path.exists(db_path):
        import shutil; shutil.rmtree(db_path)
    proc = subprocess.Popen(["./bin/rmdb", db], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    try:
        a = socket.socket(); a.connect(("127.0.0.1", PORT))
        b = socket.socket(); b.connect(("127.0.0.1", PORT))
        send(a, "create table acc (id int, bal int);")
        send(a, "insert into acc values (1, 100);")
        send(a, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send(a, "begin;")
        send(a, "update acc set bal = 120 where id = 1;")
        send(b, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;")
        send(b, "begin;")
        out = send(b, "update acc set bal = 90 where id = 1;")
        send(a, "commit;")
        send(b, "commit;")
        c = socket.socket(); c.connect(("127.0.0.1", PORT))
        final = send(c, "select bal from acc where id = 1;")
        print("WW_ABORT:", repr(out.strip()))
        print("FINAL:", repr(final))
        ok = out.strip() == "abort" and "120" in final
        print("WW_TEST_PASS:", ok)
        return ok
    finally:
        proc.terminate(); proc.wait(timeout=3)

if __name__ == "__main__":
    sys.exit(0 if main() else 1)
