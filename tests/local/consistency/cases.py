"""Targeted repro cases for audit findings C1–M8."""

from __future__ import annotations

import os
import random
import shutil
import sys
import threading
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_LOCAL = os.path.dirname(_HERE)
if _LOCAL not in sys.path:
    sys.path.insert(0, _LOCAL)

from tpcc_common import BUILD, kill_rmdb, parse_count, parse_table_rows  # noqa: E402

from consistency.harness import (  # noqa: E402
    Barrier,
    CaseResult,
    CaseSpec,
    count_rows,
    fresh_db,
    join_or_fail,
    new_client,
    output_txt_size,
    pre_dirty_table,
    restart_db,
    row_ids,
    setup_big_table,
    setup_padded_table,
    setup_si_table,
    sql,
    sql_ok,
    stop_server,
)

# ---------------------------------------------------------------------------
# C1 — heap insert visible before MVCC chain (micro-window stress)
# ---------------------------------------------------------------------------


def case_c1_insert_heap_race() -> CaseResult:
    """Concurrent readers while INSERT runs on clean DB — any_mvcc_dirty_=false reads heap."""
    db = "cons_c1_heap_race"
    proc, _ = setup_si_table(db, indexed=True)
    errors = []
    seen = {"count": 0}
    lock = threading.Lock()
    stop = threading.Event()

    def reader(tid: int):
        try:
            cli = new_client()
            sql(cli, "set transaction isolation level snapshot isolation;")
            sql(cli, "begin;")
            deadline = time.perf_counter() + 12.0
            while time.perf_counter() < deadline and not stop.is_set():
                try:
                    n = count_rows(cli)
                except (RuntimeError, OSError):
                    errors.append("reader%d disconnect" % tid)
                    break
                if n > 0:
                    with lock:
                        seen["count"] += 1
                    break
                time.sleep(0.0002)
            sql(cli, "rollback;")
            cli.close()
        except (RuntimeError, OSError) as exc:
            errors.append("reader%d: %s" % (tid, exc))

    def writer(wid: int):
        try:
            cli = new_client()
            sql(cli, "set transaction isolation level snapshot isolation;")
            for _ in range(120):
                sql(cli, "begin;")
                sql(cli, "insert into t values (%d, 100);" % (1000 + wid))
                sql(cli, "rollback;")
            cli.close()
        except (RuntimeError, OSError) as exc:
            errors.append("writer%d: %s" % (wid, exc))

    threads = [threading.Thread(target=reader, args=(i,)) for i in range(6)] + [
        threading.Thread(target=writer, args=(i,)) for i in range(3)
    ]
    for t in threads:
        t.start()
    err = join_or_fail(threads, 45)
    stop.set()
    alive = proc.poll() is None
    stop_server(proc)

    if not alive:
        return CaseResult(
            "C1",
            "insert heap/MVCC micro-window",
            False,
            "server crashed during race",
            flaky=True,
        )
    if err:
        return CaseResult("C1", "insert heap/MVCC micro-window", False, err + " " + str(errors[:2]), flaky=True)
    if errors:
        return CaseResult("C1", "insert heap/MVCC micro-window", False, errors[0], flaky=True)
    if seen["count"] > 0:
        return CaseResult(
            "C1",
            "insert heap/MVCC micro-window",
            False,
            "reader saw row while writer held uncommitted insert (x%d)" % seen["count"],
            flaky=True,
        )
    return CaseResult("C1", "insert heap/MVCC micro-window", True, "no early visibility in window")


# ---------------------------------------------------------------------------
# C2 — mvcc_other_writer ignores pending overlay
# ---------------------------------------------------------------------------


def case_c2_pending_same_key_insert() -> CaseResult:
    """Pending overlay: need active_explicit_count>=2 + pre-dirty MVCC so insert hits pending path."""
    db = "cons_c2_pending"
    proc, setup = setup_si_table(db, indexed=True)
    pre_dirty_table(setup)
    setup.close()

    a = new_client()
    b = new_client()
    sql(a, "set transaction isolation level snapshot isolation;")
    sql(b, "set transaction isolation level snapshot isolation;")
    # Both explicit txns before any insert → disables SI fast path (active_explicit_count=2)
    sql(a, "begin;")
    sql(b, "begin;")
    sql(a, "insert into t values (1, 11);")
    # End-of-statement: writer → pending overlay (ch.writer cleared)
    sql(a, "select * from t where id = 1;")
    r_b = sql(b, "insert into t values (1, 22);")
    r_b_commit = sql(b, "commit;")
    sql(a, "commit;")
    n = count_rows(b)
    rows = parse_table_rows(sql(b, "select id, v from t where id = 1;"))
    a.close()
    b.close()
    stop_server(proc)

    low_b = r_b.lower()
    low_bc = r_b_commit.lower()
    b_rejected = "abort" in low_b or "abort" in low_bc or "failure" in low_b
    val = rows[0][1].strip() if rows else ""
    dup = n > 1 or len(rows) > 1
    wrong_val = n == 1 and val == "22"
    t2_won = (not b_rejected) and (dup or wrong_val)

    if t2_won:
        return CaseResult(
            "C2",
            "pending overlay same-key insert",
            False,
            "BUG: T2 committed over pending insert n=%d val=%s ins=%r commit=%r"
            % (n, val, r_b.strip()[:80], r_b_commit.strip()[:80]),
        )
    if b_rejected and n == 1 and val == "11":
        return CaseResult("C2", "pending overlay same-key insert", True, "T2 rejected; v=11 kept")
    if n == 1 and val == "11":
        return CaseResult("C2", "pending overlay same-key insert", True, "single row v=11")
    return CaseResult(
        "C2",
        "pending overlay same-key insert",
        False,
        "unexpected state n=%d val=%s rejected=%s" % (n, val, b_rejected),
    )


# ---------------------------------------------------------------------------
# H5 — cached mvcc_on_ on long scan (deterministic)
# ---------------------------------------------------------------------------


def case_h5_scan_mvcc_on_cache() -> CaseResult:
    """After BEGIN, committed UPDATE before SELECT: mvcc_on_=false must still read snapshot."""
    db = "cons_h5_scan_cache"
    target_id = 75  # must be within seeded rows (1..n_rows)
    target_val = 888888
    expected_v = target_id * 10
    proc, seed = setup_padded_table(db, n_rows=150, pad_len=480)
    seed.close()

    reader_ready = threading.Event()
    writer_done = threading.Event()
    reader_done = {"leak": False, "seen_v": None}

    def reader():
        cli = new_client(timeout=300)
        sql(cli, "set transaction isolation level snapshot isolation;")
        sql(cli, "begin;")
        reader_ready.set()
        if not writer_done.wait(timeout=30):
            cli.close()
            return
        rows = parse_table_rows(
            sql(cli, "select id, v from t where id = %d;" % target_id)
        )
        if rows:
            reader_done["seen_v"] = rows[0][1].strip()
            reader_done["leak"] = reader_done["seen_v"] == str(target_val)
        sql(cli, "rollback;")
        cli.close()

    def writer():
        if not reader_ready.wait(timeout=15):
            return
        cli = new_client()
        sql(cli, "set transaction isolation level snapshot isolation;")
        sql(
            cli,
            "update t set v = %d where id = %d;" % (target_val, target_id),
        )
        cli.close()
        writer_done.set()

    threads = [threading.Thread(target=reader), threading.Thread(target=writer)]
    for t in threads:
        t.start()
    join_or_fail(threads, 120)
    stop_server(proc)

    if reader_done["seen_v"] is None:
        return CaseResult(
            "H5",
            "cached mvcc_on scan phantom",
            False,
            "reader got no row for id=%d (seed/visibility)" % target_id,
        )
    if reader_done["leak"]:
        return CaseResult(
            "H5",
            "cached mvcc_on scan phantom",
            False,
            "BUG: saw committed v=%s after BEGIN snapshot (expected %d)"
            % (reader_done["seen_v"], expected_v),
        )
    if reader_done["seen_v"] != str(expected_v):
        return CaseResult(
            "H5",
            "cached mvcc_on scan phantom",
            False,
            "unexpected v=%s (want %d)" % (reader_done["seen_v"], expected_v),
        )
    return CaseResult(
        "H5",
        "cached mvcc_on scan phantom",
        True,
        "saw v=%s (snapshot ok)" % reader_done["seen_v"],
    )


