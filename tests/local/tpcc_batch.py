"""TPC-C 排名路径：PREPARE_SET + EXEC_BATCH（对齐决赛附件 A §7 批次边界）。

无冲突理想往返：NewOrder 2 / Payment 2 / OrderStatus 3 / Delivery 2～3 / StockLevel 2。
AUTO_ABORT 失败后服务端已回滚，客户端不再发 ABORT。
"""

from __future__ import annotations

from typing import List, Optional, Sequence, Tuple

from tpcc_common import ENTRY_D, W_ID
from wire_client import (
    SQLTYPE_CHAR,
    SQLTYPE_FLOAT32,
    SQLTYPE_INT32,
    BatchResult,
    WireClient,
)

I = SQLTYPE_INT32
F = SQLTYPE_FLOAT32
C = SQLTYPE_CHAR

# ---- statement ids（连接级字典，与 OJ 一样每连接 PREPARE 一次）----
S_BEGIN = 1
S_COMMIT = 2
S_ABORT = 3
S_SEL_W_TAX = 4
S_UPD_W_YTD = 5
S_SEL_D_TAX_NEXT = 6
S_UPD_D_NEXT = 7
S_SEL_D_NEXT = 8
S_SEL_D_TAX_YTD = 9
S_UPD_D_YTD = 10
S_INS_ORDERS = 11
S_INS_NEW_ORDERS = 12
S_SEL_ITEM = 13
# 14..23 → s_dist_01..10
S_UPD_STOCK = 24
S_INS_OL = 25
S_SEL_CUST_PAY = 26
S_UPD_CUST_PAY = 27
S_INS_HIST = 28
S_SEL_CUST_OS = 29
S_SEL_ORDERS_LATEST = 30
S_SEL_OL_OS = 31
S_SEL_NO_MIN = 32
S_DEL_NO = 33
S_UPD_ORDERS_CARRIER = 34
S_UPD_OL_DELIVERY = 35
S_SUM_OL = 36
S_SEL_O_C_ID = 37
S_SEL_CUST_DEL = 38
S_UPD_CUST_DEL = 39
S_STOCK_LEVEL = 40
S_SEL_CUST_NO = 41
S_SEL_CUST_WH_JOIN = 42  # 决赛 PDF NewOrder 首语句：customer × warehouse 逗号连接
S_SEL_OL_DEL_DETAILS = 43  # 决赛 Delivery：逐行金额账本（与 SUM 做 0 ULP 对照）


def _stock_sel_id(d_id: int) -> int:
    return 13 + d_id  # 14..23


