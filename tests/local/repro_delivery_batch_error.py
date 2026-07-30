#!/usr/bin/env python3
"""Regression/reproducer for the finals Delivery SELECT BATCH_RESULT ERROR.

The finals report identifies this prepared statement shape:

    select ol_number, ol_amount, ol_delivery_d
      from order_line
     where ol_w_id=? and ol_d_id=? and ol_o_id=?
     order by ol_number

By default this test runs the current ``build/bin/rmdb`` and asserts that valid
prepared Delivery transactions never end in ERROR.  ``--ref`` builds an
immutable Git revision from a ``git archive`` cache; combine it with
``--expect target-error`` to reproduce the historical failure.

The compact database is deliberately larger than the configured buffer pool.
All clients rendezvous immediately before the Delivery write/detail-read batch,
which concentrates page pins while preserving the published PREPARE_SET and
EXEC_BATCH path.  Successful responses are also checked for row count, ORDER BY
order, FLOAT32 amount bits, and the just-written delivery timestamp.

Build products and logs live under ``build/repro_delivery_batch_error/``;
temporary databases and generated CSVs live under ``/tmp``.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import datetime
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


ROOT = Path(__file__).resolve().parents[2]
LOCAL = ROOT / "tests" / "local"
ARTIFACT_ROOT = ROOT / "build" / "repro_delivery_batch_error"
DEFAULT_BINARY = ROOT / "build" / "bin" / "rmdb"
LEGACY_REF = "f01553f"
HOST = "127.0.0.1"
PORT = 8765

if str(LOCAL) not in sys.path:
    sys.path.insert(0, str(LOCAL))

from wire_client import (  # noqa: E402
    SQLTYPE_CHAR as C,
    SQLTYPE_INT32 as I,
    BatchResult,
    WireClient,
    wait_ready,
)


S_BEGIN = 1
S_COMMIT = 2
S_ABORT = 3
S_MIN_NO = 10
S_DELETE_NO = 11
S_UPDATE_ORDER = 12
S_UPDATE_ORDER_LINE = 13
S_DETAIL_SELECT = 14
S_SUM_ORDER_LINE = 15
S_SELECT_CUSTOMER_ID = 16

# batch2 op index, zero based.  This is the statement named by the finals report.
DETAIL_SELECT_OP = 3
BATCH2_NAMES = (
    "delete-new-order",
    "update-orders-carrier",
    "update-order-line-delivery",
    "select-order-line-details",
    "sum-order-line",
    "select-customer-id",
)

SCHEMA = (
    "create table new_orders (no_o_id int, no_d_id int, no_w_id int);",
    "create table orders (o_id int, o_d_id int, o_w_id int, o_c_id int, "
    "o_entry_d char(19), o_carrier_id int, o_ol_cnt int, o_all_local int);",
    "create table order_line (ol_o_id int, ol_d_id int, ol_w_id int, "
    "ol_number int, ol_i_id int, ol_supply_w_id int, ol_delivery_d char(30), "
    "ol_quantity int, ol_amount float, ol_dist_info char(24));",
)

INDEXES = (
    "create index new_orders (no_w_id, no_d_id, no_o_id);",
    "create index orders (o_w_id, o_d_id, o_id);",
    "create index order_line (ol_w_id, ol_d_id, ol_o_id, ol_number);",
)

PREPARED = (
    (S_BEGIN, False, [], "begin"),
    (S_COMMIT, False, [], "commit"),
    (S_ABORT, False, [], "abort"),
    (
        S_MIN_NO,
        True,
        [I, I],
        "select min(no_o_id) from new_orders where no_w_id=$1 and no_d_id=$2",
    ),
    (
        S_DELETE_NO,
        False,
        [I, I, I],
        "delete from new_orders where no_w_id=$1 and no_d_id=$2 and no_o_id=$3",
    ),
    (
        S_UPDATE_ORDER,
        False,
        [I, I, I, I],
        "update orders set o_carrier_id=$1 "
        "where o_w_id=$2 and o_d_id=$3 and o_id=$4",
    ),
    (
        S_UPDATE_ORDER_LINE,
        False,
        [C, I, I, I],
        "update order_line set ol_delivery_d=$1 "
        "where ol_w_id=$2 and ol_d_id=$3 and ol_o_id=$4",
    ),
    (
        S_DETAIL_SELECT,
        True,
        [I, I, I],
        "select ol_number, ol_amount, ol_delivery_d from order_line "
        "where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3 order by ol_number",
    ),
    (
        S_SUM_ORDER_LINE,
        True,
        [I, I, I],
        "select sum(ol_amount) from order_line "
        "where ol_w_id=$1 and ol_d_id=$2 and ol_o_id=$3",
    ),
    (
        S_SELECT_CUSTOMER_ID,
        True,
        [I, I, I],
        "select o_c_id from orders "
        "where o_w_id=$1 and o_d_id=$2 and o_id=$3",
    ),
)


@dataclasses.dataclass
class Server:
    proc: subprocess.Popen
    log_file: object
    log_path: Path


@dataclasses.dataclass
class StressStats:
    attempted: int = 0
    committed: int = 0
    aborted: int = 0
    target_aborts: int = 0
    transport_errors: int = 0
    target_errors: int = 0
    other_errors: int = 0
    mismatches: int = 0
    first_target: str = ""
    first_other: str = ""
    first_mismatch: str = ""
    failed_ops: Dict[str, int] = dataclasses.field(
        default_factory=lambda: collections.defaultdict(int)
    )
    aborted_ops: Dict[str, int] = dataclasses.field(
        default_factory=lambda: collections.defaultdict(int)
    )

    def merge(self, other: "StressStats") -> None:
        for field in (
            "attempted",
            "committed",
            "aborted",
            "target_aborts",
            "transport_errors",
            "target_errors",
            "other_errors",
            "mismatches",
        ):
            setattr(self, field, getattr(self, field) + getattr(other, field))
        if not self.first_target:
            self.first_target = other.first_target
        if not self.first_other:
            self.first_other = other.first_other
        if not self.first_mismatch:
            self.first_mismatch = other.first_mismatch
        for key, value in other.failed_ops.items():
            self.failed_ops[key] += value
        for key, value in other.aborted_ops.items():
            self.aborted_ops[key] += value


def run_checked(argv: Sequence[str], cwd: Optional[Path] = None) -> None:
    print("+", " ".join(str(x) for x in argv), flush=True)
    subprocess.run([str(x) for x in argv], cwd=cwd, check=True)


def resolve_ref(ref: str) -> str:
    out = subprocess.check_output(
        ["git", "rev-parse", "--verify", ref + "^{commit}"],
        cwd=ROOT,
        text=True,
    )
    return out.strip()


def build_revision(ref: str, jobs: int, rebuild: bool) -> Tuple[Path, str]:
    commit = resolve_ref(ref)
    short = commit[:12]
    cache = ARTIFACT_ROOT / ("src-" + short)
    source = cache / "source"
    build = cache / "build"
    binary = build / "bin" / "rmdb"
    marker = cache / "commit.txt"

    if rebuild and cache.exists():
        shutil.rmtree(cache)
    if binary.is_file() and marker.is_file() and marker.read_text().strip() == commit:
        print("reusing pre-merge binary:", binary)
        return binary, commit

    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "source.tar"
    run_checked(
        ["git", "archive", "--format=tar", "--output", str(archive), commit],
        cwd=ROOT,
    )
    source.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, "r") as tf:
        tf.extractall(source)
    archive.unlink()

    run_checked(
        [
            "cmake",
            "-S",
            str(source),
            "-B",
            str(build),
            "-DCMAKE_BUILD_TYPE=Release",
        ]
    )
    run_checked(
        ["cmake", "--build", str(build), "--target", "rmdb", "-j", str(jobs)]
    )
    marker.write_text(commit + "\n")
    if not binary.is_file():
        raise RuntimeError("build completed without rmdb binary: " + str(binary))
    return binary, commit


def ensure_port_free() -> None:
    sock = socket.socket()
    try:
        sock.settimeout(0.2)
        if sock.connect_ex((HOST, PORT)) == 0:
            raise RuntimeError(
                "port 8765 is already in use; stop the existing rmdb before running"
            )
    finally:
        sock.close()


def start_server(
    binary: Path,
    db_dir: Path,
    frames: int,
    log_path: Path,
    append: bool = True,
    as_limit_mb: int = 0,
) -> Server:
    ensure_port_free()
    mode = "a+" if append else "w+"
    log_file = log_path.open(mode)
    env = os.environ.copy()
    env["RMDB_POOL_FRAMES"] = str(frames)
    env["RMDB_BPM_STATS"] = "1"
    env["RMDB_MVCC_STATS"] = "1"
    # A low arena count makes the address-space cap reproducible across glibc
    # versions instead of letting each client thread reserve another arena.
    env.setdefault("MALLOC_ARENA_MAX", "2")
    preexec_fn = None
    if as_limit_mb:
        import resource

        cap = as_limit_mb * 1024 * 1024

        def apply_address_space_limit() -> None:
            resource.setrlimit(resource.RLIMIT_AS, (cap, cap))

        preexec_fn = apply_address_space_limit
    proc = subprocess.Popen(
        [str(binary), str(db_dir)],
        cwd=binary.parent.parent,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        env=env,
        preexec_fn=preexec_fn,
    )
    try:
        wait_ready(timeout=60.0)
    except Exception:
        proc.poll()
        log_file.flush()
        raise RuntimeError(
            "server did not become ready, rc=%r log=%s"
            % (proc.returncode, log_path)
        )
    return Server(proc, log_file, log_path)


def stop_server(server: Server) -> None:
    if server.proc.poll() is None:
        server.proc.send_signal(signal.SIGINT)
        try:
            server.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            server.proc.kill()
            server.proc.wait(timeout=5)
    server.log_file.flush()
    server.log_file.close()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        sock = socket.socket()
        try:
            sock.settimeout(0.1)
            if sock.connect_ex((HOST, PORT)) != 0:
                return
        finally:
            sock.close()
        time.sleep(0.1)
    raise RuntimeError("rmdb stopped but port 8765 is still accepting connections")


def require_ok(result, label: str):
    if not result.ok or result.error or result.aborted:
        raise RuntimeError(
            "%s: ok=%s abort=%s error=%s diag=%r"
            % (
                label,
                result.ok,
                result.aborted,
                result.error,
                result.diagnostic,
            )
        )
    return result


def write_csvs(
    data_dir: Path,
    warehouses: int,
    districts: int,
    orders_per_district: int,
    lines_per_order: int,
) -> int:
    data_dir.mkdir(parents=True, exist_ok=True)
    rows = 0
    with (data_dir / "new_orders.csv").open("w") as new_orders, \
            (data_dir / "orders.csv").open("w") as orders, \
            (data_dir / "order_line.csv").open("w") as order_line:
        for w_id in range(1, warehouses + 1):
            for d_id in range(1, districts + 1):
                for local_o_id in range(orders_per_district):
                    o_id = 3001 + local_o_id
                    c_id = 1 + (local_o_id % 3000)
                    new_orders.write("%d,%d,%d\n" % (o_id, d_id, w_id))
                    orders.write(
                        "%d,%d,%d,%d,2026-07-30 16:00:00,0,%d,1\n"
                        % (o_id, d_id, w_id, c_id, lines_per_order)
                    )
                    for number in range(1, lines_per_order + 1):
                        item_id = 1 + (
                            (
                                w_id * 100003
                                + d_id * 1009
                                + local_o_id * 17
                                + number
                            )
                            % 100000
                        )
                        amount = 0.01 * (1 + ((o_id + number) % 9999))
                        order_line.write(
                            "%d,%d,%d,%d,%d,%d,,5,%.2f,DIST%02d\n"
                            % (
                                o_id,
                                d_id,
                                w_id,
                                number,
                                item_id,
                                w_id,
                                amount,
                                d_id,
                            )
                        )
                        rows += 1
    return rows


def bootstrap_database(
    binary: Path,
    db_dir: Path,
    data_dir: Path,
    log_path: Path,
    load_frames: int,
) -> None:
    server = start_server(
        binary, db_dir, frames=load_frames, log_path=log_path, append=False
    )
    try:
        cli = WireClient(timeout=120)
        try:
            for sql in SCHEMA:
                require_ok(cli.exec_stream(sql), sql)
            for sql in INDEXES:
                require_ok(cli.exec_stream(sql), sql)
            for table in ("new_orders", "orders", "order_line"):
                path = data_dir / (table + ".csv")
                require_ok(
                    cli.exec_stream("load %s into %s" % (path, table)),
                    "load " + table,
                )
            require_ok(
                cli.exec_stream("create static_checkpoint"),
                "create static_checkpoint",
            )
        finally:
            cli.close()
    finally:
        stop_server(server)


def install_delivery_dictionary(cli: WireClient) -> None:
    require_ok(
        cli.exec_stream("set transaction isolation level snapshot isolation"),
        "set SI",
    )
    cli.prepare_set(PREPARED)


def describe_batch_failure(label: str, result: BatchResult) -> str:
    op_name = operation_name(label, result.failed_op)
    return (
        "%s failed_op=%d(%s) executed=%d abort=%s error=%s diag=%r"
        % (
            label,
            result.failed_op,
            op_name,
            result.executed,
            result.aborted,
            result.error,
            result.diagnostic,
        )
    )


def operation_name(label: str, op: int) -> str:
    if label == "batch1":
        names = ("begin", "min-new-order")
    elif label == "batch2":
        names = BATCH2_NAMES
    elif label == "batch3":
        names = ("commit",)
    else:
        names = ()
    if 0 <= op < len(names):
        return names[op]
    if op == 0xFFFF:
        return "frame-level"
    return "unknown"


def operation_key(label: str, op: int) -> str:
    return "%s:%d:%s" % (label, op, operation_name(label, op))


def float32_bits(value: float) -> int:
    return struct.unpack(">I", struct.pack(">f", float(value)))[0]


def validate_detail_rows(
    result: BatchResult,
    worker_id: int,
    iteration: int,
    o_id: int,
    delivery_ts: str,
    lines_per_order: int,
) -> str:
    rows = result.results.get(DETAIL_SELECT_OP)
    prefix = "worker=%d iter=%d o_id=%d" % (worker_id, iteration, o_id)
    if rows is None:
        return "%s detail result missing from successful batch" % prefix
    if len(rows) != lines_per_order:
        return "%s detail rows=%d expected=%d" % (
            prefix,
            len(rows),
            lines_per_order,
        )
    for index, row in enumerate(rows, 1):
        if len(row) != 3:
            return "%s row=%d column-count=%d expected=3" % (
                prefix,
                index,
                len(row),
            )
        number, amount, observed_ts = row
        if number != index:
            return "%s row=%d ol_number=%r (ORDER BY/progress violation)" % (
                prefix,
                index,
                number,
            )
        expected_amount = 0.01 * (1 + ((o_id + index) % 9999))
        if amount is None or float32_bits(float(amount)) != float32_bits(expected_amount):
            return "%s row=%d ol_amount=%r bits=%s expected=%.2f bits=%08x" % (
                prefix,
                index,
                amount,
                "NULL" if amount is None else "%08x" % float32_bits(float(amount)),
                expected_amount,
                float32_bits(expected_amount),
            )
        if observed_ts != delivery_ts:
            return "%s row=%d ol_delivery_d=%r expected=%r" % (
                prefix,
                index,
                observed_ts,
                delivery_ts,
            )
    return ""


def run_stress(
    clients: int,
    seconds: float,
    warehouses: int,
    districts: int,
    orders_per_district: int,
    lines_per_order: int,
) -> StressStats:
    stats = StressStats()
    lock = threading.Lock()
    stop = threading.Event()
    barrier = threading.Barrier(clients)
    # Keep the three writes and the reported SELECT aligned across all 32
    # sessions.  Without this second rendezvous, commit/WAL jitter spreads the
    # clients over the whole transaction and a 64-frame local pool can remain
    # healthy even though 32 simultaneous Delivery readers cross the same
    # pin-pressure cliff.
    batch2_barrier = threading.Barrier(clients)
    timer_holder: List[threading.Timer] = []

    def request_stop() -> None:
        stop.set()
        try:
            batch2_barrier.abort()
        except threading.BrokenBarrierError:
            pass

    def record_batch_failure(
        worker_id: int, iteration: int, label: str, result: BatchResult
    ) -> bool:
        detail = "worker=%d iter=%d %s" % (
            worker_id,
            iteration,
            describe_batch_failure(label, result),
        )
        with lock:
            if result.error:
                stats.failed_ops[operation_key(label, result.failed_op)] += 1
                if label == "batch2" and result.failed_op == DETAIL_SELECT_OP:
                    stats.target_errors += 1
                    if not stats.first_target:
                        stats.first_target = detail
                else:
                    stats.other_errors += 1
                    if not stats.first_other:
                        stats.first_other = detail
                request_stop()
                return True
            elif result.aborted:
                stats.aborted += 1
                stats.aborted_ops[operation_key(label, result.failed_op)] += 1
                if label == "batch2" and result.failed_op == DETAIL_SELECT_OP:
                    stats.target_aborts += 1
        return False

    def record_mismatch(detail: str) -> None:
        with lock:
            stats.mismatches += 1
            if not stats.first_mismatch:
                stats.first_mismatch = detail
            request_stop()

    def worker(worker_id: int) -> None:
        cli: Optional[WireClient] = None
        try:
            cli = WireClient(timeout=15)
            install_delivery_dictionary(cli)
            partition = worker_id % (warehouses * districts)
            w_id = 1 + (partition // districts)
            d_id = 1 + (partition % districts)
            # A coprime stride walks every order and keeps clients on distinct
            # (w,d) partitions, avoiding ordinary write-write aborts.
            stride = 127 if orders_per_district % 127 else 125
            iteration = 0
            barrier_token = barrier.wait(timeout=30)
            if barrier_token == 0:
                timer = threading.Timer(seconds, request_stop)
                timer.daemon = True
                timer_holder.append(timer)
                timer.start()
            while not stop.is_set():
                with lock:
                    stats.attempted += 1
                local_o = (worker_id * 19 + iteration * stride) % orders_per_district
                o_id = 3001 + local_o

                batch1 = cli.exec_batch(
                    [
                        (S_BEGIN, []),
                        (S_MIN_NO, [w_id, d_id]),
                    ]
                )
                if not batch1.ok:
                    record_batch_failure(worker_id, iteration, "batch1", batch1)
                    iteration += 1
                    continue

                try:
                    batch2_barrier.wait(timeout=30)
                except threading.BrokenBarrierError:
                    if stop.is_set():
                        break
                    raise
                delivery_ts = "2026-07-30 16:%02d:%02d" % (
                    (iteration // 60) % 60,
                    iteration % 60,
                )
                batch2 = cli.exec_batch(
                    [
                        (S_DELETE_NO, [w_id, d_id, -o_id]),
                        (S_UPDATE_ORDER, [1 + (iteration % 10), w_id, d_id, o_id]),
                        (
                            S_UPDATE_ORDER_LINE,
                            [
                                delivery_ts,
                                w_id,
                                d_id,
                                o_id,
                            ],
                        ),
                        (S_DETAIL_SELECT, [w_id, d_id, o_id]),
                        (S_SUM_ORDER_LINE, [w_id, d_id, o_id]),
                        (S_SELECT_CUSTOMER_ID, [w_id, d_id, o_id]),
                    ]
                )
                if not batch2.ok:
                    record_batch_failure(worker_id, iteration, "batch2", batch2)
                    iteration += 1
                    continue
                mismatch = validate_detail_rows(
                    batch2,
                    worker_id,
                    iteration,
                    o_id,
                    delivery_ts,
                    lines_per_order,
                )
                if mismatch:
                    record_mismatch(mismatch)
                    break

                batch3 = cli.exec_batch([(S_COMMIT, [])])
                if not batch3.ok:
                    record_batch_failure(worker_id, iteration, "batch3", batch3)
                else:
                    with lock:
                        stats.committed += 1
                iteration += 1
        except Exception as exc:
            with lock:
                stats.transport_errors += 1
                if not stats.first_other:
                    stats.first_other = (
                        "worker=%d transport %s: %s"
                        % (worker_id, type(exc).__name__, exc)
                    )
            request_stop()
            try:
                barrier.abort()
            except threading.BrokenBarrierError:
                pass
        finally:
            if cli is not None:
                try:
                    # Clean up an explicit transaction left open by a client-side
                    # exception.  AUTO_ABORT already handled server batch errors.
                    cli.exec_batch([(S_ABORT, [])])
                except Exception:
                    pass
                cli.close()

    threads: List[threading.Thread] = []
    for worker_id in range(clients):
        thread = threading.Thread(
            target=worker,
            args=(worker_id,),
            name="delivery-error-%d" % worker_id,
        )
        thread.start()
        threads.append(thread)

    for thread in threads:
        thread.join(timeout=seconds + 45)
        if thread.is_alive():
            with lock:
                stats.transport_errors += 1
                if not stats.first_other:
                    stats.first_other = "worker thread did not finish"
            request_stop()
    for timer in timer_holder:
        timer.cancel()
    return stats


def interesting_log_lines(log_path: Path) -> List[str]:
    needles = (
        "[sql-error]",
        "[bpm-pressure]",
        "[sql-slow]",
        "[pressure-abort]",
        "[error-abort]",
        "[wire]",
        "bad_alloc",
        "buffer pool full",
    )
    try:
        lines = log_path.read_text(errors="replace").splitlines()
    except OSError:
        return []
    return [line for line in lines if any(needle in line for needle in needles)]


def classify_log(log_path: Path) -> Dict[str, int]:
    classes = {
        "sql_error": 0,
        "bpm_wait": 0,
        "bpm_exhausted": 0,
        "pressure_abort": 0,
        "error_abort": 0,
        "bad_alloc": 0,
        "buffer_pool_full": 0,
    }
    try:
        lines = log_path.read_text(errors="replace").splitlines()
    except OSError:
        return classes
    for line in lines:
        lower = line.lower()
        if "[sql-error]" in line:
            classes["sql_error"] += 1
        if "[bpm-pressure]" in line:
            classes["bpm_wait"] += 1
            if "exhausted" in lower:
                classes["bpm_exhausted"] += 1
        if "[pressure-abort]" in line:
            classes["pressure_abort"] += 1
        if "[error-abort]" in line:
            classes["error_abort"] += 1
        if "bad_alloc" in lower:
            classes["bad_alloc"] += 1
        if "buffer pool full" in lower:
            classes["buffer_pool_full"] += 1
    return classes


def stats_dict(
    stats: StressStats,
    commit: str,
    binary: Path,
    server_alive: bool,
    log_classes: Dict[str, int],
) -> dict:
    return {
        "revision": commit,
        "binary": str(binary),
        "server_alive_at_measurement_end": server_alive,
        "attempted": stats.attempted,
        "committed": stats.committed,
        "aborted": stats.aborted,
        "target_aborts": stats.target_aborts,
        "target_errors": stats.target_errors,
        "other_errors": stats.other_errors,
        "transport_errors": stats.transport_errors,
        "mismatches": stats.mismatches,
        "failed_ops": dict(sorted(stats.failed_ops.items())),
        "aborted_ops": dict(sorted(stats.aborted_ops.items())),
        "first_target": stats.first_target,
        "first_other": stats.first_other,
        "first_mismatch": stats.first_mismatch,
        "server_log_classes": log_classes,
    }


def evaluate(
    expectation: str,
    stats: StressStats,
    server_alive: bool,
    require_abort: bool,
) -> Tuple[bool, str]:
    if not server_alive:
        return False, "rmdb exited during measurement"
    if stats.transport_errors:
        return False, "%d transport/client failures" % stats.transport_errors
    if stats.mismatches:
        return False, "%d typed/order/result mismatches" % stats.mismatches
    if stats.attempted == 0:
        return False, "no Delivery transaction was attempted"

    total_errors = stats.target_errors + stats.other_errors
    if expectation == "clean":
        if total_errors:
            return False, "%d non-retryable ERROR terminals" % total_errors
        if stats.committed == 0:
            return False, "all transactions aborted; no successful Delivery commit"
        if require_abort and stats.aborted == 0:
            return False, "--require-abort set but no TRANSACTION_ABORT was observed"
        return True, "no ERROR terminal; typed Delivery results remained valid"
    if expectation == "target-error":
        if stats.target_errors == 0:
            return False, "target Delivery detail SELECT ERROR was not reproduced"
        return True, "target Delivery detail SELECT returned BATCH_STATUS_ERROR"
    if total_errors == 0:
        return False, "no BATCH_STATUS_ERROR was reproduced"
    return True, "a non-retryable BATCH_STATUS_ERROR was reproduced"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Delivery EXEC_BATCH ERROR regression and historical reproducer"
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument(
        "--ref",
        help="build and run an immutable Git revision (for example %s)" % LEGACY_REF,
    )
    source.add_argument(
        "--binary",
        type=Path,
        help="rmdb binary to test (default: build/bin/rmdb)",
    )
    parser.add_argument(
        "--legacy-repro",
        action="store_true",
        help=(
            "shortcut for --ref %s --expect target-error --frames 80 "
            "--seconds 90" % LEGACY_REF
        ),
    )
    parser.add_argument(
        "--expect",
        choices=("clean", "target-error", "any-error"),
        help=(
            "pass condition (default: clean); target-error requires failed_op "
            "to identify the published Delivery detail SELECT"
        ),
    )
    parser.add_argument(
        "--require-abort",
        action="store_true",
        help="in clean mode, also require at least one retryable TRANSACTION_ABORT",
    )
    parser.add_argument(
        "--quick",
        action="store_true",
        help="5s/8-client smoke profile; explicit sizing flags still override it",
    )
    parser.add_argument("--rebuild", action="store_true", help="rebuild --ref cache")
    parser.add_argument(
        "--jobs", type=int, default=max(1, min(16, os.cpu_count() or 4))
    )
    parser.add_argument("--frames", type=int)
    parser.add_argument("--load-frames", type=int, default=4096)
    parser.add_argument(
        "--as-limit-mb",
        type=int,
        default=0,
        help=(
            "pressure-server RLIMIT_AS; use with a larger --lines-per-order "
            "to reproduce the old bad_alloc -> ERROR terminal"
        ),
    )
    parser.add_argument("--clients", type=int)
    parser.add_argument("--seconds", type=float)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--warehouses", type=int)
    parser.add_argument("--districts", type=int)
    parser.add_argument("--orders-per-district", type=int)
    parser.add_argument("--lines-per-order", type=int)
    parser.add_argument(
        "--json",
        type=Path,
        help="write a machine-readable result summary",
    )
    parser.add_argument(
        "--keep-db",
        action="store_true",
        help="keep the temporary database and generated CSV files",
    )
    args = parser.parse_args()

    if args.legacy_repro:
        if args.ref or args.binary:
            parser.error("--legacy-repro cannot be combined with --ref/--binary")
        args.ref = LEGACY_REF
        args.expect = args.expect or "target-error"
        args.frames = args.frames or 80
        args.seconds = args.seconds or 90.0
    else:
        args.expect = args.expect or "clean"

    profile = {
        "frames": 96,
        "clients": 8 if args.quick else 32,
        "seconds": 5.0 if args.quick else 30.0,
        "warehouses": 2 if args.quick else 8,
        "districts": 4,
        "orders_per_district": 64 if args.quick else 128,
        "lines_per_order": 15,
    }
    for name, value in profile.items():
        if getattr(args, name) is None:
            setattr(args, name, value)

    if args.frames < 64:
        parser.error("--frames must be >=64 (the BPM has 64 shards)")
    if args.clients < 1 or args.clients > 32:
        parser.error("--clients must be in [1,32]")
    if args.warehouses * args.districts < args.clients:
        parser.error("warehouses*districts must be >= clients")
    if args.orders_per_district < 2:
        parser.error("--orders-per-district must be >=2")
    if args.lines_per_order < 1:
        parser.error("--lines-per-order must be positive")
    if args.seconds <= 0 or args.rounds < 1:
        parser.error("--seconds and --rounds must be positive")
    if args.rebuild and not args.ref:
        parser.error("--rebuild is only valid with --ref")
    if args.require_abort and args.expect != "clean":
        parser.error("--require-abort is only valid with --expect clean")

    ARTIFACT_ROOT.mkdir(parents=True, exist_ok=True)
    if args.ref:
        binary, commit = build_revision(args.ref, args.jobs, args.rebuild)
    else:
        binary = (args.binary or DEFAULT_BINARY).resolve()
        if not binary.is_file():
            parser.error("binary does not exist: " + str(binary))
        head = resolve_ref("HEAD")
        commit = "worktree-" + head[:12]

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = ARTIFACT_ROOT / ("run-%s-%s.log" % (commit[:12], stamp))
    run_root = Path(tempfile.mkdtemp(prefix="rmdb-delivery-batch-", dir="/tmp"))
    db_dir = run_root / "db"
    data_dir = run_root / "data"
    db_dir.mkdir()

    expected_rows = write_csvs(
        data_dir,
        args.warehouses,
        args.districts,
        args.orders_per_district,
        args.lines_per_order,
    )
    print("revision:", commit)
    print("binary:", binary)
    print("database:", db_dir)
    print("expectation:", args.expect)
    print(
        "shape: W=%d D=%d orders/district=%d lines/order=%d "
        "order_line_rows=%d rounds=%d"
        % (
            args.warehouses,
            args.districts,
            args.orders_per_district,
            args.lines_per_order,
            expected_rows,
            args.rounds,
        )
    )

    server: Optional[Server] = None
    stats = StressStats()
    server_alive = False
    try:
        print("[1/2] bootstrap with %d frames" % args.load_frames, flush=True)
        bootstrap_database(
            binary,
            db_dir,
            data_dir,
            log_path,
            args.load_frames,
        )

        print(
            "[2/2] Delivery EXEC_BATCH pressure: frames=%d clients=%d seconds=%.1f"
            " as_limit_mb=%s"
            % (
                args.frames,
                args.clients,
                args.seconds,
                args.as_limit_mb or "none",
            ),
            flush=True,
        )
        server = start_server(
            binary,
            db_dir,
            frames=args.frames,
            log_path=log_path,
            append=True,
            as_limit_mb=args.as_limit_mb,
        )
        for round_no in range(1, args.rounds + 1):
            print(
                "  round %d/%d: synchronized Delivery pressure" %
                (round_no, args.rounds),
                flush=True,
            )
            round_stats = run_stress(
                args.clients,
                args.seconds,
                args.warehouses,
                args.districts,
                args.orders_per_district,
                args.lines_per_order,
            )
            stats.merge(round_stats)
            if server.proc.poll() is not None:
                break
            if round_stats.target_errors or round_stats.other_errors or round_stats.mismatches:
                break
        server_alive = server.proc.poll() is None
    finally:
        if server is not None:
            stop_server(server)

    print(
        "result: attempted=%d committed=%d aborted=%d target_aborts=%d "
        "target_errors=%d other_errors=%d mismatches=%d transport_errors=%d "
        "server_alive=%s"
        % (
            stats.attempted,
            stats.committed,
            stats.aborted,
            stats.target_aborts,
            stats.target_errors,
            stats.other_errors,
            stats.mismatches,
            stats.transport_errors,
            server_alive,
        )
    )
    if stats.failed_ops:
        print("ERROR failed_op histogram:", dict(sorted(stats.failed_ops.items())))
    if stats.aborted_ops:
        print("ABORT failed_op histogram:", dict(sorted(stats.aborted_ops.items())))
    if stats.first_target:
        print("first target ERROR:", stats.first_target)
    if stats.first_other:
        print("first other failure:", stats.first_other)
    if stats.first_mismatch:
        print("first result mismatch:", stats.first_mismatch)

    lines = interesting_log_lines(log_path)
    log_classes = classify_log(log_path)
    print("server diagnostic counters:", log_classes)
    if lines:
        print("server diagnostics (tail):")
        for line in lines[-80:]:
            print("  " + line)
    print("full server log:", log_path)

    report = stats_dict(stats, commit, binary, server_alive, log_classes)
    passed, reason = evaluate(
        args.expect,
        stats,
        server_alive,
        args.require_abort,
    )
    report["expectation"] = args.expect
    report["passed"] = passed
    report["reason"] = reason
    report["log"] = str(log_path)
    if args.json:
        json_path = args.json.resolve()
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print("json report:", json_path)

    if args.keep_db:
        print("kept database:", run_root)
    else:
        shutil.rmtree(run_root, ignore_errors=True)

    if passed:
        print("PASS:", reason)
        return 0
    print("FAIL:", reason)
    if args.expect != "clean":
        print("hint: increase --seconds/--rounds or reduce --frames to >=64")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