# ---------------------------------------------------------------------------
# C1 companion — uncommitted visibility after statement (SI snapshot)
# ---------------------------------------------------------------------------


def case_c1_uncommitted_not_visible() -> CaseResult:
    """Reader snapshot before writer INSERT must not see uncommitted row."""
    db = "cons_c1_uncommitted"
    proc, _ = setup_si_table(db, indexed=False)
    r = new_client()
    w = new_client()
    sql(r, "set transaction isolation level snapshot isolation;")
    sql(w, "set transaction isolation level snapshot isolation;")
    sql(r, "begin;")
    sql(w, "begin;")
    sql(w, "insert into t values (42, 420);")
    n = count_rows(r)
    vis = parse_table_rows(sql(r, "select * from t where id = 42;"))
    sql(w, "rollback;")
    sql(r, "rollback;")
    r.close()
    w.close()
    stop_server(proc)

    if n > 0 or vis:
        return CaseResult(
            "C1b",
            "uncommitted insert invisible to snapshot reader",
            False,
            "reader saw uncommitted row: count=%d rows=%s" % (n, vis),
        )
    return CaseResult("C1b", "uncommitted insert invisible to snapshot reader", True, "ok")


# ---------------------------------------------------------------------------
# H2 — coalesce rightmost leaf (IX_NO_PAGE)
# ---------------------------------------------------------------------------


def case_h2_coalesce_rightmost() -> CaseResult:
    """Repeated delete-to-empty on indexed table must not crash (coalesce IX_NO_PAGE)."""
    db = "cons_h2_coalesce"
    proc, cli = setup_si_table(db, indexed=True)
    ok_rounds = 0
    for round_i in range(30):
        for i in range(1, 41):
            ok, r = sql_ok(cli, "insert into t values (%d, %d);" % (i, round_i * 100 + i))
            if not ok:
                cli.close()
                stop_server(proc)
                return CaseResult("H2", "coalesce rightmost leaf", False, "insert r%d: %s" % (round_i, r))
        for i in range(1, 41):
            ok, r = sql_ok(cli, "delete from t where id = %d;" % i)
            if not ok:
                cli.close()
                stop_server(proc)
                return CaseResult("H2", "coalesce rightmost leaf", False, "delete r%d id=%d: %s" % (round_i, i, r))
        if count_rows(cli) != 0:
            cli.close()
            stop_server(proc)
            return CaseResult("H2", "coalesce rightmost leaf", False, "non-empty after delete round %d" % round_i)
        ok_rounds += 1
    alive = proc.poll() is None
    cli.close()
    stop_server(proc)
    if not alive:
        return CaseResult("H2", "coalesce rightmost leaf", False, "server crashed during delete rounds")
    return CaseResult("H2", "coalesce rightmost leaf", True, "%d rounds OK" % ok_rounds)


# ---------------------------------------------------------------------------
# H1 — maintain_parent multi-level (stress delete on indexed data)
# ---------------------------------------------------------------------------


def case_h1_maintain_parent_deep() -> CaseResult:
    """Bulk indexed insert/delete exercises multi-level separator propagation."""
    db = "cons_h1_maintain"
    proc, cli = setup_si_table(db, indexed=True)
    for batch in range(5):
        base = batch * 500
        for i in range(500):
            rid = base + i + 1
            ok, r = sql_ok(cli, "insert into t values (%d, %d);" % (rid, rid))
            if not ok:
                cli.close()
                stop_server(proc)
                return CaseResult("H1", "maintain_parent deep index", False, "ins %d: %s" % (rid, r))
        for i in range(500):
            rid = base + i + 1
            ok, r = sql_ok(cli, "delete from t where id = %d;" % rid)
            if not ok:
                cli.close()
                stop_server(proc)
                return CaseResult("H1", "maintain_parent deep index", False, "del %d: %s" % (rid, r))
    # index probe
    ok, r = sql_ok(cli, "select * from t where id = 99999;")
    alive = proc.poll() is None
    cli.close()
    stop_server(proc)
    if not alive:
        return CaseResult("H1", "maintain_parent deep index", False, "server crashed")
    if not ok:
        return CaseResult("H1", "maintain_parent deep index", False, "index probe failed: " + r[:120])
    return CaseResult("H1", "maintain_parent deep index", True, "2500 insert/delete cycles OK")


# ---------------------------------------------------------------------------
# H4 — ser_finish GC without rts_latch (stress, no crash)
# ---------------------------------------------------------------------------


def case_h4_ser_gc_stress() -> CaseResult:
    """Many concurrent SER txns (>512 ser_ entries) — GC path must not crash/race."""
    db = "cons_h4_ser_gc"
    proc, _ = setup_si_table(db, indexed=False, rows=[(1, 10), (2, 20)])
    errors = []

    def worker(seed: int):
        rng = random.Random(seed)
        cli = new_client()
        sql(cli, "set transaction isolation level serializable;")
        for _ in range(40):
            sql(cli, "begin;")
            sql(cli, "select * from t where id = %d;" % rng.randint(1, 2))
            if rng.random() < 0.5:
                sql(cli, "update t set v = v + 1 where id = %d;" % rng.randint(1, 2))
            out = sql(cli, "commit;")
            if "error" in out.lower() and "abort" not in out.lower():
                errors.append(out[:100])
        cli.close()

    threads = [threading.Thread(target=worker, args=(i * 17,)) for i in range(16)]
    for t in threads:
        t.start()
    join_or_fail(threads, 180)
    alive = proc.poll() is None
    stop_server(proc)
    if not alive:
        return CaseResult("H4", "ser_finish GC concurrency", False, "server crashed", flaky=True)
    if errors:
        return CaseResult("H4", "ser_finish GC concurrency", False, errors[0], flaky=True)
    return CaseResult("H4", "ser_finish GC concurrency", True, "16x40 SER txns completed")


# ---------------------------------------------------------------------------
# H3 — INLJ under SER must track the instantiated inner predicate / hit RID
# ---------------------------------------------------------------------------


