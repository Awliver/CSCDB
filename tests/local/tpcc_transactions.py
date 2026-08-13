"""TPC-C transaction implementations (SQL over RMDB socket protocol)."""

from tpcc_common import ENTRY_D, W_ID, parse_table_rows


def _int_cell(row, col=0):
    return int(float(row[col]))


def _float_cell(row, col=0):
    return float(row[col])


def _abort_on_fail(cli, ok, r, prefix):
    if not ok:
        cli.query("abort;")
        return False, prefix + r[:80]
    return True, ""


def run_neworder(cli, rng, scale, d_id=None, c_id=None, ol_cnt=None):
    """Execute one TPC-C NewOrder transaction (explicit begin/commit)."""
    w_id = scale.get("w_id", W_ID)
    d_id = d_id or rng.randint(1, scale["districts"])
    c_id = c_id or rng.randint(1, scale["customers_per_district"])
    ol_cnt = ol_cnt or rng.randint(scale["min_ol_cnt"], scale["max_ol_cnt"])
    dist_col = "s_dist_%02d" % d_id

    ok, r = cli.query_ok("begin;")
    if not ok:
        return False, "begin: " + r[:80]

    r = cli.query(
        "select d_tax, d_next_o_id from district where d_w_id = %d and d_id = %d;"
        % (w_id, d_id)
    )
    rows = parse_table_rows(r)
    if not rows:
        cli.query("abort;")
        return False, "district select empty: " + r[:80]
    d_next_o_id = _int_cell(rows[0], 1)
    o_id = d_next_o_id

    ok, r = cli.query_ok(
        "update district set d_next_o_id = %d where d_w_id = %d and d_id = %d;"
        % (d_next_o_id + 1, w_id, d_id)
    )
    if not ok:
        cli.query("abort;")
        return False, "update district: " + r[:80]

    # 并发下 district 行锁释放后计数器可能已被抬高，用 update 后的值推导 o_id
    r = cli.query(
        "select d_next_o_id from district where d_w_id = %d and d_id = %d;"
        % (w_id, d_id)
    )
    rows = parse_table_rows(r)
    if not rows:
        cli.query("abort;")
        return False, "district re-read empty: " + r[:80]
    o_id = _int_cell(rows[0], 0) - 1

    ok, r = cli.query_ok(
        "insert into orders values (%d, %d, %d, %d, '%s', 0, %d, 1);"
        % (o_id, d_id, w_id, c_id, ENTRY_D, ol_cnt)
    )
    if not ok:
        cli.query("abort;")
        return False, "insert orders: " + r[:80]

    ok, r = cli.query_ok(
        "insert into new_orders values (%d, %d, %d);" % (o_id, d_id, w_id)
    )
    if not ok:
        cli.query("abort;")
        return False, "insert new_orders: " + r[:80]

    for ol_no in range(1, ol_cnt + 1):
        i_id = rng.randint(1, scale["items"])
        qty = rng.randint(1, 10)
        supply_w_id = w_id

        r = cli.query("select i_price from item where i_id = %d;" % i_id)
        rows = parse_table_rows(r)
        if not rows:
            cli.query("abort;")
            return False, "item %d missing" % i_id
        i_price = _float_cell(rows[0], 0)

        r = cli.query(
            "select s_quantity, %s, s_ytd, s_order_cnt, s_remote_cnt from stock "
            "where s_w_id = %d and s_i_id = %d;" % (dist_col, w_id, i_id)
        )
        rows = parse_table_rows(r)
        if not rows:
            cli.query("abort;")
            return False, "stock %d missing" % i_id
        s_qty = _int_cell(rows[0], 0)
        dist_info = rows[0][1]
        s_ytd = _float_cell(rows[0], 2)
        s_order_cnt = _int_cell(rows[0], 3)
        s_remote_cnt = _int_cell(rows[0], 4)

        if s_qty >= qty + 10:
            new_qty = s_qty - qty
        else:
            new_qty = s_qty - qty + 91
        new_ytd = s_ytd + qty * i_price
        new_order_cnt = s_order_cnt + 1
        ol_amount = qty * i_price

        ok, r = cli.query_ok(
            "update stock set s_quantity = %d, s_ytd = %s, s_order_cnt = %d, "
            "s_remote_cnt = %d where s_w_id = %d and s_i_id = %d;"
            % (new_qty, new_ytd, new_order_cnt, s_remote_cnt, w_id, i_id)
        )
        if not ok:
            cli.query("abort;")
            return False, "update stock: " + r[:80]

        ok, r = cli.query_ok(
            "insert into order_line values (%d, %d, %d, %d, %d, %d, '%s', %d, %s, '%s');"
            % (o_id, d_id, w_id, ol_no, i_id, supply_w_id, ENTRY_D, qty, ol_amount, dist_info)
        )
        if not ok:
            cli.query("abort;")
            return False, "insert order_line: " + r[:80]

    ok, r = cli.query_ok("commit;")
    if not ok:
        cli.query("abort;")
        return False, "commit: " + r[:80]
    return True, ""


