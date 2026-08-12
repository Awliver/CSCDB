#!/usr/bin/env python3
"""Reproducible random UNION differential gate: RMDB vs PostgreSQL.

Every generated query is executed four ways: RMDB and PostgreSQL before an
index exists, then both engines again after an index is created.  The SQL is
bounded to small result sets, but every branch reads a deterministic relation
whose default size is one million rows.
"""

from __future__ import annotations

import argparse
import csv
import os
import random
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
BUILD = Path(os.environ.get("RMDB_BUILD_DIR", str(ROOT / "build"))).resolve()
SERVER = BUILD / "bin" / "rmdb"
DB_REL = Path("test_dbs") / "union_differential_db"
DB_DIR = BUILD / DB_REL
DEFAULT_ROWS = 1_000_000
DEFAULT_CASES_PER_SEED = 40
DEFAULT_SEEDS = (42, 99, 20260812)

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from postgresql_predicate_differential import (  # noqa: E402
    EphemeralPostgres,
    PostgresClient,
)
from wire_client import PORT, WireClient, wait_ready  # noqa: E402


class DifferentialFailure(AssertionError):
    pass


@dataclass(frozen=True)
class QueryCase:
    seed: int
    case_id: str
    shape: str
    sql: str


def port_is_open() -> bool:
    with socket.socket() as probe:
        probe.settimeout(0.25)
        return probe.connect_ex(("127.0.0.1", PORT)) == 0


def write_data(data_dir: Path, rows: int) -> Path:
    data_dir.mkdir(parents=True, exist_ok=True)
    path = data_dir / "union_differential.csv"
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "keyv", "bucket", "grp"))
        for row_id in range(rows):
            writer.writerow(
                (
                    row_id,
                    (row_id * 7919 + 17) % 200_003,
                    (row_id * 37 + 11) % 97,
                    (row_id * 13 + 5) % 31,
                )
            )
    return path


def rmdb_execute(client: WireClient, sql: str):
    result = client.exec_stream(sql)
    if not result.ok or result.error or result.aborted:
        raise DifferentialFailure(f"RMDB SQL failed:\n{sql}\n{result.diagnostic}")
    return result


def setup_databases(
    rmdb: WireClient, postgres: PostgresClient, table: str, data_path: Path
) -> None:
    rmdb_execute(
        rmdb, f"create table {table} (id int,keyv int,bucket int,grp int)"
    )
    rmdb_execute(rmdb, f"load {data_path} into {table}")
    postgres.run(
        f"drop table if exists {table};\n"
        f"create table {table} "
        "(id integer,keyv integer,bucket integer,grp integer);\n"
        f"\\copy {table} from '{data_path}' with (format csv, header true)\n"
    )


def create_indexes(rmdb: WireClient, postgres: PostgresClient, table: str) -> None:
    rmdb_execute(rmdb, f"create index {table} (id)")
    postgres.run(f"create index {table}_id_idx on {table}(id);\n")


def count_sql(set_expression: str) -> str:
    return f"select count(*) as n from ({set_expression}) u"


def edge_cases(table: str, rows: int) -> list[QueryCase]:
    high = rows - 1
    middle = rows // 2
    expressions = [
        (
            "bare-distinct",
            f"select bucket as v from {table} a where id between 0 and 999 "
            f"union select bucket from {table} b where id between 500 and 1499",
        ),
        (
            "explicit-distinct",
            f"select keyv as v from {table} a where id between {middle} and {middle + 511} "
            f"union distinct select keyv from {table} b where id between {middle + 256} and {middle + 767}",
        ),
        (
            "all",
            f"select bucket as v from {table} a where id in (1,2,3,5,8,13,21) "
            f"union all select bucket from {table} b where id in (1,2,3,5,8,13,21)",
        ),
        (
            "left-associated-mixed",
            f"select bucket as v from {table} a where id between 0 and 511 "
            f"union all select bucket from {table} b where id between 256 and 767 "
            f"union select bucket from {table} c where id between 512 and 1023",
        ),
        (
            "right-parenthesized-mixed",
            f"select bucket as v from {table} a where id between 0 and 511 "
            f"union all (select bucket from {table} b where id between 256 and 767 "
            f"union select bucket from {table} c where id between 512 and 1023)",
        ),
        (
            "branch-order-limit",
            f"(select keyv as v from {table} a where id between 0 and 2047 order by 1 desc limit 73) "
            f"union all (select keyv from {table} b where id between {high - 2047} and {high} order by 1 limit 61)",
        ),
        (
            "set-order-limit",
            f"select keyv as v from {table} a where id between 0 and 2047 "
            f"union select keyv from {table} b where id between {high - 2047} and {high} "
            "order by 1 desc limit 127",
        ),
        (
            "multi-column",
            f"select bucket as v,grp as g from {table} a where id between 0 and 2047 "
            f"union all select bucket,grp from {table} b where id between {high - 2047} and {high}",
        ),
    ]
    return [
        QueryCase(-1, f"edge-{index:02d}", shape, count_sql(expression))
        for index, (shape, expression) in enumerate(expressions)
    ]