def case_h3_ser_inlj_dangerous_structure() -> CaseResult:
    """Build T1 ->rw T2 ->rw T3 with T3 committed first.

    T1 reads g(1), then T2 writes it (T1 ->rw T2).  T3 moves the indexed inner
    row from s.id=1 to s.id=2 after T2's snapshot and commits.  T2's subsequent
    INLJ probe still sees the old-key version.  The instantiated s.id=1 predicate
    deliberately does not match T3's new value, so the actual hit RID tracking
    must discover T2 ->rw T3 and abort that SELECT immediately.
    """
    db = "cons_h3_ser_inlj_danger"
    proc, _ = fresh_db(db)
    setup = new_client()
    for stmt in (
        "create table r (id int, v int);",
        "create table s (id int, v int);",
        "create table g (id int, v int);",
        "create index s (id);",
        "insert into r values (1, 100);",
        "insert into s values (1, 200);",
        "insert into g values (1, 10);",
    ):
        ok, reply = sql_ok(setup, stmt)
        if not ok:
            setup.close()
            stop_server(proc)
            return CaseResult("H3", "SER INLJ immediate dangerous-structure abort", False,
                              "setup failed for %r: %s" % (stmt, reply[:120]))

    join_sql = "select r.id, s.v from r, s where r.id = s.id and r.id = 1;"
    setup.close()

    t1 = new_client()
    t2 = new_client()
    t3 = new_client()
    for cli in (t1, t2, t3):
        sql(cli, "set transaction isolation level serializable;")

    sql(t1, "begin;")
    if not parse_table_rows(sql(t1, "select v from g where id = 1;")):
        for cli in (t1, t2, t3):
            cli.close()
        stop_server(proc)
        return CaseResult("H3", "SER INLJ immediate dangerous-structure abort", False,
                          "T1 failed to establish the g(1) read dependency")

    sql(t2, "begin;")                       # T2 snapshot precedes T3's write
    sql(t3, "begin;")
    t3_write = sql(t3, "update s set id = 2 where id = 1;")
    t3_commit = sql(t3, "commit;")           # Tout must commit before Tin
    if "abort" in (t3_write + t3_commit).lower() or "error" in (t3_write + t3_commit).lower():
        sql(t1, "rollback;")
        sql(t2, "rollback;")
        for cli in (t1, t2, t3):
            cli.close()
        stop_server(proc)
        return CaseResult("H3", "SER INLJ immediate dangerous-structure abort", False,
                          "T3 setup write/commit failed: %r / %r" % (t3_write[:80], t3_commit[:80]))

    t2_write = sql(t2, "update g set v = 11 where id = 1;")  # establishes T1 ->rw T2
    if "abort" in t2_write.lower() or "error" in t2_write.lower():
        sql(t1, "rollback;")
        sql(t2, "rollback;")
        for cli in (t1, t2, t3):
            cli.close()
        stop_server(proc)
        return CaseResult("H3", "SER INLJ immediate dangerous-structure abort", False,
                          "T2 could not establish inbound rw edge: %r" % t2_write[:120])

    join_reply = sql(t2, join_sql)
    immediate_abort = "abort" in join_reply.lower()
    sql(t1, "rollback;")
    sql(t2, "rollback;")
    for cli in (t1, t2, t3):
        cli.close()

    verify = new_client()
    guard_rows = parse_table_rows(sql(verify, "select v from g where id = 1;"))
    verify.close()
    stop_server(proc)
    guard_v = guard_rows[0][0].strip() if guard_rows else None

    if not immediate_abort:
        return CaseResult("H3", "SER INLJ immediate dangerous-structure abort", False,
                          "BUG: dangerous INLJ SELECT did not immediately abort: %r" % join_reply[:160])
    if guard_v != "10":
        return CaseResult("H3", "SER INLJ immediate dangerous-structure abort", False,
                          "aborted T2 write was not rolled back: g.v=%r" % guard_v)
    return CaseResult("H3", "SER INLJ immediate dangerous-structure abort", True,
                      "INLJ SELECT returned TRANSACTION_ABORT; T2 write rolled back")


def case_h3s_ser_inlj_smoke() -> CaseResult:
    """SER join via INLJ with concurrent inner updates — server stays up, no torn reads."""
    db = "cons_h3s_ser_inlj"
    proc, cli = setup_si_table(db, indexed=False)
    sql_ok(cli, "create table r (id int, v int);")
    sql_ok(cli, "create table s (id int, v int);")
    sql_ok(cli, "create index s (id);")
    for i in range(1, 51):
        sql_ok(cli, "insert into r values (%d, %d);" % (i, i))
        sql_ok(cli, "insert into s values (%d, %d);" % (i, i * 10))
    cli.close()

    def join_reader():
        c = new_client()
        sql(c, "set transaction isolation level serializable;")
        for _ in range(30):
            sql(c, "begin;")
            rows = parse_table_rows(
                sql(
                    c,
                    "select r.id, s.v from r, s where r.id = s.id and r.id >= 1 and r.id <= 50;",
                )
            )
            if len(rows) != 50:
                errors.append("join rows=%d" % len(rows))
            sql(c, "commit;")
        c.close()

    def inner_writer(seed: int):
        rng = random.Random(seed)
        c = new_client()
        sql(c, "set transaction isolation level serializable;")
        for _ in range(30):
            i = rng.randint(1, 50)
            sql(c, "begin;")
            sql(c, "update s set v = v + 1 where id = %d;" % i)
            sql(c, "commit;")
        c.close()

    errors = []
    threads = [threading.Thread(target=join_reader)] + [
        threading.Thread(target=inner_writer, args=(i,)) for i in range(4)
    ]
    for t in threads:
        t.start()
    join_or_fail(threads, 120)
    alive = proc.poll() is None
    stop_server(proc)
    if not alive:
        return CaseResult("H3S", "SER INLJ concurrent join", False, "server crashed", flaky=True)
    if errors:
        return CaseResult("H3S", "SER INLJ concurrent join", False, errors[0], flaky=True)
    return CaseResult("H3S", "SER INLJ concurrent join", True, "join+update smoke OK")


# ---------------------------------------------------------------------------
# C3/C4 — crash + restart durability smoke
# ---------------------------------------------------------------------------


def case_c4_crash_durability_smoke() -> CaseResult:
    """Committed updates survive SIGKILL + restart (WAL / dirty-page ordering)."""
    db = "cons_c4_crash"
    proc, cli = setup_si_table(db, indexed=True, rows=[(1, 0)])
    for i in range(200):
        sql(cli, "begin;")
        sql(cli, "update t set v = v + 1 where id = 1;")
        sql(cli, "commit;")
    expected = parse_table_rows(cli.query("select v from t where id = 1;"))[0][0]
    cli.close()
    if proc.poll() is None:
        proc.kill()
        proc.wait(timeout=3)
    kill_rmdb()
    time.sleep(0.5)

    proc2 = restart_db(db)
    cli2 = new_client()
    got_rows = parse_table_rows(cli2.query("select v from t where id = 1;"))
    cli2.close()
    stop_server(proc2)
    if not got_rows:
        return CaseResult("C4", "crash durability smoke", False, "row missing after restart", flaky=True)
    got = got_rows[0][0].strip()
    if got != expected.strip():
        return CaseResult(
            "C4",
            "crash durability smoke",
            False,
            "value regressed: before=%s after=%s" % (expected, got),
            flaky=True,
        )
    return CaseResult("C4", "crash durability smoke", True, "v=%s preserved" % got)


