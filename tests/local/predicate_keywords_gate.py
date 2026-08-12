#!/usr/bin/env python3
"""PostgreSQL-derived LIKE/BETWEEN/EXISTS/IN large-data local gate.

Run from the repository root:

    python3 -B tests/local/predicate_keywords_gate.py

The default workload loads 1,000,000 predicate rows and 10,000 subquery rows.
RMDB_BUILD_DIR selects another build tree.  --rows can adjust the workload,
but values below PostgreSQL's tenk regression scale are rejected.
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable, Sequence


ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("RMDB_BUILD_DIR", str(ROOT / "build"))).resolve()
SERVER = BUILD / "bin" / "rmdb"
DB_REL = Path("test_dbs") / "predicate_keywords_gate_db"
DB_DIR = BUILD / DB_REL
DEFAULT_ROWS = 1_000_000
MIN_ROWS = 10_000
MAX_SUBQUERY_ROWS = 10_000
SUBQUERY_WINDOW = 1_000

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from postgresql_predicate_cases import (  # noqa: E402
    LIKE_CASES,
    POSTGRESQL_COMMIT,
)
from wire_client import PORT, ExecResult, WireClient, wait_ready  # noqa: E402


class GateFailure(AssertionError):
    pass


def port_is_open() -> bool:
    with socket.socket() as probe:
        probe.settimeout(0.25)
        return probe.connect_ex(("127.0.0.1", PORT)) == 0


def execute_ok(client: WireClient, sql: str) -> ExecResult:
    result = client.exec_stream(sql)
    if result.error or result.aborted or not result.ok:
        raise GateFailure(f"SQL failed: {sql}\n{result.diagnostic}")
    return result


def assert_rows(
    client: WireClient,
    label: str,
    sql: str,
    expected: Sequence[Sequence[object]],
    *,
    announce: bool = True,
) -> None:
    result = execute_ok(client, sql)
    rows = [list(row) for row in expected]
    if result.rows != rows:
        raise GateFailure(
            f"{label}: rows mismatch\nSQL={sql}\n"
            f"expected={rows!r}\nactual={result.rows!r}"
        )
    if announce:
        print(f"[PASS] {label}")


def assert_count(client: WireClient, label: str, sql: str, expected: int) -> None:
    assert_rows(client, label, sql, [[expected]])


LARGE_NAMES = (
    "hawkeye",
    "indio",
    "abc",
    "foo",
    "f",
    "jack",
    "horizon",
    "Hawkeye",
    "bar",
    "i_dio",
    "be_r",
    "h%wkeye",
)


def generated_row(row_id: int) -> tuple[int, str, int, int]:
    return (
        row_id,
        LARGE_NAMES[row_id % len(LARGE_NAMES)],
        (row_id * 37) % 1_000 - 100,
        (row_id * 17) % 100,
    )


def sql_like(value: str, pattern: str) -> bool:
    """Independent Python oracle for the supported SQL LIKE subset."""
    regex = []
    index = 0
    while index < len(pattern):
        token = pattern[index]
        if token == "%":
            regex.append(".*")
        elif token == "_":
            regex.append(".")
        elif token == "\\" and index + 1 < len(pattern):
            index += 1
            regex.append(re.escape(pattern[index]))
        else:
            regex.append(re.escape(token))
        index += 1
    return re.fullmatch("".join(regex), value, flags=re.DOTALL) is not None


def count_generated(rows: int, predicate: Callable[[int, str, int, int], bool]) -> int:
    return sum(1 for row_id in range(rows) if predicate(*generated_row(row_id)))


def generate_csv_data(data_dir: Path, rows: int) -> tuple[Path, Path, int]:
    data_dir.mkdir(parents=True, exist_ok=True)
    predicate_path = data_dir / "predicate_data.csv"
    allowed_path = data_dir / "allowed_ids.csv"

    with predicate_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "name", "score", "grp"))
        for row_id in range(rows):
            writer.writerow(generated_row(row_id))

    allowed_rows = 0
    with allowed_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "grp"))
        # PostgreSQL's canonical tenk relation has 10,000 rows.  Keep the
        # subquery side at that scale even when the outer stress table grows
        # to one million rows, otherwise the intentionally naive correlated
        # executor turns this correctness gate into a quadratic benchmark.
        allowed_id_limit = min(rows, MAX_SUBQUERY_ROWS * 5)
        for row_id in range(0, allowed_id_limit, 5):
            writer.writerow((row_id, generated_row(row_id)[3]))
            allowed_rows += 1

    return predicate_path, allowed_path, allowed_rows


def load_large_data(client: WireClient, rows: int, data_dir: Path) -> tuple[int, int]:
    predicate_path, allowed_path, allowed_rows = generate_csv_data(data_dir, rows)
    started = time.monotonic()
    for sql in [
        "create table predicate_data (id int,name char(24),score int,grp int)",
        "create table allowed_ids (id int,grp int)",
        f"load {predicate_path} into predicate_data",
        f"load {allowed_path} into allowed_ids",
    ]:
        execute_ok(client, sql)
    elapsed = time.monotonic() - started
    print(
        f"[DATA] loaded {rows:,} predicate rows + {allowed_rows:,} subquery rows "
        f"in {elapsed:.2f}s"
    )
    assert_count(client, "large predicate row count", "select count(*) from predicate_data", rows)
    assert_count(client, "tenk-scale subquery row count", "select count(*) from allowed_ids", allowed_rows)
    return allowed_rows, 2


def sql_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def run_postgresql_like_matrix(client: WireClient) -> int:
    execute_ok(client, "create table pg_like_cases (case_id int,txt char(24))")
    for case_id, case in enumerate(LIKE_CASES, start=1):
        execute_ok(
            client,
            f"insert into pg_like_cases values ({case_id},{sql_string(case.value)})",
        )
    execute_ok(client, "create index pg_like_cases (case_id)")

    assertions = 0
    for case_id, case in enumerate(LIKE_CASES, start=1):
        source = f"PostgreSQL strings.sql:{case.line}"
        like_expected = [[case_id]] if case.expected else []
        not_like_expected = [] if case.expected else [[case_id]]
        assert_rows(
            client,
            f"{source} LIKE",
            "select case_id from pg_like_cases "
            f"where case_id={case_id} and txt like {sql_string(case.pattern)}",
            like_expected,
            announce=False,
        )
        assert_rows(
            client,
            f"{source} NOT LIKE",
            "select case_id from pg_like_cases "
            f"where case_id={case_id} and txt not like {sql_string(case.pattern)}",
            not_like_expected,
            announce=False,
        )
        assertions += 2

    print(
        f"[PASS] PostgreSQL LIKE/NOT LIKE matrix "
        f"({len(LIKE_CASES)} cases, {assertions} assertions)"
    )
    return assertions


def run_projection_pruning_regression(client: WireClient) -> int:
    """Keep the differential-discovered correlated key regression compact."""
    for sql in [
        "create table projection_outer (id int,name char(24),score int)",
        "create table projection_inner (id int)",
    ]:
        execute_ok(client, sql)
    expected = 0
    for row_id in range(100):
        name = LARGE_NAMES[row_id % len(LARGE_NAMES)]
        score = (row_id * 37) % 1_000 - 100
        execute_ok(
            client,
            f"insert into projection_outer values ({row_id},{sql_string(name)},{score})",
        )
        if row_id % 5 == 0:
            execute_ok(client, f"insert into projection_inner values ({row_id})")
        if (
            sql_like(name, "__%")
            and not (-1062 <= score <= 777)
            and row_id % 5 != 0
            and not sql_like(name, "H%")
        ):
            expected += 1

    sql = (
        "select count(*) from projection_outer p where "
        "((p.name like '__%' and p.score not between -1062 and 777) and "
        "(not exists (select a.id from projection_inner a where a.id=p.id) "
        "and p.name not like 'H%'))"
    )
    assert_count(client, "no-index correlated-key projection regression", sql, expected)
    execute_ok(client, "create index projection_outer (id)")
    execute_ok(client, "create index projection_inner (id)")
    assert_count(client, "indexed correlated-key projection regression", sql, expected)
    return 2


def build_large_predicate_checks(
    rows: int, allowed_rows: int
) -> list[tuple[str, str, int]]:
    large_checks: list[tuple[str, str, int]] = []

    for pattern in ("h%", "H%", "_ndio", "__%", "%____%"):
        expected = count_generated(
            rows, lambda _id, name, _score, _grp, p=pattern: sql_like(name, p)
        )
        large_checks.extend(
            [
                (
                    f"large LIKE {pattern!r}",
                    f"select count(*) from predicate_data where name like {sql_string(pattern)}",
                    expected,
                ),
                (
                    f"large NOT LIKE {pattern!r}",
                    f"select count(*) from predicate_data where name not like {sql_string(pattern)}",
                    rows - expected,
                ),
            ]
        )

    between_expected = count_generated(
        rows, lambda _id, _name, score, _grp: 100 <= score <= 300
    )
    two_ranges_expected = count_generated(
        rows,
        lambda _id, _name, score, _grp: -100 <= score <= -50 or 850 <= score <= 899,
    )
    lower_edge_expected = count_generated(
        rows, lambda _id, _name, score, _grp: score == -100
    )
    large_checks.extend(
        [
            (
                "PostgreSQL BETWEEN inclusive range",
                "select count(*) from predicate_data where score between 100 and 300",
                between_expected,
            ),
            (
                "PostgreSQL NOT BETWEEN complement",
                "select count(*) from predicate_data where score not between 100 and 300",
                rows - between_expected,
            ),
            (
                "BETWEEN includes an equal endpoint",
                "select count(*) from predicate_data where score between -100 and -100",
                lower_edge_expected,
            ),
            (
                "BETWEEN reversed bounds are empty",
                "select count(*) from predicate_data where score between 300 and 100",
                0,
            ),
            (
                "BETWEEN ranges inside OR tree",
                "select count(*) from predicate_data where "
                "score between -100 and -50 or score between 850 and 899",
                two_ranges_expected,
            ),
        ]
    )

    in_values = (10, 9, 2, 8, 3, 7, 4, 6, 5, 1)
    in_sql = ",".join(str(value) for value in in_values)
    in_expected = sum(1 for value in set(in_values) if 0 <= value < rows)
    large_checks.extend(
        [
            (
                "PostgreSQL long IN value list",
                f"select count(*) from predicate_data where id in ({in_sql})",
                in_expected,
            ),
            (
                "PostgreSQL long NOT IN value list",
                f"select count(*) from predicate_data where id not in ({in_sql})",
                rows - in_expected,
            ),
        ]
    )

    outer_last = min(rows - 1, SUBQUERY_WINDOW - 1)
    outer_rows = outer_last + 1
    subquery_matches = sum(1 for row_id in range(outer_rows) if row_id % 5 == 0)
    large_checks.extend(
        [
            (
                "PostgreSQL uncorrelated IN subquery",
                "select count(*) from predicate_data where "
                f"id between 0 and {outer_last} and id in (select id from allowed_ids)",
                subquery_matches,
            ),
            (
                "uncorrelated NOT IN subquery",
                "select count(*) from predicate_data where "
                f"id between 0 and {outer_last} and id not in (select id from allowed_ids)",
                outer_rows - subquery_matches,
            ),
            (
                "PostgreSQL correlated IN subquery",
                "select count(*) from predicate_data p where "
                f"p.id between 0 and {outer_last} and p.id in "
                "(select a.id from allowed_ids a where a.id=p.id)",
                subquery_matches,
            ),
            (
                "PostgreSQL uncorrelated EXISTS",
                "select count(*) from predicate_data where "
                "exists (select id from allowed_ids where id=10)",
                rows if allowed_rows > 2 else 0,
            ),
            (
                "PostgreSQL NOT EXISTS empty subquery",
                "select count(*) from predicate_data where "
                f"id between 0 and {outer_last} and "
                "not exists (select id from allowed_ids where id=-1)",
                outer_rows,
            ),
            (
                "PostgreSQL correlated EXISTS",
                "select count(*) from predicate_data p where "
                f"p.id between 0 and {outer_last} and exists "
                "(select a.id from allowed_ids a where a.id=p.id)",
                subquery_matches,
            ),
            (
                "correlated NOT EXISTS complement",
                "select count(*) from predicate_data p where "
                f"p.id between 0 and {outer_last} and not exists "
                "(select a.id from allowed_ids a where a.id=p.id)",
                outer_rows - subquery_matches,
            ),
        ]
    )

    boolean_expected = count_generated(
        rows,
        lambda row_id, name, score, _grp: sql_like(name, "h%")
        or not (100 <= score <= 300 or row_id in in_values),
    )
    or_exists_expected = sum(
        1 for row_id in range(200) if row_id == 2 or row_id % 5 != 0
    )
    large_checks.extend(
        [
            (
                "keywords preserve arbitrary boolean-tree semantics",
                "select count(*) from predicate_data where name like 'h%' or "
                f"not (score between 100 and 300 or id in ({in_sql}))",
                boolean_expected,
            ),
            (
                "EXISTS inside OR/NOT expression tree",
                "select count(*) from predicate_data p where p.id between 0 and 199 "
                "and (p.id=2 or not exists "
                "(select a.id from allowed_ids a where a.id=p.id))",
                or_exists_expected,
            ),
        ]
    )

    return large_checks


def run_large_predicate_checks(
    client: WireClient,
    checks: Sequence[tuple[str, str, int]],
    access_path: str,
) -> int:
    for label, sql, expected in checks:
        assert_count(client, f"{access_path}: {label}", sql, expected)
    print(f"[PASS] {access_path}: {len(checks)} large predicate queries")
    return len(checks)


def create_large_indexes(client: WireClient) -> None:
    started = time.monotonic()
    for sql in [
        "create index predicate_data (id)",
        "create index allowed_ids (id)",
    ]:
        execute_ok(client, sql)
    print(f"[DATA] built predicate indexes in {time.monotonic() - started:.2f}s")


def assert_access_plan(client: WireClient, expected_type: str) -> None:
    result = execute_ok(
        client,
        "explain select id from predicate_data where id=10",
    )
    plan = "\n".join(str(row[0]) for row in result.rows if row)
    marker = f"type={expected_type}"
    if marker not in plan:
        raise GateFailure(f"expected {marker} in access plan:\n{plan}")
    print(f"[PASS] planner selected {expected_type}")


def run_checks(client: WireClient, rows: int) -> int:
    print(f"[SOURCE] postgres/postgres@{POSTGRESQL_COMMIT}")
    allowed_rows, checks = load_large_data(client, rows, DB_DIR / "generated")
    checks += run_postgresql_like_matrix(client)
    checks += run_projection_pruning_regression(client)
    large_checks = build_large_predicate_checks(rows, allowed_rows)
    assert_access_plan(client, "SeqScan")
    checks += 1
    checks += run_large_predicate_checks(client, large_checks, "no-index path")
    create_large_indexes(client)
    assert_access_plan(client, "IndexScan")
    checks += 1
    checks += run_large_predicate_checks(client, large_checks, "indexed path")
    return checks


def stop_server(proc: subprocess.Popen[bytes] | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--rows",
        type=int,
        default=int(os.environ.get("RMDB_PREDICATE_ROWS", DEFAULT_ROWS)),
        help=f"generated predicate rows (default: {DEFAULT_ROWS:,}, minimum: {MIN_ROWS:,})",
    )
    args = parser.parse_args()
    if args.rows < MIN_ROWS:
        parser.error(
            f"--rows must be at least {MIN_ROWS:,}; this gate must not run below "
            "PostgreSQL's tenk regression scale"
        )
    return args


def main() -> int:
    args = parse_args()
    if not SERVER.is_file():
        print(f"[FATAL] server binary not found: {SERVER}", file=sys.stderr)
        return 2
    if port_is_open():
        print(f"[FATAL] 127.0.0.1:{PORT} is already in use", file=sys.stderr)
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
    started = time.monotonic()
    with tempfile.TemporaryFile() as server_log:
        try:
            proc = subprocess.Popen(
                [str(SERVER), str(DB_REL)],
                cwd=BUILD,
                stdout=server_log,
                stderr=subprocess.STDOUT,
            )
            wait_ready(timeout=15.0)
            client = WireClient(timeout=300)
            total = run_checks(client, args.rows)
        except Exception as exc:
            print(f"[FATAL] {exc}", file=sys.stderr)
            server_log.seek(0)
            tail = server_log.read().decode("utf-8", errors="replace")[-6000:]
            if tail.strip():
                print("\nServer log tail:", file=sys.stderr)
                print(tail, file=sys.stderr)
            return 1
        finally:
            if client is not None:
                client.close()
            stop_server(proc)

    elapsed = time.monotonic() - started
    allowed_rows = min(MAX_SUBQUERY_ROWS, (args.rows + 4) // 5)
    print(
        f"\nALL {total} POSTGRESQL-DERIVED PREDICATE ASSERTIONS PASSED "
        f"({args.rows:,} + {allowed_rows:,} rows, {elapsed:.2f}s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
