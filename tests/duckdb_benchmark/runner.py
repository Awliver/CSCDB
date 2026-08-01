#!/usr/bin/env python3
"""Run the RMDB-compatible subset of DuckDB's interpreted benchmarks."""

from __future__ import annotations

import argparse
import csv
import dataclasses
import json
import os
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DEFAULT_SERVER = ROOT / "build" / "bin" / "rmdb"
CASES = HERE / "cases"
HOST = "127.0.0.1"
PORT = 8765

sys.path.insert(0, str(ROOT / "tests" / "local"))
from wire_client import ExecResult, WireClient  # noqa: E402


class BenchmarkError(RuntimeError):
    pass


@dataclasses.dataclass
class Benchmark:
    path: Path
    name: str
    group: str = ""
    subgroup: str = ""
    load: List[str] = dataclasses.field(default_factory=list)
    generate: List[str] = dataclasses.field(default_factory=list)
    postload: List[str] = dataclasses.field(default_factory=list)
    run: List[str] = dataclasses.field(default_factory=list)
    cleanup: List[str] = dataclasses.field(default_factory=list)
    result_types: str = ""
    expected: List[List[str]] = dataclasses.field(default_factory=list)

    @property
    def relative_path(self) -> str:
        return self.path.relative_to(HERE).as_posix()


def split_sql(text: str) -> List[str]:
    statements: List[str] = []
    start = 0
    quoted = False
    i = 0
    while i < len(text):
        if text[i] == "'":
            if quoted and i + 1 < len(text) and text[i + 1] == "'":
                i += 2
                continue
            quoted = not quoted
        elif text[i] == ";" and not quoted:
            statement = text[start : i + 1].strip()
            if statement:
                statements.append(statement)
            start = i + 1
        i += 1
    tail = text[start:].strip()
    if tail:
        statements.append(tail)
    return statements


def parse_benchmark(path: Path) -> Benchmark:
    metadata = {}
    sections = {"load": [], "generate": [], "postload": [], "run": [], "cleanup": []}
    result_types = ""
    expected: List[List[str]] = []
    section = ""

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line in sections:
            section = line
            continue
        if line.startswith("result "):
            section = "result"
            result_types = line.split(None, 1)[1].strip()
            continue
        if section == "result":
            expected.append(raw.rstrip("\r\n").split("\t"))
            continue
        if not section and " " in line:
            key, value = line.split(None, 1)
            metadata[key] = value.strip()
            continue
        if section in sections:
            sections[section].append(raw)
            continue
        raise BenchmarkError(f"{path}: cannot parse line: {raw}")

    load = split_sql("\n".join(sections["load"]))
    postload = split_sql("\n".join(sections["postload"]))
    run = split_sql("\n".join(sections["run"]))
    cleanup = split_sql("\n".join(sections["cleanup"]))
    if not run:
        raise BenchmarkError(f"{path}: missing run section")
    return Benchmark(
        path=path,
        name=metadata.get("name", path.stem),
        group=metadata.get("group", ""),
        subgroup=metadata.get("subgroup", ""),
        load=load,
        generate=[line.strip() for line in sections["generate"] if line.strip()],
        postload=postload,
        run=run,
        cleanup=cleanup,
        result_types=result_types,
        expected=expected,
    )


_ROW_EXPR = re.compile(
    r"^(?:row|row(?P<add>[+-]\d+)|row%(?P<mod>\d+)|"
    r"row\*(?P<mul>\d+)%(?P<mulmod>\d+)|const:(?P<const>-?\d+))$"
)


def eval_row_expr(expr: str, row: int) -> int:
    match = _ROW_EXPR.fullmatch(expr)
    if not match:
        raise BenchmarkError(f"unsupported generator expression: {expr}")
    if expr == "row":
        return row
    if match.group("add") is not None:
        return row + int(match.group("add"))
    if match.group("mod") is not None:
        return row % int(match.group("mod"))
    if match.group("mul") is not None:
        return (row * int(match.group("mul"))) % int(match.group("mulmod"))
    return int(match.group("const"))


def generate_csv(spec: str, data_dir: Path) -> Tuple[str, Path]:
    parts = shlex.split(spec)
    if len(parts) < 3:
        raise BenchmarkError(f"bad generate directive: {spec}")
    table = parts[0]
    try:
        rows = int(parts[1])
    except ValueError as exc:
        raise BenchmarkError(f"bad row count in generate directive: {spec}") from exc
    if rows < 0:
        raise BenchmarkError(f"negative row count in generate directive: {spec}")
    expressions = parts[2:]
    path = data_dir / f"{table}.csv"
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        for row in range(rows):
            writer.writerow([eval_row_expr(expr, row) for expr in expressions])
    return table, path


