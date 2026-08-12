#!/usr/bin/env python3
"""Traditional SQL UNION and PostgreSQL-derived large-data regression gate.

Run from the repository root:

    python3 -B tests/local/union_query_expression_gate.py
"""

from __future__ import annotations

import argparse
import csv
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Callable, Sequence


ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("RMDB_BUILD_DIR", str(ROOT / "build"))).resolve()
SERVER = BUILD / "bin" / "rmdb"
DB_REL = Path("test_dbs") / "union_query_expression_gate_db"
DB_DIR = BUILD / DB_REL
UPSTREAM_COMMIT_FILE = ROOT / "tests" / "postgresql_regress" / "UPSTREAM_COMMIT"
DEFAULT_ROWS = 1_000_000
MIN_ROWS = 10_000
MAX_ROWS = 5_000_000
SECONDARY_ROWS = 10_000

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from postgresql_union_cases import (  # noqa: E402
    LARGE_UNION_CASES,
    POSTGRESQL_COMMIT,
    SIMPLE_UNION_CASES,
)
from wire_client import (  # noqa: E402
    PORT,
    SQLTYPE_CHAR,
    SQLTYPE_FLOAT32,
    SQLTYPE_INT32,
    ExecResult,
    WireClient,
    wait_ready,
)


I = SQLTYPE_INT32
F = SQLTYPE_FLOAT32
S = SQLTYPE_CHAR


class GateFailure(AssertionError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--rows",
        type=int,
        default=DEFAULT_ROWS,
        help=f"large PostgreSQL-derived relation size (default: {DEFAULT_ROWS})",
    )
    args = parser.parse_args()
    if args.rows < MIN_ROWS:
        parser.error(f"--rows must be at least {MIN_ROWS} (PostgreSQL tenk scale)")
    if args.rows > MAX_ROWS:
        parser.error(
            f"--rows must not exceed {MAX_ROWS}; the gate materializes UNION inputs"
        )
    return args


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


def execute_ok(client: WireClient, sql: str) -> ExecResult:
    result = client.exec_stream(sql)
    if result.error or result.aborted or not result.ok:
        raise GateFailure(f"SQL failed: {sql}\n{result.diagnostic}")
    return result


def assert_query(
    client: WireClient,
    label: str,
    sql: str,
    columns: Sequence[tuple[str, int]],
    rows: Sequence[Sequence[object]],
) -> None:
    result = execute_ok(client, sql)
    expected_columns = list(columns)
    expected_rows = [list(row) for row in rows]
    if result.columns != expected_columns or result.rows != expected_rows:
        raise GateFailure(
            f"{label}: result mismatch\nSQL={sql}\n"
            f"expected columns={expected_columns!r}, rows={expected_rows!r}\n"
            f"actual columns={result.columns!r}, rows={result.rows!r}"
        )


def assert_error(client: WireClient, label: str, sql: str) -> None:
    result = client.exec_stream(sql)
    if not (result.error or result.aborted):
        raise GateFailure(f"{label}: expected rejection, got rows={result.rows!r}")


def assert_count(client: WireClient, label: str, sql: str, expected: int) -> None:
    assert_query(client, label, sql, [("n", I)], [[expected]])


def float32(value: float) -> float:
    return struct.unpack("!f", struct.pack("!f", value))[0]


