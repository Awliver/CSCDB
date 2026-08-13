"""OJ fidelity helpers: build check, result summary, JSON baseline."""

import json
import os
import platform
import subprocess
import time

from tpcc_common import BUILD, RMDB
from perf_reporting import (
    batch_latency_summaries,
    latency_summary_seconds,
    merge_counts,
    merge_failure_samples,
    merge_nested_counts,
)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

# Self-generated full CSV (OJ does not ship official data).
DATA_SOURCE = "self-generated (build/tpcc_data/full via generate_tpcc_data.py)"


def detect_build_info():
    """Best-effort build type from rmdb binary strings."""
    info = {
        "path": RMDB,
        "exists": os.path.isfile(RMDB),
        "build_type": "unknown",
        "optimize": "unknown",
        "release_like": False,
        "warning": "",
    }
    if not info["exists"]:
        info["warning"] = "rmdb binary missing"
        return info

    try:
        out = subprocess.run(
            ["strings", RMDB],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        ).stdout
    except (OSError, subprocess.TimeoutExpired):
        info["warning"] = "could not inspect rmdb binary"
        return info

    for line in out.splitlines():
        if "CMAKE_BUILD_TYPE=" in line:
            info["build_type"] = line.split("=", 1)[-1].strip()
        if line.startswith("GCC:") or line.startswith("clang version"):
            if "-O0" in line:
                info["optimize"] = "-O0"
            elif "-O3" in line:
                info["optimize"] = "-O3"
            elif "-O2" in line:
                info["optimize"] = "-O2"

    if info["build_type"] == "unknown" or info["optimize"] == "unknown":
        cache = os.path.join(BUILD, "CMakeCache.txt")
        if os.path.isfile(cache):
            try:
                with open(cache) as f:
                    for line in f:
                        if info["build_type"] == "unknown" and line.startswith("CMAKE_BUILD_TYPE:"):
                            info["build_type"] = line.split("=", 1)[-1].strip()
                        if info["optimize"] == "unknown" and line.startswith("CMAKE_CXX_FLAGS_RELEASE:"):
                            flags = line.split("=", 1)[-1].strip()
                            if "-O0" in flags:
                                info["optimize"] = "-O0"
                            elif "-O3" in flags:
                                info["optimize"] = "-O3"
                            elif "-O2" in flags:
                                info["optimize"] = "-O2"
            except OSError:
                pass

    bt = info["build_type"].lower()
    opt = info["optimize"]
    info["release_like"] = bt == "release" or opt == "-O3"
    if bt in ("debug",) or opt == "-O0":
        info["warning"] = (
            "build looks like Debug/-O0; OJ uses Release/-O3 — tpmC numbers are NOT comparable"
        )
    return info


def require_release_build(strict=False):
    """Print build info; return False if strict and not release-like."""
    info = detect_build_info()
    print("  build:", info["build_type"], "opt:", info["optimize"], "path:", info["path"])
    if info["warning"]:
        print("  WARNING:", info["warning"])
    if strict and not info["release_like"]:
        print("  FAIL: --strict requires Release build (cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make rmdb)")
        return False
    return True


def tier_label(quick=False, mid=False, finals=False, strict=False):
    if finals:
        return "finals"
    if strict and not quick and not mid:
        return "oj"
    if mid:
        return "mid"
    if quick:
        return "quick"
    return "custom"


