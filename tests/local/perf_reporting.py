#!/usr/bin/env python3
"""Structured reporting helpers for local TPC-C performance runs."""

from __future__ import annotations

import re
from collections import Counter


_STAGE_RE = re.compile(
    r"^([a-z_]+(?:\s+(?:b\d+|commit|empty-commit|abort))?)\s+(abort|error)"
    r"(?:\s+op=-?\d+\s+executed=-?\d+)?:",
    re.IGNORECASE,
)
_FAILED_OP_RE = re.compile(r"\bop=(-?\d+)\b", re.IGNORECASE)


def classify_failure(message):
    """Classify one client-visible failure without pretending to know more than the wire reports."""
    text = (message or "").strip()
    low = text.lower()
    stage = "unknown"
    kind = "error"

    match = _STAGE_RE.match(low)
    if match:
        stage = match.group(1).replace(" ", "_")
        kind = match.group(2)
    elif "connect/prepare failed" in low or "connection" in low:
        stage = "connect"
    elif ":" in low:
        stage = low.split(":", 1)[0].strip().replace(" ", "_")[:40] or "unknown"

    op_match = _FAILED_OP_RE.search(low)
    failed_op = int(op_match.group(1)) if op_match else None

    if "startup barrier" in low:
        reason = "startup_barrier_failure"
        stage = "startup_barrier"
    elif "invalid item" in low and "rollback" in low:
        reason = "expected_business_rollback"
    elif "deadlock prevention" in low or "write conflict" in low or "serialization" in low:
        # RMDB currently reuses DEADLOCK_PREVENTION for several SI/MVCC conflicts.
        reason = "deadlock_or_si_conflict"
    elif "lock on shrinking" in low or "shrinking" in low:
        reason = "lock_shrinking"
    elif "upgrade conflict" in low or "lock upgrade" in low:
        reason = "lock_upgrade_conflict"
    elif "buffer pool" in low or "bufferpoolpressure" in low or "bpm-pressure" in low:
        reason = "buffer_pool_pressure"
    elif "bad_alloc" in low or "cannot allocate memory" in low or "out of memory" in low:
        reason = "memory_pressure"
    elif "duplicate" in low or "unique constraint" in low or "non-unique" in low:
        reason = "unique_key_conflict"
    elif "timed out" in low or "timeout" in low:
        reason = "client_timeout"
    elif any(token in low for token in ("broken pipe", "connection reset", "connection refused", "eof")):
        reason = "transport_failure"
    elif any(token in low for token in ("syntax", "parse error", "incompatible type", "column not found")):
        reason = "sql_or_schema_error"
    elif any(token in low for token in (" missing", " empty", "not found")):
        reason = "business_row_missing"
    elif kind == "abort" or "transaction abort" in low or "transaction aborted" in low:
        reason = "transaction_abort_other"
    elif not text:
        reason = "empty_diagnostic"
    else:
        reason = "unclassified_error"

    return {
        "kind": kind,
        "reason": reason,
        "stage": stage,
        "failed_op": failed_op,
    }


def merge_counts(round_results, key):
    total = Counter()
    for result in round_results or []:
        total.update(result.get(key, {}) or {})
    return dict(sorted(total.items()))


def merge_nested_counts(round_results, key):
    total = {}
    for result in round_results or []:
        for outer, counts in (result.get(key, {}) or {}).items():
            bucket = total.setdefault(outer, Counter())
            bucket.update(counts or {})
    return {
        outer: dict(sorted(counts.items()))
        for outer, counts in sorted(total.items())
    }


def merge_failure_samples(round_results, limit_per_reason=3):
    merged = {}
    for result in round_results or []:
        for reason, samples in (result.get("failure_samples", {}) or {}).items():
            bucket = merged.setdefault(reason, [])
            for sample in samples:
                if sample not in bucket and len(bucket) < limit_per_reason:
                    bucket.append(sample)
    return dict(sorted(merged.items()))


def percentile(values, quantile):
    if not values:
        return None
    ordered = sorted(values)
    index = int(round((len(ordered) - 1) * quantile))
    return ordered[max(0, min(index, len(ordered) - 1))]


