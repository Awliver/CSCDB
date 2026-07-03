"""TPC-C transactions per spec clauses 2.4-2.8.

Faithful points vs the team's old harness:
  - NURand inputs: c_id ~ NURand(1023), i_id ~ NURand(8191), c_last ~ NURand(255)
  - NewOrder: 1% intentional rollback via unused item id (spec 2.4.1.4)
  - Payment/OrderStatus: 60% access customer by last name (middle row by c_first)
  - Payment: 15% remote warehouse when W > 1
  - NewOrder stock rules: s_ytd += qty (spec, not qty*price), s_remote_cnt on remote
  - Delivery: one random carrier per txn, all districts, skipped counted
  - StockLevel: random threshold 10..20, client-side DISTINCT on joined item ids
  - Real timestamps

Every function returns (status, detail):
  'commit'   - committed
  'rollback' - intentional NewOrder rollback (counts as OK per spec, not tpmC)
  'abort'    - system abort (deadlock prevention / write-write conflict)
  'error'    - unexpected SQL error (engine or harness bug -> investigate)
"""

import time

from . import schema
from .db import one_float, one_int


def now_str():
    return time.strftime("%Y-%m-%d %H:%M:%S")


class Config:
    def __init__(self, warehouses=1, items=schema.ITEMS,
                 districts=schema.DISTRICTS_PER_W, customers=schema.CUSTOMERS_PER_D,
                 by_last_name_pct=60):
        self.warehouses = warehouses
        self.items = items
        self.districts = districts
        self.customers = customers
        self.by_last_name_pct = by_last_name_pct


def _fail(cli, status, detail):
    cli.rollback_quiet()
    return status, detail


def _run(cli, sql, ctx):
    """Execute a write/DDL statement inside a txn; abort txn on any non-ok."""
    st, r = cli.exec(sql)
    if st == "ok":
        return None
    return _fail(cli, "abort" if st == "abort" else "error", ctx + ": " + r[:100])


def _select(cli, sql, ctx):
    """Execute a select; returns (rows, err). err is a (status, detail) to return."""
    st, r = cli.exec(sql)
    if st != "ok":
        return None, _fail(cli, "abort" if st == "abort" else "error", ctx + ": " + r[:100])
    from .db import parse_rows
    return parse_rows(r), None