def build_prepare_stmts() -> List[Tuple[int, bool, Sequence[int], str]]:
    stmts: List[Tuple[int, bool, Sequence[int], str]] = [
        (S_BEGIN, False, [], "begin"),
        (S_COMMIT, False, [], "commit"),
        (S_ABORT, False, [], "abort"),
        (S_SEL_W_TAX, True, [I], "select w_tax from warehouse where w_id=$1"),
        (S_UPD_W_YTD, False, [F, I], "update warehouse set w_ytd=w_ytd+$1 where w_id=$2"),
        (
            S_SEL_D_TAX_NEXT,
            True,
            [I, I],
            "select d_tax, d_next_o_id from district where d_w_id=$1 and d_id=$2",
        ),
        (
            S_UPD_D_NEXT,
            False,
            [I, I],
            "update district set d_next_o_id=d_next_o_id+1 where d_w_id=$1 and d_id=$2",
        ),
        (
            S_SEL_D_NEXT,
            True,
            [I, I],
            "select d_next_o_id from district where d_w_id=$1 and d_id=$2",
        ),
        (
            S_SEL_D_TAX_YTD,
            True,
            [I, I],
            "select d_tax, d_ytd from district where d_w_id=$1 and d_id=$2",
        ),
        (
            S_UPD_D_YTD,
            False,
            [F, I, I],
            "update district set d_ytd=d_ytd+$1 where d_w_id=$2 and d_id=$3",
        ),
        (
            S_INS_ORDERS,
            False,
            [I, I, I, I, C, I, I],
            "insert into orders values($1,$2,$3,$4,$5,0,$6,$7)",
        ),
        (
            S_INS_NEW_ORDERS,
            False,
            [I, I, I],
            "insert into new_orders values($1,$2,$3)",
        ),
        (S_SEL_ITEM, True, [I], "select i_price from item where i_id=$1"),
        (
            S_UPD_STOCK,
            False,
            [I, F, I, I, I],
            "update stock set s_quantity=$1, s_ytd=s_ytd+$2, "
            "s_order_cnt=s_order_cnt+1, s_remote_cnt=s_remote_cnt+$3 "
            "where s_w_id=$4 and s_i_id=$5",
        ),
        (
            S_INS_OL,
            False,
            [I, I, I, I, I, I, C, I, F, C],
            "insert into order_line values($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)",
        ),
        (
            S_SEL_CUST_PAY,
            True,
            [I, I, I],
            "select c_balance, c_ytd_payment, c_payment_cnt, c_data from customer "
            "where c_w_id=$1 and c_d_id=$2 and c_id=$3",
        ),
        (
            S_UPD_CUST_PAY,
            False,
            [F, F, I, I, I],
            "update customer set c_balance=c_balance-$1, c_ytd_payment=c_ytd_payment+$2, "
            "c_payment_cnt=c_payment_cnt+1 where c_w_id=$3 and c_d_id=$4 and c_id=$5",
        ),
        (
            S_INS_HIST,
            False,
            [I, I, I, I, I, C, F, C],
            "insert into history values($1,$2,$3,$4,$5,$6,$7,$8)",
        ),
        (
            S_SEL_CUST_OS,
            True,
            [I, I, I],
            "select c_balance, c_first, c_middle, c_last from customer "
            "where c_w_id=$1 and c_d_id=$2 and c_id=$3",
        ),
        (
            S_SEL_ORDERS_LATEST,
            True,
            [I, I, I],
            "select o_id, o_entry_d, o_carrier_id from orders "
            "where o_w_id=$1 and o_d_id=$2 and o_c_id=$3 order by o_id desc limit 1",
        ),
        (
            S_SEL_OL_OS,
            True,
            [I, I, I],
            "select ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d "
            "from order_line where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3",
        ),
        (
            S_SEL_NO_MIN,
            True,
            [I, I],
            "select no_o_id from new_orders where no_w_id=$1 and no_d_id=$2 "
            "order by no_o_id asc limit 1",
        ),
        (
            S_DEL_NO,
            False,
            [I, I, I],
            "delete from new_orders where no_w_id=$1 and no_d_id=$2 and no_o_id=$3",
        ),
        (
            S_UPD_ORDERS_CARRIER,
            False,
            [I, I, I],
            "update orders set o_carrier_id=10 where o_w_id=$1 and o_d_id=$2 and o_id=$3",
        ),
        (
            S_UPD_OL_DELIVERY,
            False,
            [C, I, I, I],
            "update order_line set ol_delivery_d=$1 "
            "where ol_w_id=$2 and ol_d_id=$3 and ol_o_id=$4",
        ),
        (
            S_SUM_OL,
            True,
            [I, I, I],
            "select sum(ol_amount) from order_line "
            "where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3",
        ),
        (
            S_SEL_O_C_ID,
            True,
            [I, I, I],
            "select o_c_id from orders where o_w_id=$1 and o_d_id=$2 and o_id=$3",
        ),
        (
            S_SEL_CUST_DEL,
            True,
            [I, I, I],
            "select c_balance, c_delivery_cnt from customer "
            "where c_w_id=$1 and c_d_id=$2 and c_id=$3",
        ),
        (
            S_UPD_CUST_DEL,
            False,
            [F, I, I, I],
            "update customer set c_balance=c_balance+$1, c_delivery_cnt=c_delivery_cnt+1 "
            "where c_w_id=$2 and c_d_id=$3 and c_id=$4",
        ),
        (
            S_STOCK_LEVEL,
            True,
            [I, I, I, I, I, I],
            "select count(distinct (s_i_id)) from order_line, stock "
            "where ol_w_id=$1 and ol_d_id=$2 and ol_o_id>=$3 and ol_o_id<=$4 "
            "and s_w_id=$5 and s_i_id=ol_i_id and s_quantity<$6",
        ),
        (
            S_SEL_CUST_NO,
            True,
            [I, I, I],
            "select c_discount, c_last, c_credit from customer "
            "where c_w_id=$1 and c_d_id=$2 and c_id=$3",
        ),
        (
            S_SEL_CUST_WH_JOIN,
            True,
            [I, I, I],
            "select c_discount, c_last, c_credit, w_tax from customer, warehouse "
            "where w_id=$1 and c_w_id=w_id and c_d_id=$2 and c_id=$3",
        ),
        (
            S_SEL_OL_DEL_DETAILS,
            True,
            [I, I, I],
            "select ol_number, ol_amount, ol_delivery_d from order_line "
            "where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3 order by ol_number",
        ),
    ]
    for d in range(1, 11):
        dist = "s_dist_%02d" % d
        stmts.append(
            (
                _stock_sel_id(d),
                True,
                [I, I],
                "select s_quantity, %s, s_ytd, s_order_cnt, s_remote_cnt from stock "
                "where s_w_id=$1 and s_i_id=$2" % dist,
            )
        )
    return stmts


