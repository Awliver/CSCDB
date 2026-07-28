"""Shared helpers for consistency regression tests (audit C1–M8)."""

from __future__ import annotations

import os
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Sequence

_HERE = os.path.dirname(os.path.abspath(__file__))
_LOCAL = os.path.dirname(_HERE)
if _LOCAL not in sys.path:
    sys.path.insert(0, _LOCAL)

from tpcc_common import (  # noqa: E402
    BUILD,
    RMDB,
    RmdbClient,
    kill_rmdb,
    parse_count,
    parse_table_rows,
    start_rmdb,
)

CaseFn = Callable[[], "CaseResult"]


@dataclass
class CaseResult:
    case_id: str
    title: str
    passed: bool
    detail: str = ""
    flaky: bool = False
    attempts: int = 1
    tags: Sequence[str] = field(default_factory=tuple)


@dataclass
class CaseSpec:
    case_id: str
    title: str
    fn: CaseFn
    tags: Sequence[str]
    flaky: bool = False
    quick: bool = True
    expect_fail: bool = False  # known gap; XPASS when fixed


def ensure_rmdb_binary() -> None:
    if not os.path.isfile(RMDB):
        raise FileNotFoundError(RMDB + " not found; run: cd build && make rmdb -j$(nproc)")


def _wait_port_ready(timeout: float = 8.0) -> None:
    from wire_client import wait_ready

    wait_ready(timeout=timeout)


def fresh_db(db_name: str, log_path: Optional[str] = None):
    """Start rmdb on a clean database directory."""
    ensure_rmdb_binary()
    proc, dbpath = start_rmdb(db_name, log_path=log_path)
    _wait_port_ready(timeout=8.0)
    return proc, dbpath


def stop_server(proc) -> None:
    if proc is None:
        return
    try:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)
    except (subprocess.TimeoutExpired, OSError):
        try:
            proc.kill()
        except OSError:
            pass
    kill_rmdb()


def new_client(timeout: int = 120, retries: int = 8) -> RmdbClient:
    last = None
    for _ in range(retries):
        try:
            return RmdbClient(timeout=timeout)
        except OSError as exc:
            last = exc
            time.sleep(0.2)
    raise last  # type: ignore[misc]


def sql_ok(cli: RmdbClient, sql: str) -> tuple:
    r = cli.query(sql)
    low = r.lower()
    ok = "error" not in low
    return ok, r


def sql(client: RmdbClient, sql: str) -> str:
    return client.query(sql)


def setup_si_table(
    db_name: str,
    *,
    indexed: bool = False,
    rows: Optional[List[tuple]] = None,
) -> tuple:
    """Create t(id int, v int), optional unique index, optional seed rows."""
    proc, _ = fresh_db(db_name)
    cli = new_client()
    sql_ok(cli, "set transaction isolation level snapshot isolation;")
    ok, r = sql_ok(cli, "create table t (id int, v int);")
    if not ok:
        cli.close()
        stop_server(proc)
        raise RuntimeError("create table failed: " + r)
    if indexed:
        ok, r = sql_ok(cli, "create index t (id);")
        if not ok:
            cli.close()
            stop_server(proc)
            raise RuntimeError("create index failed: " + r)
    if rows:
        for rid, val in rows:
            ok, r = sql_ok(cli, "insert into t values (%d, %d);" % (rid, val))
            if not ok:
                cli.close()
                stop_server(proc)
                raise RuntimeError("seed insert failed: " + r)
    return proc, cli


def setup_padded_table(db_name: str, n_rows: int, pad_len: int = 480) -> tuple:
    """Wide rows to slow seq scan; seed without MVCC (auto-commit) so any_mvcc_dirty_ stays false."""
    if pad_len > 504:
        pad_len = 504  # RM_MAX_RECORD_SIZE=512, minus two ints
    proc, _ = fresh_db(db_name)
    cli = new_client(timeout=300)
    sql_ok(cli, "set transaction isolation level snapshot isolation;")
    ok, r = sql_ok(cli, "create table t (id int, v int, pad char(%d));" % pad_len)
    if not ok:
        cli.close()
        stop_server(proc)
        raise RuntimeError("create padded table failed: " + r)
    filler = "x" * pad_len
    for i in range(1, n_rows + 1):
        ok, r = sql_ok(
            cli,
            "insert into t values (%d, %d, '%s');" % (i, i * 10, filler),
        )
        if not ok:
            cli.close()
            stop_server(proc)
            raise RuntimeError("padded insert failed at %d: %s" % (i, r))
    return proc, cli


def pre_dirty_table(cli: RmdbClient) -> None:
    """One explicit SI txn write so any_mvcc_dirty_ / MVCC paths are active globally."""
    sql(cli, "begin;")
    sql(cli, "insert into t values (0, 0);")
    sql(cli, "commit;")
    sql(cli, "begin;")
    sql(cli, "delete from t where id = 0;")
    sql(cli, "commit;")


def setup_big_table(db_name: str, n_rows: int) -> tuple:
    proc, cli = setup_si_table(db_name, indexed=False)
    for i in range(1, n_rows + 1):
        ok, r = sql_ok(cli, "insert into t values (%d, %d);" % (i, i * 10))
        if not ok:
            cli.close()
            stop_server(proc)
            raise RuntimeError("bulk insert failed at %d: %s" % (i, r))
    return proc, cli


def row_ids(cli: RmdbClient) -> List[int]:
    out = []
    for cells in parse_table_rows(cli.query("select id from t;")):
        out.append(int(float(cells[0])))
    return sorted(out)


def count_rows(cli: RmdbClient) -> int:
    return parse_count(cli.query("select count(*) from t;"))


def output_txt_path(db_name: str) -> str:
    return os.path.join(BUILD, db_name, "output.txt")


def output_txt_size(db_name: str) -> int:
    path = output_txt_path(db_name)
    return os.path.getsize(path) if os.path.isfile(path) else 0


def restart_db(db_name: str):
    """Restart rmdb reusing existing database directory (for crash recovery tests)."""
    ensure_rmdb_binary()
    kill_rmdb()
    proc = subprocess.Popen([RMDB, db_name], cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    _wait_port_ready(timeout=8.0)
    return proc


def run_with_repeat(fn: CaseFn, repeat: int) -> CaseResult:
    """Run a potentially flaky case multiple times; all attempts must pass."""
    last: Optional[CaseResult] = None
    for i in range(repeat):
        last = fn()
        if not last.passed:
            last.attempts = i + 1
            return last
    assert last is not None
    last.attempts = repeat
    return last


class Barrier:
    def __init__(self, parties: int):
        self._evt = threading.Event()
        self._parties = parties
        self._count = 0
        self._lock = threading.Lock()

    def wait(self, timeout: float = 30.0) -> bool:
        with self._lock:
            self._count += 1
            need = self._count >= self._parties
        if need:
            self._evt.set()
            return True
        return self._evt.wait(timeout)


def join_or_fail(threads: Sequence[threading.Thread], timeout: float = 120.0) -> Optional[str]:
    deadline = time.perf_counter() + timeout
    for t in threads:
        remaining = deadline - time.perf_counter()
        if remaining <= 0:
            return "thread join timeout"
        t.join(remaining)
        if t.is_alive():
            return "thread join timeout"
    return None