# ---------------------------------------------------------------------------
# C3 — concurrent update under load (cleaner/evict stress proxy)
# ---------------------------------------------------------------------------


def case_c3_cleaner_stress() -> CaseResult:
    """Heavy concurrent updates — proxy for page-cleaner/evict races (no crash, monotonic counter)."""
    db = "cons_c3_cleaner"
    proc, cli = setup_si_table(db, indexed=False, rows=[(1, 0)])
    # widen working set to pressure buffer pool
    for i in range(2, 2001):
        sql_ok(cli, "insert into t values (%d, 0);" % i)
    cli.close()
    counter = {"v": 0}
    lock = threading.Lock()
    errors = []

    def worker(seed: int):
        rng = random.Random(seed)
        c = new_client()
        sql(c, "set transaction isolation level snapshot isolation;")
        for _ in range(100):
            rid = rng.randint(1, 2000)
            sql(c, "begin;")
            sql(c, "update t set v = v + 1 where id = %d;" % rid)
            out = sql(c, "commit;")
            if "error" in out.lower() and "abort" not in out.lower():
                errors.append(out[:80])
            with lock:
                counter["v"] += 1
        c.close()

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
    for t in threads:
        t.start()
    join_or_fail(threads, 300)
    alive = proc.poll() is None
    c2 = new_client()
    total_inc = 0
    for i in range(1, 2001):
        rows = parse_table_rows(c2.query("select v from t where id = %d;" % i))
        if rows:
            total_inc += int(float(rows[0][0]))
    c2.close()
    stop_server(proc)
    if not alive:
        return CaseResult("C3", "cleaner/evict stress", False, "server crashed", flaky=True)
    if errors:
        return CaseResult("C3", "cleaner/evict stress", False, errors[0], flaky=True)
    if total_inc != counter["v"]:
        return CaseResult(
            "C3",
            "cleaner/evict stress",
            False,
            "lost updates: expected sum=%d got=%d" % (counter["v"], total_inc),
            flaky=True,
        )
    return CaseResult(
        "C3",
        "cleaner/evict stress",
        True,
        "sum(v)=%d matches commits" % total_inc,
    )


# ---------------------------------------------------------------------------
# M1 — double BEGIN leaks active_explicit_count (behavioral proxy)
# ---------------------------------------------------------------------------


def case_m1_double_begin() -> CaseResult:
    """Double BEGIN then concurrent insert on two tables — SI path should stay consistent."""
    db = "cons_m1_double_begin"
    proc, cli = setup_si_table(db, indexed=False)
    sql_ok(cli, "create table u (id int, v int);")
    cli.close()

    def session():
        c = new_client()
        sql(c, "set transaction isolation level snapshot isolation;")
        sql(c, "begin;")
        sql(c, "begin;")  # duplicate — leaks active_explicit_count in buggy build
        sql(c, "insert into t values (1, 1);")
        sql(c, "insert into u values (1, 1);")
        sql(c, "commit;")
        c.close()

    threads = [threading.Thread(target=session) for _ in range(4)]
    for t in threads:
        t.start()
    join_or_fail(threads, 60)
    c2 = new_client()
    nt = count_rows(c2)
    nu = parse_count(c2.query("select count(*) from u;"))
    c2.close()
    stop_server(proc)
    if nt != 4 or nu != 4:
        return CaseResult(
            "M1",
            "double BEGIN versioning",
            False,
            "counts t=%d u=%d (expected 4/4)" % (nt, nu),
        )
    return CaseResult("M1", "double BEGIN versioning", True, "both tables have 4 rows")


# ---------------------------------------------------------------------------
# M6 — set output_file off (API gap detector)
# ---------------------------------------------------------------------------


def case_m6_output_file_off() -> CaseResult:
    """Detect whether `set output_file off` is implemented."""
    db = "cons_m6_output"
    proc, cli = setup_si_table(db, indexed=False, rows=[(1, 1)])
    path = os.path.join(BUILD, db, "output.txt")
    if os.path.isfile(path):
        os.remove(path)
    sql(cli, "set output_file off;")
    sql(cli, "select * from t;")
    size_after = output_txt_size(db)
    cli.close()
    stop_server(proc)
    if size_after > 0:
        return CaseResult(
            "M6",
            "set output_file off",
            False,
            "output.txt grew to %d bytes — command not implemented (doc/test gap, not data bug)"
            % size_after,
        )
    return CaseResult("M6", "set output_file off", True, "output.txt stayed empty")


# ---------------------------------------------------------------------------
# M7 — payment ytd drift (TPC-C mini)
# ---------------------------------------------------------------------------


def case_m7_payment_ytd() -> CaseResult:
    """Warehouse vs district ytd payment delta under concurrent Payment."""
    from tpcc_common import bootstrap_tpcc, RmdbClient as RC, W_ID as WID
    from tpcc_scale import loads_for_scale, scale_profile
    from tpcc_transactions import run_payment

    kill_note = ""
    try:
        from tpcc_common import kill_rmdb

        kill_rmdb()
        bootstrap_tpcc("cons_m7_ytd", loads=loads_for_scale("mini"))
        scale = scale_profile("mini")

        def ytd(cli):
            w = float(parse_table_rows(cli.query("select w_ytd from warehouse where w_id = %d;" % WID))[0][0])
            d = sum(
                float(r[0])
                for r in parse_table_rows(
                    cli.query("select d_ytd from district where d_w_id = %d;" % WID)
                )
            )
            return w, d

        cli = RC()
        w0, d0 = ytd(cli)
        cli.close()

        def worker(seed):
            rng = random.Random(seed)
            c = RC()
            c.query("set transaction isolation level snapshot isolation")
            for _ in range(25):
                run_payment(c, rng, scale)
            c.close()

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
        for t in threads:
            t.start()
        join_or_fail(threads, 180)

        cli2 = RC()
        w1, d1 = ytd(cli2)
        cli2.close()
        kill_rmdb()
        dw, dd = w1 - w0, d1 - d0
        if abs(dw - dd) > 1.0:
            return CaseResult(
                "M7",
                "payment ytd drift",
                False,
                "warehouse +%.4f vs district +%.4f (diff %.4f)" % (dw, dd, dw - dd),
                flaky=True,
            )
        return CaseResult("M7", "payment ytd drift", True, "delta match +%.2f" % dw)
    except Exception as exc:
        return CaseResult("M7", "payment ytd drift", False, str(exc) + kill_note, flaky=True)


# ---------------------------------------------------------------------------
# Integration — existing SI burst + strict checks
# ---------------------------------------------------------------------------