def run_statement(client: WireClient, sql: str) -> ExecResult:
    result = client.exec_stream(sql)
    if not result.ok or result.error or result.aborted:
        diagnostic = result.diagnostic or "unknown RMDB error"
        raise BenchmarkError(f"SQL failed: {sql}\n{diagnostic}")
    return result


def execute_run(client: WireClient, statements: Sequence[str]) -> ExecResult:
    result = ExecResult()
    for statement in statements:
        result = run_statement(client, statement)
    return result


def normalize_expected(value: str, kind: str):
    if kind == "I":
        return int(value)
    if kind in {"R", "F", "D"}:
        return float(value)
    return value


def verify_result(benchmark: Benchmark, result: ExecResult) -> None:
    if not benchmark.result_types:
        return
    if any(len(row) != len(benchmark.result_types) for row in benchmark.expected):
        raise BenchmarkError(f"{benchmark.relative_path}: malformed expected result")
    expected = [
        [normalize_expected(value, benchmark.result_types[i]) for i, value in enumerate(row)]
        for row in benchmark.expected
    ]
    if len(result.rows) != len(expected):
        raise BenchmarkError(
            f"result row count mismatch: expected {len(expected)}, got {len(result.rows)}"
        )
    for row_no, (actual_row, expected_row) in enumerate(zip(result.rows, expected), 1):
        if len(actual_row) != len(expected_row):
            raise BenchmarkError(
                f"result column count mismatch at row {row_no}: "
                f"expected {len(expected_row)}, got {len(actual_row)}"
            )
        for col_no, (actual, wanted) in enumerate(zip(actual_row, expected_row), 1):
            if isinstance(wanted, float):
                equal = actual is not None and abs(float(actual) - wanted) <= 1e-5
            else:
                equal = actual == wanted
            if not equal:
                raise BenchmarkError(
                    f"result mismatch at row {row_no}, column {col_no}: "
                    f"expected {wanted!r}, got {actual!r}"
                )


def port_is_open() -> bool:
    with socket.socket() as sock:
        sock.settimeout(0.2)
        return sock.connect_ex((HOST, PORT)) == 0


class Server:
    def __init__(self, binary: Path, keep_db: bool):
        self.binary = binary
        self.keep_db = keep_db
        self.process: Optional[subprocess.Popen] = None
        self.db_dir: Optional[Path] = None
        self.log_handle = None

    def __enter__(self):
        if port_is_open():
            raise BenchmarkError(f"port {PORT} is already in use; stop the existing RMDB server")
        build_dir = self.binary.resolve().parents[1]
        build_dir.mkdir(parents=True, exist_ok=True)
        self.db_dir = Path(tempfile.mkdtemp(prefix="duckdb_benchmark_", dir=build_dir))
        log_path = self.db_dir / "server.log"
        self.log_handle = log_path.open("w", encoding="utf-8")
        self.process = subprocess.Popen(
            [str(self.binary.resolve()), str(self.db_dir)],
            cwd=build_dir,
            stdout=self.log_handle,
            stderr=subprocess.STDOUT,
        )
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                self.log_handle.flush()
                detail = log_path.read_text(encoding="utf-8", errors="replace")
                raise BenchmarkError(f"RMDB exited during startup:\n{detail[-4000:]}")
            try:
                client = WireClient(timeout=2)
                client.close()
                return self
            except (ConnectionError, OSError):
                time.sleep(0.05)
        raise BenchmarkError("RMDB did not accept Wire v3 connections within 30 seconds")

    def __exit__(self, exc_type, exc, traceback):
        if self.process is not None and self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=2)
        if self.log_handle is not None:
            self.log_handle.close()
        if self.db_dir is not None:
            if self.keep_db:
                print(f"kept database: {self.db_dir}", file=sys.stderr)
            else:
                shutil.rmtree(self.db_dir, ignore_errors=True)


def load_case(client: WireClient, benchmark: Benchmark, db_dir: Path) -> None:
    for statement in benchmark.load:
        run_statement(client, statement)
    data_dir = db_dir / "generated"
    data_dir.mkdir(exist_ok=True)
    for spec in benchmark.generate:
        table, path = generate_csv(spec, data_dir)
        run_statement(client, f"LOAD {path} INTO {table};")
    for statement in benchmark.postload:
        run_statement(client, statement)


def cleanup_run(client: WireClient, benchmark: Benchmark) -> None:
    for statement in benchmark.cleanup:
        run_statement(client, statement)