class UnionGenerator:
    PROJECTIONS = (
        ("id as v", "id", "single-id"),
        ("keyv as v", "keyv", "single-key"),
        ("bucket as v", "bucket", "single-duplicate"),
        ("bucket as v,grp as g", "bucket,grp", "pair-duplicate"),
    )

    def __init__(self, seed: int, rows: int, table: str) -> None:
        self.random = random.Random(seed)
        self.rows = rows
        self.table = table

    def predicate(self) -> str:
        width = self.random.randint(0, min(2048, self.rows - 1))
        low = self.random.randint(0, self.rows - width - 1)
        high = low + width
        kind = self.random.randrange(4)
        if kind == 0:
            predicate = f"id between {low} and {high}"
        elif kind == 1:
            predicate = f"id>={low} and id<={high}"
        elif kind == 2:
            values = sorted(
                {self.random.randint(low, high) for _ in range(self.random.randint(1, 12))}
            )
            predicate = f"id in ({','.join(map(str, values))})"
        else:
            predicate = f"id between {low} and {high} and bucket between 17 and 73"
        if self.random.random() < 0.35:
            bucket = self.random.randrange(97)
            group = self.random.randrange(31)
            predicate += f" and (bucket={bucket} or grp={group})"
        return predicate

    def branch(self, select_list: str, alias: str, *, tail: bool = False) -> str:
        sql = f"select {select_list} from {self.table} {alias} where {self.predicate()}"
        if tail:
            sql += f" order by 1 {self.random.choice(('asc', 'desc'))} limit {self.random.randint(1, 128)}"
            return f"({sql})"
        return sql

    def case(self, seed: int, index: int) -> QueryCase:
        first_projection, later_projection, projection_shape = self.random.choice(
            self.PROJECTIONS
        )
        shape = self.random.randrange(7)
        a = self.branch(first_projection, "a", tail=shape == 4)
        b = self.branch(later_projection, "b", tail=shape == 4)
        c = self.branch(later_projection, "c")
        if shape == 0:
            expression = f"{a} union {b}"
            shape_name = "binary-distinct"
        elif shape == 1:
            expression = f"{a} union distinct {b}"
            shape_name = "explicit-distinct"
        elif shape == 2:
            expression = f"{a} union all {b}"
            shape_name = "binary-all"
        elif shape == 3:
            expression = f"{a} union all {b} union {c}"
            shape_name = "left-mixed"
        elif shape == 4:
            expression = f"{a} union all {b}"
            shape_name = "branch-tail"
        elif shape == 5:
            expression = f"{a} union all ({b} union {c})"
            shape_name = "right-group"
        else:
            expression = (
                f"{a} union {b} order by 1 "
                f"{self.random.choice(('asc', 'desc'))} limit {self.random.randint(1, 256)}"
            )
            shape_name = "set-tail"
        return QueryCase(
            seed,
            f"seed-{seed}-case-{index:03d}",
            f"{shape_name}/{projection_shape}",
            count_sql(expression),
        )


def build_cases(
    seeds: Sequence[int], cases_per_seed: int, rows: int, table: str
) -> list[QueryCase]:
    cases = edge_cases(table, rows)
    for seed in seeds:
        generator = UnionGenerator(seed, rows, table)
        cases.extend(generator.case(seed, index) for index in range(cases_per_seed))
    return cases


