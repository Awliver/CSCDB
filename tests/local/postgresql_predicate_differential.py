#!/usr/bin/env python3
"""Random predicate SQL differential gate: RMDB vs real PostgreSQL.

The gate generates reproducible LIKE/BETWEEN/EXISTS/IN boolean trees, executes
every query before and after indexes are created, and requires all four result
sets to agree: RMDB no-index, PostgreSQL no-index, RMDB indexed, PostgreSQL
indexed.  By default an isolated temporary PostgreSQL cluster is started.
"""

from __future__ import annotations

import argparse
import csv
import os
import pwd
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
DB_REL = Path("test_dbs") / "predicate_differential_db"
DB_DIR = BUILD / DB_REL
DEFAULT_SEEDS = (42, 99, 20260812)
DEFAULT_CASES_PER_SEED = 100
DEFAULT_ROWS = 2_000

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from wire_client import PORT, WireClient, wait_ready  # noqa: E402


class DifferentialFailure(AssertionError):
    pass


@dataclass(frozen=True)
class QueryCase:
    seed: int
    case_id: str
    expression: str
    sql: str


@dataclass(frozen=True)
class TableNames:
    data: str
    allowed: str


NAMES = (
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
    "albatross",
    "zulu",
)
LIKE_PATTERNS = (
    "h%",
    "H%",
    "_ndio",
    "in__o",
    "in_o",
    "_%",
    "%_",
    "__%",
    "%____%",
    "a%",
    "%o",
    "%",
    "____",
)


def data_row(row_id: int) -> tuple[int, str, int, int]:
    return (
        row_id,
        NAMES[(row_id * 11 + 3) % len(NAMES)],
        (row_id * 7919) % 2001 - 1000,
        (row_id * 37) % 31,
    )


def free_tcp_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def port_is_open(port: int) -> bool:
    with socket.socket() as probe:
        probe.settimeout(0.25)
        return probe.connect_ex(("127.0.0.1", port)) == 0


def find_pg_binary(name: str) -> Path:
    direct = shutil.which(name)
    if direct:
        return Path(direct)
    candidates = sorted(Path("/usr/lib/postgresql").glob(f"*/bin/{name}"), reverse=True)
    if candidates:
        return candidates[0]
    raise DifferentialFailure(
        f"PostgreSQL binary {name!r} not found; install PostgreSQL or pass --postgres-dsn"
    )


class EphemeralPostgres:
    def __init__(self) -> None:
        self.base = Path(tempfile.mkdtemp(prefix="rmdb-predicate-pg-", dir="/tmp"))
        self.data = self.base / "data"
        self.socket_dir = self.base / "socket"
        self.log = self.base / "postgres.log"
        self.port = free_tcp_port()
        self.pg_ctl = find_pg_binary("pg_ctl")
        self.initdb = find_pg_binary("initdb")
        self.prefix: list[str] = []
        self.started = False

        if os.geteuid() == 0:
            try:
                postgres = pwd.getpwnam("postgres")
            except KeyError as exc:
                raise DifferentialFailure(
                    "initdb refuses root and no postgres OS user exists"
                ) from exc
            os.chown(self.base, postgres.pw_uid, postgres.pw_gid)
            self.prefix = ["runuser", "-u", "postgres", "--"]

    @property
    def dsn(self) -> str:
        return f"postgresql://postgres@127.0.0.1:{self.port}/postgres"

    def _run(self, command: Sequence[str]) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            [*self.prefix, *map(str, command)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=60,
            check=False,
        )
        if result.returncode != 0:
            raise DifferentialFailure(
                f"PostgreSQL command failed ({result.returncode}): {' '.join(command)}\n"
                f"{result.stdout}"
            )
        return result

    def start(self) -> None:
        self.socket_dir.mkdir()
        if self.prefix:
            postgres = pwd.getpwnam("postgres")
            os.chown(self.socket_dir, postgres.pw_uid, postgres.pw_gid)
        self._run(
            [
                str(self.initdb),
                "-D",
                str(self.data),
                "--no-locale",
                "--encoding=UTF8",
                "--auth=trust",
                "-U",
                "postgres",
                "--no-instructions",
            ]
        )
        options = (
            f"-F -p {self.port} -h 127.0.0.1 -k {self.socket_dir} "
            "-c synchronous_commit=off -c full_page_writes=off"
        )
        self._run(
            [
                str(self.pg_ctl),
                "-D",
                str(self.data),
                "-l",
                str(self.log),
                "-o",
                options,
                "-w",
                "start",
            ]
        )
        self.started = True

    def close(self) -> None:
        if self.started:
            try:
                self._run(
                    [
                        str(self.pg_ctl),
                        "-D",
                        str(self.data),
                        "-m",
                        "fast",
                        "-w",
                        "stop",
                    ]
                )
            finally:
                self.started = False
        shutil.rmtree(self.base, ignore_errors=True)