def case_int_si_burst_strict() -> CaseResult:
    """8-thread SI TPC-C mini burst + strict consistency (integration)."""
    from tpcc_common import bootstrap_tpcc, kill_rmdb, parse_count as pc
    from tpcc_consistency import run_consistency_checks
    from tpcc_scale import loads_for_scale, scale_profile
    from tpcc_transactions import pick_txn, run_neworder, run_txn

    kill_rmdb()
    scale = scale_profile("mini")
    loads = list(loads_for_scale("mini"))
    proc, cli = bootstrap_tpcc("cons_int_si", loads=loads)
    before = {t: pc(cli.query("select count(*) from %s;" % t)) for t in ("orders", "order_line", "history")}
    cli.close()

    def worker(seed):
        rng = random.Random(seed)
        c = new_client()
        sql(c, "set transaction isolation level snapshot isolation;")
        for _ in range(40):
            name, _ = pick_txn(rng)
            if name == "new_order":
                run_neworder(c, rng, scale)
            else:
                run_txn(c, rng, scale, name)
        c.close()

    threads = [threading.Thread(target=worker, args=(i * 1009,)) for i in range(8)]
    for t in threads:
        t.start()
    join_or_fail(threads, 300)
    alive = proc.poll() is None
    if not alive:
        stop_server(proc)
        return CaseResult("INT", "SI burst strict", False, "server crashed", flaky=True)

    c2 = new_client()
    sql(c2, "set transaction isolation level snapshot isolation;")
    ok = run_consistency_checks(c2, districts=scale["districts"], before_counts=before, strict=True)
    c2.close()
    stop_server(proc)
    if not ok:
        return CaseResult("INT", "SI burst strict", False, "run_consistency_checks failed", flaky=True)
    return CaseResult("INT", "SI burst strict", True, "strict checks passed")


# ---------------------------------------------------------------------------
# C5/C6/C7 — 决赛门禁复现：compound rollback（单连接 SI 快路径写 + 并发第二连接把同一
# 事务后续语句逼入 MVCC 路径）之后 ROLLBACK，行不得残留可见。
#
# 根因：mvcc_write / mvcc_write_col_delta / mvcc_write_col_patch 首次建链时会把"堆当前值"
# 伪造成 commit_ts=0 的已提交基版本；但如果这一行在同一事务里已经被快路径（未建 MVCC 链）
# 写过一次，堆当前值其实是本事务自己尚未提交的数据，伪造之后 abort 会把它当成"已提交历史"
# 保留/复原，导致回滚后该行对所有事务永久可见，或只回滚到复合写的中间态而非事务前原值。
# ---------------------------------------------------------------------------


def case_c5_compound_rollback_insert_update() -> CaseResult:
    """A: BEGIN; 快路径 INSERT(直接落堆，未建 MVCC 链)。B: BEGIN（把 A 逼入 MVCC 路径）。
    A: UPDATE（col_delta，首次触达该行会尝试伪造 commit_ts=0 基版本）。A: ROLLBACK。
    行必须完全不可见，且索引项必须被正确回收（可重新插入同 id）。"""
    db = "cons_c5_insert_update_rollback"
    proc, _ = setup_si_table(db, indexed=True)
    rid = 501
    a = new_client()
    b = new_client()
    sql(a, "set transaction isolation level snapshot isolation;")
    sql(b, "set transaction isolation level snapshot isolation;")

    sql(a, "begin;")
    ok, r = sql_ok(a, "insert into t values (%d, 1);" % rid)
    if not ok:
        a.close()
        b.close()
        stop_server(proc)
        return CaseResult("C5", "compound rollback insert+update", False, "insert failed: " + r)

    sql(b, "begin;")  # active_explicit_count_ -> 2，关闭 A 的 SI 快路径

    ok, r = sql_ok(a, "update t set v = v + 100 where id = %d;" % rid)
    if not ok:
        sql(a, "rollback;")
        sql(b, "rollback;")
        a.close()
        b.close()
        stop_server(proc)
        return CaseResult("C5", "compound rollback insert+update", False, "update failed: " + r)

    sql(a, "rollback;")
    sql(b, "commit;")

    vis = parse_table_rows(sql(b, "select * from t where id = %d;" % rid))
    n = count_rows(b)
    # 索引复核：撤销若不完整，重新插入同 id 会因残留索引项报唯一冲突/failure
    ok2, r2 = sql_ok(b, "insert into t values (%d, 999);" % rid)
    a.close()
    b.close()
    stop_server(proc)

    if vis or n != 0:
        return CaseResult(
            "C5", "compound rollback insert+update", False,
            "BUG: aborted insert+update row still visible: %s (count=%d)" % (vis, n),
        )
    if not ok2 or "failure" in r2.lower():
        return CaseResult(
            "C5", "compound rollback insert+update", False,
            "BUG: stale index entry after rollback blocks re-insert: " + r2[:120],
        )
    return CaseResult("C5", "compound rollback insert+update", True, "row invisible after rollback; re-insert ok")


def case_c6_compound_rollback_update_update() -> CaseResult:
    """预置已提交行 v=1。A: BEGIN; 快路径 UPDATE(v=v+10，直接落堆)。B: BEGIN（逼 A 入 MVCC）。
    A: 再次 UPDATE(v=v+10，走 mvcc_write_col_delta，首次建链)。A: ROLLBACK。
    行必须精确回到事务前原值 v=1，而不是停在快路径写之后的中间态 v=11。"""
    db = "cons_c6_update_update_rollback"
    rid = 601
    proc, _ = setup_si_table(db, indexed=False, rows=[(rid, 1)])
    a = new_client()
    b = new_client()
    sql(a, "set transaction isolation level snapshot isolation;")
    sql(b, "set transaction isolation level snapshot isolation;")

    sql(a, "begin;")
    ok, r = sql_ok(a, "update t set v = v + 10 where id = %d;" % rid)
    if not ok:
        a.close()
        b.close()
        stop_server(proc)
        return CaseResult("C6", "compound rollback update+update", False, "update1 failed: " + r)

    sql(b, "begin;")  # active_explicit_count_ -> 2

    ok, r = sql_ok(a, "update t set v = v + 10 where id = %d;" % rid)
    if not ok:
        sql(a, "rollback;")
        sql(b, "rollback;")
        a.close()
        b.close()
        stop_server(proc)
        return CaseResult("C6", "compound rollback update+update", False, "update2 failed: " + r)

    sql(a, "rollback;")
    sql(b, "commit;")

    rows = parse_table_rows(sql(b, "select v from t where id = %d;" % rid))
    a.close()
    b.close()
    stop_server(proc)

    if not rows:
        return CaseResult("C6", "compound rollback update+update", False, "row missing after rollback")
    got = rows[0][0].strip()
    if got != "1":
        return CaseResult(
            "C6", "compound rollback update+update", False,
            "BUG: expected v=1 (original) after rollback, got v=%s (11=fast-path leak, 21=no rollback)" % got,
        )
    return CaseResult("C6", "compound rollback update+update", True, "v=1 restored exactly")


