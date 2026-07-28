#!/usr/bin/env python3
"""List local TPC-C bench history records (build/bench_history/)."""

import argparse
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from oj_fit import HISTORY_DIR, HISTORY_INDEX, HISTORY_LATEST  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description="List auto-saved local TPC-C bench records")
    ap.add_argument("-n", type=int, default=20, help="show last N index rows (default 20)")
    ap.add_argument("--latest", action="store_true", help="print LATEST.json")
    args = ap.parse_args()

    if args.latest:
        if not os.path.isfile(HISTORY_LATEST):
            print("no LATEST.json under", HISTORY_DIR)
            return 1
        with open(HISTORY_LATEST) as f:
            print(json.dumps(json.load(f), indent=2, ensure_ascii=False))
        return 0

    if not os.path.isfile(HISTORY_INDEX):
        print("no history yet:", HISTORY_INDEX)
        print("run: python3 tests/local/run_oj_perf_test.py --quick")
        return 1

    rows = []
    with open(HISTORY_INDEX) as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    rows = rows[-args.n :]
    print("%-19s %-6s %4s %4s %10s %-6s %-8s %s" % (
        "time", "tier", "W", "thr", "median", "result", "git", "file"))
    for r in rows:
        med = r.get("median_tpmc")
        med_s = "%.2f" % med if med is not None else "-"
        print("%-19s %-6s %4s %4s %10s %-6s %-8s %s" % (
            r.get("written_at", "-"),
            r.get("tier", "-"),
            r.get("warehouses", "-"),
            r.get("threads", "-"),
            med_s,
            r.get("overall", "-"),
            r.get("git_rev", "-"),
            r.get("file", "-"),
        ))
    print("dir:", HISTORY_DIR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
