#!/usr/bin/env python3
"""Scan thread counts for best median tpmC (OJ-fit, full scale, mid window)."""

import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BENCH = os.path.join(ROOT, "tests/local/bench_tpcc.py")


def main():
    ap = argparse.ArgumentParser(description="Thread sweep for OJ-fit tpmC")
    ap.add_argument("--threads", default="4,6,8,10,12,16",
                    help="comma-separated thread counts")
    ap.add_argument("--quick", action="store_true", help="use 15s quick tier (default: mid 60sx3)")
    ap.add_argument("--json-dir", default=os.path.join(ROOT, "build/tpmc_sweep"),
                    help="directory for per-run JSON results")
    args = ap.parse_args()

    thread_list = [int(x.strip()) for x in args.threads.split(",") if x.strip()]
    os.makedirs(args.json_dir, exist_ok=True)

    print("=== tpmC thread sweep (full scale, self-generated data) ===")
    print("  threads:", thread_list)
    print("  tier:", "quick" if args.quick else "mid")
    print("  NOTE: run with Release build for OJ-comparable numbers\n")

    results = []
    for t in thread_list:
        json_path = os.path.join(args.json_dir, "threads_%d.json" % t)
        cmd = [
            sys.executable, BENCH, "--scale", "full", "--threads", str(t),
            "--skip-crash", "--skip-consistency", "--json", json_path,
        ]
        cmd.append("--quick" if args.quick else "--mid")
        print(">>>", " ".join(cmd))
        rc = subprocess.call(cmd, cwd=ROOT)
        median = None
        if os.path.isfile(json_path):
            with open(json_path) as f:
                median = json.load(f).get("median_tpmc")
        results.append((t, rc, median))
        print()

    print("=== SWEEP SUMMARY ===")
    print("%8s  %8s  %12s" % ("threads", "exit", "median_tpmC"))
    best = None
    for t, rc, med in results:
        print("%8d  %8d  %12s" % (t, rc, "%.2f" % med if med is not None else "n/a"))
        if rc == 0 and med is not None and (best is None or med > best[1]):
            best = (t, med)
    if best:
        print("\n  best: threads=%d median_tpmC=%.2f" % best)
        print("  use: python3 tests/local/run_oj_perf_test.py --strict --threads %d" % best[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