def latency_summary_seconds(round_results, key="new_order_latencies"):
    values = []
    for result in round_results or []:
        values.extend(result.get(key, []) or [])
    if not values:
        return {"count": 0, "p50_ms": None, "p95_ms": None, "p99_ms": None, "max_ms": None}
    return {
        "count": len(values),
        "p50_ms": percentile(values, 0.50) * 1000.0,
        "p95_ms": percentile(values, 0.95) * 1000.0,
        "p99_ms": percentile(values, 0.99) * 1000.0,
        "max_ms": max(values) * 1000.0,
    }


def batch_latency_summaries(round_results):
    grouped = {}
    for result in round_results or []:
        for key, values in (result.get("batch_latencies", {}) or {}).items():
            grouped.setdefault(key, []).extend(values or [])
    summaries = {}
    for key, values in sorted(grouped.items()):
        if not values:
            continue
        summaries[key] = {
            "count": len(values),
            "p50_ms": percentile(values, 0.50) * 1000.0,
            "p95_ms": percentile(values, 0.95) * 1000.0,
            "p99_ms": percentile(values, 0.99) * 1000.0,
            "max_ms": max(values) * 1000.0,
        }
    return summaries


def _parse_key_values(line):
    values = {}
    for key, raw in re.findall(r"([A-Za-z0-9_/<>=-]+)=([0-9.]+)", line):
        try:
            values[key] = float(raw) if "." in raw else int(raw)
        except ValueError:
            continue
    return values


def parse_server_diagnostics(path):
    """Parse low-overhead WAL/BPM/MVCC counters from one rmdb server log."""
    result = {
        "path": path,
        "exists": False,
        "line_counts": {},
        "wal_last": {},
        "wal_samples": 0,
        "bpm": {"samples": 0, "max_pinned": None, "min_free": None, "last": {}},
        "mvcc": {"samples": 0, "last": {}},
        "batch_ops": {"samples": 0, "last": {}},
        "abort_stats": {"samples": 0, "last": {}},
        "server_abort_reasons": {},
        "server_abort_samples": {},
    }
    try:
        with open(path, errors="replace") as stream:
            lines = list(stream)
    except OSError:
        return result

    result["exists"] = True
    tags = Counter()
    abort_reasons = Counter()
    abort_samples = {}
    bpm_max_pinned = None
    bpm_min_free = None

    for raw in lines:
        line = raw.strip()
        if "[WAL_STATS " in line:
            tags["wal_stats"] += 1
            result["wal_samples"] += 1
            result["wal_last"] = _parse_key_values(line)
        if "[bpm-stats]" in line:
            tags["bpm_stats"] += 1
            values = _parse_key_values(line)
            result["bpm"]["samples"] += 1
            result["bpm"]["last"] = values
            pinned = values.get("pinned")
            free = values.get("free")
            if pinned is not None:
                bpm_max_pinned = pinned if bpm_max_pinned is None else max(bpm_max_pinned, pinned)
            if free is not None:
                bpm_min_free = free if bpm_min_free is None else min(bpm_min_free, free)
        if "[sweep-wm]" in line:
            tags["sweep_wm"] += 1
            result["mvcc"]["samples"] += 1
            result["mvcc"]["last"] = _parse_key_values(line)
        if "[batch-op-stats]" in line:
            tags["batch_op_stats"] += 1
            values = _parse_key_values(line)
            statement_id = values.get("id")
            result["batch_ops"]["samples"] += 1
            if statement_id is not None:
                result["batch_ops"]["last"][str(statement_id)] = values
        if "[abort-stats " in line:
            tags["abort_stats"] += 1
            result["abort_stats"]["samples"] += 1
            result["abort_stats"]["last"] = _parse_key_values(line)

        server_kind = None
        if "[pressure-abort]" in line:
            server_kind = "pressure_abort"
        elif "[error-abort]" in line:
            server_kind = "error_abort"
        elif "[sql-error]" in line:
            server_kind = "sql_error"
        if server_kind:
            tags[server_kind] += 1
            info = classify_failure(line)
            reason = info["reason"]
            if server_kind == "pressure_abort" and reason == "unclassified_error":
                reason = "buffer_pool_pressure"
            abort_reasons[reason] += 1
            bucket = abort_samples.setdefault(reason, [])
            if len(bucket) < 3:
                bucket.append(line[:240])

    result["line_counts"] = dict(sorted(tags.items()))
    result["bpm"]["max_pinned"] = bpm_max_pinned
    result["bpm"]["min_free"] = bpm_min_free
    result["server_abort_reasons"] = dict(sorted(abort_reasons.items()))
    result["server_abort_samples"] = dict(sorted(abort_samples.items()))
    return result