class PostgresClient:
    def __init__(self, dsn: str) -> None:
        self.dsn = dsn
        self.psql = find_pg_binary("psql")

    def run(self, sql: str, *, tuples_only: bool = False) -> str:
        command = [
            str(self.psql),
            "-X",
            "--set",
            "ON_ERROR_STOP=1",
            "--dbname",
            self.dsn,
        ]
        if tuples_only:
            command.extend(["-A", "-t", "-F", "\t"])
        result = subprocess.run(
            command,
            input=sql,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120,
            check=False,
        )
        if result.returncode != 0:
            raise DifferentialFailure(
                f"PostgreSQL SQL failed ({result.returncode}):\n{result.stderr}\nSQL:\n{sql[:8000]}"
            )
        return result.stdout

    def query_counts(self, cases: Sequence[QueryCase]) -> list[int]:
        statements = []
        for index, case in enumerate(cases):
            suffix = case.sql[len("select count(*)") :]
            statements.append(f"select {index},count(*){suffix};")
        output = self.run("\n".join(statements), tuples_only=True)
        counts: list[int | None] = [None] * len(cases)
        for line in output.splitlines():
            if not line.strip():
                continue
            fields = line.split("\t")
            if len(fields) != 2:
                raise DifferentialFailure(f"unexpected PostgreSQL output line: {line!r}")
            index, count = int(fields[0]), int(fields[1])
            counts[index] = count
        if any(count is None for count in counts):
            missing = [index for index, count in enumerate(counts) if count is None]
            raise DifferentialFailure(f"PostgreSQL omitted differential results: {missing[:20]}")
        return [int(count) for count in counts]


def write_data(data_dir: Path, rows: int) -> tuple[Path, Path]:
    data_dir.mkdir(parents=True, exist_ok=True)
    data_path = data_dir / "diff_data.csv"
    allowed_path = data_dir / "diff_allowed.csv"
    with data_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "name", "score", "grp"))
        for row_id in range(rows):
            writer.writerow(data_row(row_id))
    with allowed_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(("id", "grp"))
        for row_id in range(0, rows, 7):
            writer.writerow((row_id, data_row(row_id)[3]))
    return data_path, allowed_path


def rmdb_execute(client: WireClient, sql: str):
    result = client.exec_stream(sql)
    if not result.ok or result.error or result.aborted:
        raise DifferentialFailure(f"RMDB SQL failed: {sql}\n{result.diagnostic}")
    return result


def setup_databases(
    rmdb: WireClient,
    postgres: PostgresClient,
    tables: TableNames,
    data_path: Path,
    allowed_path: Path,
) -> None:
    for sql in [
        f"create table {tables.data} (id int,name char(24),score int,grp int)",
        f"create table {tables.allowed} (id int,grp int)",
        f"load {data_path} into {tables.data}",
        f"load {allowed_path} into {tables.allowed}",
    ]:
        rmdb_execute(rmdb, sql)

    postgres.run(
        f"drop table if exists {tables.allowed};\n"
        f"drop table if exists {tables.data};\n"
        f"create table {tables.data} (id integer,name text,score integer,grp integer);\n"
        f"create table {tables.allowed} (id integer,grp integer);\n"
        f"\\copy {tables.data} from '{data_path}' with (format csv, header true)\n"
        f"\\copy {tables.allowed} from '{allowed_path}' with (format csv, header true)\n"
    )


def create_indexes(
    rmdb: WireClient, postgres: PostgresClient, tables: TableNames
) -> None:
    for sql in [
        f"create index {tables.data} (id)",
        f"create index {tables.allowed} (id)",
    ]:
        rmdb_execute(rmdb, sql)
    postgres.run(
        f"create index {tables.data}_id_idx on {tables.data}(id);\n"
        f"create index {tables.allowed}_id_idx on {tables.allowed}(id);\n"
    )