def install_prepare(cli: WireClient) -> None:
    cli.prepare_set(build_prepare_stmts())


def _fail(br: BatchResult, prefix: str) -> Tuple[bool, str]:
    kind = "abort" if br.aborted else "error"
    # Keep the failing batch stage and operation index. The diagnostic alone is
    # often just "deadlock prevention", which cannot identify the hot statement.
    return False, "%s%s op=%d executed=%d: %s" % (
        prefix,
        kind,
        br.failed_op,
        br.executed,
        (br.diagnostic or "")[:200],
    )


def _cell_int(rows, op_idx: int, col: int = 0) -> Optional[int]:
    r = rows.get(op_idx)
    if not r:
        return None
    return int(float(r[0][col]))


def _cell_float(rows, op_idx: int, col: int = 0) -> Optional[float]:
    r = rows.get(op_idx)
    if not r:
        return None
    return float(r[0][col])


def _pick_item(rng, scale) -> int:
    """决赛：约 25% 热点商品（用 1..100 近似 24-seed 热点集）。"""
    n = scale["items"]
    route_rng = scale.get("_route_rng", rng)
    hot_items = scale.get("hot_items")
    if hot_items and route_rng.randint(1, 100) <= 25:
        return route_rng.choice(hot_items)
    hot_n = min(100, n)
    if hot_n > 0 and route_rng.randint(1, 100) <= 25:
        return route_rng.randint(1, hot_n)
    return rng.randint(1, n)


def _pick_supply_w(rng, scale, home_w: int) -> int:
    """决赛：约 8% 远程供货仓。"""
    wcount = scale.get("warehouses", 1)
    route_rng = scale.get("_route_rng", rng)
    if wcount > 1 and route_rng.randint(1, 100) <= 8:
        choices = [w for w in range(1, wcount + 1) if w != home_w]
        if choices:
            return route_rng.choice(choices)
    return home_w


