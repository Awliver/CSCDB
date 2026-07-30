#!/usr/bin/env python3
"""Reproduce the finals UPDATE col=col path through Wire v3.

The deterministic phase checks semantics.  The pressure phase shrinks the
buffer pool and runs the same prepared self-lock -> real-update sequence with
many SI clients, stopping at the first non-retryable ERROR.
"""

from __future__ import annotations

import argparse
import os
import random
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from typing import List, Optional


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
LOCAL = os.path.join(ROOT, "tests", "local")
BUILD = os.path.join(ROOT, "build")
RMDB = os.path.join(BUILD, "bin", "rmdb")
if LOCAL not in sys.path:
    sys.path.insert(0, LOCAL)

from wire_client import (  # noqa: E402
    SQLTYPE_CHAR as C,
    SQLTYPE_FLOAT32 as F,
    SQLTYPE_INT32 as I,
    BatchResult,
    WireClient,
    wait_ready,
)


@dataclass
class StressStats:
    committed: int = 0
    aborted: int = 0
    errors: int = 0
    exceptions: int = 0
    first_failure: str = ""


def require_ok(result, label: str):
    if not result.ok or result.error or result.aborted:
        raise AssertionError(
            "%s: ok=%s abort=%s error=%s diag=%r"
            % (label, result.ok, result.aborted, result.error, result.diagnostic)
        )
    return result


def start_server(frames: int):
    db_dir = tempfile.mkdtemp(prefix="rmdb-self-final-", dir="/tmp")
    log_fd, log_path = tempfile.mkstemp(prefix="rmdb-self-final-", suffix=".log", dir="/tmp")
    log = os.fdopen(log_fd, "w+")
    env = os.environ.copy()
    env["RMDB_POOL_FRAMES"] = str(frames)
    env["RMDB_MVCC_STATS"] = "1"
    proc = subprocess.Popen(
        [RMDB, db_dir],
        cwd=BUILD,
        stdout=log,
        stderr=subprocess.STDOUT,
        env=env,
    )
    try:
        wait_ready(timeout=30.0)
    except Exception:
        proc.poll()
        log.flush()
        log.seek(0)
        text = log.read()
        raise RuntimeError("server did not become ready (rc=%r):\n%s" % (proc.returncode, text[-4000:]))
    return proc, db_dir, log, log_path


def stop_server(proc, db_dir: str, log, log_path: str) -> str:
    if proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
    log.flush()
    log.seek(0)
    text = log.read()
    log.close()
    shutil.rmtree(db_dir, ignore_errors=True)
    try:
        os.unlink(log_path)
    except OSError:
        pass
    return text


def run_semantic_matrix():
    cli = WireClient(timeout=10)
    require_ok(cli.exec_stream("create table sa (id int, a int, f float, s char(16));"), "create sa")
    require_ok(cli.exec_stream("create index sa(id);"), "index sa")
    require_ok(cli.exec_stream("insert into sa values(1,7,1.25,'x');"), "insert sa")

    require_ok(cli.exec_stream("update sa set a=a where id=1 and a=7;"), "stream int self")
    require_ok(cli.exec_stream("update sa set f=f where id=1 and a=7;"), "stream float self")
    require_ok(cli.exec_stream("update sa set s=s where id=1 and a=7;"), "stream char self")
    require_ok(cli.exec_stream("update sa set id=id where id=1 and a=7;"), "stream indexed self")

    require_ok(cli.exec_stream("set transaction isolation level snapshot isolation;"), "set SI")
    cli.prepare_set([
        (1, False, [], "begin"),
        (2, False, [I, I], "update sa set a=a where id=$1 and a=$2"),
        (3, False, [I], "update sa set id=id where id=$1"),
        (4, False, [I], "update sa set f=f where id=$1"),
        (5, False, [I], "update sa set s=s where id=$1"),
        (6, False, [I, F, C, I], "update sa set a=$1,f=f+$2,s=$3 where id=$4"),
        (7, True, [I], "select id,a,f,s from sa where id=$1"),
        (8, False, [], "commit"),
        (9, False, [], "abort"),
    ])

    first = require_ok(cli.exec_batch([(1, []), (2, [1, 7]), (3, [1]), (4, [1]), (5, [1])]),
                       "prepared self-lock batch")
    if first.executed != 5:
        raise AssertionError("prepared self-lock executed=%d" % first.executed)
    second = require_ok(cli.exec_batch([(6, [8, 2.5, "y", 1]), (7, [1]), (8, [])]),
                        "same-txn real update batch")
    if second.results.get(1) != [[1, 8, 3.75, "y"]]:
        raise AssertionError("same-txn result mismatch: %r" % second.results)

    # A self-assignment must still be a real write for conflict purposes.
    other = WireClient(timeout=10)
    require_ok(other.exec_stream("set transaction isolation level snapshot isolation;"), "set SI other")
    other.prepare_set([
        (1, False, [], "begin"),
        (2, False, [I], "update sa set a=a where id=$1"),
        (3, False, [], "commit"),
    ])
    require_ok(cli.exec_batch([(1, []), (2, [1, 8])]), "holder self-lock")
    conflict = other.exec_batch([(1, []), (2, [1]), (3, [])])
    if not conflict.aborted or conflict.error:
        raise AssertionError("self-lock conflict was not retryable abort: %r" % conflict)
    require_ok(cli.exec_batch([(9, [])]), "holder rollback")

    final = require_ok(cli.exec_batch([(1, []), (7, [1]), (8, [])]), "final read")
    if final.results.get(1) != [[1, 8, 3.75, "y"]]:
        raise AssertionError("rollback changed row: %r" % final.results)
    other.close()
    cli.close()


