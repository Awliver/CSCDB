#!/usr/bin/env python3
"""复现 OJ 决赛 TPC-C 失败：UPDATE t SET a = a WHERE k1=? AND k2=?（自赋值）
覆盖 EXEC_STREAM 与 PREPARE_SET+EXEC_BATCH、INT/FLOAT/CHAR/索引列、冲突与回滚。"""
import os, sys, subprocess, time, shutil

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BUILD = os.environ.get("RMDB_BUILD", os.path.join(REPO, "build"))
DB = "repro_selfassign_db"
DBPATH = os.path.join(BUILD, DB)
LOG = os.path.join(BUILD, "repro_selfassign_db.server.log")
sys.path.insert(0, os.path.join(REPO, "tests", "local"))

subprocess.run(["pkill", "-x", "rmdb"], check=False)
time.sleep(0.5)
shutil.rmtree(DBPATH, ignore_errors=True)
logf = open(LOG, "w")
proc = subprocess.Popen([os.path.join(BUILD, "bin", "rmdb"), DB], cwd=BUILD,
                        stdout=logf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
time.sleep(2)

from wire_client import (WireClient, TAG_EXEC_STREAM, send_frame, recv_frame,
                         TAG_COMMAND_OK, TAG_ERROR, TAG_TRANSACTION_ABORT,
                         SQLTYPE_INT32, SQLTYPE_FLOAT32, SQLTYPE_CHAR)

def stream(c, sql):
    send_frame(c.sock, TAG_EXEC_STREAM, sql.encode())
    rows = []
    while True:
        tag, flags, payload = recv_frame(c.sock)
        if tag == TAG_COMMAND_OK:
            return ("OK", rows)
        if tag == 0x11:  # RESULT_END
            return ("OK", rows)
        if tag == TAG_ERROR:
            return ("ERROR", payload.decode(errors="replace"))
        if tag == TAG_TRANSACTION_ABORT:
            return ("ABORT", payload.decode(errors="replace"))
        if tag in (0x01, 0x02):  # META/ROW
            rows.append(payload)
            continue
        return ("??tag=%02x" % tag, payload)

fails = []
def check(name, got, want="OK"):
    status = got[0]
    ok = status == want
    print(("PASS" if ok else "FAIL"), name, "->", status, ("" if ok else repr(got[1])[:200]))
    if not ok:
        fails.append(name)

c = WireClient()
check("create table", stream(c, "create table t (id int, w int, a int, f float, c char(8));"))
check("create index", stream(c, "create index t (id, w);"))
check("insert1", stream(c, "insert into t values (1, 1, 100, 1.5, 'abc');"))
check("insert2", stream(c, "insert into t values (2, 1, 200, 2.5, 'def');"))
check("set SI", stream(c, "set transaction isolation level snapshot isolation;"))

# --- OJ 推荐检查 2：EXEC_STREAM 上的自赋值（各类型 + 索引列）---
check("stream int self", stream(c, "update t set a = a where id = 1 and w = 1;"))
check("stream float self", stream(c, "update t set f = f where id = 1 and w = 1;"))
check("stream char self", stream(c, "update t set c = c where id = 1 and w = 1;"))
check("stream indexed-col self", stream(c, "update t set id = id where id = 1 and w = 1;"))
check("row survives", stream(c, "select a from t where id = 1 and w = 1;"))

# --- col = 其他列（fast-parse 新分支与 yacc 同构性）---
check("stream col=othercol", stream(c, "update t set a = w where id = 2 and w = 1;"))
got = stream(c, "select a from t where id = 2 and w = 1;")
ok = got[0] == "OK" and len(got[1]) == 2 and got[1][1].endswith(b"\x00\x00\x00\x01")
print(("PASS" if ok else "FAIL"), "col=othercol value", "->", got[0])
if not ok: fails.append("col=othercol value")
check("restore a", stream(c, "update t set a = 200 where id = 2 and w = 1;"))

# --- 显式事务 + 回滚 ---
check("begin", stream(c, "begin;"))
check("txn self-assign", stream(c, "update t set a = a where id = 1 and w = 1;"))
check("txn abort", stream(c, "abort;"))
check("post-abort read", stream(c, "select a from t where id = 1 and w = 1;"))

# --- OJ 推荐检查 1：PREPARE_SET + EXEC_BATCH（排名路径，含 begin/commit）---
c2 = WireClient()
check("c2 set SI", stream(c2, "set transaction isolation level snapshot isolation;"))
try:
    c2.prepare_set([
        (1, False, [], "begin"),
        (2, False, [SQLTYPE_INT32, SQLTYPE_INT32], "update t set a = a where id = $1 and w = $2"),
        (3, False, [SQLTYPE_INT32, SQLTYPE_INT32], "update t set c = c where id = $1 and w = $2"),
        (4, False, [], "commit"),
        (5, True,  [SQLTYPE_INT32, SQLTYPE_INT32], "select a from t where id = $1 and w = $2"),
    ])
    print("PASS prepare_set")
except Exception as e:
    print("FAIL prepare_set ->", e)
    fails.append("prepare_set")

def batch(cli, ops):
    r = cli.exec_batch(ops)
    if r.error:
        return ("ERROR", r.diagnostic, r)
    if r.aborted:
        return ("ABORT", r.diagnostic, r)
    return ("OK", "", r)

if "prepare_set" not in fails:
    check("batch int self", batch(c2, [(1, []), (2, [1, 1]), (4, [])]))
    check("batch char self", batch(c2, [(1, []), (3, [1, 1]), (4, [])]))
    check("batch read", batch(c2, [(5, [1, 1])]))

# --- 写冲突语义：两连接都 begin 后写同一行，期望 ABORT 而非 ERROR ---
ca, cb = WireClient(), WireClient()
stream(ca, "set transaction isolation level snapshot isolation;")
stream(cb, "set transaction isolation level snapshot isolation;")
stream(ca, "begin;")
stream(cb, "begin;")
check("conflict writer1", stream(ca, "update t set a = a where id = 2 and w = 1;"))
got = stream(cb, "update t set a = a where id = 2 and w = 1;")
check("conflict writer2 must ABORT", got, want="ABORT")
stream(ca, "commit;")
stream(cb, "abort;")

for cli in (c, c2, ca, cb):
    cli.close()

print("\n=== server stderr tail ===")
logf.flush()
subprocess.run(["tail", "-30", LOG])
proc.terminate()
proc.wait(timeout=10)
print("\nFAILURES:", fails if fails else "none")
sys.exit(1 if fails else 0)
