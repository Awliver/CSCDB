#!/usr/bin/env python3
"""Six-feature correctness gate on sequential-scan and index-scan paths.

The default corpus contains 1,000,000 rows.  Every query is executed before
and after unique indexes are created on the filtering/join key.  The gate
checks an independent expected result, verifies that the two executions are
identical, and requires SeqScan/IndexScan in the corresponding plans.

Run from the repository root:

    python3 -B tests/local/extended_syntax_index_gate.py
    python3 -B tests/local/extended_syntax_index_gate.py --rows 10000
"""

from __future__ import annotations

import argparse
import csv
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("RMDB_BUILD_DIR", ROOT / "build")).resolve()
SERVER = BUILD / "bin" / "rmdb"
DB_REL = Path("test_dbs") / "extended_syntax_index_gate_db"
DB_DIR = BUILD / DB_REL
DEFAULT_ROWS = 1_000_000
MIN_ROWS = 10_000
MAX_ROWS = 5_000_000

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from wire_client import (  # noqa: E402
    PORT,
    SQLTYPE_FLOAT32,
    SQLTYPE_INT32,
    ExecResult,
    WireClient,
    wait_ready,
)


I = SQLTYPE_INT32
F = SQLTYPE_FLOAT32


class GateFailure(AssertionError):
    pass


@dataclass(frozen=True)
class QueryCase:
    label: str
    sql: str
    columns: tuple[tuple[str, int], ...]
    rows: tuple[tuple[object, ...], ...]
    minimum_index_scans: int = 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rows", type=int, default=DEFAULT_ROWS)
    args = parser.parse_args()
    if not MIN_ROWS <= args.rows <= MAX_ROWS:
        parser.error(f"--rows must be in [{MIN_ROWS}, {MAX_ROWS}]")
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


def result_signature(result: ExecResult) -> tuple[tuple[tuple[str, int], ...], tuple[tuple[object, ...], ...]]:
    return tuple(result.columns), tuple(tuple(row) for row in result.rows)


def assert_expected(case: QueryCase, result: ExecResult) -> None:
    actual = result_signature(result)
    expected = (case.columns, case.rows)
    if actual != expected:
        raise GateFailure(
            f"{case.label}: result mismatch\nSQL={case.sql}\n"
            f"expected={expected!r}\nactual={actual!r}"
        )


def plan_lines(client: WireClient, sql: str) -> list[str]:
    result = execute_ok(client, "explain " + sql)
    return [str(row[0]) for row in result.rows if len(row) == 1]


def require_plan(case: QueryCase, lines: Sequence[str], needle: str, minimum: int) -> None:
    count = sum(needle in line for line in lines)
    if count < minimum:
        raise GateFailure(
            f"{case.label}: expected at least {minimum} {needle} node(s), got {count}\n"
            + "\n".join(lines)
        )


def generate_data(directory: Path, rows: int) -> tuple[Path, Path]:
    directory.mkdir(parents=True, exist_ok=True)
    main_path = directory / "extended_syntax_main.csv"
    lookup_path = directory / "extended_syntax_lookup.csv"
    with main_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "grp", "amount"))
        for row_id in range(rows):
            writer.writerow((row_id, row_id % 100, row_id % 1000))
    with lookup_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "payload"))
        # A bounded lookup side keeps outer-join NULL checks practical at the
        # million-row default while the main scan still traverses the complete
        # stress corpus before indexes exist.
        lower = rows // 3
        for row_id in range(lower + (lower % 2), lower + 1000, 2):
            writer.writerow((row_id, row_id * 2))
    return main_path, lookup_path


def integer_rows(values: Sequence[int]) -> tuple[tuple[object, ...], ...]:
    return tuple((value,) for value in values)