def deterministic_expressions(tables: TableNames) -> list[str]:
    data, allowed = tables.data, tables.allowed
    del data  # The outer table is always referenced through alias t.
    return [
        "t.name like 'h%'",
        "t.name not like '_ndio'",
        "t.score between -100 and 100",
        "t.score not between -100 and 100",
        "t.id in (1,2,3,5,8,13,21)",
        "t.id not in (1,2,3,5,8,13,21)",
        f"t.id in (select a.id from {allowed} a where a.id between 0 and 700)",
        f"t.id not in (select a.id from {allowed} a where a.id between 0 and 700)",
        f"exists (select a.id from {allowed} a where a.id=14)",
        f"not exists (select a.id from {allowed} a where a.id=-1)",
        f"exists (select a.id from {allowed} a where a.id=t.id)",
        f"not exists (select a.id from {allowed} a where a.id=t.id)",
        f"t.id in (select a.id from {allowed} a where a.id=t.id)",
        "t.name like 'h%' or not (t.score between -50 and 50 or t.id in (2,4,6))",
        "((t.name like '__%' and t.score not between -1062 and 777) and "
        f"(not exists (select a.id from {allowed} a where a.id=t.id) "
        "and t.name not like 'H%'))",
    ]


class ExpressionGenerator:
    def __init__(self, seed: int, rows: int, tables: TableNames) -> None:
        self.random = random.Random(seed)
        self.rows = rows
        self.allowed = tables.allowed

    def integer(self, low: int = -100, high: int | None = None) -> int:
        upper = self.rows + 100 if high is None else high
        return self.random.randint(low, upper)

    def atom(self) -> str:
        kind = self.random.randrange(10)
        if kind == 0:
            first, second = self.integer(), self.integer()
            return f"t.id between {first} and {second}"
        if kind == 1:
            first, second = self.integer(-1100, 1100), self.integer(-1100, 1100)
            return f"t.score not between {first} and {second}"
        if kind in (2, 3):
            values = [self.integer() for _ in range(self.random.randint(1, 8))]
            keyword = "in" if kind == 2 else "not in"
            return f"t.id {keyword} ({','.join(map(str, values))})"
        if kind in (4, 5):
            pattern = self.random.choice(LIKE_PATTERNS)
            keyword = "like" if kind == 4 else "not like"
            return f"t.name {keyword} '{pattern}'"
        if kind == 6:
            first, second = sorted((self.integer(), self.integer()))
            keyword = self.random.choice(("in", "not in"))
            return (
                f"t.id {keyword} (select a.id from {self.allowed} a "
                f"where a.id between {first} and {second})"
            )
        if kind == 7:
            target = self.integer()
            prefix = "not " if self.random.random() < 0.5 else ""
            return (
                f"{prefix}exists (select a.id from {self.allowed} a "
                f"where a.id={target})"
            )
        if kind == 8:
            prefix = "not " if self.random.random() < 0.5 else ""
            return (
                f"{prefix}exists (select a.id from {self.allowed} a "
                "where a.id=t.id)"
            )
        keyword = self.random.choice(("in", "not in"))
        return (
            f"t.id {keyword} (select a.id from {self.allowed} a "
            "where a.id=t.id)"
        )

    def expression(self, depth: int = 0) -> str:
        if depth >= 3 or self.random.random() < 0.42:
            return self.atom()
        choice = self.random.random()
        if choice < 0.2:
            return f"not ({self.expression(depth + 1)})"
        operator = "and" if choice < 0.6 else "or"
        return (
            f"({self.expression(depth + 1)} {operator} "
            f"{self.expression(depth + 1)})"
        )


def build_cases(
    seeds: Sequence[int], cases_per_seed: int, rows: int, tables: TableNames
) -> list[QueryCase]:
    cases: list[QueryCase] = []
    for index, expression in enumerate(deterministic_expressions(tables)):
        cases.append(
            QueryCase(
                seed=-1,
                case_id=f"edge-{index:02d}",
                expression=expression,
                sql=f"select count(*) from {tables.data} t where {expression}",
            )
        )
    for seed in seeds:
        generator = ExpressionGenerator(seed, rows, tables)
        for index in range(cases_per_seed):
            expression = generator.expression()
            cases.append(
                QueryCase(
                    seed=seed,
                    case_id=f"seed-{seed}-case-{index:03d}",
                    expression=expression,
                    sql=f"select count(*) from {tables.data} t where {expression}",
                )
            )
    return cases