def run_payment(cli, rng, scale, d_id=None, c_id=None):
    """TPC-C Payment transaction."""
    w_id = scale.get("w_id", W_ID)
    d_id = d_id or rng.randint(1, scale["districts"])
    c_w_id = w_id
    c_d_id = d_id
    if c_id is None and scale.get("warehouses", 1) > 1 and rng.randint(1, 100) > 85:
        c_w_id = rng.choice([w for w in range(1, scale["warehouses"] + 1) if w != w_id])
        c_d_id = rng.randint(1, scale["districts"])
    c_id = c_id or rng.randint(1, scale["customers_per_district"])
    amount = round(rng.uniform(1.0, 5000.0), 2)

    ok, r = cli.query_ok("begin;")
    if not ok:
        return False, "begin: " + r[:80]

    r = cli.query("select w_tax from warehouse where w_id = %d;" % w_id)
    rows = parse_table_rows(r)
    if not rows:
        cli.query("abort;")
        return False, "warehouse missing"

    ok, r = cli.query_ok(
        "update warehouse set w_ytd = w_ytd + %s where w_id = %d;" % (amount, w_id)
    )
    if not ok:
        cli.query("abort;")
        return False, "update warehouse: " + r[:80]

    r = cli.query(
        "select d_tax, d_ytd from district where d_w_id = %d and d_id = %d;"
        % (w_id, d_id)
    )
    rows = parse_table_rows(r)
    if not rows:
        cli.query("abort;")
        return False, "district missing"

    ok, r = cli.query_ok(
        "update district set d_ytd = d_ytd + %s where d_w_id = %d and d_id = %d;"
        % (amount, w_id, d_id)
    )
    if not ok:
        cli.query("abort;")
        return False, "update district: " + r[:80]

    r = cli.query(
        "select c_balance, c_ytd_payment, c_payment_cnt, c_data from customer "
        "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (c_w_id, c_d_id, c_id)
    )
    rows = parse_table_rows(r)
    if not rows:
        cli.query("abort;")
        return False, "customer missing"
    c_balance = _float_cell(rows[0], 0)
    c_ytd_payment = _float_cell(rows[0], 1)
    c_payment_cnt = _int_cell(rows[0], 2)
    c_data = rows[0][3]

    new_balance = c_balance - amount
    ok, r = cli.query_ok(
        "update customer set c_balance = %s, c_ytd_payment = %s, c_payment_cnt = %d "
        "where c_w_id = %d and c_d_id = %d and c_id = %d;"
        % (new_balance, c_ytd_payment + amount, c_payment_cnt + 1, c_w_id, c_d_id, c_id)
    )
    if not ok:
        cli.query("abort;")
        return False, "update customer: " + r[:80]

    ok, r = cli.query_ok(
        "insert into history values (%d, %d, %d, %d, %d, '%s', %s, '%s');"
        % (c_id, c_d_id, c_w_id, d_id, w_id, ENTRY_D, amount, c_data[:24])
    )
    if not ok:
        cli.query("abort;")
        return False, "insert history: " + r[:80]

    ok, r = cli.query_ok("commit;")
    if not ok:
        cli.query("abort;")
        return False, "commit: " + r[:80]
    return True, ""


