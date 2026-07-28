#!/usr/bin/env python3
"""
Consistency regression runner — maps audit findings (C1–M8) to local repro cases.

Usage:
  python3 tests/local/consistency/run_consistency_regress.py --list
  python3 tests/local/consistency/run_consistency_regress.py --quick
  python3 tests/local/consistency/run_consistency_regress.py --case C2
  python3 tests/local/consistency/run_consistency_regress.py --tag mvcc --repeat 5
  python3 tests/local/consistency/run_consistency_regress.py          # full suite

Prerequisites:
  cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make rmdb -j$(nproc)
  ps aux | grep '[r]mdb' | awk '{print $2}' | xargs -r kill -9   # free port 8765
"""

from __future__ import annotations

import argparse
import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_LOCAL = os.path.dirname(_HERE)
if _LOCAL not in sys.path:
    sys.path.insert(0, _LOCAL)
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from tpcc_common import kill_rmdb  # noqa: E402

from consistency.cases import ALL_CASES, CASE_BY_ID  # noqa: E402
from consistency.harness import CaseResult, ensure_rmdb_binary, run_with_repeat  # noqa: E402


def parse_args():
    p = argparse.ArgumentParser(description="RMDB consistency regression (audit C1–M8)")
    p.add_argument("--list", action="store_true", help="list cases and exit")
    p.add_argument("--case", action="append", dest="cases", metavar="ID", help="run case ID (repeatable)")
    p.add_argument("--tag", action="append", dest="tags", metavar="TAG", help="filter by tag (mvcc,wal,...)")
    p.add_argument("--quick", action="store_true", help="skip slow / long cases")
    p.add_argument("--repeat", type=int, default=1, metavar="N", help="repeat flaky cases N times")
    p.add_argument("--stability", type=int, default=0, metavar="N",
                   help="run selected cases N times and print fail rate (for bug repro tuning)")
    p.add_argument("--fail-fast", action="store_true", help="stop on first failure")
    return p.parse_args()


def select_cases(args):
    if args.cases:
        selected = []
        for cid in args.cases:
            key = cid.upper()
            if key not in CASE_BY_ID:
                raise SystemExit("unknown case: %s (use --list)" % cid)
            selected.append(CASE_BY_ID[key])
        return selected
    out = ALL_CASES
    if args.quick:
        out = [c for c in out if c.quick]
    if args.tags:
        tagset = {t.lower() for t in args.tags}
        out = [c for c in out if tagset.intersection(t.lower() for t in c.tags)]
    return out


def print_list():
    print("ID     quick  flaky  xfail  tags                  title")
    print("-" * 78)
    for c in ALL_CASES:
        tags = ",".join(c.tags)
        print(
            "%-6s %-5s  %-5s  %-5s  %-20s  %s"
            % (
                c.case_id,
                "yes" if c.quick else "no",
                "yes" if c.flaky else "no",
                "yes" if c.expect_fail else "no",
                tags,
                c.title,
            )
        )


def run_case(spec, repeat: int) -> CaseResult:
    n = repeat if spec.flaky else 1
    if n > 1:
        return run_with_repeat(spec.fn, n)
    return spec.fn()


def run_stability(selected, rounds: int) -> int:
    print("Stability check: %d rounds x %d case(s)\n" % (rounds, len(selected)))
    any_miss = False
    for spec in selected:
        fails = 0
        for i in range(rounds):
            kill_rmdb()
            time.sleep(0.5)
            try:
                res = spec.fn()
            except Exception as exc:
                res = CaseResult(spec.case_id, spec.title, False, "exception: " + str(exc))
            if spec.expect_fail:
                detected = res.passed
            else:
                detected = (not res.passed) and (not res.detail.startswith("exception:"))
            if detected:
                fails += 1
        rate = 100.0 * fails / rounds
        label = "detected" if not spec.expect_fail else "still-broken"
        print("  %s: %d/%d (%.0f%% %s)" % (spec.case_id, fails, rounds, rate, label))
        if not spec.expect_fail and fails < rounds:
            any_miss = True
        if spec.expect_fail and fails > 0:
            any_miss = True
    return 1 if any_miss else 0


def main():
    args = parse_args()
    if args.list:
        print_list()
        return 0

    ensure_rmdb_binary()
    kill_rmdb()

    selected = select_cases(args)
    if not selected:
        print("No cases selected.")
        return 1

    if args.stability > 0:
        return run_stability(selected, args.stability)

    print("Running %d case(s)%s\n" % (len(selected), " (quick)" if args.quick else ""))
    results: list[CaseResult] = []
    t0 = time.perf_counter()

    for spec in selected:
        label = "[%s] %s" % (spec.case_id, spec.title)
        print("%s ..." % label, flush=True)
        try:
            res = run_case(spec, args.repeat)
        except Exception as exc:
            res = CaseResult(spec.case_id, spec.title, False, "exception: %s" % exc)

        if spec.expect_fail:
            if res.passed:
                status = "XPASS"
                passed_for_summary = True
            else:
                status = "XFAIL"
                passed_for_summary = True
        else:
            status = "PASS" if res.passed else "FAIL"
            passed_for_summary = res.passed

        results.append((spec, res, passed_for_summary))
        extra = ""
        if res.flaky or spec.flaky:
            extra = " (attempts=%d)" % res.attempts
        print("  -> %s%s — %s\n" % (status, extra, res.detail))

        if not passed_for_summary and args.fail_fast:
            break
        kill_rmdb()
        time.sleep(0.8)

    elapsed = time.perf_counter() - t0
    passed = sum(1 for _, _, ok in results if ok)
    failed = len(results) - passed

    print("=" * 60)
    print("SUMMARY: %d/%d passed, %d failed, %.1fs" % (passed, len(results), failed, elapsed))
    if failed:
        print("\nFailed:")
        for spec, r, ok in results:
            if not ok:
                print("  %s: %s" % (r.case_id, r.detail[:120]))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
