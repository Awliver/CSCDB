#!/usr/bin/env python3
"""PostgreSQL-derived gate for DISTINCT/USING/OFFSET/set ops/null tests.

Run from the repository root after building RMDB:

    python3 -B tests/local/postgresql_extended_syntax_gate.py

Set RMDB_BUILD_DIR to select a different build tree.  Provenance, exact
upstream statements, and adaptation notes live in
``postgresql_extended_syntax_cases.py``.  Every supported semantic property is
asserted here; unsupported PostgreSQL fixture syntax is never silently run or
dropped.
"""

from __future__ import annotations

import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("RMDB_BUILD_DIR", str(ROOT / "build"))).resolve()
SERVER = BUILD / "bin" / "rmdb"
DB_REL = Path("test_dbs") / "postgresql_extended_syntax_gate_db"
DB_DIR = BUILD / DB_REL
UPSTREAM_COMMIT_FILE = ROOT / "tests" / "postgresql_regress" / "UPSTREAM_COMMIT"

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from postgresql_extended_syntax_cases import (  # noqa: E402
    CASES,
    POSTGRESQL_COMMIT,
    RMDB_EXTENSION_CASES,
    UPSTREAM_SOURCES,
)
from wire_client import PORT, ExecResult, WireClient, wait_ready  # noqa: E402


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


def describe(result: ExecResult) -> str:
    kind = "TRANSACTION_ABORT" if result.aborted else "ERROR" if result.error else "OK"
    return f"{kind}: {result.diagnostic}" if result.diagnostic else kind


def execute_ok(client: WireClient, sql: str) -> ExecResult:
    result = client.exec_stream(sql)
    if result.error or result.aborted or not result.ok:
        raise GateFailure(f"SQL failed: {sql}\n{describe(result)}")
    return result


def assert_rows(
    client: WireClient,
    label: str,
    sql: str,
    expected: Sequence[Sequence[object]],
    *,
    columns: Sequence[str] | None = None,
) -> None:
    result = execute_ok(client, sql)
    wanted = [list(row) for row in expected]
    if result.rows != wanted:
        raise GateFailure(
            f"{label}: rows mismatch\nSQL={sql}\nexpected={wanted!r}\nactual={result.rows!r}"
        )
    if columns is not None:
        actual_columns = [name for name, _ in result.columns]
        if actual_columns != list(columns):
            raise GateFailure(
                f"{label}: columns mismatch\nSQL={sql}\n"
                f"expected={list(columns)!r}\nactual={actual_columns!r}"
            )
    print(f"[PASS] {label}")


def assert_case(
    client: WireClient,
    case_id: str,
    expected: Sequence[Sequence[object]],
    *,
    sql: str | None = None,
    columns: Sequence[str] | None = None,
) -> None:
    case = CASES[case_id]
    assert_rows(
        client,
        f"PostgreSQL {case.source}.sql:{case.lines} / {case_id}",
        sql or case.rmdb_sql,
        expected,
        columns=columns,
    )


def setup_fixture(client: WireClient) -> None:
    statements = [
        "create table pgx_distinct (a int,b int,s char(8))",
        "insert into pgx_distinct values (1,10,'aa')",
        "insert into pgx_distinct values (1,10,'aa')",
        "insert into pgx_distinct values (1,20,'bb')",
        "insert into pgx_distinct values (2,10,'aa')",
        "insert into pgx_distinct values (2,10,'cc')",
        "insert into pgx_distinct values (3,30,'dd')",
        "create table pgx_l (k int,k2 int,lv int)",
        "insert into pgx_l values (1,10,100)",
        "insert into pgx_l values (2,20,200)",
        "insert into pgx_l values (3,30,300)",
        "insert into pgx_l values (5,50,500)",
        "create table pgx_r (k int,k2 int,rv int)",
        "insert into pgx_r values (1,10,1000)",
        "insert into pgx_r values (2,99,2000)",
        "insert into pgx_r values (4,40,4000)",
        "insert into pgx_r values (5,50,5000)",
        "create table pgx_limit (id int)",
        *[f"insert into pgx_limit values ({value})" for value in range(10)],
        "create table pgx_agg (ten int,four int)",
        *[
            f"insert into pgx_agg values ({ten},{four})"
            for ten, four in ((0, 0), (0, 0), (0, 1), (0, 2),
                              (1, 1), (1, 1), (1, 2), (1, 3))
        ],
        "create table pgx_set_a (v int,g int)",
        *[
            f"insert into pgx_set_a values ({value},{group})"
            for value, group in ((1, 10), (1, 10), (2, 20), (2, 21), (3, 30), (5, 50))
        ],
        "create table pgx_set_b (v int,g int)",
        *[
            f"insert into pgx_set_b values ({value},{group})"
            for value, group in ((1, 10), (2, 20), (2, 20), (4, 40), (5, 51))
        ],
    ]
    for sql in statements:
        execute_ok(client, sql)