def run_neworder_batch(cli: WireClient, rng, scale, d_id=None, c_id=None, ol_cnt=None):
    w_id = scale.get("w_id", W_ID)
    d_id = d_id or rng.randint(1, scale["districts"])
    c_id = c_id or rng.randint(1, scale["customers_per_district"])
    ol_cnt = ol_cnt or rng.randint(scale["min_ol_cnt"], scale["max_ol_cnt"])

    lines = []
    all_local = 1
    for ol_no in range(1, ol_cnt + 1):
        i_id = _pick_item(rng, scale)
        supply_w = _pick_supply_w(rng, scale, w_id)
        if supply_w != w_id:
            all_local = 0
        qty = rng.randint(1, 10)
        lines.append((ol_no, i_id, supply_w, qty))

    # 升序去重锁定键（决赛要求）
    lock_keys = sorted({(sw, iid) for _, iid, sw, _ in lines})

    # ---- batch1: BEGIN / 基础行 / 锁库存 / 按原序读 item+stock ----
    ops1 = [
        (S_BEGIN, []),
        (S_SEL_CUST_WH_JOIN, [w_id, d_id, c_id]),
        (S_SEL_D_TAX_NEXT, [w_id, d_id]),
        (S_UPD_D_NEXT, [w_id, d_id]),
        (S_SEL_D_NEXT, [w_id, d_id]),
    ]
    lock_base = len(ops1)
    for sw, iid in lock_keys:
        ops1.append((_stock_sel_id(d_id), [sw, iid]))

    item_ops = []
    for _ol_no, i_id, _sw, _qty in lines:
        item_ops.append(len(ops1))
        ops1.append((S_SEL_ITEM, [i_id]))

    br1 = cli.exec_batch(ops1)
    if not br1.ok:
        return _fail(br1, "neworder b1 ")

    if not br1.results.get(1):
        return False, "neworder: customer×warehouse join missing"
    if not br1.results.get(2):
        return False, "neworder: district missing"
    o_id = _cell_int(br1.results, 4)
    if o_id is None:
        return False, "neworder: district re-read empty"
    o_id = o_id - 1

    stock_by_key = {}
    for i, key in enumerate(lock_keys):
        rows = br1.results.get(lock_base + i)
        if not rows:
            return False, "neworder: stock (%d,%d) missing" % key
        stock_by_key[key] = rows[0]

    prices = []
    for idx in item_ops:
        rows = br1.results.get(idx)
        if not rows:
            # 业务预期回滚：发 ABORT（单独 batch）
            br_ab = cli.exec_batch([(S_ABORT, [])])
            if not br_ab.ok and not br_ab.aborted:
                return _fail(br_ab, "neworder abort ")
            return False, "neworder: invalid item (expected rollback)"
        prices.append(float(rows[0][0]))

    # ---- batch2: 写入明细 / COMMIT ----
    # 同键多明细：按原序累计本地 qty，避免重复用锁时快照
    qty_cache = {k: int(float(stock_by_key[k][0])) for k in stock_by_key}
    ops2 = [
        (S_INS_ORDERS, [o_id, d_id, w_id, c_id, ENTRY_D, ol_cnt, all_local]),
        (S_INS_NEW_ORDERS, [o_id, d_id, w_id]),
    ]
    for (ol_no, i_id, supply_w, qty), i_price in zip(lines, prices):
        key = (supply_w, i_id)
        row = stock_by_key[key]
        s_qty = qty_cache[key]
        dist_info = row[1] if row[1] is not None else ""
        if s_qty >= qty + 10:
            new_qty = s_qty - qty
        else:
            new_qty = s_qty - qty + 91
        qty_cache[key] = new_qty
        remote = 1 if supply_w != w_id else 0
        ol_amount = float(qty) * float(i_price)
        ops2.append((S_UPD_STOCK, [new_qty, float(qty), remote, supply_w, i_id]))
        ops2.append(
            (
                S_INS_OL,
                [
                    o_id,
                    d_id,
                    w_id,
                    ol_no,
                    i_id,
                    supply_w,
                    ENTRY_D,
                    qty,
                    ol_amount,
                    str(dist_info)[:24],
                ],
            )
        )
    ops2.append((S_COMMIT, []))
    br2 = cli.exec_batch(ops2)
    if not br2.ok:
        return _fail(br2, "neworder b2 ")
    return True, ""