def run_order_status(cli, rng, scale, d_id=None, c_id=None):
    """TPC-C Order-Status (read-only)."""
    w_id = scale.get("w_id", W_ID)
    d_id = d_id or rng.randint(1, scale["districts"])
    c_id = c_id or rng.randint(1, scale["customers_per_district"])

    ok, r = cli.query_ok("begin;")
    if not ok:
        return False, "begin: " + r[:80]

    r = cli.query(
        "select c_balance, c_first, c_middle, c_last from customer "
        "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (w_id, d_id, c_id)
    )
    if not parse_table_rows(r):
        cli.query("abort;")
        return False, "customer missing"

    r = cli.query(
        "select o_id, o_entry_d, o_carrier_id from orders "
        "where o_w_id = %d and o_d_id = %d and o_c_id = %d order by o_id desc limit 1;"
        % (w_id, d_id, c_id)
    )
    rows = parse_table_rows(r)
    if not rows:
        ok, r = cli.query_ok("commit;")
        return ok, "" if ok else "commit: " + r[:80]
    o_id = _int_cell(rows[0], 0)

    r = cli.query(
        "select ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d "
        "from order_line where ol_w_id = %d and ol_d_id = %d and ol_o_id = %d;"
        % (w_id, d_id, o_id)
    )
    if "error" in r.lower():
        cli.query("abort;")
        return False, "order_line: " + r[:80]

    ok, r = cli.query_ok("commit;")
    if not ok:
        cli.query("abort;")
        return False, "commit: " + r[:80]
    return True, ""


def run_delivery(cli, rng, scale):
    """TPC-C Delivery: process oldest new_order per district."""
    w_id = scale.get("w_id", W_ID)
    ok, r = cli.query_ok("begin;")
    if not ok:
        return False, "begin: " + r[:80]

    for d_id in range(1, scale["districts"] + 1):
        r = cli.query(
            "select no_o_id from new_orders where no_w_id = %d and no_d_id = %d "
            "order by no_o_id asc limit 1;" % (w_id, d_id)
        )
        rows = parse_table_rows(r)
        if not rows:
            continue
        o_id = _int_cell(rows[0], 0)

        ok, r = cli.query_ok(
            "delete from new_orders where no_w_id = %d and no_d_id = %d and no_o_id = %d;"
            % (w_id, d_id, o_id)
        )
        if not ok:
            cli.query("abort;")
            return False, "delete new_orders: " + r[:80]

        ok, r = cli.query_ok(
            "update orders set o_carrier_id = 10 where o_w_id = %d and o_d_id = %d and o_id = %d;"
            % (w_id, d_id, o_id)
        )
        if not ok:
            cli.query("abort;")
            return False, "update orders: " + r[:80]

        ok, r = cli.query_ok(
            "update order_line set ol_delivery_d = '%s' "
            "where ol_w_id = %d and ol_d_id = %d and ol_o_id = %d;"
            % (ENTRY_D, w_id, d_id, o_id)
        )
        if not ok:
            cli.query("abort;")
            return False, "update order_line: " + r[:80]

        r = cli.query(
            "select sum(ol_amount) from order_line "
            "where ol_w_id = %d and ol_d_id = %d and ol_o_id = %d;" % (w_id, d_id, o_id)
        )
        rows = parse_table_rows(r)
        if not rows:
            cli.query("abort;")
            return False, "sum order_line failed"
        total = _float_cell(rows[0], 0)

        r = cli.query(
            "select o_c_id from orders where o_w_id = %d and o_d_id = %d and o_id = %d;"
            % (w_id, d_id, o_id)
        )
        rows = parse_table_rows(r)
        if not rows:
            cli.query("abort;")
            return False, "orders missing for delivery"
        c_id = _int_cell(rows[0], 0)

        r = cli.query(
            "select c_balance, c_delivery_cnt from customer "
            "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (w_id, d_id, c_id)
        )
        rows = parse_table_rows(r)
        if not rows:
            cli.query("abort;")
            return False, "customer missing for delivery"
        c_balance = _float_cell(rows[0], 0)
        c_delivery_cnt = _int_cell(rows[0], 1)

        ok, r = cli.query_ok(
            "update customer set c_balance = %s, c_delivery_cnt = %d "
            "where c_w_id = %d and c_d_id = %d and c_id = %d;"
            % (c_balance + total, c_delivery_cnt + 1, w_id, d_id, c_id)
        )
        if not ok:
            cli.query("abort;")
            return False, "update customer delivery: " + r[:80]

    ok, r = cli.query_ok("commit;")
    if not ok:
        cli.query("abort;")
        return False, "commit: " + r[:80]
    return True, ""