def generate_postgresql_data(data_dir: Path, rows: int) -> tuple[Path, Path]:
    """Generate scaled tenk-style cardinalities without a runtime download."""
    data_dir.mkdir(parents=True, exist_ok=True)
    large_path = data_dir / "postgresql_union_large.csv"
    secondary_path = data_dir / "postgresql_union_secondary.csv"
    words = ("alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta")

    with large_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(
            ("id", "unique1", "fivethous", "four", "ten", "score", "amount", "txt")
        )
        for row_id in range(rows):
            writer.writerow(
                (
                    row_id,
                    row_id,
                    row_id % 5_000,
                    row_id % 4,
                    row_id % 10,
                    (row_id * 37) % 200_001 - 100_000,
                    f"{float(row_id % 75_000):.1f}",
                    words[(row_id * 11 + 3) % len(words)],
                )
            )

    # Half overlaps unique1's high end and half extends beyond it. This makes
    # DISTINCT cardinality and ALL multiplicity independently observable.
    secondary_start = rows - SECONDARY_ROWS // 2
    with secondary_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "unique1", "ten", "txt"))
        for row_id in range(SECONDARY_ROWS):
            writer.writerow(
                (
                    row_id,
                    secondary_start + row_id,
                    row_id % 10,
                    words[(row_id * 5 + 1) % len(words)],
                )
            )
    return large_path, secondary_path


def load_postgresql_data(
    client: WireClient, rows: int, data_dir: Path
) -> None:
    large_path, secondary_path = generate_postgresql_data(data_dir, rows)
    started = time.monotonic()
    for sql in [
        "create table pgu_i (v int)",
        "create table pgu_f (v float)",
        "insert into pgu_i values (1)",
        "insert into pgu_i values (2)",
        "insert into pgu_i values (3)",
        "insert into pgu_f values (1.0)",
        "insert into pgu_f values (1.1)",
        "insert into pgu_f values (2.2)",
        "create table pgu_large "
        "(id int,unique1 int,fivethous int,four int,ten int,score int,"
        "amount float,txt char(16))",
        "create table pgu_secondary (id int,unique1 int,ten int,txt char(24))",
        f"load {large_path} into pgu_large",
        f"load {secondary_path} into pgu_secondary",
    ]:
        execute_ok(client, sql)
    elapsed = time.monotonic() - started
    assert_count(
        client,
        "PostgreSQL large relation row count",
        "select count(*) as n from pgu_large",
        rows,
    )
    assert_count(
        client,
        "PostgreSQL secondary relation row count",
        "select count(*) as n from pgu_secondary",
        SECONDARY_ROWS,
    )
    print(
        f"[DATA] PostgreSQL {POSTGRESQL_COMMIT[:12]} UNION corpus: "
        f"{rows:,} + {SECONDARY_ROWS:,} rows loaded in {elapsed:.2f}s"
    )


class Checks:
    def __init__(self) -> None:
        self.total = 0
        self.failures: list[tuple[str, str]] = []

    def run(self, label: str, check: Callable[[], None]) -> None:
        self.total += 1
        try:
            check()
        except Exception as exc:
            self.failures.append((label, str(exc)))
            print(f"[FAIL] {label}\n{exc}")
        else:
            print(f"[PASS] {label}")