def rmdb_counts(client: WireClient, cases: Sequence[QueryCase]) -> list[int]:
    values: list[int] = []
    for case in cases:
        result = rmdb_execute(client, case.sql)
        if len(result.rows) != 1 or len(result.rows[0]) != 1:
            raise DifferentialFailure(
                f"{case.case_id} returned non-scalar result: {result.rows!r}\n{case.sql}"
            )
        values.append(int(result.rows[0][0]))
    return values


def postgres_counts(
    postgres: PostgresClient, cases: Sequence[QueryCase]
) -> list[int]:
    statements = [
        f"select {index},q.n from ({case.sql}) q;"
        for index, case in enumerate(cases)
    ]
    output = postgres.run("\n".join(statements), tuples_only=True)
    values: list[int | None] = [None] * len(cases)
    for line in output.splitlines():
        if not line.strip():
            continue
        fields = line.split("\t")
        if len(fields) != 2:
            raise DifferentialFailure(f"unexpected PostgreSQL output line: {line!r}")
        values[int(fields[0])] = int(fields[1])
    if any(value is None for value in values):
        missing = [index for index, value in enumerate(values) if value is None]
        raise DifferentialFailure(f"PostgreSQL omitted results: {missing[:20]}")
    return [int(value) for value in values]


def compare(
    label: str,
    cases: Sequence[QueryCase],
    rmdb_values: Sequence[int],
    postgres_values: Sequence[int],
) -> None:
    mismatches = [
        (case, rmdb_value, pg_value)
        for case, rmdb_value, pg_value in zip(cases, rmdb_values, postgres_values)
        if rmdb_value != pg_value
    ]
    if mismatches:
        details = []
        for case, rmdb_value, pg_value in mismatches[:10]:
            details.append(
                f"{case.case_id} seed={case.seed} shape={case.shape}: "
                f"RMDB={rmdb_value}, PostgreSQL={pg_value}\nSQL: {case.sql}"
            )
        raise DifferentialFailure(
            f"{label}: {len(mismatches)} mismatches\n" + "\n".join(details)
        )
    print(f"[PASS] {label}: {len(cases)} random/edge queries agree")


def representative_sql(table: str) -> str:
    return count_sql(
        f"select keyv as v from {table} a where id=14 "
        f"union all select keyv from {table} b where id=21"
    )


def assert_plan(client: WireClient, table: str, *, indexed: bool) -> None:
    result = rmdb_execute(client, f"explain {representative_sql(table)}")
    plan = "\n".join(str(row[0]) for row in result.rows if row)
    index_scans = plan.count("IndexScan")
    if indexed and index_scans < 2:
        raise DifferentialFailure(
            f"indexed UNION did not use both index branches:\n{plan}"
        )
    if not indexed and index_scans:
        raise DifferentialFailure(f"no-index phase unexpectedly used IndexScan:\n{plan}")
    expected = "indexed" if indexed else "no-index"
    print(f"[PASS] {expected} representative UNION plan")


