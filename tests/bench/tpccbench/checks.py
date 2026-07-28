"""TPC-C consistency conditions C1..C12 (spec clause 3.3.2).

C1-C5, C8, C9 run as cheap aggregates over every warehouse/district.
C6, C7, C10, C12 join per order/customer, so they run on random samples
(--samples controls the size). C11 is implied by C5 in this dialect
(carrier sentinel <-> new_orders row) and is folded into it.

Money tolerance: engine floats accumulate rounding across thousands of
updates; diffs are compared with abs+rel tolerance and reported.
"""

from . import schema
from .db import one_float, one_int


def _tol(expected, abs_tol=1.0, rel_tol=1e-5):
    return abs_tol + rel_tol * abs(expected)


class Report:
    def __init__(self):
        self.results = []           # (name, ok, detail)

    def add(self, name, ok, detail=""):
        self.results.append((name, ok, detail))
        print("  %-4s %s %s" % (name, "PASS" if ok else "FAIL",
                                ("- " + detail) if (detail and not ok) else ""), flush=True)

    @property
    def ok(self):
        return all(ok for _, ok, _ in self.results)


def run_checks(cli, warehouses, districts, samples=8, rng=None, deep=False):
    """Run all consistency conditions; returns Report."""
    import random
    rng = rng or random.Random(7)
    rep = Report()

    for w in range(1, warehouses + 1):
        # ---- C1: w_ytd = sum(d_ytd) ------------------------------------------
        _, r1 = cli.rows("select w_ytd from warehouse where w_id = %d;" % w)
        _, r2 = cli.rows("select sum(d_ytd) from district where d_w_id = %d;" % w)
        w_ytd, sum_d = one_float(r1, 0), one_float(r2, 0)
        okc = w_ytd is not None and sum_d is not None and abs(w_ytd - sum_d) <= _tol(w_ytd)
        rep.add("C1", okc, "w=%d w_ytd=%s sum(d_ytd)=%s" % (w, w_ytd, sum_d))

        # ---- C8: w_ytd = sum(h_amount) over the warehouse ----------------------
        _, rh = cli.rows("select sum(h_amount) from history where h_w_id = %d;" % w)
        sum_h = one_float(rh, 0)
        okc = w_ytd is not None and sum_h is not None and abs(w_ytd - sum_h) <= _tol(w_ytd)
        rep.add("C8", okc, "w=%d w_ytd=%s sum(h_amount)=%s" % (w, w_ytd, sum_h))

        for d in range(1, districts + 1):
            where_d = "d_w_id = %d and d_id = %d" % (w, d)
            where_o = "o_w_id = %d and o_d_id = %d" % (w, d)
            where_no = "no_w_id = %d and no_d_id = %d" % (w, d)
            where_ol = "ol_w_id = %d and ol_d_id = %d" % (w, d)

            _, rd = cli.rows("select d_next_o_id, d_ytd from district where %s;" % where_d)
            next_o = one_int(rd, 0)
            d_ytd = one_float(rd, 1)

            # ---- C2: d_next_o_id - 1 = max(o_id) = max(no_o_id) ---------------
            _, ro = cli.rows("select max(o_id) from orders where %s;" % where_o)
            max_o = one_int(ro, 0)
            _, rn = cli.rows("select count(*), max(no_o_id), min(no_o_id) "
                             "from new_orders where %s;" % where_no)
            no_cnt = one_int(rn, 0, 0)
            ok2 = (max_o == next_o - 1)
            det = "d=%d next=%s max(o)=%s" % (d, next_o, max_o)
            if no_cnt and no_cnt > 0:
                max_no, min_no = one_int(rn, 1), one_int(rn, 2)
                ok2 = ok2 and (max_no == next_o - 1)
                det += " max(no)=%s" % max_no
                # ---- C3: contiguity of new_orders ------------------------------
                rep.add("C3", max_no - min_no + 1 == no_cnt,
                        "d=%d max-min+1=%d count=%d" % (d, max_no - min_no + 1, no_cnt))
            rep.add("C2", ok2, det)

            # ---- C4: sum(o_ol_cnt) = count(order_line) --------------------------
            _, rs = cli.rows("select sum(o_ol_cnt) from orders where %s;" % where_o)
            _, rc = cli.rows("select count(*) from order_line where %s;" % where_ol)
            s_ol, c_ol = one_int(rs, 0, 0), one_int(rc, 0, 0)
            rep.add("C4", s_ol == c_ol, "d=%d sum(o_ol_cnt)=%d count(ol)=%d" % (d, s_ol, c_ol))

            # ---- C5 (+C11): undelivered orders <-> new_orders rows --------------
            _, ru = cli.rows("select count(*) from orders where %s and o_carrier_id = %d;"
                             % (where_o, schema.CARRIER_NULL))
            undeliv = one_int(ru, 0, 0)
            rep.add("C5", undeliv == no_cnt,
                    "d=%d carrier-null orders=%d new_orders=%d" % (d, undeliv, no_cnt))

            # ---- C9: d_ytd = sum(h_amount) per district --------------------------
            _, rh = cli.rows("select sum(h_amount) from history "
                             "where h_w_id = %d and h_d_id = %d;" % (w, d))
            sum_h = one_float(rh, 0)
            okc = d_ytd is not None and sum_h is not None and abs(d_ytd - sum_h) <= _tol(d_ytd)
            rep.add("C9", okc, "d=%d d_ytd=%s sum(h)=%s" % (d, d_ytd, sum_h))

            # ---- C6 / C7: sampled per-order checks --------------------------------
            if next_o and next_o > 2:
                for o_id in rng.sample(range(1, next_o), min(samples, next_o - 1)):
                    _, ro1 = cli.rows("select o_ol_cnt, o_carrier_id from orders "
                                      "where %s and o_id = %d;" % (where_o, o_id))
                    if not ro1:
                        rep.add("C6", False, "d=%d order %d missing" % (d, o_id))
                        continue
                    ol_cnt = one_int(ro1, 0)
                    carrier = one_int(ro1, 1)
                    _, rc1 = cli.rows("select count(*) from order_line "
                                      "where %s and ol_o_id = %d;" % (where_ol, o_id))
                    rep.add("C6", one_int(rc1, 0, -1) == ol_cnt,
                            "d=%d o=%d ol_cnt=%d lines=%s" % (d, o_id, ol_cnt, one_int(rc1, 0)))
                    # C7: delivery date sentinel iff order undelivered
                    _, rp = cli.rows("select count(*) from order_line where %s and ol_o_id = %d "
                                     "and ol_delivery_d = '%s';"
                                     % (where_ol, o_id, schema.DELIVERY_D_NULL))
                    pending = one_int(rp, 0, -1)
                    exp = ol_cnt if carrier == schema.CARRIER_NULL else 0
                    rep.add("C7", pending == exp,
                            "d=%d o=%d carrier=%s pending_lines=%d expect=%d"
                            % (d, o_id, carrier, pending, exp))

            # ---- C10 / C12: sampled per-customer balance identities ---------------
            if deep:
                _, rcm = cli.rows("select max(c_id) from customer "
                                  "where c_w_id = %d and c_d_id = %d;" % (w, d))
                cmax = one_int(rcm, 0, 0)
                for c_id in rng.sample(range(1, cmax + 1), min(3, cmax)):
                    ok10, det10 = _check_customer_balance(cli, w, d, c_id)
                    rep.add("C10", ok10, det10)

    return rep


