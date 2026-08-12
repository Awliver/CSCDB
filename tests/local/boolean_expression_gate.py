#!/usr/bin/env python3
"""AND/OR/NOT/括号条件表达式的独立 Wire-v3 回归门禁。

在仓库根目录执行：

    python3 -B tests/local/boolean_expression_gate.py

可通过 RMDB_BUILD_DIR 指定构建目录。门禁覆盖 WHERE、ON、HAVING、
UPDATE、DELETE、索引残余谓词、LATERAL 相关谓词以及 SQL 三值逻辑。
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
DB_REL = Path("test_dbs") / "boolean_expression_gate_db"
DB_DIR = BUILD / DB_REL

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

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
    client: WireClient, label: str, sql: str, expected: Sequence[Sequence[object]]
) -> None:
    result = execute_ok(client, sql)
    rows = [list(row) for row in expected]
    if result.rows != rows:
        raise GateFailure(
            f"{label}: rows mismatch\nSQL={sql}\nexpected={rows!r}\nactual={result.rows!r}"
        )


def assert_rejected(client: WireClient, label: str, sql: str) -> None:
    result = client.exec_stream(sql)
    if not (result.error or result.aborted):
        raise GateFailure(f"{label}: expected rejection, got {describe(result)}")


def run_checks(client: WireClient) -> int:
    for sql in [
        "create table t (id int,a int,b int,c int,g int)",
        "insert into t values (1,1,0,1,1)",
        "insert into t values (2,0,1,1,1)",
        "insert into t values (3,0,1,0,2)",
        "insert into t values (4,0,0,1,2)",
        "insert into t values (5,0,0,0,3)",
        "create index t (a,b,c)",
    ]:
        execute_ok(client, sql)

    checks: list[tuple[str, str, list[list[object]]]] = [
        (
            "NOT/AND/OR precedence",
            "select id from t where a=1 or b=1 and not c=1 order by id",
            [[1], [3]],
        ),
        (
            "parentheses override precedence",
            "select id from t where (a=1 or b=1) and not c=1 order by id",
            [[3]],
        ),
        ("right-associative unary NOT", "select id from t where not not a=1", [[1]]),
        (
            "OR does not become an unsafe index bound",
            "select id from t where a=1 or b=1 order by id",
            [[1], [2], [3]],
        ),
        (
            "safe index conjunct plus full residual tree",
            "select id from t where a=0 and (b=1 or c=1) order by id",
            [[2], [3], [4]],
        ),
        (
            "NOT falls back without losing rows",
            "select id from t where not a=1 order by id",
            [[2], [3], [4], [5]],
        ),
        (
            "HAVING precedence and hidden aggregates",
            "select g from t group by g "
            "having count(*)=1 or max(a)=1 and not min(c)=0 order by g",
            [[1], [3]],
        ),
        (
            "HAVING parentheses",
            "select g from t group by g "
            "having (count(*)=1 or max(a)=1) and not min(c)=0 order by g",
            [[1]],
        ),
        (
            "UNION branches preserve their expression roots",
            "select * from (select id from t where id=1 or id=3 "
            "union select id from t where not (id=1 or id=2 or id=3 or id=4)) "
            "as u order by id",
            [[1], [3], [5]],
        ),
    ]
    for label, sql, expected in checks:
        assert_rows(client, label, sql, expected)
        print(f"[PASS] {label}")

    explain = execute_ok(
        client,
        "explain analyze select id from t where a=1 or b=1 and not c=1",
    )
    plan_text = "\n".join(
        str(row[0]) for row in explain.rows if len(row) == 1 and row[0] is not None
    )
    if " OR " not in plan_text or " AND " not in plan_text or "NOT " not in plan_text:
        raise GateFailure(f"EXPLAIN lost boolean tree topology:\n{plan_text}")
    print("[PASS] EXPLAIN renders the complete boolean tree")

    for sql in [
        "create table l (id int,flag int)",
        "create table r (id int,flag int)",
        "insert into l values (1,0)",
        "insert into l values (2,0)",
        "insert into l values (3,1)",
        "insert into r values (1,0)",
        "insert into r values (2,1)",
        "insert into r values (4,1)",
        "create index r (id)",
    ]:
        execute_ok(client, sql)

    join_checks: list[tuple[str, str, list[list[object]]]] = [
        (
            "arbitrary ON tree",
            "select l.id as lid,r.id as rid from l join r "
            "on l.id=r.id or (l.flag=1 and not r.flag=0) order by l.id,r.id",
            [[1, 1], [2, 2], [3, 2], [3, 4]],
        ),
        (
            "NOT UNKNOWN stays UNKNOWN after LEFT JOIN",
            "select l.id as lid,r.id as rid from l left join r on l.id=r.id "
            "where not (r.flag=1) order by l.id",
            [[1, 1]],
        ),
        (
            "TRUE OR UNKNOWN is TRUE",
            "select l.id as lid,r.id as rid from l left join r on l.id=r.id "
            "where l.id=3 or r.flag=0 order by l.id",
            [[1, 1], [3, None]],
        ),
        (
            "SEMI JOIN evaluates the complete ON tree",
            "select l.id from l semi join r "
            "on l.id=r.id or (l.flag=1 and r.flag=1) order by l.id",
            [[1], [2], [3]],
        ),
        (
            "correlated LATERAL OR tree",
            "select l.id as lid,x.id as rid from l join lateral "
            "(select id from r where r.id=l.id or l.flag=1) x on true "
            "order by l.id,x.id",
            [[1, 1], [2, 2], [3, 1], [3, 2], [3, 4]],
        ),
    ]
    for label, sql, expected in join_checks:
        assert_rows(client, label, sql, expected)
        print(f"[PASS] {label}")

    assert_rows(
        client,
        "HAVING keeps NOT UNKNOWN as UNKNOWN",
        "select r.flag from l left join r on l.id=r.id group by r.flag "
        "having not (r.flag=1) order by r.flag",
        [[0]],
    )
    print("[PASS] HAVING keeps NOT UNKNOWN as UNKNOWN")

    execute_ok(client, "create table z (id int)")
    execute_ok(client, "insert into z values (1)")
    assert_rows(
        client,
        "ON keeps NOT UNKNOWN as UNKNOWN",
        "select l.id from l left join r on l.id=r.id join z "
        "on not (r.flag=1) order by l.id",
        [[1]],
    )
    print("[PASS] ON keeps NOT UNKNOWN as UNKNOWN")

    for sql in [
        "create table lc (k int,marker int)",
        "create table rc (k int,extra int,payload int)",
        "insert into lc values (1,10)",
        "insert into lc values (2,20)",
        "insert into rc values (1,100,1000)",
        "insert into rc values (1,101,1001)",
        "insert into rc values (2,200,2000)",
        "create index rc (k,extra)",
    ]:
        execute_ok(client, sql)
    assert_rows(
        client,
        "INLJ rejects an incomplete composite exact key",
        "select lc.k,rc.extra from lc join rc on lc.k=rc.k order by lc.k,rc.extra",
        [[1, 100], [1, 101], [2, 200]],
    )
    print("[PASS] INLJ rejects an incomplete composite exact key")

    execute_ok(client, "update t set g=9 where id=4 or not (a=0)")
    assert_rows(
        client,
        "UPDATE boolean expression",
        "select id,g from t where g=9 order by id",
        [[1, 9], [4, 9]],
    )
    print("[PASS] UPDATE boolean expression")

    execute_ok(client, "delete from t where (id=2 or id=5) and not g=9")
    assert_rows(
        client,
        "DELETE boolean expression",
        "select id,g from t order by id",
        [[1, 9], [3, 2], [4, 9]],
    )
    print("[PASS] DELETE boolean expression")

    for label, sql in [
        ("empty parentheses", "select id from t where ()"),
        ("dangling NOT", "select id from t where not"),
        ("dangling OR", "select id from t where id=1 or"),
        ("missing right parenthesis", "select id from t where (id=1 or id=3"),
    ]:
        assert_rejected(client, label, sql)
        print(f"[PASS] rejects {label}")

    return len(checks) + 1 + len(join_checks) + 2 + 1 + 2 + 4


def main() -> int:
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
            total = run_checks(client)
        except Exception as exc:
            print(f"[FATAL] {exc}", file=sys.stderr)
            server_log.seek(0)
            log_tail = server_log.read().decode("utf-8", errors="replace")[-6000:]
            if log_tail.strip():
                print("\nServer log tail:", file=sys.stderr)
                print(log_tail, file=sys.stderr)
            return 1
        finally:
            if client is not None:
                client.close()
            stop_server(proc)

    print(f"\nBoolean-expression gate: {total}/{total} PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