def stop_rmdb(proc: subprocess.Popen[bytes] | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def parse_seeds(value: str) -> tuple[int, ...]:
    try:
        seeds = tuple(int(item.strip()) for item in value.split(",") if item.strip())
    except ValueError as exc:
        raise argparse.ArgumentTypeError("seeds must be comma-separated integers") from exc
    if not seeds:
        raise argparse.ArgumentTypeError("at least one seed is required")
    return seeds


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rows", type=int, default=DEFAULT_ROWS)
    parser.add_argument("--cases-per-seed", type=int, default=DEFAULT_CASES_PER_SEED)
    parser.add_argument(
        "--seeds",
        type=parse_seeds,
        default=DEFAULT_SEEDS,
        help="comma-separated deterministic random seeds",
    )
    parser.add_argument(
        "--postgres-dsn",
        default=os.environ.get("RMDB_POSTGRES_DSN", ""),
        help="existing PostgreSQL DSN; default starts an isolated temporary cluster",
    )
    args = parser.parse_args()
    if args.rows < 10_000:
        parser.error("--rows must be at least 10000")
    if args.rows > 5_000_000:
        parser.error("--rows must not exceed 5000000")
    if args.cases_per_seed < 1:
        parser.error("--cases-per-seed must be positive")
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
        print(f"[FATAL] unsafe RMDB path: {DB_DIR}", file=sys.stderr)
        return 2
    expected_parent.mkdir(parents=True, exist_ok=True)
    if DB_DIR.exists():
        shutil.rmtree(DB_DIR)
    DB_DIR.mkdir()

    suffix = f"{os.getpid()}_{abs(args.seeds[0]) % 1_000_000}"
    table = f"udiff_data_{suffix}"
    cases = build_cases(args.seeds, args.cases_per_seed, args.rows, table)
    data_path = write_data(DB_DIR / "generated", args.rows)

    ephemeral: EphemeralPostgres | None = None
    rmdb_proc: subprocess.Popen[bytes] | None = None
    rmdb: WireClient | None = None
    postgres: PostgresClient | None = None
    started = time.monotonic()
    with tempfile.TemporaryFile() as rmdb_log:
        try:
            if args.postgres_dsn:
                dsn = args.postgres_dsn
                pg_mode = "external DSN"
            else:
                ephemeral = EphemeralPostgres()
                ephemeral.start()
                dsn = ephemeral.dsn
                pg_mode = f"ephemeral PostgreSQL on port {ephemeral.port}"
            postgres = PostgresClient(dsn)

            rmdb_proc = subprocess.Popen(
                [str(SERVER), str(DB_REL)],
                cwd=BUILD,
                stdout=rmdb_log,
                stderr=subprocess.STDOUT,
            )
            wait_ready(timeout=15.0)
            rmdb = WireClient(timeout=180)
            setup_databases(rmdb, postgres, table, data_path)
            print(
                f"[DATA] {args.rows:,} rows, {len(cases)} UNION queries, "
                f"seeds={','.join(map(str, args.seeds))}, {pg_mode}"
            )

            assert_plan(rmdb, table, indexed=False)
            rmdb_no_index = rmdb_counts(rmdb, cases)
            pg_no_index = postgres_counts(postgres, cases)
            compare("no-index RMDB/PostgreSQL differential", cases, rmdb_no_index, pg_no_index)

            create_indexes(rmdb, postgres, table)
            assert_plan(rmdb, table, indexed=True)
            rmdb_indexed = rmdb_counts(rmdb, cases)
            pg_indexed = postgres_counts(postgres, cases)
            compare("indexed RMDB/PostgreSQL differential", cases, rmdb_indexed, pg_indexed)

            if rmdb_no_index != rmdb_indexed:
                changed = [
                    cases[index].case_id
                    for index, (left, right) in enumerate(zip(rmdb_no_index, rmdb_indexed))
                    if left != right
                ]
                raise DifferentialFailure(
                    f"RMDB results changed after index creation: {changed[:20]}"
                )
            if pg_no_index != pg_indexed:
                raise DifferentialFailure("PostgreSQL results changed after index creation")
            print(f"[PASS] four-way access-path invariance: {len(cases)} queries")
        except Exception as exc:
            print(f"[FATAL] {exc}", file=sys.stderr)
            rmdb_log.seek(0)
            tail = rmdb_log.read().decode("utf-8", errors="replace")[-8000:]
            if tail.strip():
                print("\nRMDB log tail:", file=sys.stderr)
                print(tail, file=sys.stderr)
            if ephemeral is not None and ephemeral.log.exists():
                pg_tail = ephemeral.log.read_text(encoding="utf-8", errors="replace")[-4000:]
                if pg_tail.strip():
                    print("\nPostgreSQL log tail:", file=sys.stderr)
                    print(pg_tail, file=sys.stderr)
            return 1
        finally:
            if postgres is not None:
                try:
                    postgres.run(f"drop table if exists {table};\n")
                except Exception:
                    pass
            if rmdb is not None:
                rmdb.close()
            stop_rmdb(rmdb_proc)
            if ephemeral is not None:
                ephemeral.close()

    elapsed = time.monotonic() - started
    print(
        f"\nALL {len(cases)} UNION SQL CASES PASSED ON FOUR PATHS "
        f"({len(cases) * 4} executions, {elapsed:.2f}s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
