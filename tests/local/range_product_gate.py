#!/usr/bin/env python3
"""RANGE/PRODUCT 聚合的独立 Wire-v3 本地功能门禁。

在仓库根目录执行：

    python3 -B tests/local/range_product_gate.py

可通过 RMDB_BUILD_DIR 指定构建目录。测试只使用 int32 结果可表示的 PRODUCT
数据；超出 int32 的结果不属于当前 SAME_AS_ARGUMENT 输出契约。
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
DB_REL = Path("test_dbs") / "range_product_gate_db"
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
            float(actual), expected, rel_tol=1e-6, abs_tol=1e-6
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
        "create table rp (g int, i int, f float, s char(8))",
        "insert into rp values (1,-2,1.5,'a')",
        "insert into rp values (1,3,-2.0,'b')",
        "insert into rp values (1,4,4.0,'c')",
        "insert into rp values (2,0,0.5,'d')",
        "insert into rp values (2,5,2.0,'e')",
        "insert into rp values (2,-1,-3.0,'f')",
    ]:
        execute_ok(client, sql)

    def plain_numeric_and_schema() -> None:
        assert_query(
            client,
            "plain INT",
            "select min(i) mn,max(i) mx,RANGE(i) ri,PRODUCT(i) pi,sum(i) sm,count(*) c from rp",
            [("mn", I), ("mx", I), ("ri", I), ("pi", I), ("sm", I), ("c", I)],
            [[-2, 5, 7, 0, 9, 6]],
        )
        assert_query(
            client,
            "plain FLOAT",
            "select min(f) mn,max(f) mx,range(f) rf,product(f) pf,sum(f) sm from rp",
            [("mn", F), ("mx", F), ("rf", F), ("pf", F), ("sm", F)],
            [[-3.0, 4.0, 7.0, 36.0, 3.0]],
        )

    checks.run("plain INT/FLOAT values and Wire schema", plain_numeric_and_schema)

    def grouping_and_state_isolation() -> None:
        assert_query(
            client,
            "grouped RANGE/PRODUCT",
            "select g,range(i) ri,product(i) pi,range(f) rf,product(f) pf "
            "from rp group by g order by g",
            [("g", I), ("ri", I), ("pi", I), ("rf", F), ("pf", F)],
            [[1, 6, -24, 6.0, -12.0], [2, 6, 0, 5.0, -3.0]],
        )

    checks.run("GROUP BY state isolation, zero and negative product", grouping_and_state_isolation)

    def hidden_having() -> None:
        assert_query(
            client,
            "hidden PRODUCT HAVING",
            "select g,count(*) c from rp group by g having product(i)<0 order by g",
            [("g", I), ("c", I)],
            [[1, 3]],
        )
        assert_query(
            client,
            "hidden RANGE HAVING",
            "select g,count(*) c from rp group by g having range(f)>5 order by g",
            [("g", I), ("c", I)],
            [[1, 3]],
        )

    checks.run("hidden HAVING aggregates", hidden_having)

    def empty_and_invalid_contracts() -> None:
        assert_query(
            client,
            "empty input",
            "select range(i) ri,product(i) pi,range(f) rf,product(f) pf from rp where g=99",
            [("ri", I), ("pi", I), ("rf", F), ("pf", F)],
            [[0, 0, 0.0, 0.0]],
        )
        invalid = [
            ("RANGE CHAR", "select range(s) from rp"),
            ("PRODUCT CHAR", "select product(s) from rp"),
            ("RANGE star", "select range(*) from rp"),
            ("PRODUCT star", "select product(*) from rp"),
            ("RANGE DISTINCT", "select range(distinct i) from rp"),
            ("PRODUCT DISTINCT", "select product(distinct i) from rp"),
        ]
        for label, sql in invalid:
            assert_error(client, label, sql)
        assert_query(
            client,
            "post-error control",
            "select count(*) c,range(i) r from rp",
            [("c", I), ("r", I)],
            [[6, 7]],
        )

    checks.run("empty input and invalid argument contracts", empty_and_invalid_contracts)

    def outer_join_nulls() -> None:
        for sql in [
            "create table rp_key (id int)",
            "create table rp_fact (id int, v int)",
            "insert into rp_key values (1)",
            "insert into rp_key values (2)",
            "insert into rp_key values (3)",
            "insert into rp_fact values (1,2)",
            "insert into rp_fact values (1,3)",
            "insert into rp_fact values (2,-4)",
        ]:
            execute_ok(client, sql)
        assert_query(
            client,
            "LEFT JOIN NULL input",
            "select k.id,count(*) rows,count(f.v) vals,range(f.v) r,product(f.v) p "
            "from rp_key k left join rp_fact f on k.id=f.id group by k.id order by k.id",
            [("id", I), ("rows", I), ("vals", I), ("r", I), ("p", I)],
            [[1, 2, 2, 1, 6], [2, 1, 1, 0, -4], [3, 1, 0, 0, 0]],
        )

    checks.run("LEFT JOIN generated NULL input", outer_join_nulls)

    def lateral_reentry() -> None:
        for sql in [
            "create table rp_outer (seq int, id int)",
            "insert into rp_outer values (10,1)",
            "insert into rp_outer values (20,2)",
            "insert into rp_outer values (30,1)",
            "insert into rp_outer values (40,3)",
        ]:
            execute_ok(client, sql)
        query = (
            "select o.seq,x.r,x.p from rp_outer o cross join lateral "
            "(select range(f.v) r,product(f.v) p from rp_fact f where f.id=o.id) x "
            "order by o.seq"
        )
        expected = [[10, 1, 6], [20, 0, -4], [30, 1, 6], [40, 0, 0]]
        columns = [("seq", I), ("r", I), ("p", I)]
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
                print(f"\nRANGE/PRODUCT gate: {passed}/{checks.total} PASS", file=sys.stderr)
                for label, detail in checks.failures:
                    print(f"- {label}: {detail.splitlines()[0]}", file=sys.stderr)
            if log_tail.strip():
                print("\nServer log tail:", file=sys.stderr)
                print(log_tail, file=sys.stderr)
            return 1

    print(f"\nRANGE/PRODUCT gate: {checks.total}/{checks.total} PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
