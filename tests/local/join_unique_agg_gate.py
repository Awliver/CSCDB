#!/usr/bin/env python3
"""JOIN、唯一索引和聚合的 Wire-v3 跨功能门禁。

本文件中的用例专门覆盖功能边界，而不是孤立测试各个子系统。特别是，对于被拒绝
的唯一索引操作，会比较操作前后的类型化状态快照；仅收到一个 ERROR 帧，不能证明
堆表和所有索引都没有发生变化。

构建 ``rmdb`` 后，在仓库根目录运行::

    python3 tests/local/join_unique_agg_gate.py

无需修改测试即可指定仓库外的构建目录::

    RMDB_BUILD_DIR=/tmp/csc-db-join-agg-build \
      python3 tests/local/join_unique_agg_gate.py
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
DB_NAME = "join_unique_agg_gate_db"
DB_REL = Path("test_dbs") / DB_NAME
DB_DIR = BUILD / DB_REL

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

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
C = SQLTYPE_CHAR


class GateFailure(AssertionError):
    """表示语义或 Wire 协议约定不匹配。"""


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


def failure_detail(result: ExecResult) -> str:
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
        raise GateFailure(f"SQL failed: {sql}\n{failure_detail(result)}")
    return result


def cell_equal(actual: object, expected: object) -> bool:
    if isinstance(expected, float):
        return isinstance(actual, (int, float)) and math.isclose(
            float(actual), expected, rel_tol=1e-6, abs_tol=1e-6
        )
    # bool 是 int 的子类，但在这些用例中绝不是合法的 SQL 整数值。
    if isinstance(expected, int):
        return isinstance(actual, int) and not isinstance(actual, bool) and actual == expected
    return actual == expected


def assert_query(
    client: WireClient,
    label: str,
    sql: str,
    expected_columns: Sequence[tuple[str, int]],
    expected_rows: Sequence[Sequence[object]],
) -> ExecResult:
    result = execute_ok(client, sql)
    expected_columns = list(expected_columns)
    expected_rows = [list(row) for row in expected_rows]
    if result.columns != expected_columns:
        raise GateFailure(
            f"{label}: metadata mismatch\n"
            f"expected={expected_columns!r}\nactual={result.columns!r}"
        )
    if len(result.rows) != len(expected_rows):
        raise GateFailure(
            f"{label}: row-count mismatch: expected {len(expected_rows)}, "
            f"actual {len(result.rows)}\nactual rows={result.rows!r}"
        )
    for row_no, (actual, expected) in enumerate(zip(result.rows, expected_rows), 1):
        if len(actual) != len(expected):
            raise GateFailure(
                f"{label}: row {row_no} width mismatch: "
                f"expected={expected!r}, actual={actual!r}"
            )
        for col_no, (actual_cell, expected_cell) in enumerate(zip(actual, expected), 1):
            if not cell_equal(actual_cell, expected_cell):
                raise GateFailure(
                    f"{label}: value mismatch at row {row_no}, column {col_no}: "
                    f"expected={expected_cell!r}, actual={actual_cell!r}\n"
                    f"all rows={result.rows!r}"
                )
    return result


def snapshot(client: WireClient, sqls: Sequence[str]) -> list[tuple[object, object]]:
    states: list[tuple[object, object]] = []
    for sql in sqls:
        result = execute_ok(client, sql)
        states.append((tuple(result.columns), tuple(tuple(row) for row in result.rows)))
    return states


def assert_rejected_without_change(
    client: WireClient,
    label: str,
    sql: str,
    state_sqls: Sequence[str],
    *,
    require_failure_frame: bool = False,
) -> ExecResult:
    before = snapshot(client, state_sqls)
    result = client.exec_stream(sql)
    if require_failure_frame and not (result.error or result.aborted):
        raise GateFailure(
            f"{label}: expected ERROR/TRANSACTION_ABORT, got {failure_detail(result)}"
        )
    after = snapshot(client, state_sqls)
    if after != before:
        raise GateFailure(
            f"{label}: rejected operation changed persistent state\n"
            f"SQL={sql}\nbefore={before!r}\nafter={after!r}\n"
            f"wire={failure_detail(result)}"
        )
    return result


def assert_plan_contains(client: WireClient, label: str, sql: str, needles: Sequence[str]) -> None:
    result = execute_ok(client, "explain analyze " + sql)
    if result.columns != [("QUERY PLAN", C)]:
        raise GateFailure(f"{label}: bad EXPLAIN metadata: {result.columns!r}")
    lines = [row[0] for row in result.rows if len(row) == 1 and isinstance(row[0], str)]
    for needle in needles:
        if not any(needle in line for line in lines):
            raise GateFailure(
                f"{label}: plan does not contain {needle!r}\n" + "\n".join(lines)
            )


class Checks:
    def __init__(self) -> None:
        self.total = 0
        self.failures: list[tuple[str, str]] = []

    def run(self, label: str, check: Callable[[], None]) -> None:
        self.total += 1
        try:
            check()
        except Exception as exc:  # 继续执行，避免一个边界缺陷遮蔽其他缺陷
            self.failures.append((label, str(exc)))
            print(f"[FAIL] {label}")
            print(str(exc))
        else:
            print(f"[PASS] {label}")


def run_checks(client: WireClient) -> Checks:
    checks = Checks()

    def outer_join_aggregate() -> None:
        setup = [
            "create table oa_l (id int, g int)",
            "create table oa_r (id int, v int)",
            "insert into oa_l values (1,10)",
            "insert into oa_l values (2,10)",
            "insert into oa_l values (3,20)",
            "insert into oa_l values (4,30)",
            "insert into oa_r values (2,5)",
            "insert into oa_r values (2,7)",
            "insert into oa_r values (3,9)",
            "insert into oa_r values (5,11)",
        ]
        for sql in setup:
            execute_ok(client, sql)

        assert_query(
            client,
            "LEFT JOIN NULL-aware aggregate",
            "select l.g,count(*) rows,count(r.v) matched,count(distinct r.v) distinct_v,"
            "sum(r.v) total,min(r.v) lo,max(r.v) hi,avg(r.v) av "
            "from oa_l l left join oa_r r on l.id=r.id "
            "group by l.g order by l.g",
            [("g", I), ("rows", I), ("matched", I), ("distinct_v", I),
             ("total", I), ("lo", I), ("hi", I), ("av", F)],
            [
                [10, 3, 2, 2, 12, 5, 7, 6.0],
                [20, 1, 1, 1, 9, 9, 9, 9.0],
                # RMDB 已有方言约定：空输入聚合最终生成零值。
                [30, 1, 0, 0, 0, 0, 0, 0.0],
            ],
        )
        assert_query(
            client,
            "hidden COUNT DISTINCT in HAVING over NULL-extended rows",
            "select l.g,count(*) rows from oa_l l left join oa_r r on l.id=r.id "
            "group by l.g having count(distinct r.v)>1 order by l.g",
            [("g", I), ("rows", I)],
            [[10, 3]],
        )

    checks.run("outer JOIN NULL aggregation + hidden DISTINCT HAVING", outer_join_aggregate)

    def natural_semi_anti_aggregate() -> None:
        setup = [
            "create table jx_l (id int, g int, score int)",
            "create table jx_r (id int, bonus int)",
            "insert into jx_l values (1,1,10)",
            "insert into jx_l values (2,1,20)",
            "insert into jx_l values (3,2,30)",
            "insert into jx_l values (4,2,40)",
            "insert into jx_r values (2,200)",
            "insert into jx_r values (3,300)",
            "insert into jx_r values (4,400)",
            "insert into jx_r values (4,401)",
        ]
        for sql in setup:
            execute_ok(client, sql)

        assert_query(
            client,
            "NATURAL JOIN aggregate",
            "select l.g,count(*) c,sum(r.bonus) total "
            "from jx_l l natural join jx_r r group by l.g order by l.g",
            [("g", I), ("c", I), ("total", I)],
            [[1, 1, 200], [2, 3, 1101]],
        )
        assert_query(
            client,
            "SEMI JOIN aggregate",
            "select l.g,count(*) c,sum(l.score) total from jx_l l "
            "semi join jx_r r on l.id=r.id group by l.g order by l.g",
            [("g", I), ("c", I), ("total", I)],
            [[1, 1, 20], [2, 2, 70]],
        )
        assert_query(
            client,
            "ANTI JOIN aggregate",
            "select l.g,count(*) c,sum(l.score) total from jx_l l "
            "anti join jx_r r on l.id=r.id group by l.g order by l.g",
            [("g", I), ("c", I), ("total", I)],
            [[1, 1, 10]],
        )

        state_sqls = [
            "select id,g,score from jx_l order by id",
            "select id,bonus from jx_r order by id,bonus",
        ]
        assert_rejected_without_change(
            client,
            "SEMI hidden-side aggregate reference",
            "select l.g,count(*) c from jx_l l semi join jx_r r on l.id=r.id "
            "group by l.g having count(r.bonus)>0",
            state_sqls,
            require_failure_frame=True,
        )
        assert_rejected_without_change(
            client,
            "ANTI hidden-side aggregate reference",
            "select l.g,count(*) c from jx_l l anti join jx_r r on l.id=r.id "
            "group by l.g having count(r.bonus)>0",
            state_sqls,
            require_failure_frame=True,
        )
        # 两次错误后立即执行合法聚合，用于证明连接和已分析的连接作用域没有
        # 残留在受污染的中间状态。
        assert_query(
            client,
            "post-rejection SEMI control",
            "select count(l.id) c from jx_l l semi join jx_r r on l.id=r.id",
            [("c", I)],
            [[3]],
        )

    checks.run("NATURAL/SEMI/ANTI aggregation + hidden-side rejection", natural_semi_anti_aggregate)

    def lateral_reentry_aggregate() -> None:
        setup = [
            "create table lx_l (seq int, id int)",
            "create table lx_r (owner int, v int)",
            "insert into lx_l values (10,1)",
            "insert into lx_l values (20,2)",
            "insert into lx_l values (30,1)",
            "insert into lx_l values (40,3)",
            "insert into lx_r values (1,10)",
            "insert into lx_r values (1,10)",
            "insert into lx_r values (1,20)",
            "insert into lx_r values (2,3)",
            "insert into lx_r values (2,7)",
            "insert into lx_r values (9,999)",
        ]
        for sql in setup:
            execute_ok(client, sql)

        sql = (
            "select l.seq,x.cd,x.av from lx_l l cross join lateral "
            "(select count(distinct r.v) cd,avg(r.v) av from lx_r r "
            "where r.owner=l.id) x order by l.seq"
        )
        columns = [("seq", I), ("cd", I), ("av", F)]
        rows = [
            [10, 2, 13.333333],
            [20, 2, 5.0],
            # 同一关联键经过另一条外层记录后再次出现：右侧聚合必须重新启动，
            # 不能携带上一次执行的状态。
            [30, 2, 13.333333],
            [40, 0, 0.0],
        ]
        assert_query(client, "LATERAL aggregate re-entry", sql, columns, rows)
        assert_query(client, "LATERAL aggregate second execution", sql, columns, rows)

    checks.run("correlated LATERAL COUNT DISTINCT/AVG repeated re-entry", lateral_reentry_aggregate)

    def unique_index_lifecycle() -> None:
        # 在重复数据上建索引失败后，不得残留已注册索引或临时状态；删除重复记录后，
        # 原样重试同一条 DDL 必须成功。
        for sql in [
            "create table ui_dirty (k int, v int)",
            "insert into ui_dirty values (1,10)",
            "insert into ui_dirty values (1,20)",
            "insert into ui_dirty values (2,30)",
        ]:
            execute_ok(client, sql)
        dirty_state = [
            "select k,v from ui_dirty order by k,v",
            "desc ui_dirty",
        ]
        assert_rejected_without_change(
            client,
            "CREATE INDEX rejects existing duplicate keys atomically",
            "create index ui_dirty(k)",
            dirty_state,
            require_failure_frame=True,
        )
        assert_query(
            client,
            "failed index build leaves metadata unregistered",
            "desc ui_dirty",
            [("Field", C), ("Type", C), ("Index", C)],
            [["k", "INT", "NO"], ["v", "INT", "NO"]],
        )
        execute_ok(client, "delete from ui_dirty where v=20")
        execute_ok(client, "create index ui_dirty(k)")
        assert_query(
            client,
            "same-name retry after failed build",
            "desc ui_dirty",
            [("Field", C), ("Type", C), ("Index", C)],
            [["k", "INT", "YES"], ["v", "INT", "NO"]],
        )

        for sql in [
            "create table ui_live (k int, v int)",
            "insert into ui_live values (1,10)",
            "insert into ui_live values (2,20)",
            "insert into ui_live values (3,30)",
            "create index ui_live(k)",
        ]:
            execute_ok(client, sql)

        live_state = [
            "select k,v from ui_live order by k",
            "desc ui_live",
        ]
        assert_rejected_without_change(
            client,
            "duplicate CREATE INDEX",
            "create index ui_live(k)",
            live_state,
            require_failure_frame=True,
        )
        # 旧 DML 路径在拒绝唯一性冲突时可能返回 COMMAND_OK；权威判据是堆表和
        # 索引状态均未发生变化。
        assert_rejected_without_change(
            client,
            "duplicate indexed INSERT",
            "insert into ui_live values (2,999)",
            live_state,
        )
        assert_rejected_without_change(
            client,
            "duplicate-key UPDATE",
            "update ui_live set k=2 where k=3",
            live_state,
        )

        execute_ok(client, "delete from ui_live where k=2")
        execute_ok(client, "insert into ui_live values (2,222)")
        baseline_columns = [("k", I), ("v", I)]
        baseline_rows = [[1, 10], [2, 222], [3, 30]]
        assert_query(
            client,
            "DELETE releases the unique key",
            "select k,v from ui_live order by k",
            baseline_columns,
            baseline_rows,
        )

        # 在同一事务中回滚一次 DELETE、一次 INSERT 和一次索引列 UPDATE。随后检查
        # 所有受影响键：旧键必须仍被占用，仅由已回滚事务引入的键必须可以复用。
        for sql in [
            "begin",
            "delete from ui_live where k=2",
            "insert into ui_live values (4,444)",
            "update ui_live set k=5 where k=3",
        ]:
            execute_ok(client, sql)
        assert_query(
            client,
            "read-your-writes before rollback",
            "select k,v from ui_live order by k",
            baseline_columns,
            [[1, 10], [4, 444], [5, 30]],
        )
        execute_ok(client, "rollback")
        assert_query(
            client,
            "rollback restores heap values",
            "select k,v from ui_live order by k",
            baseline_columns,
            baseline_rows,
        )
        assert_rejected_without_change(
            client,
            "rollback restores old unique key ownership",
            "insert into ui_live values (2,999)",
            ["select k,v from ui_live order by k", "desc ui_live"],
        )
        execute_ok(client, "insert into ui_live values (4,444)")
        execute_ok(client, "insert into ui_live values (5,555)")
        assert_query(
            client,
            "rollback releases inserted and updated-to keys",
            "select k,v from ui_live order by k",
            baseline_columns,
            [[1, 10], [2, 222], [3, 30], [4, 444], [5, 555]],
        )

    checks.run("unique index create/DML/release/rollback lifecycle", unique_index_lifecycle)

    def indexed_inner_join_aggregate() -> None:
        setup = [
            "create table uj_fact (rid int, k int, amount int)",
            "create table uj_dim (k int, weight int)",
            "insert into uj_fact values (10,1,5)",
            "insert into uj_fact values (11,1,7)",
            "insert into uj_fact values (12,2,11)",
            "insert into uj_fact values (13,4,13)",
            "insert into uj_fact values (14,9,99)",
            "insert into uj_dim values (1,100)",
            "insert into uj_dim values (2,200)",
            "insert into uj_dim values (3,300)",
            "insert into uj_dim values (4,400)",
            "create index uj_dim(k)",
        ]
        for sql in setup:
            execute_ok(client, sql)

        sql = (
            "select d.k,count(*) c,sum(f.amount) total,avg(f.amount) av "
            "from uj_fact f inner join uj_dim d on f.k=d.k "
            "group by d.k order by d.k"
        )
        assert_query(
            client,
            "unique-index INNER JOIN aggregate values",
            sql,
            [("k", I), ("c", I), ("total", I), ("av", F)],
            [[1, 2, 12, 6.0], [2, 1, 11, 11.0], [4, 1, 13, 13.0]],
        )
        assert_query(
            client,
            "unique-index JOIN hidden aggregate HAVING",
            "select d.k,sum(f.amount) total from uj_fact f inner join uj_dim d on f.k=d.k "
            "group by d.k having count(distinct f.amount)>1 order by d.k",
            [("k", I), ("total", I)],
            [[1, 12]],
        )
        assert_plan_contains(
            client,
            "aggregate query retains INLJ access path",
            sql,
            ["Aggregate(", "type=IndexScan", "using_index=(k)"],
        )

    checks.run("unique-index INNER JOIN + aggregate/hidden HAVING", indexed_inner_join_aggregate)
    return checks


def main() -> int:
    if not SERVER.is_file():
        print(f"[FATAL] server binary not found: {SERVER}", file=sys.stderr)
        print("Build it first with: cmake --build build -j", file=sys.stderr)
        return 2
    if port_is_open():
        print(
            f"[FATAL] 127.0.0.1:{PORT} is already in use; "
            "refusing to kill an unrelated server",
            file=sys.stderr,
        )
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
                print(
                    f"\nJOIN/UNIQUE/AGG gate: "
                    f"{checks.total - len(checks.failures)}/{checks.total} PASS",
                    file=sys.stderr,
                )
                for label, detail in checks.failures:
                    print(f"- {label}: {detail.splitlines()[0]}", file=sys.stderr)
            if log_tail.strip():
                print("\nServer log tail:", file=sys.stderr)
                print(log_tail, file=sys.stderr)
            return 1

    print(f"\nJOIN/UNIQUE/AGG gate: {checks.total}/{checks.total} PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