def case_c7_compound_rollback_insert_delete() -> CaseResult:
    """A: BEGIN; 快路径 INSERT(直接落堆)。B: BEGIN（逼 A 入 MVCC）。A: DELETE(mvcc_write，
    is_delete=true，首次建链)。A: ROLLBACK。行必须不可见，索引项必须可重新插入。"""
    db = "cons_c7_insert_delete_rollback"
    proc, _ = setup_si_table(db, indexed=True)
    rid = 701
    a = new_client()
    b = new_client()
    sql(a, "set transaction isolation level snapshot isolation;")
    sql(b, "set transaction isolation level snapshot isolation;")

    sql(a, "begin;")
    ok, r = sql_ok(a, "insert into t values (%d, 1);" % rid)
    if not ok:
        a.close()
        b.close()
        stop_server(proc)
        return CaseResult("C7", "compound rollback insert+delete", False, "insert failed: " + r)

    sql(b, "begin;")

    ok, r = sql_ok(a, "delete from t where id = %d;" % rid)
    if not ok:
        sql(a, "rollback;")
        sql(b, "rollback;")
        a.close()
        b.close()
        stop_server(proc)
        return CaseResult("C7", "compound rollback insert+delete", False, "delete failed: " + r)

    sql(a, "rollback;")
    sql(b, "commit;")

    vis = parse_table_rows(sql(b, "select * from t where id = %d;" % rid))
    n = count_rows(b)
    ok2, r2 = sql_ok(b, "insert into t values (%d, 999);" % rid)
    a.close()
    b.close()
    stop_server(proc)

    if vis or n != 0:
        return CaseResult(
            "C7", "compound rollback insert+delete", False,
            "BUG: aborted insert+delete row still visible: %s (count=%d)" % (vis, n),
        )
    if not ok2 or "failure" in r2.lower():
        return CaseResult(
            "C7", "compound rollback insert+delete", False,
            "BUG: stale index entry after rollback blocks re-insert: " + r2[:120],
        )
    return CaseResult("C7", "compound rollback insert+delete", True, "row invisible after rollback; re-insert ok")


# ---------------------------------------------------------------------------
# SI1 — 决赛门禁复现：陈旧快照写必须 TRANSACTION_ABORT，不得变基合并写入新版本。
#
# 根因：mvcc_write / mvcc_write_col_delta / mvcc_write_col_patch 在检测到
# "快照之后已有其它事务提交了新版本"(ch.hist.back().commit_ts > txn->get_read_ts())时，
# 曾经把本次写的增量变基(rebase)合并到最新提交版本上继续放行，而不是直接 abort——
# 直接违反决赛赛题整理 §5.3："SI 陈旧写：依据过期版本更新 → 必须 TRANSACTION_ABORT，
# 不得写到新版本上"。对应决赛评测报告 "Snapshot Isolation Model" / "Transaction Commit
# Index" 两项失败。
# ---------------------------------------------------------------------------


def case_si1_stale_write_must_abort() -> CaseResult:
    """A、B 各自 BEGIN(SI) 拿到同一快照；B 先 UPDATE 并 COMMIT 产生新版本；A 基于旧快照
    对同一行做单列绝对赋值 UPDATE（mvcc_write_col_delta 通用路径），必须被
    TRANSACTION_ABORT，不能 COMMAND_OK 把两次写"缝合"到一起。"""
    db = "cons_si1_stale_write"
    rid = 901
    proc, _ = setup_si_table(db, indexed=False, rows=[(rid, 1)])
    a = new_client()
    b = new_client()
    sql(a, "set transaction isolation level snapshot isolation;")
    sql(b, "set transaction isolation level snapshot isolation;")

    sql(a, "begin;")
    sql(b, "begin;")
    # A 先读一次，固定其对该行可见的是旧版本 v=1
    sql(a, "select * from t where id = %d;" % rid)
    ok, r = sql_ok(b, "update t set v = 2 where id = %d;" % rid)
    if not ok:
        sql(a, "rollback;")
        sql(b, "rollback;")
        a.close()
        b.close()
        stop_server(proc)
        return CaseResult("SI1", "stale snapshot write must abort", False, "setup update failed: " + r)
    sql(b, "commit;")

    # A 仍持旧快照（v=1），对同一行做绝对赋值 UPDATE：必须 abort，不能悄悄改到 v=2 之上
    r_upd = sql(a, "update t set v = 3 where id = %d;" % rid)
    r_commit = sql(a, "commit;")

    c = new_client()
    rows = parse_table_rows(sql(c, "select v from t where id = %d;" % rid))
    n = count_rows(c)
    a.close()
    b.close()
    c.close()
    stop_server(proc)

    aborted = "abort" in r_upd.lower() or "abort" in r_commit.lower()
    final_v = rows[0][0].strip() if rows else None

    if n != 1 or final_v is None:
        return CaseResult(
            "SI1", "stale snapshot write must abort", False,
            "BUG: unexpected row count after conflict: n=%d rows=%s" % (n, rows),
        )
    if not aborted:
        return CaseResult(
            "SI1", "stale snapshot write must abort", False,
            "BUG: stale write not aborted (upd=%r commit=%r), final v=%s (expected TRANSACTION_ABORT, v stays 2)"
            % (r_upd.strip()[:80], r_commit.strip()[:80], final_v),
        )
    if final_v != "2":
        return CaseResult(
            "SI1", "stale snapshot write must abort", False,
            "BUG: aborted but v=%s (expected 2 — B's committed value, untouched by A's rebase)" % final_v,
        )
    return CaseResult("SI1", "stale snapshot write must abort", True, "A aborted; v=2 (B's commit) preserved")


# ---------------------------------------------------------------------------
# F1 — 决赛门禁复现：FLOAT32 wire 参数经 wire_param_literal(%.9g) 生成科学计数法字面量
# （如 1e-07 / -3.26937805e+09），旧词法 value_float 不支持指数后缀，回填给 yacc 解析
# 会报 Parse Error，导致 EXEC_BATCH 整体 Server ERROR（对应报告 Float Precision 失败项）。
# ---------------------------------------------------------------------------


def case_f1_float_scientific_wire_param() -> CaseResult:
    """PREPARE_SET + EXEC_BATCH（决赛排名路径）绑定触发 %.9g 科学计数法的 FLOAT32 边界值，
    校验 INSERT 不报错且 SELECT 读回值与原始 float32 一致（往返误差在 1e-5 相对误差内）。"""
    db = "cons_f1_float_sci"
    proc, _ = fresh_db(db)
    cli = new_client()
    ok, r = sql_ok(cli, "create table ft (id int, v float);")
    if not ok:
        cli.close()
        stop_server(proc)
        return CaseResult("F1", "float32 wire param scientific literal", False, "create table: " + r)

    cli.prepare_set([
        (1, False, [1, 2], "insert into ft values ($1, $2)"),
        (2, True, [1], "select v from ft where id = $1"),
    ])
    # 依次覆盖：极小值(指数<-4)、大幅负值(指数>=9)、边界值、另一个极小值
    cases = [(1, 1e-7), (2, -3.26937805e9), (3, 5.0e8), (4, 1.0e-4)]
    errors = []
    for rid, val in cases:
        rb = cli.exec_batch([(1, [rid, val])])
        if not rb.ok:
            errors.append("insert id=%d val=%r: %s" % (rid, val, rb.diagnostic[:160]))
            continue
        rb2 = cli.exec_batch([(2, [rid])])
        if not rb2.ok or 0 not in rb2.results or not rb2.results[0]:
            errors.append("select id=%d failed: %s" % (rid, rb2.diagnostic[:160]))
            continue
        got = float(rb2.results[0][0][0])
        rel = abs(got - val) / max(1.0, abs(val))
        if rel > 1e-5:
            errors.append("id=%d want=%r got=%r (rel=%.2e)" % (rid, val, got, rel))
    cli.close()
    stop_server(proc)
    if errors:
        return CaseResult("F1", "float32 wire param scientific literal", False, "; ".join(errors[:3]))
    return CaseResult(
        "F1", "float32 wire param scientific literal", True, "%d values round-tripped" % len(cases)
    )