def run_payment_batch(cli: WireClient, rng, scale, d_id=None, c_id=None):
    w_id = scale.get("w_id", W_ID)
    d_id = d_id or rng.randint(1, scale["districts"])
    c_w_id = w_id
    c_d_id = d_id
    # 决赛：约 30% 远程客户仓
    route_rng = scale.get("_route_rng", rng)
    if c_id is None and scale.get("warehouses", 1) > 1 and route_rng.randint(1, 100) <= 30:
        c_w_id = route_rng.choice([w for w in range(1, scale["warehouses"] + 1) if w != w_id])
        c_d_id = route_rng.randint(1, scale["districts"])
    c_id = c_id or rng.randint(1, scale["customers_per_district"])
    amount = float("%.2f" % rng.uniform(1.0, 5000.0))

    # batch1: BEGIN / 读+更基础行 / 读客户
    br1 = cli.exec_batch(
        [
            (S_BEGIN, []),
            (S_SEL_W_TAX, [w_id]),
            (S_UPD_W_YTD, [amount, w_id]),
            (S_SEL_D_TAX_YTD, [w_id, d_id]),
            (S_UPD_D_YTD, [amount, w_id, d_id]),
            (S_SEL_CUST_PAY, [c_w_id, c_d_id, c_id]),
        ]
    )
    if not br1.ok:
        return _fail(br1, "payment b1 ")
    if not br1.results.get(1):
        return False, "payment: warehouse missing"
    if not br1.results.get(3):
        return False, "payment: district missing"
    crow = br1.results.get(5)
    if not crow:
        return False, "payment: customer missing"
    c_data = crow[0][3]
    h_data = (str(c_data) if c_data is not None else "")[:24]

    # batch2: 更新客户 / history / COMMIT
    br2 = cli.exec_batch(
        [
            (S_UPD_CUST_PAY, [amount, amount, c_w_id, c_d_id, c_id]),
            (S_INS_HIST, [c_id, c_d_id, c_w_id, d_id, w_id, ENTRY_D, amount, h_data]),
            (S_COMMIT, []),
        ]
    )
    if not br2.ok:
        return _fail(br2, "payment b2 ")
    return True, ""


def run_order_status_batch(cli: WireClient, rng, scale, d_id=None, c_id=None):
    w_id = scale.get("w_id", W_ID)
    d_id = d_id or rng.randint(1, scale["districts"])
    c_id = c_id or rng.randint(1, scale["customers_per_district"])

    br1 = cli.exec_batch([(S_BEGIN, []), (S_SEL_CUST_OS, [w_id, d_id, c_id])])
    if not br1.ok:
        return _fail(br1, "orderstatus b1 ")
    if not br1.results.get(1):
        return False, "orderstatus: customer missing"

    br2 = cli.exec_batch([(S_SEL_ORDERS_LATEST, [w_id, d_id, c_id])])
    if not br2.ok:
        return _fail(br2, "orderstatus b2 ")
    orows = br2.results.get(0)
    if not orows:
        br3 = cli.exec_batch([(S_COMMIT, [])])
        if not br3.ok:
            return _fail(br3, "orderstatus commit ")
        return True, ""

    o_id = int(float(orows[0][0]))
    br3 = cli.exec_batch(
        [(S_SEL_OL_OS, [w_id, d_id, o_id]), (S_COMMIT, [])]
    )
    if not br3.ok:
        return _fail(br3, "orderstatus b3 ")
    return True, ""