def print_oj_summary(
    *,
    tier,
    scale,
    threads,
    seed,
    warmup,
    measure,
    rounds,
    tpms,
    median_tpm,
    round_results,
    consistency_ok=None,
    crash_ok=None,
    overall_pass,
    data_source=DATA_SOURCE,
    mix_desc=None,
    protocol=None,
):
    """OJ-aligned one-screen summary for comparing with online results."""
    total_no_ok = sum(r.get("new_order_ok", 0) for r in round_results)
    total_no_fail = sum(r.get("new_order_fail", 0) for r in round_results)
    total_other_fail = sum(r.get("other_fail", 0) for r in round_results)

    print("\n=== OJ FIT SUMMARY ===")
    print("  tier:          %s" % tier)
    print("  data:          %s (%s)" % (scale, data_source))
    print("  threads:       %d" % threads)
    print("  seed:          %d" % seed)
    print("  window:        warmup=%ss measure=%ss rounds=%d" % (warmup, measure, rounds))
    print("  protocol:      %s" % (protocol or "PREPARE_SET+EXEC_BATCH"))
    print("  mix:           %s" % (mix_desc or "finals 45/43/4/4/4"))
    print("  isolation:     snapshot isolation")
    print("  rounds tpmC:   %s" % ["%.2f" % x for x in tpms])
    print("  median tpmC:   %.2f  (OJ metric)" % median_tpm)
    print("  new_order:     ok=%d fail=%d" % (total_no_ok, total_no_fail))
    print("  other_fail:    %d" % total_other_fail)
    pa1_attempted = 0
    pa1_rhs = 0
    for r in round_results:
        for counts in r.get("p_a1_outcomes", {}).values():
            pa1_attempted += counts.get("attempted", 0)
            pa1_rhs += sum(counts.get(k, 0) for k in (
                "committed", "business_rollback", "abnormal_abort", "error"
            ))
    if pa1_attempted:
        print("  P-A1 reconcile: %s (%d=%d)" % (
            "PASS" if pa1_attempted == pa1_rhs else "FAIL", pa1_attempted, pa1_rhs
        ))
    for i, r in enumerate(round_results, 1):
        coverage = r.get("p_a1_home_coverage")
        if coverage is not None:
            print("  round %d home:   %d/%d warehouses" % (
                i, coverage, r.get("p_a1_home_warehouse_total", coverage)
            ))
    attempts = total_no_ok + total_no_fail
    if attempts:
        print("  NO abort rate: %.2f%%" % (total_no_fail * 100.0 / attempts))
    latency = latency_summary_seconds(round_results)
    all_success_latency = latency_summary_seconds(round_results, "all_success_latencies")
    if latency["count"]:
        print("  pooled NO lat: p50=%.2fms p95=%.2fms p99=%.2fms" % (
            latency["p50_ms"], latency["p95_ms"], latency["p99_ms"]))
    if all_success_latency["count"]:
        print("  all-success:   p50=%.2fms p95=%.2fms p99=%.2fms (diagnostic)" % (
            all_success_latency["p50_ms"], all_success_latency["p95_ms"],
            all_success_latency["p99_ms"]))
    batch_latency = batch_latency_summaries(round_results)
    for key in ("new_order.b1", "new_order.b2"):
        summary = batch_latency.get(key)
        if summary:
            print("  %-14s p50=%.2fms p95=%.2fms p99=%.2fms" % (
                key + ":", summary["p50_ms"], summary["p95_ms"], summary["p99_ms"]))
    abort_reasons = merge_counts(round_results, "abort_reasons")
    if abort_reasons:
        ranked = sorted(abort_reasons.items(), key=lambda item: (-item[1], item[0]))
        print("  abort reasons: %s" % ", ".join("%s=%d" % item for item in ranked[:6]))
    for i, r in enumerate(round_results, 1):
        elapsed = r.get("elapsed", 0)
        expected = measure
        flag = ""
        if elapsed < expected * 0.85:
            flag = "  *** elapsed %.1fs << measure %.0fs (workers exited early?) ***" % (
                elapsed, expected
            )
        print("  round %d wall:   %.1fs%s" % (i, elapsed, flag))
    if consistency_ok is not None:
        print("  consistency:   %s" % ("PASS" if consistency_ok else "FAIL"))
    if crash_ok is not None:
        print("  crash_recv:    %s" % ("PASS" if crash_ok else "FAIL"))
    build = detect_build_info()
    print("  build:         %s (%s)" % (build["build_type"], build["optimize"]))
    print("  OVERALL:       %s" % ("PASS" if overall_pass else "FAIL"))
    if tier in ("quick", "mid"):
        print("  NOTE: tier=%s is NOT the OJ 150s×3 submission window; use --finals/--strict." % tier)
    elif tier == "finals":
        print("  NOTE: local generated CSV follows W=50 cardinalities; hardware and hidden identifiers")
        print("        still make absolute tpmC non-comparable with the OJ.")


HISTORY_DIR = os.path.join(BUILD, "bench_history")
HISTORY_INDEX = os.path.join(HISTORY_DIR, "index.jsonl")
HISTORY_LATEST = os.path.join(HISTORY_DIR, "LATEST.json")


def write_json_result(path, payload):
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    payload = dict(payload)
    if "written_at" not in payload:
        payload["written_at"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    with open(path, "w") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)
    print("  json:          %s" % path)


