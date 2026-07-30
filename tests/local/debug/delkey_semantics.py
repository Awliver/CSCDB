#!/usr/bin/env python3
"""被删键索引（del_keys_）语义专项测试——覆盖 mvcc_insert_key_conflict 的全部分支。

场景（在 mini 库 new_orders 表上，键=no_o_id 首列）：
  S1 他人未提交删除同键 → 本事务插入同键 → 必须 abort（冲突）
  S2 我的快照之后已提交的删除 → 插入同键 → 必须 abort
  S3 我的快照之前已提交的删除 → 插入同键 → 必须成功
  S4 自删重插（同事务内 delete 后 insert 同键）→ 必须成功
  S5 删除方 abort 后 → 插入同键 → 必须成功（登记撤销）
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bench"))

from tpccbench.db import Client, parse_rows  # noqa: E402
from tpccbench import cli as tcli  # noqa: E402

DB = "delkey_db"
args = type("A", (), {"scale": "mini", "warehouses": 1, "seed": 42,
                      "data": None, "db": DB, "isolation": "si"})()
server = tcli.cmd_load(args, keep_server=True)

results = []

def check(name, ok, detail=""):
    results.append((name, ok))
    print("  %-38s %s %s" % (name, "PASS" if ok else "FAIL", detail), flush=True)

def conn():
    c = Client(timeout=30)
    c.query("set transaction isolation level snapshot isolation")
    c.query("set output_file off")
    return c

def rows(c, sql):
    return parse_rows(c.query(sql))

def visible(c, oid, d=1):
    return len(rows(c, "select no_o_id from new_orders where no_w_id=1 and no_d_id=%d "
                       "and no_o_id=%d;" % (d, oid)))

c1, c2 = conn(), conn()
# 预置若干行（键 9001..9005，各区隔离用 d=1）
for oid in range(9001, 9006):
    c1.query("insert into new_orders values (%d, 1, 1);" % oid)

# ---- S1: 他人未提交删除同键 → 插入冲突 abort ------------------------------------
# 注意：c2 必须先 begin 形成两个并发显式事务，否则 c1 的删除走单连接 SI 快路径
# （物理删除、不进版本链、不登记删除键——引擎既有设计取舍），冲突检测不适用。
c2.query("begin;")
c1.query("begin;")
c1.query("delete from new_orders where no_w_id=1 and no_d_id=1 and no_o_id=9001;")
st, r = c2.exec("insert into new_orders values (9001, 2, 1);")   # 同首列键 9001（不同区）
check("S1 未提交删除同键插入被拒", st == "abort", "%s %s" % (st, r[:60]))
c2.query("abort;")
c1.query("abort;")   # 还原

# ---- S2: 快照后已提交删除 → 旧快照插入冲突 ---------------------------------------
c2.query("begin;")                       # c2 先取快照
_ = rows(c2, "select no_o_id from new_orders where no_w_id=1 and no_d_id=1 and no_o_id=9002;")
c1.query("begin;")
c1.query("delete from new_orders where no_w_id=1 and no_d_id=1 and no_o_id=9002;")
c1.query("commit;")                      # 删除在 c2 快照之后提交
st, r = c2.exec("insert into new_orders values (9002, 2, 1);")
check("S2 快照后已提交删除插入被拒", st == "abort", "%s %s" % (st, r[:60]))
c2.query("abort;")

# ---- S3: 快照前已提交删除 → 插入成功 ---------------------------------------------
c1.query("begin;")
c1.query("delete from new_orders where no_w_id=1 and no_d_id=1 and no_o_id=9003;")
c1.query("commit;")
c2.query("begin;")                       # 新快照，删除已在快照前
st, r = c2.exec("insert into new_orders values (9003, 2, 1);")
ok = st == "ok"
c2.query("commit;" if ok else "abort;")
check("S3 快照前已提交删除插入成功", ok and visible(conn(), 9003, d=2) == 1, st)

# ---- S4: 自删重插 -----------------------------------------------------------------
c1.query("begin;")
c1.query("delete from new_orders where no_w_id=1 and no_d_id=1 and no_o_id=9004;")
st, r = c1.exec("insert into new_orders values (9004, 1, 1);")
ok = st == "ok"
c1.query("commit;" if ok else "abort;")
check("S4 自删重插成功", ok and visible(conn(), 9004) == 1, st)

# ---- S5: 删除方 abort 后插入成功 ---------------------------------------------------
c1.query("begin;")
c1.query("delete from new_orders where no_w_id=1 and no_d_id=1 and no_o_id=9005;")
c1.query("abort;")                       # 撤销删除 → 登记应清除
c2.query("begin;")
st, r = c2.exec("insert into new_orders values (9005, 2, 1);")
ok = st == "ok"
c2.query("commit;" if ok else "abort;")
check("S5 删除abort后插入成功", ok and visible(conn(), 9005, d=2) == 1, st)

c1.close(); c2.close()
server.stop()
n_fail = sum(1 for _, ok in results if not ok)
print("DELKEY SEMANTICS: %s (%d/%d)" % ("PASS" if n_fail == 0 else "FAIL",
                                        len(results) - n_fail, len(results)))
sys.exit(1 if n_fail else 0)
