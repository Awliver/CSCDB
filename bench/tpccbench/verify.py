"""Transaction correctness (smoke) tests: run each transaction once against a
loaded database and assert its EXACT side effects — not just "didn't error".

new_order commit   -> d_next_o_id+1, orders/new_orders +1, order_line +o_ol_cnt
new_order rollback -> zero side effects (spec 2.4.1.4)
payment            -> w_ytd/d_ytd +amt, c_balance -amt, c_ytd_payment +amt, history +1
order_status       -> read-only, commits
delivery           -> oldest new_order removed, carrier set, lines dated,
                      c_balance += sum(ol_amount), c_delivery_cnt+1
stock_level        -> read-only, commits
"""

from . import schema, workload
from .db import one_float, one_int
from .tpcrand import TpccRandom

TOL = 0.05      # single-op float tolerance


class SmokeReport:
    def __init__(self):
        self.results = []

    def add(self, name, ok, detail=""):
        self.results.append((name, ok, detail))
        print("  %-28s %s %s" % (name, "PASS" if ok else "FAIL",
                                 ("- " + detail) if (detail and not ok) else ""), flush=True)

    @property
    def ok(self):
        return all(ok for _, ok, _ in self.results)


def _counts(cli, w, d):
    _, r1 = cli.rows("select d_next_o_id from district where d_w_id=%d and d_id=%d;" % (w, d))
    _, r2 = cli.rows("select count(*) from orders where o_w_id=%d and o_d_id=%d;" % (w, d))
    _, r3 = cli.rows("select count(*) from new_orders where no_w_id=%d and no_d_id=%d;" % (w, d))
    _, r4 = cli.rows("select count(*) from order_line where ol_w_id=%d and ol_d_id=%d;" % (w, d))
    return (one_int(r1, 0), one_int(r2, 0), one_int(r3, 0), one_int(r4, 0))


