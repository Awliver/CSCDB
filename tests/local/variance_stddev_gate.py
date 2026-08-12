#!/usr/bin/env python3
"""VARIANCE/STDDEV 聚合的独立 Wire-v3 本地功能门禁。

在仓库根目录执行：

    python3 -B tests/local/variance_stddev_gate.py

可通过 RMDB_BUILD_DIR 指定构建目录。项目将 VARIANCE/STDDEV 定义为总体
方差/总体标准差（除以 N），空输入和全 NULL 输入按项目方言返回 0.0。
"""

from __future__ import annotations

import math
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Callable, Sequence


ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("RMDB_BUILD_DIR", str(ROOT / "build"))).resolve()
SERVER = BUILD / "bin" / "rmdb"
DB_REL = Path("test_dbs") / "variance_stddev_gate_db"
DB_DIR = BUILD / DB_REL

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
    if result.aborted:
        kind = "TRANSACTION_ABORT"
    elif result.error:
        kind = "ERROR"
    else:
        kind = "COMMAND_OK"
    return f"{kind}: {result.diagnostic}" if result.diagnostic else kind


def execute_ok(client: WireClient, sql: str) -> ExecResult:
    result = client.exec_stream(sql)
    if result.error or result.aborted or not result.ok:
        raise GateFailure(f"SQL failed: {sql}\n{describe(result)}")
    return result


def cell_equal(actual: object, expected: object) -> bool:
    if isinstance(expected, float):
        return isinstance(actual, (int, float)) and math.isclose(
            float(actual), expected, rel_tol=1e-5, abs_tol=1e-5
        )
    if isinstance(expected, int):
        return isinstance(actual, int) and not isinstance(actual, bool) and actual == expected
    return actual == expected


def assert_query(
    client: WireClient,
    label: str,
    sql: str,
    expected_columns: Sequence[tuple[str, int]],
    expected_rows: Sequence[Sequence[object]],
) -> None:
    result = execute_ok(client, sql)
    columns = list(expected_columns)
    rows = [list(row) for row in expected_rows]
    if result.columns != columns:
        raise GateFailure(
            f"{label}: metadata mismatch\nexpected={columns!r}\nactual={result.columns!r}"
        )
    if len(result.rows) != len(rows):
        raise GateFailure(
            f"{label}: row-count mismatch; expected={rows!r}, actual={result.rows!r}"
        )
    for row_no, (actual, expected) in enumerate(zip(result.rows, rows), 1):
        if len(actual) != len(expected):
            raise GateFailure(
                f"{label}: row {row_no} width mismatch; expected={expected!r}, actual={actual!r}"
            )
        for col_no, (actual_cell, expected_cell) in enumerate(zip(actual, expected), 1):
            if not cell_equal(actual_cell, expected_cell):
                raise GateFailure(
                    f"{label}: row {row_no}, column {col_no}: "
                    f"expected={expected_cell!r}, actual={actual_cell!r}\n"
                    f"all rows={result.rows!r}"
                )


def assert_error(client: WireClient, label: str, sql: str) -> None:
    result = client.exec_stream(sql)
    if not (result.error or result.aborted):
        raise GateFailure(f"{label}: expected rejection, got {describe(result)}")


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
            print(f"[FAIL] {label}")
            print(str(exc))
        else:
            print(f"[PASS] {label}")