def _customer_by_last_name(cli, r, cfg, w_id, d_id):
    """Spec 2.5.2.2: select matching customers ordered by c_first, take the middle.
    Returns (c_id, err)."""
    c_last = r.nurand_c_last()
    rows, err = _select(
        cli,
        "select c_id, c_first from customer where c_w_id = %d and c_d_id = %d "
        "and c_last = '%s' order by c_first;" % (w_id, d_id, c_last),
        "customer by last name")
    if err:
        return None, err
    if not rows:
        # name absent (downscaled data) -> fall back to NURand c_id
        return r.nurand_c_id(cfg.customers), None
    return int(float(rows[len(rows) // 2][0])), None


# ---- 2.4 New-Order ---------------------------------------------------------------

def new_order(cli, r, cfg, w_id, d_id=None, c_id=None, force_rollback=None):
    d_id = d_id or r.randint(1, cfg.districts)
    c_id = c_id or r.nurand_c_id(cfg.customers)
    ol_cnt = r.randint(5, 15)
    # spec 2.4.1.4: 1% must roll back (force_rollback pins it for smoke tests)
    rbk = force_rollback if force_rollback is not None else (r.randint(1, 100) == 1)
    entry_d = now_str()
    dist_col = "s_dist_%02d" % d_id

    # generate all lines up front; invalid item goes last for the rollback case
    lines = []
    all_local = 1
    for n in range(1, ol_cnt + 1):
        i_id = r.nurand_i_id(cfg.items)
        supply_w = w_id
        if cfg.warehouses > 1 and r.randint(1, 100) == 1:   # 1% remote line
            supply_w = r.choice([w for w in range(1, cfg.warehouses + 1) if w != w_id])
            all_local = 0
        lines.append([n, i_id, supply_w, r.randint(1, 10)])
    if rbk:
        lines[-1][1] = cfg.items + 1      # unused item id -> select comes back empty

    st, _ = cli.exec("begin;")
    if st != "ok":
        return "abort" if st == "abort" else "error", "begin"

    # customer discount / warehouse tax (fetched per spec even if unused afterwards)
    rows, err = _select(cli, "select w_tax from warehouse where w_id = %d;" % w_id, "w_tax")
    if err:
        return err
    if not rows:
        return _fail(cli, "error", "warehouse %d missing" % w_id)
    rows, err = _select(
        cli, "select c_discount, c_last, c_credit from customer "
             "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (w_id, d_id, c_id),
        "customer")
    if err:
        return err
    if not rows:
        return _fail(cli, "error", "customer %d missing" % c_id)

    rows, err = _select(
        cli, "select d_tax, d_next_o_id from district where d_w_id = %d and d_id = %d;"
        % (w_id, d_id), "district")
    if err:
        return err
    if not rows:
        return _fail(cli, "error", "district %d missing" % d_id)
    o_id = one_int(rows, 1)

    err = _run(cli, "update district set d_next_o_id = %d where d_w_id = %d and d_id = %d;"
               % (o_id + 1, w_id, d_id), "update district")
    if err:
        return err
    # re-read after acquiring the row: another txn may have bumped the counter first
    rows, err = _select(
        cli, "select d_next_o_id from district where d_w_id = %d and d_id = %d;"
        % (w_id, d_id), "district re-read")
    if err:
        return err
    o_id = one_int(rows, 0) - 1

    err = _run(cli, "insert into orders values (%d, %d, %d, %d, '%s', %d, %d, %d);"
               % (o_id, d_id, w_id, c_id, entry_d, schema.CARRIER_NULL, ol_cnt, all_local),
               "insert orders")
    if err:
        return err
    err = _run(cli, "insert into new_orders values (%d, %d, %d);" % (o_id, d_id, w_id),
               "insert new_orders")
    if err:
        return err

    for ol_no, i_id, supply_w, qty in lines:
        rows, err = _select(cli, "select i_price from item where i_id = %d;" % i_id, "item")
        if err:
            return err
        if not rows:
            # invalid item: spec-mandated user rollback of the whole txn
            cli.rollback_quiet()
            return ("rollback", "") if rbk else ("error", "item %d unexpectedly missing" % i_id)
        i_price = one_float(rows, 0)

        rows, err = _select(
            cli, "select s_quantity, %s, s_ytd, s_order_cnt, s_remote_cnt from stock "
                 "where s_w_id = %d and s_i_id = %d;" % (dist_col, supply_w, i_id), "stock")
        if err:
            return err
        if not rows:
            return _fail(cli, "error", "stock (%d,%d) missing" % (supply_w, i_id))
        s_qty = int(float(rows[0][0]))
        dist_info = rows[0][1]
        s_ytd = float(rows[0][2])
        s_order_cnt = int(float(rows[0][3]))
        s_remote_cnt = int(float(rows[0][4]))

        new_qty = s_qty - qty if s_qty - qty >= 10 else s_qty - qty + 91
        remote = 1 if supply_w != w_id else 0
        err = _run(cli, "update stock set s_quantity = %d, s_ytd = %.2f, s_order_cnt = %d, "
                   "s_remote_cnt = %d where s_w_id = %d and s_i_id = %d;"
                   % (new_qty, s_ytd + qty, s_order_cnt + 1, s_remote_cnt + remote,
                      supply_w, i_id), "update stock")
        if err:
            return err

        ol_amount = round(qty * i_price, 2)
        err = _run(cli, "insert into order_line values (%d, %d, %d, %d, %d, %d, '%s', %d, %.2f, '%s');"
                   % (o_id, d_id, w_id, ol_no, i_id, supply_w,
                      schema.DELIVERY_D_NULL, qty, ol_amount, dist_info), "insert order_line")
        if err:
            return err

    st, rr = cli.exec("commit;")
    if st != "ok":
        return _fail(cli, "abort" if st == "abort" else "error", "commit: " + rr[:100])
    return "commit", ""


# ---- 2.5 Payment ------------------------------------------------------------------

def payment(cli, r, cfg, w_id, d_id=None, c_id=None, amount=None):
    fixed_c = c_id is not None
    d_id = d_id or r.randint(1, cfg.districts)
    amount = amount if amount is not None else r.money(1.00, 5000.00)
    # 85% home / 15% remote customer (spec 2.5.1.2); degenerate to home when W=1
    if not fixed_c and cfg.warehouses > 1 and r.randint(1, 100) > 85:
        c_w_id = r.choice([w for w in range(1, cfg.warehouses + 1) if w != w_id])
        c_d_id = r.randint(1, cfg.districts)
    else:
        c_w_id, c_d_id = w_id, d_id

    st, _ = cli.exec("begin;")
    if st != "ok":
        return "abort" if st == "abort" else "error", "begin"

    err = _run(cli, "update warehouse set w_ytd = w_ytd + %.2f where w_id = %d;"
               % (amount, w_id), "update warehouse")
    if err:
        return err
    rows, err = _select(cli, "select w_name, w_street_1, w_city from warehouse "
                        "where w_id = %d;" % w_id, "warehouse")
    if err:
        return err

    err = _run(cli, "update district set d_ytd = d_ytd + %.2f "
               "where d_w_id = %d and d_id = %d;" % (amount, w_id, d_id), "update district")
    if err:
        return err
    rows, err = _select(cli, "select d_name, d_street_1, d_city from district "
                        "where d_w_id = %d and d_id = %d;" % (w_id, d_id), "district")
    if err:
        return err

    if not fixed_c:
        if r.randint(1, 100) <= cfg.by_last_name_pct:
            c_id, err = _customer_by_last_name(cli, r, cfg, c_w_id, c_d_id)
            if err:
                return err
        else:
            c_id = r.nurand_c_id(cfg.customers)

    rows, err = _select(
        cli, "select c_balance, c_ytd_payment, c_payment_cnt, c_credit from customer "
             "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (c_w_id, c_d_id, c_id),
        "customer")
    if err:
        return err
    if not rows:
        return _fail(cli, "error", "customer %d missing" % c_id)
    c_balance = float(rows[0][0])
    c_ytd = float(rows[0][1])
    c_cnt = int(float(rows[0][2]))

    err = _run(cli, "update customer set c_balance = %.2f, c_ytd_payment = %.2f, "
               "c_payment_cnt = %d where c_w_id = %d and c_d_id = %d and c_id = %d;"
               % (c_balance - amount, c_ytd + amount, c_cnt + 1, c_w_id, c_d_id, c_id),
               "update customer")
    if err:
        return err

    err = _run(cli, "insert into history values (%d, %d, %d, %d, %d, '%s', %.2f, '%s');"
               % (c_id, c_d_id, c_w_id, d_id, w_id, now_str(), amount, "payment"),
               "insert history")
    if err:
        return err

    st, rr = cli.exec("commit;")
    if st != "ok":
        return _fail(cli, "abort" if st == "abort" else "error", "commit: " + rr[:100])
    return "commit", ""


# ---- 2.6 Order-Status (read only) --------------------------------------------------

def order_status(cli, r, cfg, w_id, d_id=None, c_id=None):
    fixed_c = c_id is not None
    d_id = d_id or r.randint(1, cfg.districts)

    st, _ = cli.exec("begin;")
    if st != "ok":
        return "abort" if st == "abort" else "error", "begin"

    if not fixed_c:
        if r.randint(1, 100) <= cfg.by_last_name_pct:
            c_id, err = _customer_by_last_name(cli, r, cfg, w_id, d_id)
            if err:
                return err
        else:
            c_id = r.nurand_c_id(cfg.customers)

    rows, err = _select(
        cli, "select c_balance, c_first, c_middle, c_last from customer "
             "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (w_id, d_id, c_id),
        "customer")
    if err:
        return err
    if not rows:
        return _fail(cli, "error", "customer %d missing" % c_id)

    rows, err = _select(
        cli, "select o_id, o_entry_d, o_carrier_id from orders "
             "where o_w_id = %d and o_d_id = %d and o_c_id = %d order by o_id desc limit 1;"
        % (w_id, d_id, c_id), "latest order")
    if err:
        return err
    if rows:
        o_id = one_int(rows, 0)
        _, err = _select(
            cli, "select ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d "
                 "from order_line where ol_w_id = %d and ol_d_id = %d and ol_o_id = %d;"
            % (w_id, d_id, o_id), "order lines")
        if err:
            return err

    st, rr = cli.exec("commit;")
    if st != "ok":
        return _fail(cli, "abort" if st == "abort" else "error", "commit: " + rr[:100])
    return "commit", ""


# ---- 2.7 Delivery -------------------------------------------------------------------

def delivery(cli, r, cfg, w_id):
    carrier = r.randint(1, 10)
    dd = now_str()

    st, _ = cli.exec("begin;")
    if st != "ok":
        return "abort" if st == "abort" else "error", "begin"

    for d_id in range(1, cfg.districts + 1):
        rows, err = _select(
            cli, "select no_o_id from new_orders where no_w_id = %d and no_d_id = %d "
                 "order by no_o_id asc limit 1;" % (w_id, d_id), "oldest new_order")
        if err:
            return err
        if not rows:
            continue                       # skipped delivery (spec 2.7.4.2)
        o_id = one_int(rows, 0)

        err = _run(cli, "delete from new_orders where no_w_id = %d and no_d_id = %d "
                   "and no_o_id = %d;" % (w_id, d_id, o_id), "delete new_orders")
        if err:
            return err
        err = _run(cli, "update orders set o_carrier_id = %d "
                   "where o_w_id = %d and o_d_id = %d and o_id = %d;"
                   % (carrier, w_id, d_id, o_id), "update orders")
        if err:
            return err
        err = _run(cli, "update order_line set ol_delivery_d = '%s' "
                   "where ol_w_id = %d and ol_d_id = %d and ol_o_id = %d;"
                   % (dd, w_id, d_id, o_id), "update order_line")
        if err:
            return err

        rows, err = _select(
            cli, "select sum(ol_amount) from order_line "
                 "where ol_w_id = %d and ol_d_id = %d and ol_o_id = %d;"
            % (w_id, d_id, o_id), "sum ol_amount")
        if err:
            return err
        total = one_float(rows, 0, 0.0)

        rows, err = _select(
            cli, "select o_c_id from orders where o_w_id = %d and o_d_id = %d and o_id = %d;"
            % (w_id, d_id, o_id), "order customer")
        if err:
            return err
        if not rows:
            return _fail(cli, "error", "order %d vanished during delivery" % o_id)
        c_id = one_int(rows, 0)

        rows, err = _select(
            cli, "select c_balance, c_delivery_cnt from customer "
                 "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (w_id, d_id, c_id),
            "customer")
        if err:
            return err
        if not rows:
            return _fail(cli, "error", "customer %d missing in delivery" % c_id)
        bal = float(rows[0][0])
        cnt = int(float(rows[0][1]))

        err = _run(cli, "update customer set c_balance = %.2f, c_delivery_cnt = %d "
                   "where c_w_id = %d and c_d_id = %d and c_id = %d;"
                   % (bal + total, cnt + 1, w_id, d_id, c_id), "update customer")
        if err:
            return err

    st, rr = cli.exec("commit;")
    if st != "ok":
        return _fail(cli, "abort" if st == "abort" else "error", "commit: " + rr[:100])
    return "commit", ""


# ---- 2.8 Stock-Level (read only) ----------------------------------------------------

def stock_level(cli, r, cfg, w_id, d_id=None):
    d_id = d_id or r.randint(1, cfg.districts)
    threshold = r.randint(10, 20)

    st, _ = cli.exec("begin;")
    if st != "ok":
        return "abort" if st == "abort" else "error", "begin"

    rows, err = _select(
        cli, "select d_next_o_id from district where d_w_id = %d and d_id = %d;"
        % (w_id, d_id), "district")
    if err:
        return err
    if not rows:
        return _fail(cli, "error", "district %d missing" % d_id)
    next_o = one_int(rows, 0)
    lo, hi = max(1, next_o - 20), next_o - 1

    # spec wants COUNT(DISTINCT s_i_id); engine has no DISTINCT -> fetch joined
    # item ids and dedupe client-side (documented deviation, same result)
    rows, err = _select(
        cli, "select s_i_id from order_line, stock where ol_w_id = %d and ol_d_id = %d "
             "and ol_o_id >= %d and ol_o_id <= %d and s_w_id = %d and s_i_id = ol_i_id "
             "and s_quantity < %d;" % (w_id, d_id, lo, hi, w_id, threshold), "stock join")
    if err:
        return err
    _ = len({row[0] for row in rows})      # low-stock distinct item count

    st, rr = cli.exec("commit;")
    if st != "ok":
        return _fail(cli, "abort" if st == "abort" else "error", "commit: " + rr[:100])
    return "commit", ""


# ---- mix ----------------------------------------------------------------------------

TXNS = [("new_order", new_order), ("payment", payment),
        ("order_status", order_status), ("delivery", delivery),
        ("stock_level", stock_level)]

MIX_PRESETS = {
    "tpcc": (45, 43, 4, 4, 4),   # spec 5.2.3 standard mix
    "oj":   (10, 10, 1, 1, 1),   # competition perf-test mix
}


def build_deck(weights):
    """Weighted txn deck; shuffle-and-deal gives an exact mix per pass (spec 5.2.4.2)."""
    deck = []
    for (name, fn), w in zip(TXNS, weights):
        deck.extend([(name, fn)] * w)
    return deck