def run_smoke(cli, cfg, seed=1234):
    r = TpccRandom(seed)
    rep = SmokeReport()
    w, d, c = 1, 1, 1

    # ---- new_order commit ---------------------------------------------------------
    before = _counts(cli, w, d)
    st, det = workload.new_order(cli, r, cfg, w, d_id=d, c_id=c, force_rollback=False)
    after = _counts(cli, w, d)
    if st != "commit":
        rep.add("new_order commit", False, "%s %s" % (st, det))
    else:
        o_id = after[0] - 1
        _, ro = cli.rows("select o_ol_cnt from orders where o_w_id=%d and o_d_id=%d "
                         "and o_id=%d;" % (w, d, o_id))
        ol_cnt = one_int(ro, 0, -1)
        rep.add("new_order commit", st == "commit")
        rep.add("  d_next_o_id +1", after[0] == before[0] + 1,
                "%s -> %s" % (before[0], after[0]))
        rep.add("  orders +1", after[1] == before[1] + 1)
        rep.add("  new_orders +1", after[2] == before[2] + 1)
        rep.add("  order_line +o_ol_cnt", after[3] == before[3] + ol_cnt,
                "+%d expect +%d" % (after[3] - before[3], ol_cnt))
        # new lines must carry the PENDING sentinel
        _, rp = cli.rows("select count(*) from order_line where ol_w_id=%d and ol_d_id=%d "
                         "and ol_o_id=%d and ol_delivery_d='%s';"
                         % (w, d, o_id, schema.DELIVERY_D_NULL))
        rep.add("  lines pending sentinel", one_int(rp, 0, -1) == ol_cnt)

    # ---- new_order intentional rollback: ZERO side effects ---------------------------
    before = _counts(cli, w, d)
    st, det = workload.new_order(cli, r, cfg, w, d_id=d, c_id=c, force_rollback=True)
    after = _counts(cli, w, d)
    rep.add("new_order rollback path", st == "rollback", "%s %s" % (st, det))
    rep.add("  zero side effects", before == after, "%s -> %s" % (before, after))

    # ---- payment ---------------------------------------------------------------------
    amt = 123.45
    _, rw = cli.rows("select w_ytd from warehouse where w_id=%d;" % w)
    _, rd = cli.rows("select d_ytd from district where d_w_id=%d and d_id=%d;" % (w, d))
    _, rc = cli.rows("select c_balance, c_ytd_payment, c_payment_cnt from customer "
                     "where c_w_id=%d and c_d_id=%d and c_id=%d;" % (w, d, c))
    _, rh = cli.rows("select count(*) from history where h_w_id=%d;" % w)
    w0, d0 = one_float(rw, 0), one_float(rd, 0)
    b0, y0, n0 = one_float(rc, 0), one_float(rc, 1), one_int(rc, 2)
    h0 = one_int(rh, 0)

    st, det = workload.payment(cli, r, cfg, w, d_id=d, c_id=c, amount=amt)
    rep.add("payment commit", st == "commit", "%s %s" % (st, det))
    if st == "commit":
        _, rw = cli.rows("select w_ytd from warehouse where w_id=%d;" % w)
        _, rd = cli.rows("select d_ytd from district where d_w_id=%d and d_id=%d;" % (w, d))
        _, rc = cli.rows("select c_balance, c_ytd_payment, c_payment_cnt from customer "
                         "where c_w_id=%d and c_d_id=%d and c_id=%d;" % (w, d, c))
        _, rh = cli.rows("select count(*) from history where h_w_id=%d;" % w)
        rep.add("  w_ytd +amt", abs(one_float(rw, 0) - (w0 + amt)) < TOL,
                "%.2f -> %.2f" % (w0, one_float(rw, 0)))
        rep.add("  d_ytd +amt", abs(one_float(rd, 0) - (d0 + amt)) < TOL)
        rep.add("  c_balance -amt", abs(one_float(rc, 0) - (b0 - amt)) < TOL,
                "%.2f -> %.2f" % (b0, one_float(rc, 0)))
        rep.add("  c_ytd_payment +amt", abs(one_float(rc, 1) - (y0 + amt)) < TOL)
        rep.add("  c_payment_cnt +1", one_int(rc, 2) == n0 + 1)
        rep.add("  history +1", one_int(rh, 0) == h0 + 1)

    # ---- order_status (read-only) ---------------------------------------------------
    st, det = workload.order_status(cli, r, cfg, w, d_id=d, c_id=c)
    rep.add("order_status commit", st == "commit", "%s %s" % (st, det))

    # ---- delivery ---------------------------------------------------------------------
    _, rn = cli.rows("select min(no_o_id) from new_orders where no_w_id=%d and no_d_id=%d;"
                     % (w, d))
    oldest = one_int(rn, 0)
    if oldest is None:
        rep.add("delivery", True, "skipped: no pending new_orders")
    else:
        _, ro = cli.rows("select o_c_id from orders where o_w_id=%d and o_d_id=%d and o_id=%d;"
                         % (w, d, oldest))
        oc = one_int(ro, 0)
        _, rs = cli.rows("select sum(ol_amount) from order_line where ol_w_id=%d and "
                         "ol_d_id=%d and ol_o_id=%d;" % (w, d, oldest))
        expect_add = one_float(rs, 0, 0.0)
        _, rb = cli.rows("select c_balance, c_delivery_cnt from customer where c_w_id=%d "
                         "and c_d_id=%d and c_id=%d;" % (w, d, oc))
        bal0, cnt0 = one_float(rb, 0), one_int(rb, 1)

        st, det = workload.delivery(cli, r, cfg, w)
        rep.add("delivery commit", st == "commit", "%s %s" % (st, det))
        if st == "commit":
            _, rg = cli.rows("select count(*) from new_orders where no_w_id=%d and "
                             "no_d_id=%d and no_o_id=%d;" % (w, d, oldest))
            rep.add("  oldest new_order removed", one_int(rg, 0, -1) == 0)
            _, rc2 = cli.rows("select o_carrier_id from orders where o_w_id=%d and "
                              "o_d_id=%d and o_id=%d;" % (w, d, oldest))
            rep.add("  carrier set", one_int(rc2, 0, 0) != schema.CARRIER_NULL)
            _, rp = cli.rows("select count(*) from order_line where ol_w_id=%d and ol_d_id=%d "
                             "and ol_o_id=%d and ol_delivery_d='%s';"
                             % (w, d, oldest, schema.DELIVERY_D_NULL))
            rep.add("  lines dated", one_int(rp, 0, -1) == 0)
            _, rb2 = cli.rows("select c_balance, c_delivery_cnt from customer where c_w_id=%d "
                              "and c_d_id=%d and c_id=%d;" % (w, d, oc))
            rep.add("  c_balance += sum(ol_amount)",
                    abs(one_float(rb2, 0) - (bal0 + expect_add)) < TOL,
                    "%.2f + %.2f -> %.2f" % (bal0, expect_add, one_float(rb2, 0)))
            rep.add("  c_delivery_cnt +1", one_int(rb2, 1) == cnt0 + 1)

    # ---- stock_level (read-only) -------------------------------------------------------
    st, det = workload.stock_level(cli, r, cfg, w, d_id=d)
    rep.add("stock_level commit", st == "commit", "%s %s" % (st, det))

    return rep
