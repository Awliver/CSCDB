#!/usr/bin/env python3
import os, sys, time, socket, subprocess, shutil, re

BUILD = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build")
BIN = os.path.join(BUILD, "bin", "rmdb")
PORT = 8765


def wait_port(timeout=8):
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.socket()
            s.settimeout(0.3)
            s.connect(("127.0.0.1", PORT))
            s.close()
            return True
        except Exception:
            time.sleep(0.2)
    return False


def sql(sock, q):
    if not q.strip().endswith(";"):
        q += ";"
    sock.sendall(q.encode() + b"\x00")
    data = b""
    while True:
        try:
            sock.settimeout(5)
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
            if b"\x00" in data:
                break
        except socket.timeout:
            break
    return data.split(b"\x00")[0].decode("utf-8", errors="replace")


def rows(reply):
    lines = [l.strip() for l in reply.strip().splitlines() if l.strip().startswith("|")]
    if len(lines) < 2:
        return []
    return [re.split(r"\s*\|\s*", l.strip("| ")) for l in lines[1:]]


def kill_server():
    subprocess.run(["pkill", "-9", "-f", "[b]in/rmdb"], stderr=subprocess.DEVNULL)


def start_db(name):
    kill_server()
    time.sleep(0.5)
    db = os.path.join(BUILD, "test_dbs", name)
    if os.path.isdir(db):
        shutil.rmtree(db)
    os.makedirs(os.path.join(BUILD, "test_dbs"), exist_ok=True)
    proc = subprocess.Popen([BIN, db], cwd=BUILD, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if not wait_port():
        err = proc.stderr.read().decode()
        print("FATAL server:", err)
        proc.kill()
        sys.exit(1)
    sock = socket.socket()
    sock.settimeout(10)
    sock.connect(("127.0.0.1", PORT))
    return proc, sock


def stop(proc, sock):
    try:
        sql(sock, "exit")
        sock.close()
    except Exception:
        pass
    try:
        proc.wait(3)
    except Exception:
        proc.kill()


def main():
    fails = 0
    proc, s = start_db("p6_union_test")

    setup = [
        "CREATE TABLE orders1 (order_id INT, amount FLOAT, region CHAR(10));",
        "CREATE TABLE orders2 (order_id INT, amount FLOAT, region CHAR(10));",
        "CREATE TABLE orders3 (order_id INT, amount FLOAT, region CHAR(10));",
        "INSERT INTO orders1 VALUES (1, 150.0, 'Beijing');",
        "INSERT INTO orders1 VALUES (2, 230.5, 'Shanghai');",
        "INSERT INTO orders1 VALUES (3, 89.99, 'Guangzhou');",
        "INSERT INTO orders2 VALUES (1, 150.0, 'Beijing');",
        "INSERT INTO orders2 VALUES (4, 120.0, 'Shenzhen');",
        "INSERT INTO orders2 VALUES (5, 560.0, 'Chengdu');",
        "INSERT INTO orders3 VALUES (1, 150.0, 'Beijing');",
        "INSERT INTO orders3 VALUES (6, 199.99, 'Wuhan');",
    ]
    for q in setup:
        r = sql(s, q)
        if r.startswith("Error") or r.strip() == "failure":
            print("SETUP FAIL:", q, "->", r)
            stop(proc, s)
            return 1

    q1 = (
        "SELECT * FROM (SELECT * FROM orders1 UNION SELECT * FROM orders2 "
        "UNION SELECT * FROM orders3) AS all_orders ORDER BY amount DESC;"
    )
    r1 = sql(s, q1)
    exp1 = [
        ["5", "560", "Chengdu"],
        ["2", "230.5", "Shanghai"],
        ["6", "199.99", "Wuhan"],
        ["1", "150", "Beijing"],
        ["4", "120", "Shenzhen"],
        ["3", "89.99", "Guangzhou"],
    ]
    got1 = rows(r1)
    exp_ids = [r[0] for r in exp1]
    got_ids = [r[0] for r in got1]
    ok = got_ids == exp_ids and len(got1) == len(exp1)
    print(f"[{'PASS' if ok else 'FAIL'}] union dedup + order by: {len(got1)} rows, order={got_ids}")
    if not ok:
        print("  got:", got1)
        fails += 1

    sql(s, "CREATE TABLE o1 (order_id INT, amount INT, region CHAR(10));")
    sql(s, "CREATE TABLE o2 (order_id INT, amount FLOAT, region CHAR(20));")

    out_path = os.path.join(BUILD, "test_dbs", "p6_union_test", "output.txt")
    for label, q in [
        ("col count", "SELECT * FROM (SELECT amount FROM o1 UNION SELECT * FROM o2) AS t;"),
        ("type mismatch", "SELECT * FROM (SELECT amount FROM o1 UNION SELECT region FROM o2) AS t;"),
        ("bad order by", "SELECT * FROM (SELECT amount FROM o1 UNION SELECT amount FROM o2) AS t ORDER BY order_id;"),
    ]:
        sql(s, q)
        with open(out_path, "r") as f:
            last = f.readlines()[-1].strip()
        ok = last == "failure"
        print(f"[{'PASS' if ok else 'FAIL'}] failure case {label}: output.txt={last!r}")
        if not ok:
            fails += 1

    stop(proc, s)
    print(f"Total failures: {fails}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
