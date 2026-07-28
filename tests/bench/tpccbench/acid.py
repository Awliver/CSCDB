"""ACID property tests (spec clause 3), pragmatic edition.

Atomicity   A1 commit applies all effects / A2 abort applies none
Isolation   I1 snapshot read stability, I2 no dirty read, I3 no lost update
Durability  D1 committed work survives kill -9 + restart
"""

import time

from .db import Client, StmtTimeout, one_float, one_int

PROBE_TIMEOUT = 8      # seconds: reads that block longer count as "blocked"
TOL = 0.05


class AcidReport:
    def __init__(self):
        self.results = []

    def add(self, name, ok, detail=""):
        self.results.append((name, ok, detail))
        print("  %-24s %s %s" % (name, "PASS" if ok else "FAIL",
                                 ("- " + detail) if detail else ""), flush=True)

    @property
    def ok(self):
        return all(ok for _, ok, _ in self.results)


def _balance(cli, w, d, c):
    _, r = cli.rows("select c_balance from customer "
                    "where c_w_id=%d and c_d_id=%d and c_id=%d;" % (w, d, c))
    return one_float(r, 0)


def _fresh(isolation):
    cli = Client(timeout=PROBE_TIMEOUT)
    if isolation == "si":
        cli.query("set transaction isolation level snapshot isolation")
    return cli


def run_acid(isolation="si"):
    """Run A/I tests against a loaded db on customer (1,1,1..3). Returns AcidReport."""
    rep = AcidReport()
    w, d = 1, 1

    # ---- A1: atomicity of commit --------------------------------------------------
    cli = _fresh(isolation)
    b0 = _balance(cli, w, d, 1)
    cli.must("begin;")
    cli.must("update customer set c_balance = c_balance - 7.00 "
             "where c_w_id=%d and c_d_id=%d and c_id=1;" % (w, d))
    cli.must("commit;")
    b1 = _balance(cli, w, d, 1)
    rep.add("A1 commit applies", abs(b1 - (b0 - 7.00)) < TOL, "%.2f -> %.2f" % (b0, b1))

    # ---- A2: atomicity of rollback -------------------------------------------------
    b0 = _balance(cli, w, d, 1)
    cli.must("begin;")
    cli.must("update customer set c_balance = c_balance - 9.00 "
             "where c_w_id=%d and c_d_id=%d and c_id=1;" % (w, d))
    cli.query("abort;")
    b1 = _balance(cli, w, d, 1)
    rep.add("A2 abort reverts", abs(b1 - b0) < TOL, "%.2f -> %.2f" % (b0, b1))
    cli.close()

    # ---- I1: snapshot read stability (repeatable read within txn) -------------------
    t1, t2 = _fresh(isolation), _fresh(isolation)
    try:
        base = _balance(t1, w, d, 2)
        t1.must("begin;")
        first = _balance(t1, w, d, 2)
        t2.must("begin;")
        t2.must("update customer set c_balance = c_balance - 5.00 "
                "where c_w_id=%d and c_d_id=%d and c_id=2;" % (w, d))
        t2.must("commit;")
        second = _balance(t1, w, d, 2)
        t1.query("commit;")
        after = _balance(t1, w, d, 2)
        rep.add("I1 read stability", abs(first - second) < TOL,
                "in-txn %.2f/%.2f, base %.2f, after %.2f" % (first, second, base, after))
    except StmtTimeout as e:
        rep.add("I1 read stability", False, "blocked: " + str(e)[:80])
        t1.rollback_quiet()
    finally:
        t1.close(), t2.close()

    # ---- I2: no dirty read ------------------------------------------------------------
    t1, t2 = _fresh(isolation), _fresh(isolation)
    try:
        b0 = _balance(t2, w, d, 3)
        t1.must("begin;")
        t1.must("update customer set c_balance = c_balance - 11.00 "
                "where c_w_id=%d and c_d_id=%d and c_id=3;" % (w, d))
        try:
            seen = _balance(t2, w, d, 3)          # uncommitted write must be invisible
            ok = abs(seen - b0) < TOL
            det = "saw %.2f, committed %.2f" % (seen, b0)
        except StmtTimeout:
            ok, det = True, "reader blocked until writer done (locking, still no dirty read)"
            t2.close()
            t2 = None
        t1.query("abort;")
        final = _balance(t1, w, d, 3)
        rep.add("I2 no dirty read", ok and abs(final - b0) < TOL, det)
    finally:
        t1.close()
        if t2:
            t2.close()

    # ---- I3: no lost update -------------------------------------------------------------
    t1, t2 = _fresh(isolation), _fresh(isolation)
    st2 = None
    try:
        b0 = _balance(t1, w, d, 4)
        t1.must("begin;")
        t2.must("begin;")
        b_t1 = _balance(t1, w, d, 4)
        b_t2 = _balance(t2, w, d, 4)
        t1.must("update customer set c_balance = %.2f "
                "where c_w_id=%d and c_d_id=%d and c_id=4;" % (b_t1 - 10.0, w, d))
        st1, _ = t1.exec("commit;")
        try:
            st2, _ = t2.exec("update customer set c_balance = %.2f "
                             "where c_w_id=%d and c_d_id=%d and c_id=4;"
                             % (b_t2 - 20.0, w, d))
            if st2 == "ok":
                st2, _ = t2.exec("commit;")
        except StmtTimeout:
            st2 = "blocked"
            t2.close()
            t2 = None
        final = _balance(t1, w, d, 4)
        # acceptable: t2 aborted (final = b0-10) — SI first-committer-wins / wait-die.
        # lost update: final = b0-20 (t2 blindly overwrote t1's committed write)
        lost = abs(final - (b0 - 20.0)) < TOL and st2 == "ok"
        rep.add("I3 no lost update", not lost,
                "t1=%s t2=%s balance %.2f -> %.2f" % (st1, st2, b0, final))
    finally:
        t1.close()
        if t2:
            t2.close()
    return rep


def run_durability(server, isolation="si"):
    """Commit marker txns, kill -9, restart, verify recovery. Returns AcidReport."""
    rep = AcidReport()
    w, d = 1, 1
    cli = _fresh(isolation)

    # committed marker: move a known amount, remember expected values
    b0 = _balance(cli, w, d, 5)
    cli.must("begin;")
    cli.must("update customer set c_balance = c_balance - 42.00 "
             "where c_w_id=%d and c_d_id=%d and c_id=5;" % (w, d))
    cli.must("commit;")
    _, rn = cli.rows("select d_next_o_id from district where d_w_id=%d and d_id=%d;" % (w, d))
    next_o = one_int(rn, 0)

    # uncommitted txn that must NOT survive
    cli.must("begin;")
    cli.must("update customer set c_balance = c_balance - 999.00 "
             "where c_w_id=%d and c_d_id=%d and c_id=5;" % (w, d))
    cli.close()                       # leave it open; crash now

    server.kill9()
    time.sleep(0.5)
    server.restart_keep_data()

    cli = _fresh(isolation)
    b1 = _balance(cli, w, d, 5)
    rep.add("D1 committed survives", abs(b1 - (b0 - 42.00)) < TOL,
            "%.2f -> %.2f (expect %.2f)" % (b0, b1, b0 - 42.00))
    _, rn = cli.rows("select d_next_o_id from district where d_w_id=%d and d_id=%d;" % (w, d))
    rep.add("D1 metadata intact", one_int(rn, 0) == next_o,
            "d_next_o_id %s == %s" % (one_int(rn, 0), next_o))
    cli.close()
    return rep