def run_delivery_batch(cli: WireClient, rng, scale):
    w_id = scale.get("w_id", W_ID)
    nd = scale["districts"]

    ops1 = [(S_BEGIN, [])]
    for d_id in range(1, nd + 1):
        ops1.append((S_SEL_NO_MIN, [w_id, d_id]))
    br1 = cli.exec_batch(ops1)
    if not br1.ok:
        return _fail(br1, "delivery b1 ")

    claimed = []
    for d_id in range(1, nd + 1):
        rows = br1.results.get(d_id)  # op 0 = begin
        if rows:
            claimed.append((d_id, int(float(rows[0][0]))))

    if not claimed:
        br2 = cli.exec_batch([(S_COMMIT, [])])
        if not br2.ok:
            return _fail(br2, "delivery empty-commit ")
        return True, ""

    # batch2: 领取 + 读 SUM / o_c_id / customer（仍依赖后续更新）
    ops2 = []
    sum_ops = []
    cid_ops = []
    for d_id, o_id in claimed:
        ops2.append((S_DEL_NO, [w_id, d_id, o_id]))
        ops2.append((S_UPD_ORDERS_CARRIER, [w_id, d_id, o_id]))
        ops2.append((S_UPD_OL_DELIVERY, [ENTRY_D, w_id, d_id, o_id]))
        ops2.append((S_SEL_OL_DEL_DETAILS, [w_id, d_id, o_id]))
        sum_ops.append(len(ops2))
        ops2.append((S_SUM_OL, [w_id, d_id, o_id]))
        cid_ops.append(len(ops2))
        ops2.append((S_SEL_O_C_ID, [w_id, d_id, o_id]))

    br2 = cli.exec_batch(ops2)
    if not br2.ok:
        return _fail(br2, "delivery b2 ")

    # batch3: 用 SUM 相对更新客户 + COMMIT
    ops3 = []
    for i, (d_id, _o_id) in enumerate(claimed):
        total = _cell_float(br2.results, sum_ops[i])
        c_id = _cell_int(br2.results, cid_ops[i])
        if total is None or c_id is None:
            # br2 本身成功时 AUTO_ABORT 不会介入；不能带着半成品显式事务进入
            # 下一轮 BEGIN，否则旧快照会长期卡住 MVCC 水位并污染后续压力结论。
            cli.exec_batch([(S_ABORT, [])])
            return False, "delivery: sum/c_id missing d=%d" % d_id
        ops3.append((S_UPD_CUST_DEL, [float(total), w_id, d_id, c_id]))
    ops3.append((S_COMMIT, []))

    br3 = cli.exec_batch(ops3)
    if not br3.ok:
        return _fail(br3, "delivery b3 ")
    return True, ""


def run_stock_level_batch(cli: WireClient, rng, scale, d_id=None):
    w_id = scale.get("w_id", W_ID)
    d_id = d_id or rng.randint(1, scale["districts"])
    threshold = rng.randint(10, 20)

    br1 = cli.exec_batch(
        [(S_BEGIN, []), (S_SEL_D_NEXT, [w_id, d_id])]
    )
    if not br1.ok:
        return _fail(br1, "stocklevel b1 ")
    d_next = _cell_int(br1.results, 1)
    if d_next is None:
        return False, "stocklevel: district missing"
    last_o_id = d_next - 1
    first_o_id = max(1, last_o_id - 19)

    br2 = cli.exec_batch(
        [
            (
                S_STOCK_LEVEL,
                [w_id, d_id, first_o_id, last_o_id, w_id, threshold],
            ),
            (S_COMMIT, []),
        ]
    )
    if not br2.ok:
        return _fail(br2, "stocklevel b2 ")
    return True, ""


BATCH_RUNNERS = {
    "new_order": run_neworder_batch,
    "payment": run_payment_batch,
    "order_status": run_order_status_batch,
    "delivery": run_delivery_batch,
    "stock_level": run_stock_level_batch,
}


def run_txn_batch(cli: WireClient, rng, scale, txn_name: str, route=None):
    fn = BATCH_RUNNERS.get(txn_name)
    if fn is None:
        return False, "unknown txn " + txn_name
    if route and txn_name != "delivery":
        return fn(cli, rng, scale, d_id=route.get("d_id"))
    return fn(cli, rng, scale)