def load_pressure_rows(rows: int):
    ddl = WireClient(timeout=30)
    require_ok(ddl.exec_stream("create table pressure (k1 int,k2 int,v int,pad char(256));"), "create pressure")
    require_ok(ddl.exec_stream("create index pressure(k1,k2);"), "index pressure")
    ddl.prepare_set([
        (1, False, [], "begin"),
        (2, False, [I, I, C], "insert into pressure values($1,$2,0,$3)"),
        (3, False, [], "commit"),
    ])
    pad = "x" * 256
    for base in range(0, rows, 120):
        ops = [(1, [])]
        for key in range(base, min(rows, base + 120)):
            ops.append((2, [key // 10, key % 10, pad]))
        ops.append((3, []))
        require_ok(ddl.exec_batch(ops), "load pressure rows @%d" % base)
    ddl.close()


def install_pressure(cli: WireClient):
    require_ok(cli.exec_stream("set transaction isolation level snapshot isolation;"), "pressure set SI")
    cli.prepare_set([
        (1, False, [], "begin"),
        (2, False, [I, I], "update pressure set v=v where k1=$1 and k2=$2"),
        (3, False, [I, I], "update pressure set v=v+1 where k1=$1 and k2=$2"),
        (4, False, [], "commit"),
    ])


def run_pressure(clients: int, seconds: float, rows: int) -> StressStats:
    stats = StressStats()
    stats_lock = threading.Lock()
    stop = threading.Event()
    barrier = threading.Barrier(clients)

    def record_failure(kind: str, worker: int, iteration: int, detail: str):
        with stats_lock:
            if kind == "error":
                stats.errors += 1
            else:
                stats.exceptions += 1
            if not stats.first_failure:
                stats.first_failure = "%s worker=%d iter=%d: %s" % (kind, worker, iteration, detail)
        stop.set()

    def worker(worker_id: int):
        cli: Optional[WireClient] = None
        try:
            cli = WireClient(timeout=8)
            install_pressure(cli)
            rng = random.Random(0x5E1F0000 + worker_id)
            barrier.wait(timeout=20)
            deadline = time.monotonic() + seconds
            iteration = 0
            while time.monotonic() < deadline and not stop.is_set():
                if rng.random() < 0.75:
                    key = rng.randrange(min(rows, 32))
                else:
                    key = rng.randrange(rows)
                params = [key // 10, key % 10]
                first: BatchResult = cli.exec_batch([(1, []), (2, params)])
                if first.error:
                    record_failure("error", worker_id, iteration, "self-lock: " + first.diagnostic)
                    break
                if first.aborted:
                    with stats_lock:
                        stats.aborted += 1
                    iteration += 1
                    continue
                second: BatchResult = cli.exec_batch([(3, params), (4, [])])
                if second.error:
                    record_failure("error", worker_id, iteration, "real-update: " + second.diagnostic)
                    break
                with stats_lock:
                    if second.aborted:
                        stats.aborted += 1
                    else:
                        stats.committed += 1
                iteration += 1
        except Exception as exc:
            record_failure("exception", worker_id, -1, "%s: %s" % (type(exc).__name__, exc))
        finally:
            if cli is not None:
                cli.close()

    threads: List[threading.Thread] = []
    for i in range(clients):
        t = threading.Thread(target=worker, args=(i,), name="self-pressure-%d" % i)
        t.start()
        threads.append(t)
    for t in threads:
        t.join(timeout=seconds + 30)
        if t.is_alive():
            record_failure("exception", -1, -1, "worker did not finish")
    return stats


def interesting_log_lines(text: str) -> List[str]:
    needles = ("[sql-error]", "[bpm-pressure]", "[sql-slow]", "[wire]", "[mvcc-stats]", "[heap]")
    return [line for line in text.splitlines() if any(n in line for n in needles)]


def main() -> int:
    global RMDB
    ap = argparse.ArgumentParser()
    ap.add_argument("--rmdb", default=RMDB, help="rmdb binary to test")
    ap.add_argument("--frames", type=int, default=256)
    ap.add_argument("--clients", type=int, default=32)
    ap.add_argument("--rows", type=int, default=2400)
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--semantic-only", action="store_true")
    args = ap.parse_args()
    RMDB = os.path.abspath(args.rmdb)

    if not os.path.isfile(RMDB):
        raise SystemExit("rmdb binary not found: " + RMDB)

    proc = None
    db_dir = ""
    log = None
    log_path = ""
    failure = ""
    stats = StressStats()
    try:
        proc, db_dir, log, log_path = start_server(args.frames)
        print("[1/2] deterministic semantic matrix")
        run_semantic_matrix()
        print("  PASS")
        if not args.semantic_only:
            print("[2/2] pressure: frames=%d clients=%d rows=%d seconds=%.1f"
                  % (args.frames, args.clients, args.rows, args.seconds))
            load_pressure_rows(args.rows)
            stats = run_pressure(args.clients, args.seconds, args.rows)
            print("  committed=%d aborted=%d errors=%d exceptions=%d"
                  % (stats.committed, stats.aborted, stats.errors, stats.exceptions))
            if stats.first_failure:
                failure = stats.first_failure
                print("  first_failure=" + failure)
    except Exception as exc:
        failure = "%s: %s" % (type(exc).__name__, exc)
        print("FAIL: " + failure)
    finally:
        server_log = ""
        if proc is not None:
            server_log = stop_server(proc, db_dir, log, log_path)
        lines = interesting_log_lines(server_log)
        if lines:
            print("server diagnostics:")
            for line in lines[-80:]:
                print("  " + line)

    if failure or stats.errors or stats.exceptions:
        return 1
    print("PASS: no non-retryable ERROR reproduced")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