def run_checks(client: WireClient, rows: int, data_dir: Path) -> Checks:
    checks = Checks()
    setup = [
        "create table ua (v int)",
        "create table ub (v int)",
        "create table uc (v int)",
        "create table labels (v int,label char(6))",
        "create table ut_i (k int,n int,s char(3))",
        "create table ut_f (k int,n float,s char(8))",
        "create table uzp (v float)",
        "create table uzn (v float)",
        "create table ut_o (k int,n int)",
        "insert into ua values (1)",
        "insert into ua values (1)",
        "insert into ua values (2)",
        "insert into ua values (5)",
        "insert into ub values (1)",
        "insert into ub values (3)",
        "insert into ub values (3)",
        "insert into uc values (1)",
        "insert into uc values (4)",
        "insert into labels values (1,'one')",
        "insert into labels values (3,'three')",
        "insert into labels values (5,'five')",
        "insert into ut_i values (1,10,'abc')",
        "insert into ut_i values (2,20,'xy')",
        "insert into ut_f values (1,10.0,'abc')",
        "insert into ut_f values (3,30.5,'longtext')",
        "insert into uzp values (0.0)",
        "insert into uzn values (-0.0)",
        "insert into ut_o values (1,40)",
        "insert into ut_o values (2,10)",
        "insert into ut_o values (3,30)",
    ]
    for sql in setup:
        execute_ok(client, sql)

    load_postgresql_data(client, rows, data_dir)

    def postgresql_simple_cases() -> None:
        for case in SIMPLE_UNION_CASES:
            output_type = I if case.output_type == "int" else F
            expected = []
            for row in case.expected_rows:
                expected.append(
                    [float32(value) if case.output_type == "float" else value for value in row]
                )
            assert_query(
                client,
                f"PostgreSQL union.sql:{case.line} {case.case_id}",
                case.rmdb_sql,
                [(case.output_name, output_type)],
                expected,
            )

    checks.run(
        f"PostgreSQL official simple UNION matrix ({len(SIMPLE_UNION_CASES)} cases)",
        postgresql_simple_cases,
    )

    def postgresql_large_cases() -> None:
        # union.sql:127-129 and 156-158. unique1 covers [0, rows), while
        # fivethous is a duplicate-heavy subset of that domain.
        assert_count(
            client,
            "PostgreSQL tenk UNION cardinality",
            "select count(*) as n from "
            "(select unique1 from pgu_large union "
            "select fivethous from pgu_large) ss",
            rows,
        )
        # union.sql:46-63: same input under set and bag semantics, plus numeric
        # type promotion. amount's domain is a subset of unique1.
        assert_count(
            client,
            "PostgreSQL same-table UNION DISTINCT",
            "select count(*) as n from "
            "(select unique1 as v from pgu_large union "
            "select unique1 from pgu_large) ss",
            rows,
        )
        assert_count(
            client,
            "PostgreSQL same-table UNION ALL",
            "select count(*) as n from "
            "(select unique1 as v from pgu_large union all "
            "select unique1 from pgu_large) ss",
            rows * 2,
        )
        assert_count(
            client,
            "PostgreSQL large INT/FLOAT promotion",
            "select count(*) as n from "
            "(select amount as v from pgu_large union "
            "select unique1 from pgu_large) ss",
            rows,
        )
        # The 10k secondary relation overlaps 5k keys and contributes 5k new
        # keys, so DISTINCT and ALL have deliberately different oracles.
        assert_count(
            client,
            "PostgreSQL overlapping relations DISTINCT",
            "select count(*) as n from "
            "(select unique1 from pgu_large union "
            "select unique1 from pgu_secondary) ss",
            rows + SECONDARY_ROWS // 2,
        )
        assert_count(
            client,
            "PostgreSQL overlapping relations ALL",
            "select count(*) as n from "
            "(select unique1 from pgu_large union all "
            "select unique1 from pgu_secondary) ss",
            rows + SECONDARY_ROWS,
        )
        assert_count(
            client,
            "PostgreSQL full-row duplicate elimination",
            "select count(*) as n from "
            "(select four,ten from pgu_large union "
            "select four,ten from pgu_large) ss",
            20,
        )
        # union.sql:463-488, adapted from WHERE 1=2 to the equivalent id<0.
        assert_query(
            client,
            "PostgreSQL empty UNION input",
            "select unique1 as four from pgu_large where id<0 union "
            "select four from pgu_large order by 1",
            [("four", I)],
            [[0], [1], [2], [3]],
        )
        assert_count(
            client,
            "PostgreSQL all UNION inputs empty",
            "select count(*) as n from "
            "(select unique1 from pgu_large where id<0 union "
            "select four from pgu_large where id<0 union "
            "select ten from pgu_large where id<0) ss",
            0,
        )
        # union.sql:542-586 and 672-676: derived UNION qualification and join.
        assert_count(
            client,
            "PostgreSQL predicate over derived UNION",
            "select count(*) as n from "
            "(select unique1 from pgu_large union "
            "select fivethous from pgu_large) ss where ss.unique1<1000",
            1000,
        )
        expected_tail = [[value] for value in range(rows - 5, rows + 5)]
        assert_query(
            client,
            "PostgreSQL ordered derived UNION limit",
            "select ss.unique1 from "
            "(select unique1 from pgu_large union "
            "select unique1 from pgu_secondary) ss "
            f"where ss.unique1>={rows - 5} order by ss.unique1 limit 10",
            [("unique1", I)],
            expected_tail,
        )
        assert_count(
            client,
            "PostgreSQL UNION derived relation join",
            "select count(*) as n from pgu_large t join "
            "(select ten from pgu_large union select ten from pgu_secondary) s "
            "on s.ten=t.unique1",
            10,
        )
        assert_query(
            client,
            "PostgreSQL CHAR width promotion",
            "select txt as label from pgu_large union "
            "select txt from pgu_secondary order by label",
            [("label", S)],
            [[word] for word in ("alpha", "beta", "delta", "epsilon", "eta", "gamma", "zeta")],
        )

    checks.run(
        f"PostgreSQL official large-data UNION adaptations "
        f"({len(LARGE_UNION_CASES)} source groups, {rows:,} rows)",
        postgresql_large_cases,
    )

    def postgresql_indexed_branches() -> None:
        execute_ok(client, "create index pgu_large (unique1)")
        execute_ok(client, "create index pgu_secondary (unique1)")
        lower = rows - 1000
        assert_count(
            client,
            "indexed large UNION branches",
            "select count(*) as n from "
            f"(select unique1 from pgu_large where unique1>={lower} union "
            f"select unique1 from pgu_secondary where unique1>={lower}) ss",
            6000,
        )
        explain = execute_ok(
            client,
            f"explain select unique1 from pgu_large where unique1>={lower} union "
            f"select unique1 from pgu_secondary where unique1>={lower}",
        )
        plan_text = "\n".join(str(row[0]) for row in explain.rows)
        if "UnionDistinct" not in plan_text or plan_text.count("IndexScan") < 2:
            raise GateFailure(f"indexed UNION EXPLAIN mismatch:\n{plan_text}")

    checks.run("PostgreSQL large UNION indexed branch equivalence", postgresql_indexed_branches)

    def quantifiers() -> None:
        assert_query(
            client, "bare UNION", "select v from ua union select v from ub order by v",
            [("v", I)], [[1], [2], [3], [5]],
        )
        assert_query(
            client, "UNION DISTINCT",
            "select v from ua union distinct select v from ub order by v",
            [("v", I)], [[1], [2], [3], [5]],
        )
        assert_query(
            client, "UNION ALL", "select v from ua union all select v from ub order by v",
            [("v", I)], [[1], [1], [1], [2], [3], [3], [5]],
        )

    checks.run("top-level UNION / DISTINCT / ALL", quantifiers)

    def mixed_and_parenthesized() -> None:
        assert_query(
            client, "DISTINCT then ALL",
            "select v from ua union select v from ub union all select v from uc order by v",
            [("v", I)], [[1], [1], [2], [3], [4], [5]],
        )
        assert_query(
            client, "ALL then DISTINCT",
            "select v from ua union all select v from ub union select v from uc order by v",
            [("v", I)], [[1], [2], [3], [4], [5]],
        )
        assert_query(
            client, "right parenthesized set expression",
            "select v from ua union all (select v from ub union select v from uc) order by v",
            [("v", I)], [[1], [1], [1], [2], [3], [4], [5]],
        )

    checks.run("mixed quantifiers retain left association and parentheses", mixed_and_parenthesized)

    def ordering_and_limits() -> None:
        assert_query(
            client, "global ordinal order and limit",
            "select v from ua union all select v from ub order by 1 desc limit 4",
            [("v", I)], [[5], [3], [3], [2]],
        )
        assert_query(
            client, "branch-local order and limit",
            "(select v from ua order by v desc limit 2) union all "
            "(select v from ub order by v asc limit 2) order by v",
            [("v", I)], [[1], [2], [3], [5]],
        )
        assert_query(
            client, "parenthesized UNION keeps its own tail",
            "(select v from ua union all select v from ub order by v desc limit 4) "
            "union select v from uc order by v",
            [("v", I)], [[1], [2], [3], [4], [5]],
        )
        assert_query(
            client, "limit zero keeps metadata",
            "select v from ua union all select v from ub limit 0",
            [("v", I)], [],
        )

    checks.run("query-level and branch-local ORDER BY/LIMIT", ordering_and_limits)

    def derived_tables() -> None:
        assert_query(
            client, "derived WHERE",
            "select d.v from (select v from ua union all select v from ub) as d "
            "where d.v>=3 order by d.v",
            [("v", I)], [[3], [3], [5]],
        )
        assert_query(
            client, "derived alias without AS",
            "select d.v as picked from (select v from ua union select v from ub) d "
            "where d.v>1 order by picked desc limit 2",
            [("picked", I)], [[5], [3]],
        )
        assert_query(
            client, "derived GROUP BY",
            "select d.v,count(*) as n from "
            "(select v from ua union all select v from ub) d "
            "group by d.v having count(*)>1 order by d.v",
            [("v", I), ("n", I)], [[1, 3], [3, 2]],
        )
        assert_query(
            client, "derived JOIN",
            "select d.v,l.label from (select v from ua union select v from ub) d "
            "join labels l on d.v=l.v order by d.v",
            [("v", I), ("label", S)], [[1, "one"], [3, "three"], [5, "five"]],
        )
        assert_query(
            client, "correlated LATERAL UNION",
            "select a.label,x.v from labels a cross join lateral "
            "(select b.v from ub b where b.v=a.v union all "
            "select c.v from uc c where c.v=a.v) x order by a.label,x.v",
            [("label", S), ("v", I)],
            [["one", 1], ["one", 1], ["three", 3], ["three", 3]],
        )

    checks.run("UNION as a general derived table", derived_tables)

    def schema_and_subqueries() -> None:
        assert_query(
            client, "schema promotion and first-branch names",
            "select k as first_k,n as amount,s as text_value from ut_i "
            "union distinct select k as later_k,n as later_amount,s as later_text from ut_f "
            "order by first_k",
            [("first_k", I), ("amount", F), ("text_value", S)],
            [[1, 10.0, "abc"], [2, 20.0, "xy"], [3, 30.5, "longtext"]],
        )
        assert_query(
            client, "UNION in IN predicate",
            "select v from ua where v in "
            "(select v from ub union select v from uc) order by v",
            [("v", I)], [[1], [1]],
        )
        assert_query(
            client, "correlated UNION in EXISTS",
            "select a.label from labels a where exists "
            "(select b.v from ub b where b.v=a.v union "
            "select c.v from uc c where c.v=a.v) order by a.label",
            [("label", S)], [["one"], ["three"]],
        )
        assert_query(
            client, "parenthesized correlated SELECT",
            "select a.v from ua a where exists "
            "((select b.v from ub b where b.v=a.v)) order by a.v",
            [("v", I)], [[1], [1]],
        )
        assert_query(
            client, "ordinal sort with duplicate output names",
            "select k as x,n as x from ut_o where k<3 "
            "union all select k,n from ut_o where k=3 "
            "order by 2 desc,1 asc",
            [("x", I), ("x", I)],
            [[1, 40], [3, 30], [2, 10]],
        )
        assert_query(
            client, "numeric set equality for signed zero",
            "select v from uzp union select v from uzn",
            [("v", F)], [[0.0]],
        )
        assert_query(
            client, "NULL set equality and mask propagation",
            "select l.label as x from uc c left join labels l on c.v=l.v "
            "union select l.label as y from uc c left join labels l on c.v=l.v "
            "order by x",
            [("x", S)], [["one"], [None]],
        )
        assert_query(
            client, "widened leading CHAR preserves following offsets",
            "select s as txt,k as key from ut_i union all "
            "select s,k from ut_f order by key",
            [("txt", S), ("key", I)],
            [["abc", 1], ["abc", 1], ["xy", 2], ["longtext", 3]],
        )
        assert_query(
            client, "aggregate SELECT operands",
            "select v,count(*) as n from ua group by v union all "
            "select v,count(*) as n from ub group by v order by v,n",
            [("v", I), ("n", I)],
            [[1, 1], [1, 2], [2, 1], [3, 2], [5, 1]],
        )
        explain = execute_ok(client, "explain select v from ua union all select v from ub")
        if explain.columns != [("QUERY PLAN", S)] or not any(
            "UnionAll" in str(row[0]) for row in explain.rows
        ):
            raise GateFailure(f"EXPLAIN UNION mismatch: {explain!r}")

    checks.run("schema promotion, names, and predicate subquery", schema_and_subqueries)

    def invalid_contracts() -> None:
        invalid = [
            ("column count", "select v from ua union select k,n from ut_i"),
            ("incompatible type", "select v from ua union select s from ut_i"),
            ("unknown final order", "select v from ua union select v from ub order by missing"),
            ("later alias invisible",
             "select v as first_name from ua union select v as second_name from ub "
             "order by second_name"),
            ("mixed modifiers", "select v from ua union all distinct select v from ub"),
            ("unparenthesized branch order",
             "select v from ua order by v limit 1 union all select v from ub"),
            ("ordinal out of range", "select v from ua union select v from ub order by 2"),
            ("zero ordinal", "select v from ua union select v from ub order by 0"),
            ("ambiguous output name",
             "select k as x,n as x from ut_i union all select k,n from ut_f order by x"),
            ("negative limit", "select v from ua union select v from ub limit -1"),
        ]
        for label, sql in invalid:
            assert_error(client, label, sql)
        assert_query(
            client, "post-error control", "select v from uc order by v",
            [("v", I)], [[1], [4]],
        )

    checks.run("invalid UNION contracts and recovery", invalid_contracts)
    return checks