def _check_customer_balance(cli, w, d, c_id):
    """C10/C12: c_balance = sum(delivered ol_amount) - sum(h_amount) for customer.
    Scans orders by o_c_id (unindexed) — only used with --deep."""
    _, rb = cli.rows("select c_balance from customer "
                     "where c_w_id = %d and c_d_id = %d and c_id = %d;" % (w, d, c_id))
    bal = one_float(rb, 0)
    if bal is None:
        return False, "c=%d missing" % c_id
    _, rh = cli.rows("select sum(h_amount) from history "
                     "where h_c_w_id = %d and h_c_d_id = %d and h_c_id = %d;" % (w, d, c_id))
    sum_h = one_float(rh, 0, 0.0) or 0.0
    _, ro = cli.rows("select o_id, o_carrier_id from orders "
                     "where o_w_id = %d and o_d_id = %d and o_c_id = %d;" % (w, d, c_id))
    sum_ol = 0.0
    for row in ro:
        if int(float(row[1])) == schema.CARRIER_NULL:
            continue                      # undelivered lines don't count
        _, rs = cli.rows("select sum(ol_amount) from order_line "
                         "where ol_w_id = %d and ol_d_id = %d and ol_o_id = %d;"
                         % (w, d, int(float(row[0]))))
        sum_ol += one_float(rs, 0, 0.0) or 0.0
    expect = sum_ol - sum_h
    ok = abs(bal - expect) <= _tol(expect, abs_tol=1.0)
    return ok, "w=%d d=%d c=%d balance=%.2f expect=%.2f (ol=%.2f h=%.2f)" % (
        w, d, c_id, bal, expect, sum_ol, sum_h)
