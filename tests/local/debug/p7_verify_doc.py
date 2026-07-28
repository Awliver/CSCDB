#!/usr/bin/env python3
"""Verify P7 doc examples against current implementation."""
import os, shutil, socket, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD = os.path.join(ROOT, "build")
RMDB = os.path.join(BUILD, "bin", "rmdb")

def run_session(db_name, sqls):
    db = os.path.join(BUILD, db_name)
    if os.path.exists(db): shutil.rmtree(db)
    os.makedirs(db)
    p = subprocess.Popen([RMDB, db], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    s = socket.socket(); s.connect(("127.0.0.1", 8765))
    def q(sql):
        s.sendall((sql + "\0").encode()); d = b""
        while True:
            c = s.recv(65536)
            if not c: break
            d += c
            if b"\0" in c: break
        return d.decode(errors="replace").replace("\0", "")
    outs = []
    for sql in sqls:
        outs.append((sql, q(sql)))
    q("exit"); s.close(); p.kill()
    return outs

def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" — {detail}" if detail else ""))
    return cond

def main():
    results = []

    # Doc 3.1 NLJ explain
    setup = [
        "create table departments (dept_id int, dept_name char(20));",
        "create table employees (emp_id int, dept_id int, emp_name char(20), salary int);",
    ]
    for i, (d, n) in enumerate([("Finance","Alice"),("HR","Bob"),("Sales","Charlie"),("Engineering","Diana"),("Marketing","Eve")], 1):
        setup.append(f"insert into departments values ({i*10}, '{d}');")
        setup.append(f"insert into employees values ({i}, {i*10}, '{n}', {60000+i*1000});")

    outs = run_session("p7v1", setup + [
        "select departments.dept_name, employees.emp_name from departments join employees on departments.dept_id = employees.dept_id;",
        "explain analyze select departments.dept_name, employees.emp_name from departments join employees on departments.dept_id = employees.dept_id;",
    ])
    sel = outs[-2][1]
    exp = outs[-1][1]
    results.append(check("3.1 SELECT 5 rows", "Alice" in sel and sel.count("Total record(s): 5") > 0, sel[-80:]))
    results.append(check("3.1 NLJ right rows=25", "rows=25)" in exp and "type=SeqScan" in exp))
    results.append(check("3.1 Join rows=5", "rows=5)" in exp.split("Join")[0] or "rows=5)" in exp))

    outs2 = run_session("p7v2", setup + [
        "create index employees(dept_id);",
        "explain analyze select departments.dept_name, employees.emp_name from departments join employees on departments.dept_id = employees.dept_id;",
    ])
    exp2 = outs2[-1][1]
    results.append(check("3.1.3 INLJ IndexScan", "type=IndexScan" in exp2 and "using_index=(dept_id)" in exp2))
    results.append(check("3.1.3 INLJ right rows=5", "employees" in exp2 and exp2.count("rows=5)") >= 2))

    # Doc 3.2 three-table
    setup3 = [
        "create table departments (dept_id int, dept_name char(20));",
        "create table employees (emp_id int, dept_id int, emp_name char(20));",
        "create table offices (office_id int, dept_id int, office_name char(20));",
        "insert into departments values (10, 'Finance');",
        "insert into departments values (20, 'HR');",
        "insert into departments values (30, 'Sales');",
        "insert into employees values (1, 10, 'Alice');",
        "insert into employees values (2, 20, 'Bob');",
        "insert into employees values (3, 30, 'Charlie');",
        "insert into offices values (101, 10, 'HQ');",
        "insert into offices values (102, 20, 'Remote');",
        "insert into offices values (103, 30, 'Field');",
        "explain analyze select departments.dept_name, employees.emp_name, offices.office_name from departments join employees on departments.dept_id = employees.dept_id join offices on departments.dept_id = offices.dept_id;",
    ]
    exp3 = run_session("p7v3", setup3)[-1][1]
    results.append(check("3.2 nested Join structure", exp3.count("Join(tables=") >= 2))
    results.append(check("3.2 employees rows=9", "employees" in exp3 and "rows=9)" in exp3))
    results.append(check("3.2 offices rows=9", "offices" in exp3 and exp3.count("rows=9)") >= 2))

    # P4 alias path should NOT use p7 explain (uses P4 format)
    setup4 = [
        "create table customers (customer_id int, name char(50));",
        "create table orders (order_id int, customer_id int);",
        "insert into customers values (1, 'A');",
        "insert into orders values (1, 1);",
        "explain analyze select c.name, o.order_id from customers c join orders o on c.customer_id = o.customer_id;",
    ]
    exp4 = run_session("p7v4", setup4)[-1][1]
    results.append(check("P4 alias uses Filter/alias format", "c.customer_id" in exp4 or "Filter" in exp4 or "Scan(table=customers" in exp4))

    passed = sum(results)
    total = len(results)
    print(f"\n汇总: {passed}/{total}")
    return 0 if passed == total else 1

if __name__ == "__main__":
    sys.exit(main())
