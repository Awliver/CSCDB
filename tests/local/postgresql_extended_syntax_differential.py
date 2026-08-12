#!/usr/bin/env python3
"""Reproducible six-feature random SQL differential: RMDB vs PostgreSQL.

The primary relation contains 1,000,000 rows by default.  Every generated SQL
case runs against both engines before and after indexes are created.  The six
feature groups are SELECT DISTINCT, JOIN USING, LIMIT/OFFSET, DISTINCT
aggregates, INTERSECT/EXCEPT, and IS NULL/IS NOT NULL.

Examples:

    python3 -B tests/local/postgresql_extended_syntax_differential.py
    python3 -B tests/local/postgresql_extended_syntax_differential.py \
        --rows 10000 --rounds 2 --seeds 42
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("RMDB_BUILD_DIR", str(ROOT / "build"))).resolve()
SERVER = BUILD / "bin" / "rmdb"
DB_REL = Path("test_dbs") / "extended_syntax_differential_db"
DB_DIR = BUILD / DB_REL
DEFAULT_ROWS = 1_000_000
DEFAULT_ROUNDS = 8
DEFAULT_SEEDS = (42, 99, 20260813)
LOOKUP_STRIDE = 257
NULL = "<SQL-NULL>"

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
    round: int
    case_id: str
    family: str
    sql: str
    kinds: tuple[str, ...]
    ordered: bool = True


@dataclass(frozen=True)
class CaseResult:
    columns: tuple[str, ...]
    rows: tuple[tuple[str, ...], ...]


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
    parser.add_argument("--rounds", type=int, default=DEFAULT_ROUNDS)
    parser.add_argument(
        "--seeds", type=parse_seeds, default=DEFAULT_SEEDS,
        help="comma-separated deterministic random seeds",
    )
    parser.add_argument(
        "--case-id", default="",
        help="run one generated case (the failure report supplies this value)",
    )
    parser.add_argument(
        "--postgres-dsn", default=os.environ.get("RMDB_POSTGRES_DSN", ""),
        help="existing PostgreSQL DSN; default starts an isolated temporary cluster",
    )
    parser.add_argument(
        "--failure-dir", type=Path,
        default=Path("/tmp/rmdb-extended-syntax-differential-failures"),
    )
    args = parser.parse_args()
    if not 1_000 <= args.rows <= 5_000_000:
        parser.error("--rows must be in [1000, 5000000]")
    if not 1 <= args.rounds <= 10_000:
        parser.error("--rounds must be in [1, 10000]")
    return args


def port_is_open() -> bool:
    with socket.socket() as probe:
        probe.settimeout(0.25)
        return probe.connect_ex(("127.0.0.1", PORT)) == 0


def stop_rmdb(proc: subprocess.Popen[bytes] | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def write_fixture(directory: Path, rows: int) -> tuple[Path, Path]:
    """Write deterministic CSV without retaining the million rows in memory."""
    directory.mkdir(parents=True, exist_ok=True)
    main_path = directory / "extended_diff_main.csv"
    lookup_path = directory / "extended_diff_lookup.csv"
    with main_path.open("w", encoding="ascii", newline="") as handle:
        handle.write("id,grp,bucket,amount\n")
        for row_id in range(rows):
            grp = (row_id * 37 + 11) % 101
            bucket = (row_id * 13 + 5) % 29
            amount = (row_id * 7919 + 17) % 20_003 - 10_001
            handle.write(f"{row_id},{grp},{bucket},{amount}\n")
    with lookup_path.open("w", encoding="ascii", newline="") as handle:
        handle.write("id,rhs,band\n")
        for row_id in range(0, rows, LOOKUP_STRIDE):
            handle.write(f"{row_id},{row_id * 3 + 1},{row_id % 17}\n")
    return main_path, lookup_path


def rmdb_execute(client: WireClient, sql: str):
    result = client.exec_stream(sql)
    if not result.ok or result.error or result.aborted:
        raise DifferentialFailure(f"RMDB SQL failed:\n{sql}\n{result.diagnostic}")
    return result


def setup_databases(
    rmdb: WireClient, postgres: PostgresClient, main_path: Path, lookup_path: Path
) -> None:
    for sql in (
        "create table extdiff_main (id int,grp int,bucket int,amount int)",
        "create table extdiff_lookup (id int,rhs int,band int)",
        f"load {main_path} into extdiff_main",
        f"load {lookup_path} into extdiff_lookup",
    ):
        rmdb_execute(rmdb, sql)
    postgres.run(
        "drop table if exists extdiff_lookup;\n"
        "drop table if exists extdiff_main;\n"
        "create table extdiff_main "
        "(id integer,grp integer,bucket integer,amount integer);\n"
        "create table extdiff_lookup (id integer,rhs integer,band integer);\n"
        f"\\copy extdiff_main from '{main_path}' with (format csv, header true)\n"
        f"\\copy extdiff_lookup from '{lookup_path}' with (format csv, header true)\n"
    )


def create_indexes(rmdb: WireClient, postgres: PostgresClient) -> None:
    rmdb_execute(rmdb, "create index extdiff_main(id)")
    rmdb_execute(rmdb, "create index extdiff_lookup(id)")
    postgres.run(
        "create index extdiff_main_id_idx on extdiff_main(id);\n"
        "create index extdiff_lookup_id_idx on extdiff_lookup(id);\n"
    )


def f32(value: object) -> float:
    return struct.unpack(">f", struct.pack(">f", float(value)))[0]


def normalize_cell(value: object, kind: str) -> str:
    if value is None or str(value) == "__RMDB_NULL__":
        return NULL
    if kind == "int":
        return str(int(str(value)))
    if kind == "float":
        number = f32(value)
        if math.isnan(number):
            return "nan"
        if math.isinf(number):
            return "+inf" if number > 0 else "-inf"
        # hex() gives a stable, exact representation of the binary32 result.
        return number.hex()
    if kind == "text":
        return str(value).rstrip("\x00")
    raise DifferentialFailure(f"unknown normalization kind: {kind}")


def normalize_rows(
    rows: Sequence[Sequence[object]], kinds: Sequence[str], ordered: bool
) -> tuple[tuple[str, ...], ...]:
    normalized: list[tuple[str, ...]] = []
    for row in rows:
        if len(row) != len(kinds):
            raise DifferentialFailure(
                f"column count mismatch: expected {len(kinds)}, got {len(row)} in {row!r}"
            )
        normalized.append(tuple(normalize_cell(value, kind) for value, kind in zip(row, kinds)))
    if not ordered:
        normalized.sort()
    return tuple(normalized)


def rmdb_query(client: WireClient, case: QueryCase) -> CaseResult:
    result = rmdb_execute(client, case.sql)
    columns = tuple(name.lower() for name, _ in result.columns)
    return CaseResult(columns, normalize_rows(result.rows, case.kinds, case.ordered))


def postgres_query(postgres: PostgresClient, case: QueryCase) -> CaseResult:
    command = [
        str(postgres.psql), "-X", "--set", "ON_ERROR_STOP=1", "--dbname", postgres.dsn,
        "-A", "-q", "-t", "-F", "\t", "-P", "null=__RMDB_NULL__",
    ]
    result = subprocess.run(
        command, input=case.sql + ";\n", text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=300, check=False,
    )
    if result.returncode != 0:
        raise DifferentialFailure(
            f"PostgreSQL SQL failed ({result.returncode}):\n{case.sql}\n{result.stderr}"
        )
    rows: list[list[str]] = []
    for line in result.stdout.splitlines():
        if line == "" and len(case.kinds) != 1:
            continue
        rows.append(line.split("\t"))
    return CaseResult((), normalize_rows(rows, case.kinds, case.ordered))


class CaseGenerator:
    def __init__(self, seed: int, rows: int) -> None:
        self.seed = seed
        self.rows = rows
        self.random = random.Random(seed)

    def window(self, minimum: int, maximum: int) -> tuple[int, int]:
        width = self.random.randint(minimum, min(maximum, self.rows - 1))
        low = self.random.randint(0, self.rows - width - 1)
        return low, low + width

    def lookup_window(self, radius_minimum: int, radius_maximum: int) -> tuple[int, int]:
        """Return a bounded window guaranteed to contain a sparse lookup key."""
        last_anchor = (self.rows - 1) // LOOKUP_STRIDE
        if last_anchor >= 2:
            anchor_index = self.random.randint(1, last_anchor - 1)
        else:
            anchor_index = self.random.randint(0, last_anchor)
        anchor = anchor_index * LOOKUP_STRIDE
        radius = self.random.randint(radius_minimum, radius_maximum)
        return max(0, anchor - radius), min(self.rows - 1, anchor + radius)

    def make(self, round_id: int) -> list[QueryCase]:
        prefix = f"seed-{self.seed}-round-{round_id:03d}"
        distinct_low, distinct_high = self.window(80, 800)
        limit_low, limit_high = self.window(40, 500)
        agg_low, agg_high = self.window(200, 1600)
        set_low, set_high = self.lookup_window(20, 140)
        join_low, join_high = self.lookup_window(6, 32)
        null_low, null_high = self.window(260, 700)
        limit = self.random.randint(1, 19)
        offset = self.random.randint(0, 13)
        set_shift = self.random.randint(0, max(1, (set_high - set_low) // 4))

        def case(name: str, family: str, sql: str, kinds: tuple[str, ...]) -> QueryCase:
            return QueryCase(self.seed, round_id, f"{prefix}-{name}", family, sql, kinds)

        return [
            case(
                "select-distinct", "SELECT DISTINCT",
                "select distinct grp,bucket from extdiff_main "
                f"where id between {distinct_low} and {distinct_high} "
                f"order by grp,bucket limit {limit} offset {offset}",
                ("int", "int"),
            ),
            case(
                "join-using", "JOIN USING",
                "select id,m.grp,l.rhs from extdiff_main m "
                "join extdiff_lookup l using(id) "
                f"where m.id between {join_low} and {join_high} "
                f"and l.id between {join_low} and {join_high} order by id",
                ("int", "int", "int"),
            ),
            case(
                "limit-offset", "LIMIT OFFSET",
                "select id,grp,amount from extdiff_main "
                f"where id between {limit_low} and {limit_high} "
                f"order by amount desc,id limit {limit} offset {offset}",
                ("int", "int", "int"),
            ),
            case(
                "aggregate-distinct", "aggregate DISTINCT",
                "select sum(distinct grp) as s,avg(distinct grp) as a,"
                "count(distinct (grp,bucket)) as c from extdiff_main "
                f"where id between {agg_low} and {agg_high}",
                ("int", "float", "int"),
            ),
            case(
                "intersect", "INTERSECT/EXCEPT",
                "select id from extdiff_main "
                f"where id between {set_low} and {set_high} intersect "
                "select id from extdiff_lookup "
                f"where id between {set_low + set_shift} and {set_high + set_shift} "
                "order by id",
                ("int",),
            ),
            case(
                "except", "INTERSECT/EXCEPT",
                "select id from extdiff_main "
                f"where id between {set_low} and {set_high} except "
                "select id from extdiff_lookup "
                f"where id between {set_low + set_shift} and {set_high + set_shift} "
                "order by id",
                ("int",),
            ),
            case(
                "is-null", "IS NULL/IS NOT NULL",
                "select count(*) as n from extdiff_main m "
                "left join extdiff_lookup l using(id) "
                f"where m.id between {null_low} and {null_high} and l.rhs is null",
                ("int",),
            ),
            case(
                "is-not-null", "IS NULL/IS NOT NULL",
                "select count(*) as n from extdiff_main m "
                "left join extdiff_lookup l using(id) "
                f"where m.id between {null_low} and {null_high} and l.rhs is not null",
                ("int",),
            ),
        ]


def build_cases(seeds: Sequence[int], rounds: int, rows: int) -> list[QueryCase]:
    result: list[QueryCase] = []
    for seed in seeds:
        generator = CaseGenerator(seed, rows)
        for round_id in range(rounds):
            result.extend(generator.make(round_id))
    return result


def repro_command(args: argparse.Namespace, case: QueryCase) -> str:
    command = [
        "python3", "-B", "tests/local/postgresql_extended_syntax_differential.py",
        "--rows", str(args.rows), "--rounds", str(case.round + 1),
        "--seeds", str(case.seed), "--case-id", case.case_id,
    ]
    if args.postgres_dsn:
        command.extend(("--postgres-dsn", args.postgres_dsn))
    return " ".join(command)


def write_failure(
    args: argparse.Namespace, case: QueryCase, phase: str, error: BaseException,
    rmdb_result: CaseResult | None = None, pg_result: CaseResult | None = None,
) -> Path:
    args.failure_dir.mkdir(parents=True, exist_ok=True)
    path = args.failure_dir / f"{case.case_id}-{phase}.json"
    payload = {
        "seed": case.seed,
        "round": case.round,
        "case_id": case.case_id,
        "family": case.family,
        "phase": phase,
        "rows": args.rows,
        "sql": case.sql,
        "error": str(error),
        "rmdb": asdict(rmdb_result) if rmdb_result is not None else None,
        "postgresql": asdict(pg_result) if pg_result is not None else None,
        "repro": repro_command(args, case),
    }
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return path


def run_phase(
    args: argparse.Namespace, label: str, cases: Sequence[QueryCase],
    rmdb: WireClient, postgres: PostgresClient,
) -> tuple[dict[str, CaseResult], dict[str, CaseResult]]:
    rmdb_results: dict[str, CaseResult] = {}
    pg_results: dict[str, CaseResult] = {}
    for index, case in enumerate(cases, 1):
        rmdb_result: CaseResult | None = None
        pg_result: CaseResult | None = None
        try:
            rmdb_result = rmdb_query(rmdb, case)
            pg_result = postgres_query(postgres, case)
            if rmdb_result.rows != pg_result.rows:
                raise DifferentialFailure(
                    f"normalized rows differ: RMDB={rmdb_result.rows!r}, "
                    f"PostgreSQL={pg_result.rows!r}"
                )
            rmdb_results[case.case_id] = rmdb_result
            pg_results[case.case_id] = pg_result
            print(
                f"[PASS] {label} {index}/{len(cases)} {case.case_id} "
                f"({len(rmdb_result.rows)} row(s))", flush=True,
            )
        except Exception as exc:
            artifact = write_failure(
                args, case, label, exc, rmdb_result=rmdb_result, pg_result=pg_result
            )
            raise DifferentialFailure(
                f"{label} failure\ncase={case.case_id}\nseed={case.seed}\n"
                f"family={case.family}\nSQL: {case.sql}\n{exc}\n"
                f"artifact={artifact}\nrepro: {repro_command(args, case)}"
            ) from exc
    return rmdb_results, pg_results


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

    cases = build_cases(args.seeds, args.rounds, args.rows)
    if args.case_id:
        cases = [case for case in cases if case.case_id == args.case_id]
        if not cases:
            print(f"[FATAL] generated case not found: {args.case_id}", file=sys.stderr)
            return 2

    ephemeral: EphemeralPostgres | None = None
    rmdb_proc: subprocess.Popen[bytes] | None = None
    rmdb: WireClient | None = None
    postgres: PostgresClient | None = None
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="rmdb-extended-diff-", dir="/tmp") as data_dir:
        main_path, lookup_path = write_fixture(Path(data_dir), args.rows)
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
                    [str(SERVER), str(DB_REL)], cwd=BUILD,
                    stdout=rmdb_log, stderr=subprocess.STDOUT,
                )
                wait_ready(timeout=20.0)
                rmdb = WireClient(timeout=300)
                setup_databases(rmdb, postgres, main_path, lookup_path)
                print(
                    f"[DATA] main={args.rows:,}, lookup={(args.rows - 1) // LOOKUP_STRIDE + 1:,}, "
                    f"cases={len(cases)}, seeds={','.join(map(str, args.seeds))}, {pg_mode}",
                    flush=True,
                )

                rmdb_seq, pg_seq = run_phase(args, "no-index", cases, rmdb, postgres)
                create_indexes(rmdb, postgres)
                rmdb_idx, pg_idx = run_phase(args, "indexed", cases, rmdb, postgres)

                for case in cases:
                    if rmdb_seq[case.case_id].rows != rmdb_idx[case.case_id].rows:
                        error = DifferentialFailure("RMDB result changed after indexes")
                        artifact = write_failure(
                            args, case, "access-invariance", error,
                            rmdb_result=rmdb_idx[case.case_id],
                            pg_result=pg_idx[case.case_id],
                        )
                        raise DifferentialFailure(
                            f"{error}: {case.case_id}\nseed={case.seed}\nSQL: {case.sql}\n"
                            f"artifact={artifact}\nrepro: {repro_command(args, case)}"
                        )
                    if pg_seq[case.case_id].rows != pg_idx[case.case_id].rows:
                        error = DifferentialFailure(
                            "PostgreSQL result changed after indexes"
                        )
                        artifact = write_failure(
                            args, case, "access-invariance", error,
                            rmdb_result=rmdb_idx[case.case_id],
                            pg_result=pg_idx[case.case_id],
                        )
                        raise DifferentialFailure(
                            f"{error}: {case.case_id}\nseed={case.seed}\nSQL: {case.sql}\n"
                            f"artifact={artifact}\nrepro: {repro_command(args, case)}"
                        )
                print(f"[PASS] access-path invariance: {len(cases)} cases", flush=True)
            except Exception as exc:
                print(f"[FATAL] {exc}", file=sys.stderr)
                rmdb_log.seek(0)
                tail = rmdb_log.read().decode("utf-8", errors="replace")[-8000:]
                if tail.strip():
                    print(f"\nRMDB log tail:\n{tail}", file=sys.stderr)
                if ephemeral is not None and ephemeral.log.exists():
                    pg_tail = ephemeral.log.read_text(
                        encoding="utf-8", errors="replace"
                    )[-4000:]
                    if pg_tail.strip():
                        print(f"\nPostgreSQL log tail:\n{pg_tail}", file=sys.stderr)
                return 1
            finally:
                if postgres is not None:
                    try:
                        postgres.run(
                            "drop table if exists extdiff_lookup;\n"
                            "drop table if exists extdiff_main;\n"
                        )
                    except Exception:
                        pass
                if rmdb is not None:
                    rmdb.close()
                stop_rmdb(rmdb_proc)
                if ephemeral is not None:
                    ephemeral.close()

    elapsed = time.monotonic() - started
    print(
        f"\nALL {len(cases)} RANDOM SQL CASES PASSED BEFORE/AFTER INDEXES "
        f"({len(cases) * 4} engine executions, {elapsed:.2f}s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