def run_distinct_checks(client: WireClient) -> int:
    assert_case(client, "distinct_single", [[1], [2], [3]], columns=["a"])
    assert_case(
        client,
        "distinct_multi",
        [[1, "aa", 10], [1, "bb", 20], [2, "aa", 10], [2, "cc", 10], [3, "dd", 30]],
        columns=["a", "s", "b"],
    )
    assert_case(client, "distinct_repeated_projection", [[4]])
    assert_case(client, "distinct_order_limit", [[1, 20], [2, 10]])
    return 4


def run_using_checks(client: WireClient) -> int:
    # SQL USING output layout is: merged keys, remaining left columns, then
    # remaining right columns.  Checking column names prevents an ON-equivalent
    # implementation from accidentally exposing both key columns.
    assert_case(
        client,
        "using_inner",
        [
            [1, 10, 100, 10, 1000],
            [2, 20, 200, 99, 2000],
            [5, 50, 500, 50, 5000],
        ],
        columns=["k", "k2", "lv", "k2", "rv"],
    )
    assert_case(
        client,
        "using_left",
        [
            [1, 10, 100, 10, 1000],
            [2, 20, 200, 99, 2000],
            [3, 30, 300, None, None],
            [5, 50, 500, 50, 5000],
        ],
        columns=["k", "k2", "lv", "k2", "rv"],
    )
    assert_case(
        client,
        "using_right",
        [
            [1, 10, 100, 10, 1000],
            [2, 20, 200, 99, 2000],
            [4, None, None, 40, 4000],
            [5, 50, 500, 50, 5000],
        ],
        columns=["k", "k2", "lv", "k2", "rv"],
    )
    assert_case(
        client,
        "using_full",
        [
            [1, 10, 100, 10, 1000],
            [2, 20, 200, 99, 2000],
            [3, 30, 300, None, None],
            [4, None, None, 40, 4000],
            [5, 50, 500, 50, 5000],
        ],
        columns=["k", "k2", "lv", "k2", "rv"],
    )
    assert_case(
        client,
        "using_multiple_columns",
        [[1, 10, 100, 1000], [5, 50, 500, 5000]],
        columns=["k", "k2", "lv", "rv"],
    )
    assert_rows(
        client,
        "qualified source keys remain addressable after USING",
        "select l.k,r.k from pgx_l l join pgx_r r using (k) order by l.k",
        [[1, 1], [2, 2], [5, 5]],
        columns=["k", "k"],
    )
    return 6


def run_limit_checks(client: WireClient) -> int:
    assert_case(client, "limit_offset", [[2], [3], [4]])
    assert_case(client, "limit_offset_empty", [])
    assert_case(client, "offset_limit", [[2], [3], [4]])
    assert_rows(
        client,
        "LIMIT zero after OFFSET",
        "select id from pgx_limit order by id limit 0 offset 2",
        [],
    )
    assert_rows(
        client,
        "parenthesized inner and outer LIMIT/OFFSET boundaries",
        "select id from (select id from pgx_limit order by id desc limit 6 offset 1) s order by id limit 2 offset 2",
        [[5], [6]],
    )
    return 5


def run_aggregate_checks(client: WireClient) -> int:
    assert_case(client, "count_distinct", [[4]], columns=["cnt_4"])
    assert_case(client, "sum_distinct_grouped", [[0, 4, 3], [1, 4, 6]])
    assert_case(client, "avg_distinct", [[1.5, 6]])
    sql, note = RMDB_EXTENSION_CASES["count_distinct_multiple_columns"]
    assert_rows(client, f"RMDB extension / multi-column COUNT DISTINCT: {note}", sql, [[6]])
    sql, note = RMDB_EXTENSION_CASES[
        "count_distinct_parenthesized_multiple_columns"
    ]
    assert_rows(
        client,
        f"RMDB extension / parenthesized multi-column COUNT DISTINCT: {note}",
        sql,
        [[6]],
    )
    return 5


