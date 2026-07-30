#!/usr/bin/env python3
"""官方 57 语句词典变体扫描：在已装载 W=50 库上逐条经 PREPARE_SET+EXEC_BATCH 重放。

覆盖本地词典全部语句 + OJ 可能的形态变体（谓词顺序、join 谓词反写/换位、
表限定列名、边界/无效参数）。任何 ERROR 终结立即报出（ABORT 可接受——SI 冲突语义）。
前提：服务器已在 8765 运行且已装载 W=50。用法: python3 stmt_variant_sweep.py
"""
import os, sys
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tests", "local"))
from wire_client import WireClient, SQLTYPE_INT32 as I, SQLTYPE_FLOAT32 as F, SQLTYPE_CHAR as C
from tpcc_batch import build_prepare_stmts

failures = []

def run_one(cli, sid, is_query, types, sql, params):
    try:
        cli.prepare_set([(900, is_query, types, sql)])
    except Exception as e:
        failures.append(("PREPARE", sql[:90], repr(e)[:120]))
        return
    r = cli.exec_batch([(900, params)])
    if r.error:
        failures.append(("ERROR", sql[:90], r.diagnostic[:120]))
    # aborted 可接受（写写冲突）；OK 可接受

def default_params(types):
    out = []
    for t in types:
        if t == I: out.append(1)
        elif t == F: out.append(1.0)
        else: out.append("x")
    return out

cli = WireClient(timeout=60)
cli.exec_stream("set transaction isolation level snapshot isolation")

# 1) 本地词典全部语句（默认参数 + 边界参数）
base = build_prepare_stmts()
print(f"local dict: {len(base)} stmts")
for sid, is_query, types, sql in base:
    s = sql.strip().rstrip(";")
    low = s.lower()
    if low in ("begin", "commit", "abort"):
        continue
    # 写语句放进 begin/abort 包裹避免污染库
    if not is_query:
        try:
            cli.prepare_set([(901, False, [], "begin"), (902, False, [], "abort"),
                             (900, is_query, types, sql)])
            r = cli.exec_batch([(901, []), (900, default_params(types)), (902, [])])
            if r.error:
                failures.append(("ERROR", sql[:90], r.diagnostic[:120]))
        except Exception as e:
            failures.append(("EXC", sql[:90], repr(e)[:120]))
    else:
        run_one(cli, sid, is_query, types, sql, default_params(types))

# 2) 决赛新增/可疑形态变体（在 W=50 真实数据上）
V = [
    # NewOrder join：标准形态 + 谓词顺序变体 + join 谓词反写 + 换位
    (True, [I, I, I], "select c_discount, c_last, c_credit, w_tax from customer, warehouse where w_id=$1 and c_w_id=w_id and c_d_id=$2 and c_id=$3", [1, 1, 1]),
    (True, [I, I, I], "select c_discount, c_last, c_credit, w_tax from customer, warehouse where c_w_id=w_id and w_id=$1 and c_d_id=$2 and c_id=$3", [1, 1, 1]),
    (True, [I, I, I], "select c_discount, c_last, c_credit, w_tax from customer, warehouse where w_id=$1 and w_id=c_w_id and c_d_id=$2 and c_id=$3", [1, 1, 1]),
    (True, [I, I, I], "select c_discount, c_last, c_credit, w_tax from warehouse, customer where w_id=$1 and c_w_id=w_id and c_d_id=$2 and c_id=$3", [1, 1, 1]),
    (True, [I, I, I], "select c_discount, c_last, c_credit, w_tax from customer, warehouse where c_id=$3 and c_d_id=$2 and c_w_id=w_id and w_id=$1", [1, 1, 1]),
    # 边界/不存在参数（不应 ERROR，应 0 行）
    (True, [I, I, I], "select c_discount, c_last, c_credit, w_tax from customer, warehouse where w_id=$1 and c_w_id=w_id and c_d_id=$2 and c_id=$3", [50, 10, 3000]),
    (True, [I, I, I], "select c_discount, c_last, c_credit, w_tax from customer, warehouse where w_id=$1 and c_w_id=w_id and c_d_id=$2 and c_id=$3", [51, 11, 99999]),
    (True, [I, I, I], "select c_discount, c_last, c_credit, w_tax from customer, warehouse where w_id=$1 and c_w_id=w_id and c_d_id=$2 and c_id=$3", [0, 0, 0]),
    # Delivery 3 谓词点查（12:52 形态）各种谓词顺序 + 不存在值
    (True, [I, I, I], "select o_c_id from orders where o_w_id=$1 and o_d_id=$2 and o_id=$3", [1, 1, 1]),
    (True, [I, I, I], "select o_c_id from orders where o_id=$3 and o_d_id=$2 and o_w_id=$1", [1, 1, 1]),
    (True, [I, I, I], "select o_c_id from orders where o_w_id=$1 and o_d_id=$2 and o_id=$3", [1, 1, 999999]),
    (True, [I, I, I], "select no_o_id from new_orders where no_w_id=$1 and no_d_id=$2 and no_o_id=$3", [1, 1, 2101]),
    (True, [I, I, I], "select c_balance from customer where c_w_id=$1 and c_d_id=$2 and c_id=$3", [1, 1, 1]),
    # OrderStatus 最近订单（desc limit）与明细
    (True, [I, I, I], "select o_id, o_entry_d, o_carrier_id from orders where o_w_id=$1 and o_d_id=$2 and o_c_id=$3 order by o_id desc limit 1", [1, 1, 1]),
    (True, [I, I, I], "select ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d from order_line where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3", [1, 1, 1]),
    # StockLevel 两种括号
    (True, [I, I, I, I, I], "select count(distinct (s_i_id)) from order_line, stock where ol_w_id=$1 and ol_d_id=$2 and ol_o_id>=$3 and ol_o_id<$4 and s_w_id=ol_w_id and s_i_id=ol_i_id and s_quantity<$5", [1, 1, 2081, 2101, 15]),
    (True, [I, I, I, I, I], "select count(distinct s_i_id) from order_line, stock where ol_w_id=$1 and ol_d_id=$2 and ol_o_id>=$3 and ol_o_id<$4 and s_w_id=ol_w_id and s_i_id=ol_i_id and s_quantity<$5", [1, 1, 2081, 2101, 15]),
    # item 无效商品（业务回滚路径的探测查询：0 行不应 ERROR）
    (True, [I], "select i_price, i_name, i_data from item where i_id=$1", [999999]),
    (True, [I], "select i_price, i_name, i_data from item where i_id=$1", [100001]),
    # Delivery MIN 两种形态
    (True, [I, I], "select no_o_id from new_orders where no_w_id=$1 and no_d_id=$2 order by no_o_id asc limit 1", [1, 1]),
    (True, [I, I], "select min(no_o_id) from new_orders where no_w_id=$1 and no_d_id=$2", [1, 1]),
    # SUM 形态（含决赛缺首列 skip scan）
    (True, [I, I, I], "select sum(ol_amount) from order_line where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3", [1, 1, 2101]),
    (True, [I, I], "select sum(ol_amount) from order_line where ol_o_id=$1 and ol_d_id=$2", [2101, 1]),
]
for is_query, types, sql, params in V:
    run_one(cli, 0, is_query, types, sql, params)

cli.close()
print(f"\nvariants tested: {len(V)} + dict")
if failures:
    print(f"FAILURES: {len(failures)}")
    for f in failures:
        print(" ", f)
    sys.exit(1)
print("ALL CLEAN — no ERROR terminal on any statement/variant")