def run_stock_level(cli, rng, scale, d_id=None):
    """TPC-C Stock-Level (read-only aggregate)."""
    w_id = scale.get("w_id", W_ID)
    d_id = d_id or rng.randint(1, scale["districts"])
    threshold = 20

    ok, r = cli.query_ok("begin;")
    if not ok:
        return False, "begin: " + r[:80]

    r = cli.query(
        "select d_next_o_id from district where d_w_id = %d and d_id = %d;"
        % (w_id, d_id)
    )
    rows = parse_table_rows(r)
    if not rows:
        cli.query("abort;")
        return False, "district missing"
    d_next_o_id = _int_cell(rows[0], 0)
    last_o_id = d_next_o_id - 1
    first_o_id = max(1, last_o_id - 19)

    # 决赛规范：低于阈值的“不重复商品数”须用引擎原生 COUNT(DISTINCT (col))，
    # 只发单条查询，禁止客户端对 join 结果去重（同一商品可能出现在多条 order_line 里）。
    r = cli.query(
        "select count(distinct (s_i_id)) from order_line, stock where ol_w_id = %d and ol_d_id = %d "
        "and ol_o_id >= %d and ol_o_id <= %d and s_w_id = %d and s_i_id = ol_i_id "
        "and s_quantity < %d;" % (w_id, d_id, first_o_id, last_o_id, w_id, threshold)
    )
    if "error" in r.lower():
        cli.query("abort;")
        return False, "stock-level query: " + r[:80]

    ok, r = cli.query_ok("commit;")
    if not ok:
        cli.query("abort;")
        return False, "commit: " + r[:80]
    return True, ""


# 初赛风格混合（10/23）；决赛正式为 TXN_WEIGHTS_FINALS
TXN_WEIGHTS_LEGACY = [
    ("new_order", 10, run_neworder),
    ("payment", 10, run_payment),
    ("order_status", 1, run_order_status),
    ("delivery", 1, run_delivery),
    ("stock_level", 1, run_stock_level),
]

# 决赛 OJ：NewOrder 45% / Payment 43% / 其余各 4%
TXN_WEIGHTS_FINALS = [
    ("new_order", 45, run_neworder),
    ("payment", 43, run_payment),
    ("order_status", 4, run_order_status),
    ("delivery", 4, run_delivery),
    ("stock_level", 4, run_stock_level),
]

# 默认跟随决赛混合（本地 mid/finals 与 OJ 可比）；--legacy-mix 切回 10/23
TXN_WEIGHTS = TXN_WEIGHTS_FINALS


def make_txn_population(weights=None):
    weights = weights or TXN_WEIGHTS
    pop = []
    for name, w, fn in weights:
        pop.extend([(name, fn)] * w)
    return pop


TXN_POPULATION = make_txn_population(TXN_WEIGHTS)


def pick_txn(rng, population=None):
    return rng.choice(population or TXN_POPULATION)


def run_txn(cli, rng, scale, txn_name=None, population=None, route=None):
    if txn_name:
        for name, _, fn in TXN_WEIGHTS_LEGACY:
            if name == txn_name:
                if route and name != "delivery":
                    return fn(cli, rng, scale, d_id=route.get("d_id"))
                return fn(cli, rng, scale)
        return False, "unknown txn " + txn_name
    _, fn = pick_txn(rng, population)
    return fn(cli, rng, scale)