def run_case(
    benchmark: Benchmark,
    binary: Path,
    warmup_runs: int,
    timed_runs: int,
    keep_db: bool,
) -> List[float]:
    with Server(binary, keep_db) as server:
        assert server.db_dir is not None
        load_client = WireClient(timeout=None)
        try:
            load_case(load_client, benchmark, server.db_dir)
            result = execute_run(load_client, benchmark.run)
            verify_result(benchmark, result)
            cleanup_run(load_client, benchmark)
            for _ in range(warmup_runs):
                execute_run(load_client, benchmark.run)
                cleanup_run(load_client, benchmark)
            timings = []
            for _ in range(timed_runs):
                started = time.perf_counter()
                execute_run(load_client, benchmark.run)
                timings.append(time.perf_counter() - started)
                cleanup_run(load_client, benchmark)
            return timings
        finally:
            load_client.close()


def discover(pattern: str) -> List[Benchmark]:
    try:
        matcher = re.compile(pattern)
    except re.error as exc:
        raise BenchmarkError(f"invalid benchmark regex: {exc}") from exc
    benchmarks = [parse_benchmark(path) for path in sorted(CASES.rglob("*.benchmark"))]
    return [
        benchmark
        for benchmark in benchmarks
        if matcher.search(benchmark.relative_path) or matcher.search(benchmark.name)
    ]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="DuckDB interpreted benchmark compatibility runner for RMDB"
    )
    parser.add_argument("pattern", nargs="?", default=".*", help="regex over case path or name")
    parser.add_argument("--list", action="store_true", help="list matching benchmarks")
    parser.add_argument("--info", action="store_true", help="show benchmark metadata")
    parser.add_argument("--query", action="store_true", help="print benchmark run SQL")
    parser.add_argument("--server", type=Path, default=DEFAULT_SERVER)
    parser.add_argument("--warmup-runs", type=int, default=1)
    parser.add_argument("--timed-runs", type=int, default=5)
    parser.add_argument("--out", type=Path, help="write timings only, one per line")
    parser.add_argument("--json", type=Path, help="write machine-readable results")
    parser.add_argument("--keep-db", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args()

    if args.warmup_runs < 0 or args.timed_runs < 1:
        parser.error("--warmup-runs must be >= 0 and --timed-runs must be >= 1")
    try:
        benchmarks = discover(args.pattern)
    except BenchmarkError as exc:
        parser.error(str(exc))
    if not benchmarks:
        parser.error(f"no benchmark matched {args.pattern!r}")

    if args.list:
        for benchmark in benchmarks:
            print(benchmark.relative_path)
        return 0
    if args.info:
        for benchmark in benchmarks:
            print(
                f"{benchmark.relative_path}\n"
                f"  name: {benchmark.name}\n"
                f"  group: {benchmark.group}\n"
                f"  subgroup: {benchmark.subgroup or '-'}"
            )
        return 0
    if args.query:
        for benchmark in benchmarks:
            print(f"-- {benchmark.relative_path}")
            print("\n".join(benchmark.run))
        return 0
    if not args.server.is_file():
        parser.error(f"{args.server} not found; build with: cd build && make rmdb -j$(nproc)")

    records = []
    failures = 0
    timing_lines = []
    print("name\trun\ttiming")
    for benchmark in benchmarks:
        try:
            timings = run_case(
                benchmark,
                args.server,
                args.warmup_runs,
                args.timed_runs,
                args.keep_db,
            )
            for run_no, elapsed in enumerate(timings, 1):
                print(f"{benchmark.relative_path}\t{run_no}\t{elapsed:.6f}")
                timing_lines.append(f"{elapsed:.6f}")
            records.append(
                {
                    "benchmark": benchmark.relative_path,
                    "name": benchmark.name,
                    "status": "PASS",
                    "timings": timings,
                }
            )
            print(f"PASS {benchmark.relative_path}", file=sys.stderr)
        except (BenchmarkError, OSError, ConnectionError) as exc:
            failures += 1
            records.append(
                {
                    "benchmark": benchmark.relative_path,
                    "name": benchmark.name,
                    "status": "FAIL",
                    "error": str(exc),
                }
            )
            print(f"FAIL {benchmark.relative_path}: {exc}", file=sys.stderr)
            if args.fail_fast:
                break

    if args.out:
        args.out.write_text("\n".join(timing_lines) + ("\n" if timing_lines else ""), encoding="utf-8")
    if args.json:
        args.json.write_text(
            json.dumps(
                {
                    "upstream_commit": (
                        HERE / "UPSTREAM_COMMIT"
                    ).read_text(encoding="utf-8").strip(),
                    "results": records,
                },
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    print(
        f"SUMMARY: {len(records) - failures} passed, {failures} failed",
        file=sys.stderr,
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