def run_checks(client: WireClient) -> Checks:
    checks = Checks()

    for sql in [
        "create table vs (g int, i int, f float, s char(8))",
        "insert into vs values (1,1,-1.0,'a')",
        "insert into vs values (1,2,1.0,'b')",
        "insert into vs values (1,3,3.0,'c')",
        "insert into vs values (1,4,5.0,'d')",
        "insert into vs values (2,10,7.0,'e')",
    ]:
        execute_ok(client, sql)

    def plain_numeric_and_schema() -> None:
        assert_query(
            client,
            "plain population statistics",
            "select variance(i) vi,stddev(i) si,variance(f) vf,stddev(f) sf from vs",
            [("vi", F), ("si", F), ("vf", F), ("sf", F)],
            [[10.0, math.sqrt(10.0), 8.0, math.sqrt(8.0)]],
        )

    checks.run("plain INT/FLOAT values, population semantics and Wire schema", plain_numeric_and_schema)

    def grouping_and_state_isolation() -> None:
        assert_query(
            client,
            "grouped population statistics",
            "select g,variance(i) vi,stddev(i) si,variance(f) vf,stddev(f) sf "
            "from vs group by g order by g",
            [("g", I), ("vi", F), ("si", F), ("vf", F), ("sf", F)],
            [[1, 1.25, math.sqrt(1.25), 5.0, math.sqrt(5.0)], [2, 0.0, 0.0, 0.0, 0.0]],
        )

    checks.run("GROUP BY state isolation and singleton groups", grouping_and_state_isolation)

    def hidden_having() -> None:
        assert_query(
            client,
            "hidden VARIANCE HAVING",
            "select g,count(*) c from vs group by g having variance(i)>1 order by g",
            [("g", I), ("c", I)],
            [[1, 4]],
        )
        assert_query(
            client,
            "hidden STDDEV HAVING",
            "select g,count(*) c from vs group by g having stddev(f)>2 order by g",
            [("g", I), ("c", I)],
            [[1, 4]],
        )

    checks.run("hidden HAVING aggregates", hidden_having)

    def empty_invalid_and_stability() -> None:
        assert_query(
            client,
            "empty input",
            "select variance(i) vi,stddev(i) si,variance(f) vf,stddev(f) sf from vs where g=99",
            [("vi", F), ("si", F), ("vf", F), ("sf", F)],
            [[0.0, 0.0, 0.0, 0.0]],
        )
        for sql in [
            "create table vs_large (i int)",
            "insert into vs_large values (1000000000)",
            "insert into vs_large values (1000000001)",
            "insert into vs_large values (1000000002)",
        ]:
            execute_ok(client, sql)
        assert_query(
            client,
            "large-offset Welford stability",
            "select variance(i) vi,stddev(i) si from vs_large",
            [("vi", F), ("si", F)],
            [[2.0 / 3.0, math.sqrt(2.0 / 3.0)]],
        )
        invalid = [
            ("VARIANCE CHAR", "select variance(s) from vs"),
            ("STDDEV CHAR", "select stddev(s) from vs"),
            ("VARIANCE star", "select variance(*) from vs"),
            ("STDDEV star", "select stddev(*) from vs"),
            ("VARIANCE DISTINCT", "select variance(distinct i) from vs"),
            ("STDDEV DISTINCT", "select stddev(distinct i) from vs"),
        ]
        for label, sql in invalid:
            assert_error(client, label, sql)
        assert_query(
            client,
            "post-error control",
            "select count(*) c,variance(i) vi from vs",
            [("c", I), ("vi", F)],
            [[5, 10.0]],
        )

    checks.run("empty input, numerical stability and invalid contracts", empty_invalid_and_stability)

    def outer_join_nulls() -> None:
        for sql in [
            "create table vs_key (id int)",
            "create table vs_fact (id int, v int)",
            "insert into vs_key values (1)",
            "insert into vs_key values (2)",
            "insert into vs_key values (3)",
            "insert into vs_fact values (1,2)",
            "insert into vs_fact values (1,4)",
            "insert into vs_fact values (2,-4)",
        ]:
            execute_ok(client, sql)
        assert_query(
            client,
            "LEFT JOIN NULL input",
            "select k.id,count(*) rows,count(f.v) vals,variance(f.v) vi,stddev(f.v) si "
            "from vs_key k left join vs_fact f on k.id=f.id group by k.id order by k.id",
            [("id", I), ("rows", I), ("vals", I), ("vi", F), ("si", F)],
            [[1, 2, 2, 1.0, 1.0], [2, 1, 1, 0.0, 0.0], [3, 1, 0, 0.0, 0.0]],
        )

    checks.run("LEFT JOIN generated NULL input", outer_join_nulls)

    def lateral_reentry() -> None:
        for sql in [
            "create table vs_outer (seq int, id int)",
            "insert into vs_outer values (10,1)",
            "insert into vs_outer values (20,2)",
            "insert into vs_outer values (30,1)",
            "insert into vs_outer values (40,3)",
        ]:
            execute_ok(client, sql)
        query = (
            "select o.seq,x.vi,x.si from vs_outer o cross join lateral "
            "(select variance(f.v) vi,stddev(f.v) si from vs_fact f where f.id=o.id) x "
            "order by o.seq"
        )
        expected = [[10, 1.0, 1.0], [20, 0.0, 0.0], [30, 1.0, 1.0], [40, 0.0, 0.0]]
        columns = [("seq", I), ("vi", F), ("si", F)]
        assert_query(client, "LATERAL first execution", query, columns, expected)
        assert_query(client, "LATERAL repeated execution", query, columns, expected)

    checks.run("correlated LATERAL repeated aggregate restart", lateral_reentry)
    return checks


def main() -> int:
    if not SERVER.is_file():
        print(f"[FATAL] server binary not found: {SERVER}", file=sys.stderr)
        print("Build it first with: cmake --build build --target rmdb -j2", file=sys.stderr)
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
    with tempfile.TemporaryFile() as server_log:
        try:
            proc = subprocess.Popen(
                [str(SERVER), str(DB_REL)],
                cwd=BUILD,
                stdout=server_log,
                stderr=subprocess.STDOUT,
            )
            wait_ready(timeout=15.0)
            client = WireClient(timeout=60)
            checks = run_checks(client)
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
                print(f"\nVARIANCE/STDDEV gate: {passed}/{checks.total} PASS", file=sys.stderr)
                for label, detail in checks.failures:
                    print(f"- {label}: {detail.splitlines()[0]}", file=sys.stderr)
            if log_tail.strip():
                print("\nServer log tail:", file=sys.stderr)
                print(log_tail, file=sys.stderr)
            return 1

    print(f"\nVARIANCE/STDDEV gate: {checks.total}/{checks.total} PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
