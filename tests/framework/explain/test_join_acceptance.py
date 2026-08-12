#!/usr/bin/env python3
"""Exact Wire-v3 acceptance gate for JOIN EXPLAIN ANALYZE output.

This test deliberately fails with a non-zero exit status on every mismatch.  It
replaces the older print-only EXPLAIN probes with assertions against Wire-v3's
one-column ``QUERY PLAN`` result set.
"""

from __future__ import annotations

import difflib
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Callable, Sequence


ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "build"
SERVER = BUILD / "bin" / "rmdb"
DB_NAME = "join_explain_acceptance_db"
DB_REL = Path("test_dbs") / DB_NAME
DB_DIR = BUILD / DB_REL

LOCAL_TESTS = ROOT / "tests" / "local"
if str(LOCAL_TESTS) not in sys.path:
    sys.path.insert(0, str(LOCAL_TESTS))

from wire_client import PORT, SQLTYPE_CHAR, WireClient, wait_ready  # noqa: E402


class GateFailure(AssertionError):
    pass


def port_is_open() -> bool:
    with socket.socket() as probe:
        probe.settimeout(0.25)
        return probe.connect_ex(("127.0.0.1", PORT)) == 0


def stop_server(proc: subprocess.Popen[bytes] | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def require_ok(client: WireClient, sql: str) -> None:
    result = client.exec_stream(sql)
    if result.error or result.aborted or not result.ok:
        raise GateFailure(f"setup SQL failed: {sql}\n{result.diagnostic}")


def explain_lines(client: WireClient, sql: str) -> list[str]:
    result = client.exec_stream(sql)
    if result.error or result.aborted or not result.ok:
        raise GateFailure(f"EXPLAIN failed: {sql}\n{result.diagnostic}")
    expected_meta = [("QUERY PLAN", SQLTYPE_CHAR)]
    if result.columns != expected_meta:
        raise GateFailure(
            f"EXPLAIN Wire metadata mismatch for {sql}\n"
            f"expected: {expected_meta!r}\nactual:   {result.columns!r}"
        )

    lines: list[str] = []
    for row_no, row in enumerate(result.rows, 1):
        if len(row) != 1 or not isinstance(row[0], str):
            raise GateFailure(
                f"EXPLAIN row {row_no} must contain one CHAR cell; got {row!r}"
            )
        lines.append(row[0])
    if not lines:
        raise GateFailure(f"EXPLAIN returned an empty plan: {sql}")
    return lines


def require_exact(label: str, actual: Sequence[str], expected: Sequence[str]) -> None:
    if list(actual) == list(expected):
        return
    diff = "\n".join(
        difflib.unified_diff(
            list(expected),
            list(actual),
            fromfile=f"{label}.expected",
            tofile=f"{label}.actual",
            lineterm="",
        )
    )
    raise GateFailure(f"{label}: exact plan mismatch\n{diff}")


def require_line(label: str, actual: Sequence[str], expected: str) -> None:
    if expected not in actual:
        rendered = "\n".join(repr(line) for line in actual)
        raise GateFailure(
            f"{label}: missing exact plan line {expected!r}\nactual lines:\n{rendered}"
        )


class Checks:
    def __init__(self) -> None:
        self.failures: list[tuple[str, str]] = []

    def run(self, label: str, check: Callable[[], None]) -> None:
        try:
            check()
        except Exception as exc:  # keep running to report all plan regressions
            self.failures.append((label, str(exc)))
            print(f"[FAIL] {label}")
            print(str(exc))
        else:
            print(f"[PASS] {label}")


def run_checks(client: WireClient) -> list[tuple[str, str]]:
    setup = [
        "create table ja_l (id int, lv int)",
        "create table ja_r (id int, rv int)",
        "insert into ja_l values (1,10)",
        "insert into ja_l values (2,20)",
        "insert into ja_l values (4,40)",
        "insert into ja_r values (2,200)",
        "insert into ja_r values (3,300)",
        "insert into ja_r values (4,400)",
    ]
    for sql in setup:
        require_ok(client, sql)

    checks = Checks()
    join_sql = (
        "explain analyze select * from ja_l l "
        "join ja_r r on l.id=r.id"
    )

    def check_nlj() -> None:
        require_exact(
            "ordinary NLJ",
            explain_lines(client, join_sql),
            [
                "Project(columns=[*], rows=2)",
                "\tJoin(tables=[l, r], condition=[l.id=r.id], rows=2)",
                "\t\tScan(table=ja_l AS l, type=SeqScan, rows=3)",
                "\t\tScan(table=ja_r AS r, type=SeqScan, rows=9)",
            ],
        )

    checks.run("ordinary no-index NLJ exact format", check_nlj)

    require_ok(client, "create index ja_r(id)")

    def check_inlj() -> None:
        require_exact(
            "ordinary INLJ",
            explain_lines(client, join_sql),
            [
                "Project(columns=[*], rows=2)",
                "\tJoin(tables=[l, r], condition=[l.id=r.id], rows=2)",
                "\t\tScan(table=ja_l AS l, type=SeqScan, rows=3)",
                "\t\tScan(table=ja_r AS r, type=IndexScan, using_index=(id), rows=2)",
            ],
        )

    checks.run("ordinary unique-index INLJ exact format", check_inlj)

    def check_natural() -> None:
        plan = explain_lines(
            client,
            "explain analyze select * from ja_l natural join ja_r",
        )
        require_line(
            "NATURAL JOIN",
            plan,
            "\tJoin(type=NATURAL INNER, tables=[ja_l, ja_r], "
            "condition=[ja_l.id=ja_r.id], rows=2)",
        )
        require_line(
            "NATURAL JOIN",
            plan,
            "\t\tScan(table=ja_r, type=SeqScan, rows=9)",
        )

    checks.run("NATURAL JOIN label and runtime rows", check_natural)

    def check_semi() -> None:
        plan = explain_lines(
            client,
            "explain analyze select * from ja_l l "
            "semi join ja_r r on l.id=r.id",
        )
        require_line(
            "SEMI JOIN",
            plan,
            "\tJoin(type=LEFT SEMI, tables=[l, r], condition=[l.id=r.id], rows=2)",
        )
        require_line(
            "SEMI JOIN",
            plan,
            "\t\t\tScan(table=ja_r AS r, type=SeqScan, rows=9)",
        )

    checks.run("SEMI JOIN label, existence rows, and NLJ inner scan", check_semi)

    def check_anti() -> None:
        plan = explain_lines(
            client,
            "explain analyze select * from ja_l l "
            "anti join ja_r r on l.id=r.id",
        )
        require_line(
            "ANTI JOIN",
            plan,
            "\tJoin(type=LEFT ANTI, tables=[l, r], condition=[l.id=r.id], rows=1)",
        )
        require_line(
            "ANTI JOIN",
            plan,
            "\t\t\tScan(table=ja_r AS r, type=SeqScan, rows=9)",
        )

    checks.run("ANTI JOIN label, anti-existence rows, and NLJ inner scan", check_anti)

    def check_lateral() -> None:
        plan = explain_lines(
            client,
            "explain analyze select x.rv from ja_l l cross join lateral "
            "(select r.rv from ja_r r where r.id=l.id "
            "order by r.rv desc limit 1) x",
        )
        for expected in [
            "\tJoin(type=CROSS LATERAL, tables=[l, x], condition=[], rows=2)",
            "\t\tDerivedTable(alias=x)",
            "\t\t\tLimit(count=1)",
            "\t\t\t\tProject(columns=[r.rv], rows=2)",
            "\t\t\t\t\tSort(columns=[r.rv DESC])",
            "\t\t\t\t\t\tCorrelatedFilter(condition=[r.id=l.id])",
            "\t\t\t\t\t\t\tScan(table=ja_r AS r, type=SeqScan, rows=9)",
        ]:
            require_line("LATERAL JOIN", plan, expected)

    checks.run("LATERAL label, dependent rows, and parameterized RHS nodes", check_lateral)
    return checks.failures


def main() -> int:
    if not SERVER.is_file():
        print(f"[FATAL] server binary not found: {SERVER}", file=sys.stderr)
        print("Build it first with: cmake --build build -j", file=sys.stderr)
        return 2
    if port_is_open():
        print(
            f"[FATAL] 127.0.0.1:{PORT} is already in use; refusing to kill an unrelated server",
            file=sys.stderr,
        )
        return 2

    expected_parent = BUILD / "test_dbs"
    if DB_DIR.parent != expected_parent:
        print(f"[FATAL] unsafe database path: {DB_DIR}", file=sys.stderr)
        return 2
    expected_parent.mkdir(parents=True, exist_ok=True)
    if DB_DIR.exists():
        shutil.rmtree(DB_DIR)
    # rmdb accepts an existing empty directory and initializes db.meta/db.log
    # while opening it.  Pre-create it here to avoid SmManager::create_db's
    # relative nested-path cwd transition on the first launch.
    DB_DIR.mkdir()

    proc: subprocess.Popen[bytes] | None = None
    client: WireClient | None = None
    with tempfile.TemporaryFile() as server_log:
        try:
            proc = subprocess.Popen(
                [str(SERVER), str(DB_REL)],
                cwd=BUILD,
                stdout=server_log,
                stderr=subprocess.STDOUT,
            )
            wait_ready(timeout=15.0)
            client = WireClient(timeout=30)
            failures = run_checks(client)
        except Exception as exc:
            print(f"[FATAL] {exc}", file=sys.stderr)
            failures = [("gate execution", str(exc))]
        finally:
            if client is not None:
                client.close()
            stop_server(proc)

        if failures:
            server_log.seek(0)
            log_tail = server_log.read().decode("utf-8", errors="replace")[-4000:]
            print(f"\nJOIN EXPLAIN acceptance: {len(failures)} failure(s)", file=sys.stderr)
            for label, detail in failures:
                print(f"- {label}: {detail.splitlines()[0]}", file=sys.stderr)
            if log_tail.strip():
                print("\nServer log tail:", file=sys.stderr)
                print(log_tail, file=sys.stderr)
            return 1

    print("\nJOIN EXPLAIN acceptance: 6/6 PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