# ---------------------------------------------------------------------------
# F2 — 决赛门禁复现：FLOAT32 wire 查询参数的 +inf/-inf/nan 不能因为 SQL 文本化而报错。
#
# 这里刻意只覆盖查询比较，不要求把非 finite 值写入表。存储约束与参数比较语义是两件事：
# 前者可拒绝，后者必须保持 binary32 参数可用。旧实现会将它们格式化为 inf/-inf/nan 后
# 触发 lexer/parser ERROR；087cb 又把它们全部 WireProtocolError 拒绝，均会失败。
# 期望 IEEE 比较结果：有限值 < +inf、有限值 > -inf、有限值 != NaN。
# ---------------------------------------------------------------------------


def case_f2_float_nonfinite_wire_param() -> CaseResult:
    """非 finite FLOAT32 查询参数必须成功执行并返回 IEEE 定义的筛选结果。"""
    db = "cons_f2_float_nonfinite"
    proc, _ = fresh_db(db)
    cli = new_client()
    ok, r = sql_ok(cli, "create table ft (id int, v float);")
    if not ok:
        cli.close()
        stop_server(proc)
        return CaseResult("F2", "float32 wire param inf/nan query", False, "create table: " + r)
    for rid, val in ((1, -1.5), (2, 2.5)):
        ok, r = sql_ok(cli, "insert into ft values (%d, %.1f);" % (rid, val))
        if not ok:
            cli.close()
            stop_server(proc)
            return CaseResult("F2", "float32 wire param inf/nan query", False, "seed insert: " + r)

    cli.prepare_set([
        (1, True, [2], "select id from ft where v < $1 order by id"),
        (2, True, [2], "select id from ft where v > $1 order by id"),
        (3, True, [2], "select id from ft where v <> $1 order by id"),
    ])
    probes = (
        ("+inf", 1, float("inf")),
        ("-inf", 2, float("-inf")),
        ("nan", 3, float("nan")),
    )
    errors = []
    for label, stmt_id, val in probes:
        rb = cli.exec_batch([(stmt_id, [val])])
        rows = rb.results.get(0, [])
        got = [row[0] for row in rows]
        if not rb.ok or got != [1, 2]:
            errors.append("%s: ok=%s error=%s rows=%r diag=%s" %
                          (label, rb.ok, rb.error, got, rb.diagnostic[:100]))
    alive = proc.poll() is None
    ok2, r2 = sql_ok(cli, "select count(*) from ft;")
    cli.close()
    stop_server(proc)

    if not alive:
        return CaseResult("F2", "float32 wire param inf/nan query", False, "server crashed on non-finite param")
    if errors:
        return CaseResult("F2", "float32 wire param inf/nan query", False, "; ".join(errors))
    if not ok2:
        return CaseResult(
            "F2", "float32 wire param inf/nan query", False,
            "connection unusable after non-finite query: " + r2[:120],
        )
    return CaseResult("F2", "float32 wire param inf/nan query", True, "inf/-inf/nan comparisons returned ids 1,2")


# ---------------------------------------------------------------------------
# SI2 — SI 活跃写冲突必须在冲突语句立即 abort，不能等待持锁事务结束。
# ---------------------------------------------------------------------------


def case_si2_active_write_must_abort_immediately() -> CaseResult:
    """A 持有同一记录的未提交 UPDATE；B 的 UPDATE 必须很快 abort。

    旧实现在 LockManager 中等待 A 的记录锁，既违反 SI no-wait，也会触发评测端超时。
    本用例故意在 B 尚未收到响应时保持 A 不结束，以确定检测到的是等待而非最终结果。
    """
    db = "cons_si2_active_write"
    rid = 902
    proc, _ = setup_si_table(db, indexed=False, rows=[(rid, 1)])
    a = new_client()
    b = new_client(timeout=10)
    sql(a, "set transaction isolation level snapshot isolation;")
    sql(b, "set transaction isolation level snapshot isolation;")
    sql(a, "begin;")
    sql(b, "begin;")
    ok, first = sql_ok(a, "update t set v = 2 where id = %d;" % rid)
    if not ok:
        a.close()
        b.close()
        stop_server(proc)
        return CaseResult("SI2", "active SI write aborts immediately", False, "A setup update failed: " + first)

    result = {}

    def conflicting_write():
        try:
            result["reply"] = sql(b, "update t set v = 3 where id = %d;" % rid)
        except Exception as exc:
            result["exception"] = str(exc)

    t = threading.Thread(target=conflicting_write)
    t.start()
    t.join(1.5)
    waited = t.is_alive()
    if waited:
        # Let the old waiting implementation unwind before shutting down the server.
        sql(a, "rollback;")
        t.join(5)
    else:
        sql(a, "rollback;")
    sql(b, "rollback;")
    a.close()
    b.close()
    stop_server(proc)

    reply = result.get("reply", "")
    if waited:
        return CaseResult(
            "SI2", "active SI write aborts immediately", False,
            "BUG: conflicting UPDATE still waited after 1.5s (reply after release=%r)" % reply[:100],
        )
    if result.get("exception"):
        return CaseResult(
            "SI2", "active SI write aborts immediately", False,
            "conflicting UPDATE raised transport exception: " + result["exception"][:100],
        )
    if "abort" not in reply.lower():
        return CaseResult(
            "SI2", "active SI write aborts immediately", False,
            "BUG: conflicting UPDATE was not TRANSACTION_ABORT: %r" % reply[:120],
        )
    return CaseResult("SI2", "active SI write aborts immediately", True, "B aborted before A released record lock")


# ---------------------------------------------------------------------------
# SI3 — delete must remain logically visible to a pre-existing snapshot, then be
# physically reclaimed (heap and index) only after that snapshot finishes.
# ---------------------------------------------------------------------------


def case_si3_delete_snapshot_and_index_lifetime() -> CaseResult:
    """Old SI reader sees a row deleted after BEGIN through both SeqScan and IndexScan.

    Once the reader commits, a new transaction must not see the row and must be able to reinsert
    the key. This catches immediate heap reclamation, stale index RIDs, and missing deferred GC.
    """
    db = "cons_si3_delete_snapshot_index"
    rid = 903
    proc, _ = setup_si_table(db, indexed=True, rows=[(rid, 9)])
    reader = new_client()
    writer = new_client()
    sql(reader, "set transaction isolation level snapshot isolation;")
    sql(writer, "set transaction isolation level snapshot isolation;")
    sql(reader, "begin;")
    # Establish the old snapshot before the delete transaction obtains its timestamp.
    before = parse_table_rows(sql(reader, "select id, v from t where id = %d;" % rid))
    sql(writer, "begin;")
    ok, deleted = sql_ok(writer, "delete from t where id = %d;" % rid)
    if not ok:
        sql(reader, "rollback;")
        sql(writer, "rollback;")
        reader.close()
        writer.close()
        stop_server(proc)
        return CaseResult("SI3", "delete snapshot/index lifetime", False, "delete failed: " + deleted)
    sql(writer, "commit;")

    # No predicate must use SeqScan; equality on indexed id must use IndexScan.
    old_seq = parse_table_rows(sql(reader, "select id, v from t;"))
    old_idx = parse_table_rows(sql(reader, "select id, v from t where id = %d;" % rid))
    sql(reader, "commit;")  # advances the GC watermark and permits physical removal

    verifier = new_client()
    sql(verifier, "set transaction isolation level snapshot isolation;")
    new_rows = parse_table_rows(sql(verifier, "select id, v from t where id = %d;" % rid))
    reinsert_ok, reinsert = sql_ok(verifier, "insert into t values (%d, 99);" % rid)
    reader.close()
    writer.close()
    verifier.close()
    stop_server(proc)

    want = [[str(rid), "9"]]
    if before != want:
        return CaseResult("SI3", "delete snapshot/index lifetime", False, "seed read mismatch: %s" % before)
    if old_seq != want or old_idx != want:
        return CaseResult(
            "SI3", "delete snapshot/index lifetime", False,
            "BUG: old snapshot lost deleted row (seq=%s index=%s)" % (old_seq, old_idx),
        )
    if new_rows:
        return CaseResult(
            "SI3", "delete snapshot/index lifetime", False,
            "BUG: row resurrected after old snapshot ended: %s" % new_rows,
        )
    if not reinsert_ok or "failure" in reinsert.lower():
        return CaseResult(
            "SI3", "delete snapshot/index lifetime", False,
            "BUG: stale index entry blocks re-insert: " + reinsert[:120],
        )
    return CaseResult(
        "SI3", "delete snapshot/index lifetime", True,
        "old snapshot saw row via seq/index; post-GC reader did not; re-insert succeeded",
    )


