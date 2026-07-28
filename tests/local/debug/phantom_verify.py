#!/usr/bin/env python3
import socket, subprocess, time, os, sys, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PORT = 8765
BUILD = os.path.join(ROOT, "build")
EXPECTED = open("/home/neo/CSC_DB/db_alter/T202510487995894-3920-main/references/tests/T202410616993406-3316_/test/concurrency/concurrency_sql/phantom_read_test_4_output.txt").read()

def send(sock, sql):
    sock.sendall((sql + "\0").encode())
    data = b""
    while True:
        chunk = sock.recv(65536)
        if not chunk: break
        data += chunk
        if b"\0" in data: break
    return data.split(b"\0")[0].decode()

def norm(s):
    s = re.sub(r"Total record\(s\): \d+", "Total record(s): N", s)
    return s.strip()

def main():
    db = "phantom_db"
    path = os.path.join(BUILD, db)
    if os.path.exists(path):
        import shutil; shutil.rmtree(path)
    proc = subprocess.Popen(["./bin/rmdb", db], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    try:
        s = [socket.socket() for _ in range(4)]
        for x in s: x.connect(("127.0.0.1", PORT))
        setup = [
            "create table concurrency_test (id int, name char(8), score float);",
            "create index concurrency_test (id);",
        ]
        rows = [
            "(1, 'xiaohong', 90.0)", "(2, 'xiaoming', 95.0)", "(4, 'zhanghua', 88.5)",
            "(7, 'xiaoyang', 91.0)", "(10, 'wangming', 92.0)", "(8, 'wanghong', 93.0)",
            "(100, 'zhaoming', 94.0)", "(201, 'zhaohong', 95.0)",
        ]
        for q in setup + [f"insert into concurrency_test values {r};" for r in rows]:
            send(s[0], q)
        for i in range(4):
            send(s[i], "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;")
        schedule = [
            (0, "begin;"), (1, "begin;"), (2, "begin;"), (3, "begin;"),
            (0, "select * from concurrency_test where id > 2 and id < 10;"),
            (1, "delete from concurrency_test where id = 7;"),
            (0, "select * from concurrency_test where id > 4 and id < 20;"),
            (2, "insert into concurrency_test values (11, 'zhaoyang', 99.0);"),
            (0, "select * from concurrency_test where id > 9 and id < 200;"),
            (3, "update concurrency_test set id = 13 where name = 'wanghong';"),
            (0, "select * from concurrency_test where id > 9 and id < 100;"),
            (0, "commit;"),
            (1, "abort;"), (2, "abort;"), (3, "abort;"),
        ]
        chunks = []
        for sess, sql in schedule:
            out = send(s[sess], sql)
            if out.strip():
                chunks.append(out)
        got = "\n".join(norm(c) for c in chunks)
        exp = norm(EXPECTED)
        print(got)
        print("---")
        print("PHANTOM_MATCH:", got == exp)
        return got == exp
    finally:
        for x in s: x.close()
        proc.terminate(); proc.wait(timeout=3)

if __name__ == "__main__":
    sys.exit(0 if main() else 1)