def make_cases(rows: int) -> list[QueryCase]:
    lower = rows // 3
    upper = lower + 999
    join_end = lower + 30
    intersect_values = tuple(
        value for value in range(lower + 5, lower + 21) if value % 2 == 0
    )
    except_values = tuple(
        value for value in range(lower, lower + 16) if value % 2 != 0
    )
    even_count = sum(value % 2 == 0 for value in range(lower, upper + 1))
    odd_count = 1000 - even_count
    join_rows = tuple(
        (value, value % 100, value * 2)
        for value in range(lower, join_end + 1)
        if value % 2 == 0
    )
    return [
        QueryCase(
            "SELECT DISTINCT",
            f"select distinct grp from syntax_main where id between {lower} and {upper} "
            "order by grp",
            (("grp", I),),
            integer_rows(range(100)),
        ),
        QueryCase(
            "JOIN USING",
            "select id,m.grp,l.payload from syntax_main m join syntax_lookup l using(id) "
            f"where m.id between {lower} and {join_end} order by id",
            (("id", I), ("grp", I), ("payload", I)),
            join_rows,
        ),
        QueryCase(
            "LIMIT OFFSET",
            f"select id from syntax_main where id between {lower} and {lower + 99} "
            "order by id limit 7 offset 5",
            (("id", I),),
            integer_rows(range(lower + 5, lower + 12)),
        ),
        QueryCase(
            "aggregate DISTINCT",
            "select sum(distinct grp) as s,avg(distinct grp) as a,"
            "count(distinct (grp,amount)) as c from syntax_main "
            f"where id between {lower} and {upper}",
            (("s", I), ("a", F), ("c", I)),
            ((4950, 49.5, 1000),),
        ),
        QueryCase(
            "INTERSECT",
            f"select id from syntax_main where id between {lower} and {lower + 20} "
            "intersect select id from syntax_lookup "
            f"where id between {lower + 5} and {lower + 25} order by id",
            (("id", I),),
            integer_rows(intersect_values),
            minimum_index_scans=2,
        ),
        QueryCase(
            "EXCEPT",
            f"select id from syntax_main where id between {lower} and {lower + 15} "
            "except select id from syntax_lookup "
            f"where id between {lower} and {lower + 15} order by id",
            (("id", I),),
            integer_rows(except_values),
            minimum_index_scans=2,
        ),
        QueryCase(
            "IS NULL",
            "select count(*) as n from syntax_main m left join syntax_lookup l using(id) "
            f"where m.id between {lower} and {upper} and l.payload is null",
            (("n", I),),
            ((odd_count,),),
        ),
        QueryCase(
            "IS NOT NULL",
            "select count(*) as n from syntax_main m left join syntax_lookup l using(id) "
            f"where m.id between {lower} and {upper} and l.payload is not null",
            (("n", I),),
            ((even_count,),),
        ),
    ]


def run_gate(client: WireClient, rows: int, temp_dir: Path) -> None:
    main_path, lookup_path = generate_data(temp_dir, rows)
    started = time.monotonic()
    for sql in (
        "create table syntax_main (id int,grp int,amount int)",
        "create table syntax_lookup (id int,payload int)",
        f"load {main_path} into syntax_main",
        f"load {lookup_path} into syntax_lookup",
    ):
        execute_ok(client, sql)
    print(f"[DATA] loaded {rows:,} + 500 rows in "
          f"{time.monotonic() - started:.2f}s", flush=True)

    cases = make_cases(rows)
    sequential: dict[str, tuple[tuple[tuple[str, int], ...], tuple[tuple[object, ...], ...]]] = {}
    for case in cases:
        result = execute_ok(client, case.sql)
        assert_expected(case, result)
        lines = plan_lines(client, case.sql)
        require_plan(case, lines, "SeqScan", 1)
        if any("IndexScan" in line for line in lines):
            raise GateFailure(f"{case.label}: unexpected IndexScan before indexes\n" + "\n".join(lines))
        sequential[case.label] = result_signature(result)
        print(f"[PASS] {case.label}: sequential path", flush=True)

    execute_ok(client, "create index syntax_main(id)")
    execute_ok(client, "create index syntax_lookup(id)")

    for case in cases:
        result = execute_ok(client, case.sql)
        assert_expected(case, result)
        signature = result_signature(result)
        if signature != sequential[case.label]:
            raise GateFailure(
                f"{case.label}: indexed result differs from sequential result\n"
                f"sequential={sequential[case.label]!r}\nindexed={signature!r}"
            )
        lines = plan_lines(client, case.sql)
        require_plan(case, lines, "IndexScan", case.minimum_index_scans)
        print(f"[PASS] {case.label}: indexed path and equivalence", flush=True)


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
    fatal: str | None = None
    with tempfile.TemporaryDirectory(prefix="rmdb-extended-syntax-", dir="/tmp") as temp_dir:
        with tempfile.TemporaryFile() as server_log:
            try:
                proc = subprocess.Popen(
                    [str(SERVER), str(DB_REL)], cwd=BUILD,
                    stdout=server_log, stderr=subprocess.STDOUT,
                )
                wait_ready(timeout=20.0)
                client = WireClient(timeout=300)
                run_gate(client, args.rows, Path(temp_dir))
            except Exception as exc:
                fatal = str(exc)
                print(f"[FATAL] {fatal}", file=sys.stderr)
            finally:
                if client is not None:
                    client.close()
                stop_server(proc)
            if fatal is not None:
                server_log.seek(0)
                log_tail = server_log.read().decode("utf-8", errors="replace")[-8000:]
                if log_tail.strip():
                    print(f"\nServer log tail:\n{log_tail}", file=sys.stderr)
                return 1

    print(f"\nALL {len(make_cases(args.rows))} QUERY CASES PASSED ON SEQUENTIAL AND INDEX PATHS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