def _git_cmd(*args):
    try:
        return subprocess.check_output(
            ["git"] + list(args),
            cwd=ROOT,
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return ""


def git_revision():
    return _git_cmd("rev-parse", "--short", "HEAD") or "unknown"


def git_info():
    """Code version snapshot for bench history records."""
    tested_rev = os.environ.get("RMDB_BENCH_GIT_REV")
    if tested_rev:
        return {
            "git_rev": tested_rev[:12],
            "git_rev_full": tested_rev,
            "git_branch": os.environ.get("RMDB_BENCH_GIT_BRANCH", "detached"),
            "git_dirty": os.environ.get("RMDB_BENCH_GIT_DIRTY", "0") == "1",
            "git_subject": os.environ.get("RMDB_BENCH_GIT_SUBJECT", ""),
            "git_commit_time": os.environ.get("RMDB_BENCH_GIT_COMMIT_TIME", ""),
        }
    short = git_revision()
    full = _git_cmd("rev-parse", "HEAD") or short
    branch = _git_cmd("rev-parse", "--abbrev-ref", "HEAD") or "unknown"
    dirty = bool(_git_cmd("status", "--porcelain"))
    subject = _git_cmd("log", "-1", "--pretty=%s")
    commit_time = _git_cmd("log", "-1", "--pretty=%ci")
    return {
        "git_rev": short,
        "git_rev_full": full,
        "git_branch": branch,
        "git_dirty": dirty,
        "git_subject": subject,
        "git_commit_time": commit_time,
    }


def build_result_payload(
    *,
    tier,
    scale,
    warehouses,
    threads,
    seed,
    warmup,
    measure,
    rounds,
    tpms,
    median_tpm,
    round_results,
    overall,
    fail_stage=None,
    consistency_ok=None,
    crash_ok=None,
    data_source=DATA_SOURCE,
    extra=None,
):
    """Assemble one bench record (PASS or FAIL)."""
    build = detect_build_info()
    g = git_info()
    pa1_outcomes = {}
    pa1_abort_rows = {}
    pa1_home_warehouses = {}
    for result in round_results or []:
        for txn, counts in result.get("p_a1_outcomes", {}).items():
            dst = pa1_outcomes.setdefault(txn, {
                "attempted": 0, "committed": 0, "business_rollback": 0,
                "abnormal_abort": 0, "error": 0,
            })
            for key in dst:
                dst[key] += counts.get(key, 0)
        for row in result.get("p_a1_abort_attribution", []):
            key = (row.get("txn"), row.get("stmt_id"), row.get("failed_op"),
                   row.get("reason"), row.get("hotspot"))
            if key not in pa1_abort_rows:
                pa1_abort_rows[key] = dict(row)
                pa1_abort_rows[key]["count"] = 0
            pa1_abort_rows[key]["count"] += row.get("count", 0)
        for warehouse, count in result.get("p_a1_home_warehouses", {}).items():
            key = str(warehouse)
            pa1_home_warehouses[key] = pa1_home_warehouses.get(key, 0) + count
    total_abnormal = sum(v.get("abnormal_abort", 0) for v in pa1_outcomes.values())
    reason_counts = {}
    for row in pa1_abort_rows.values():
        reason = row.get("reason", "OTHER")
        reason_counts[reason] = reason_counts.get(reason, 0) + row.get("count", 0)
    top3 = sum(sorted(reason_counts.values(), reverse=True)[:3])
    pa1_reconcile = all(
        counts.get("attempted", 0) == sum(counts.get(k, 0) for k in (
            "committed", "business_rollback", "abnormal_abort", "error"
        ))
        for counts in pa1_outcomes.values()
    )

    results = round_results or []
    new_order_ok = sum(r.get("new_order_ok", 0) for r in results)
    new_order_fail = sum(r.get("new_order_fail", 0) for r in results)
    new_order_attempts = new_order_ok + new_order_fail
    by_type = merge_nested_counts(results, "by_type")
    abort_reasons = merge_counts(results, "abort_reasons")
    failure_reasons = merge_counts(results, "failure_reasons")
    failure_stages = merge_counts(results, "failure_stages")
    abort_by_type = merge_nested_counts(results, "abort_by_type")
    latency = latency_summary_seconds(results)
    all_success_latency = latency_summary_seconds(results, "all_success_latencies")
    batch_latency = batch_latency_summaries(results)
    warehouse_attempts = merge_counts(results, "warehouse_attempts")
    warehouse_ok = merge_counts(results, "warehouse_ok")
    round_details = []
    for result in results:
        round_details.append({
            "tpmc": result.get("tpm"),
            "elapsed_sec": result.get("elapsed"),
            "new_order_ok": result.get("new_order_ok", 0),
            "new_order_fail": result.get("new_order_fail", 0),
            "other_ok": result.get("other_ok", 0),
            "other_fail": result.get("other_fail", 0),
            "abort_reasons": result.get("abort_reasons", {}),
            "failure_reasons": result.get("failure_reasons", {}),
            "failure_stages": result.get("failure_stages", {}),
            "warehouse_attempts": result.get("warehouse_attempts", {}),
            "warehouse_ok": result.get("warehouse_ok", {}),
        })
    payload = {
        "written_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "tier": tier,
        "scale": scale,
        "warehouses": warehouses,
        "data_source": data_source,
        "threads": threads,
        "seed": seed,
        "warmup_sec": warmup,
        "measure_sec": measure,
        "rounds": rounds,
        "tpms": list(tpms) if tpms is not None else [],
        "median_tpmc": median_tpm,
        "p_a1_outcomes": pa1_outcomes,
        "p_a1_abort_attribution": sorted(
            pa1_abort_rows.values(),
            key=lambda row: (-row.get("count", 0), row.get("txn", ""), row.get("stmt_id", 0)),
        ),
        "p_a1_reason_counts": reason_counts,
        "p_a1_top3_abort_coverage": (top3 / total_abnormal) if total_abnormal else 1.0,
        "p_a1_reconcile": pa1_reconcile,
        "p_a1_home_warehouses": pa1_home_warehouses,
        "p_a1_home_coverage": len(pa1_home_warehouses),
        "new_order_ok": new_order_ok,
        "new_order_fail": new_order_fail,
        "new_order_abort_rate": (
            float(new_order_fail) / new_order_attempts if new_order_attempts else None
        ),
        "other_ok": sum(r.get("other_ok", 0) for r in results),
        "other_fail": sum(r.get("other_fail", 0) for r in results),
        "by_type": by_type,
        "abort_reasons": abort_reasons,
        "abort_by_type": abort_by_type,
        "failure_reasons": failure_reasons,
        "failure_stages": failure_stages,
        "failure_samples": merge_failure_samples(results),
        "new_order_latency": latency,
        "all_success_latency": all_success_latency,
        "batch_latency": batch_latency,
        "warehouse_attempts": warehouse_attempts,
        "warehouse_ok": warehouse_ok,
        "round_elapsed_sec": [r.get("elapsed", 0) for r in results],
        "round_details": round_details,
        "consistency": (
            None if consistency_ok is None else ("PASS" if consistency_ok else "FAIL")
        ),
        "crash_recovery": (
            None if crash_ok is None else ("PASS" if crash_ok else "FAIL")
        ),
        "build_type": build["build_type"],
        "optimize": build["optimize"],
        "host": {
            "hostname": platform.node(),
            "platform": platform.platform(),
            "cpu_count": os.cpu_count(),
        },
        "overall": overall,
        "fail_stage": fail_stage,
        **g,
    }
    if extra:
        payload.update(extra)
    return payload


def _history_filename(payload):
    ts = time.strftime("%Y%m%d_%H%M%S")
    tier = payload.get("tier") or "custom"
    w = payload.get("warehouses")
    t = payload.get("threads")
    med = payload.get("median_tpmc")
    overall = payload.get("overall") or "UNKNOWN"
    parts = [ts, tier]
    if w is not None:
        parts.append("w%d" % w)
    if t is not None:
        parts.append("t%d" % t)
    if med is not None and overall == "PASS":
        parts.append("%.0f" % med)
    else:
        parts.append(str(overall).lower())
    return "_".join(parts) + ".json"


def save_bench_history(payload, history_dir=None, also_path=None):
    """
    Persist bench result + code version under build/bench_history/.

    Writes:
      - timestamped JSON
      - LATEST.json (overwrite)
      - index.jsonl (append one line)
      - optional also_path (--json)
    """
    history_dir = history_dir or HISTORY_DIR
    os.makedirs(history_dir, exist_ok=True)
    payload = dict(payload)
    if "written_at" not in payload:
        payload["written_at"] = time.strftime("%Y-%m-%dT%H:%M:%S")

    path = os.path.join(history_dir, _history_filename(payload))
    with open(path, "w") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)

    with open(HISTORY_LATEST if history_dir == HISTORY_DIR else os.path.join(history_dir, "LATEST.json"), "w") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)

    index_path = HISTORY_INDEX if history_dir == HISTORY_DIR else os.path.join(history_dir, "index.jsonl")
    index_row = {
        "written_at": payload.get("written_at"),
        "file": os.path.basename(path),
        "tier": payload.get("tier"),
        "warehouses": payload.get("warehouses"),
        "threads": payload.get("threads"),
        "median_tpmc": payload.get("median_tpmc"),
        "new_order_abort_rate": payload.get("new_order_abort_rate"),
        "new_order_p99_ms": (payload.get("new_order_latency") or {}).get("p99_ms"),
        "pooled_new_order_p50_ms": (payload.get("new_order_latency") or {}).get("p50_ms"),
        "pooled_new_order_p99_ms": (payload.get("new_order_latency") or {}).get("p99_ms"),
        "all_success_p99_ms": (payload.get("all_success_latency") or {}).get("p99_ms"),
        "overall": payload.get("overall"),
        "fail_stage": payload.get("fail_stage"),
        "git_rev": payload.get("git_rev"),
        "git_branch": payload.get("git_branch"),
        "git_dirty": payload.get("git_dirty"),
        "build_type": payload.get("build_type"),
    }
    with open(index_path, "a") as f:
        f.write(json.dumps(index_row, ensure_ascii=False) + "\n")

    print("  history:       %s" % path)
    print("  history index: %s" % index_path)

    if also_path:
        write_json_result(also_path, payload)
    return path