def main() -> int:
    args = parse_args()
    pinned_commit = UPSTREAM_COMMIT_FILE.read_text(encoding="utf-8").strip()
    if pinned_commit != POSTGRESQL_COMMIT:
        print(
            f"[FATAL] PostgreSQL provenance mismatch: {pinned_commit!r} != "
            f"{POSTGRESQL_COMMIT!r}",
            file=sys.stderr,
        )
        return 2
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
    checks: Checks | None = None
    fatal: str | None = None
    with tempfile.TemporaryDirectory(prefix="rmdb-pg-union-", dir="/tmp") as temp_dir:
        with tempfile.TemporaryFile() as server_log:
            try:
                proc = subprocess.Popen(
                    [str(SERVER), str(DB_REL)], cwd=BUILD,
                    stdout=server_log, stderr=subprocess.STDOUT,
                )
                wait_ready(timeout=15.0)
                client = WireClient(timeout=180)
                checks = run_checks(client, args.rows, Path(temp_dir))
            except Exception as exc:
                fatal = str(exc)
                print(f"[FATAL] {fatal}", file=sys.stderr)
            finally:
                if client is not None:
                    client.close()
                stop_server(proc)

            if fatal is not None or checks is None or checks.failures:
                server_log.seek(0)
                log_tail = server_log.read().decode("utf-8", errors="replace")[-6000:]
                if checks is not None:
                    passed = checks.total - len(checks.failures)
                    print(f"\nUNION gate: {passed}/{checks.total} PASS", file=sys.stderr)
                if log_tail.strip():
                    print(f"\nServer log tail:\n{log_tail}", file=sys.stderr)
                return 1

    print(f"\nUNION gate: {checks.total}/{checks.total} PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