def rmdb_counts(client: WireClient, cases: Sequence[QueryCase]) -> list[int]:
    counts = []
    for case in cases:
        result = rmdb_execute(client, case.sql)
        if len(result.rows) != 1 or len(result.rows[0]) != 1:
            raise DifferentialFailure(
                f"RMDB returned a non-scalar count for {case.case_id}: {result.rows!r}"
            )
        counts.append(int(result.rows[0][0]))
    return counts


def compare_counts(
    label: str,
    cases: Sequence[QueryCase],
    rmdb_values: Sequence[int],
    postgres_values: Sequence[int],
) -> None:
    mismatches = []
    for case, rmdb_value, postgres_value in zip(cases, rmdb_values, postgres_values):
        if rmdb_value != postgres_value:
            mismatches.append((case, rmdb_value, postgres_value))
    if mismatches:
        details = []
        for case, rmdb_value, postgres_value in mismatches[:10]:
            details.append(
                f"{case.case_id} seed={case.seed}: RMDB={rmdb_value}, "
                f"PostgreSQL={postgres_value}\nSQL: {case.sql}"
            )
        raise DifferentialFailure(
            f"{label}: {len(mismatches)} differential mismatches\n" + "\n".join(details)
        )
    print(f"[PASS] {label}: {len(cases)} RMDB/PostgreSQL results agree")


def assert_index_plan(client: WireClient, tables: TableNames) -> None:
    result = rmdb_execute(
        client, f"explain select id from {tables.data} where id=14"
    )
    plan = "\n".join(str(row[0]) for row in result.rows if row)
    if "type=IndexScan" not in plan:
        raise DifferentialFailure(f"indexed differential path did not use IndexScan:\n{plan}")


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
    if args.rows < 100:
        parser.error("--rows must be at least 100")
    if args.cases_per_seed < 1:
        parser.error("--cases-per-seed must be positive")
    return args


def main() -> int:
    args = parse_args()
    if not SERVER.is_file():
        print(f"[FATAL] server binary not found: {SERVER}", file=sys.stderr)
        return 2
    if port_is_open(PORT):
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
    tables = TableNames(data=f"pdiff_data_{suffix}", allowed=f"pdiff_allowed_{suffix}")
    cases = build_cases(args.seeds, args.cases_per_seed, args.rows, tables)
    data_path, allowed_path = write_data(DB_DIR / "generated", args.rows)

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
            setup_databases(rmdb, postgres, tables, data_path, allowed_path)
            print(
                f"[DATA] {args.rows:,} rows, {len(cases)} queries, "
                f"seeds={','.join(map(str, args.seeds))}, {pg_mode}"
            )

            rmdb_no_index = rmdb_counts(rmdb, cases)
            pg_no_index = postgres.query_counts(cases)
            compare_counts("no-index differential", cases, rmdb_no_index, pg_no_index)

            create_indexes(rmdb, postgres, tables)
            assert_index_plan(rmdb, tables)
            rmdb_indexed = rmdb_counts(rmdb, cases)
            pg_indexed = postgres.query_counts(cases)
            compare_counts("indexed differential", cases, rmdb_indexed, pg_indexed)

            if rmdb_no_index != rmdb_indexed:
                changed = [
                    cases[index].case_id
                    for index, (left, right) in enumerate(zip(rmdb_no_index, rmdb_indexed))
                    if left != right
                ]
                raise DifferentialFailure(
                    f"RMDB access-path invariance failed for {changed[:20]}"
                )
            if pg_no_index != pg_indexed:
                raise DifferentialFailure("PostgreSQL results changed after index creation")
            print(f"[PASS] access-path invariance: {len(cases)} queries")
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
                    postgres.run(
                        f"drop table if exists {tables.allowed};\n"
                        f"drop table if exists {tables.data};\n"
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
        f"\nALL {len(cases)} RANDOM/EDGE SQL CASES PASSED ON BOTH ACCESS PATHS "
        f"({len(cases) * 2} PostgreSQL comparisons, {elapsed:.2f}s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