# ---------------------------------------------------------------------------
# C8 — delete + reinsert can reuse the freed Rid. Undo must remove the empty
# aborted-insert MVCC chain after restoring the original heap record.
# ---------------------------------------------------------------------------


def case_c8_delete_reinsert_abort_restores_visibility() -> CaseResult:
    """Rollback of delete→same-key reinsert restores the original committed row.

    The insert can reuse the delete's slot. Undo runs insert first (leaving an empty,
    invisible MVCC chain), then restores the pre-transaction heap bytes for the delete.
    That empty chain must be erased; otherwise ``mvcc_read`` hides the restored row.
    """
    db = "cons_c8_delete_reinsert_abort"
    rid = 904
    proc, _ = setup_si_table(db, indexed=True, rows=[(rid, 1)])
    cli = new_client()
    sql(cli, "set transaction isolation level snapshot isolation;")
    sql(cli, "begin;")
    deleted_ok, deleted = sql_ok(cli, "delete from t where id = %d;" % rid)
    inserted_ok, inserted = sql_ok(cli, "insert into t values (%d, 2);" % rid)
    rolled = sql(cli, "rollback;")
    rows = parse_table_rows(sql(cli, "select id, v from t where id = %d;" % rid))

    # Confirm the restored index/heap pair remains usable after a normal delete.
    cleanup_ok, cleanup = sql_ok(cli, "delete from t where id = %d;" % rid)
    reinsert_ok, reinsert = sql_ok(cli, "insert into t values (%d, 3);" % rid)
    cli.close()
    stop_server(proc)

    if not deleted_ok or not inserted_ok:
        return CaseResult(
            "C8", "delete-reinsert rollback restores visibility", False,
            "setup failed: delete=%r insert=%r rollback=%r" %
            (deleted[:100], inserted[:100], rolled[:100]),
        )
    if rows != [[str(rid), "1"]]:
        return CaseResult(
            "C8", "delete-reinsert rollback restores visibility", False,
            "BUG: rollback did not restore original row: %s" % rows,
        )
    if not cleanup_ok or not reinsert_ok or "failure" in reinsert.lower():
        return CaseResult(
            "C8", "delete-reinsert rollback restores visibility", False,
            "restored row/index unusable: delete=%r reinsert=%r" %
            (cleanup[:100], reinsert[:100]),
        )
    return CaseResult(
        "C8", "delete-reinsert rollback restores visibility", True,
        "original row restored; index accepted post-rollback delete/reinsert",
    )


# ---------------------------------------------------------------------------
# Registry
# ---------------------------------------------------------------------------

ALL_CASES: List[CaseSpec] = [
    CaseSpec("C1", "insert heap/MVCC micro-window", case_c1_insert_heap_race, ("mvcc",)),
    CaseSpec("C1b", "uncommitted insert invisible", case_c1_uncommitted_not_visible, ("mvcc",)),
    CaseSpec("C2", "pending overlay same-key insert", case_c2_pending_same_key_insert, ("mvcc",)),
    CaseSpec("C3", "cleaner/evict lost-update stress", case_c3_cleaner_stress, ("wal", "bpm"), flaky=True, quick=False),
    CaseSpec("C4", "crash durability smoke", case_c4_crash_durability_smoke, ("wal",), flaky=True),
    CaseSpec("C5", "compound rollback insert+update", case_c5_compound_rollback_insert_update, ("mvcc", "finals")),
    CaseSpec("C6", "compound rollback update+update", case_c6_compound_rollback_update_update, ("mvcc", "finals")),
    CaseSpec("C7", "compound rollback insert+delete", case_c7_compound_rollback_insert_delete, ("mvcc", "finals")),
    CaseSpec("F1", "float32 wire param scientific literal", case_f1_float_scientific_wire_param, ("float", "finals")),
    CaseSpec("F2", "float32 wire param inf/nan query", case_f2_float_nonfinite_wire_param, ("float", "finals")),
    CaseSpec("SI1", "stale snapshot write must abort", case_si1_stale_write_must_abort, ("mvcc", "finals")),
    CaseSpec("SI2", "active SI write aborts immediately", case_si2_active_write_must_abort_immediately, ("mvcc", "finals")),
    CaseSpec("SI3", "delete snapshot/index lifetime", case_si3_delete_snapshot_and_index_lifetime, ("mvcc", "index", "finals")),
    CaseSpec("C8", "delete-reinsert rollback restores visibility",
             case_c8_delete_reinsert_abort_restores_visibility, ("mvcc", "index", "finals")),
    CaseSpec("H1", "maintain_parent deep index", case_h1_maintain_parent_deep, ("index",), quick=False),
    CaseSpec("H2", "coalesce rightmost leaf", case_h2_coalesce_rightmost, ("index",)),
    CaseSpec("H3", "SER INLJ immediate dangerous-structure abort",
             case_h3_ser_inlj_dangerous_structure, ("ssi", "finals")),
    CaseSpec("H3S", "SER INLJ concurrent join", case_h3s_ser_inlj_smoke,
             ("ssi",), flaky=True, quick=False),
    CaseSpec("H4", "ser_finish GC concurrency", case_h4_ser_gc_stress, ("ssi",), flaky=True, quick=False),
    CaseSpec("H5", "cached mvcc_on scan phantom", case_h5_scan_mvcc_on_cache, ("mvcc",)),
    CaseSpec("M1", "double BEGIN versioning", case_m1_double_begin, ("meta",)),
    CaseSpec("M6", "set output_file off API", case_m6_output_file_off, ("meta",), expect_fail=True),
    CaseSpec("M7", "payment ytd drift", case_m7_payment_ytd, ("tpcc",), flaky=True, quick=False),
    CaseSpec("INT", "SI burst strict integration", case_int_si_burst_strict, ("integration",), flaky=True, quick=False),
]

CASE_BY_ID = {c.case_id.upper(): c for c in ALL_CASES}