def run_set_operation_checks(client: WireClient) -> int:
    assert_case(client, "intersect", [[1], [2], [5]])
    assert_case(
        client,
        "intersect",
        [[1], [2], [2], [5]],
        sql="select v from pgx_set_a intersect all select v from pgx_set_b order by 1",
    )
    assert_case(client, "except", [[3]])
    assert_case(
        client,
        "except",
        [[1], [3]],
        sql="select v from pgx_set_a except all select v from pgx_set_b order by 1",
    )
    assert_rows(
        client,
        "PostgreSQL union.sql:148-150 / multi-column INTERSECT",
        "select v,g from pgx_set_a intersect select v,g from pgx_set_b order by 1,2",
        [[1, 10], [2, 20]],
    )
    assert_rows(
        client,
        "PostgreSQL union.sql:101-109 / multi-column EXCEPT",
        "select v,g from pgx_set_a except select v,g from pgx_set_b order by 1,2",
        [[2, 21], [3, 30], [5, 50]],
    )
    assert_case(client, "set_precedence", [[1], [3]])
    return 7


def run_null_test_checks(client: WireClient) -> int:
    assert_case(client, "is_null", [[3]])
    assert_case(client, "is_not_null", [[1], [2], [5]])
    assert_rows(
        client,
        "IS NULL remains two-valued under NOT",
        "select l.k from pgx_l l left join pgx_r r on l.k=r.k where not (r.rv is null) order by l.k",
        [[1], [2], [5]],
    )
    assert_rows(
        client,
        "USING merged key is non-NULL when either FULL JOIN side exists",
        "select k from pgx_l l full join pgx_r r using (k) where k is not null order by k",
        [[1], [2], [3], [4], [5]],
    )
    return 4


def verify_provenance_shape() -> None:
    pinned_commit = UPSTREAM_COMMIT_FILE.read_text(encoding="utf-8").strip()
    if pinned_commit != POSTGRESQL_COMMIT:
        raise GateFailure(
            f"extended PostgreSQL pin mismatch: {pinned_commit!r} != "
            f"{POSTGRESQL_COMMIT!r}"
        )
    required = {
        "select_distinct", "join", "limit", "aggregates", "union", "groupingsets"
    }
    if set(UPSTREAM_SOURCES) != required:
        raise GateFailure(f"provenance source set changed: {set(UPSTREAM_SOURCES)!r}")
    for case_id, case in CASES.items():
        if case.case_id != case_id or case.source not in required:
            raise GateFailure(f"invalid provenance case: {case_id}: {case!r}")
        if not case.upstream_sql.strip() or not case.rmdb_sql.strip() or not case.adaptation.strip():
            raise GateFailure(f"incomplete provenance case: {case_id}")


def main() -> int:
    verify_provenance_shape()
    if not SERVER.is_file():
        print(f"[FATAL] RMDB binary not found: {SERVER}", file=sys.stderr)
        return 2
    if port_is_open():
        print(f"[FATAL] port {PORT} is already in use", file=sys.stderr)
        return 2

    expected_parent = BUILD / "test_dbs"
    if DB_DIR.parent != expected_parent:
        print(f"[FATAL] unsafe database path: {DB_DIR}", file=sys.stderr)
        return 2
    expected_parent.mkdir(parents=True, exist_ok=True)
    if DB_DIR.exists():
        shutil.rmtree(DB_DIR)
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
            setup_fixture(client)
            total = 0
            total += run_distinct_checks(client)
            total += run_using_checks(client)
            total += run_limit_checks(client)
            total += run_aggregate_checks(client)
            total += run_set_operation_checks(client)
            total += run_null_test_checks(client)
        except Exception as exc:
            print(f"[FATAL] {exc}", file=sys.stderr)
            server_log.seek(0)
            tail = server_log.read().decode("utf-8", errors="replace")[-8000:]
            if tail.strip():
                print("\nServer log tail:", file=sys.stderr)
                print(tail, file=sys.stderr)
            return 1
        finally:
            if client is not None:
                client.close()
            stop_server(proc)

    print(
        f"\nPostgreSQL-derived extended syntax gate: {total}/{total} PASS "
        f"(upstream {POSTGRESQL_COMMIT})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
