#!/usr/bin/env python3
"""
One-shot OJ-style TPC-C performance test runner.

Tiers (all use self-generated full CSV unless noted):
  --quick              Smoke: 3s + 15s x 1 (NOT OJ window)
  --mid                Daily trend: 30s + 60s x 3, BATCH + finals mix
  --finals             OJ-shaped: 30s + 150s x 3, 32 clients, BATCH
  default / --strict   Pre-submit checks; window 150s x 3 (finals-shaped) + full gates

Protocol default: PREPARE_SET + EXEC_BATCH (pass --stream through for A/B).
"""

import argparse
import subprocess
import sys
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def _banner(tier):
    if tier == "quick":
        print("\n" + "=" * 72)
        print("  WARNING: --quick is SMOKE ONLY (15s x 1). NOT the OJ 150s x 3 metric.")
        print("=" * 72 + "\n")
    elif tier == "mid":
        print("\n" + "=" * 72)
        print("  NOTE: --mid uses 60s x 3 for trend. Use --finals for OJ-shaped 150s x 3.")
        print("=" * 72 + "\n")
    elif tier == "finals":
        print("\n" + "=" * 72)
        print("  FINALS: W=50 / 150s x 3 / 32 clients / BATCH / 45/43/4/4/4.")
        print("=" * 72 + "\n")


def main():
    ap = argparse.ArgumentParser(description="OJ-style TPC-C performance test runner")
    ap.add_argument("--quick", action="store_true",
                    help="smoke: full data, 15s x 1 (NOT OJ metric)")
    ap.add_argument("--mid", action="store_true",
                    help="trend: full data, 60s x 3 median tpmC")
    ap.add_argument("--finals", action="store_true",
                    help="OJ-shaped: 150s x 3, 32 clients, BATCH")
    ap.add_argument("--generate-only", action="store_true", help="only generate full CSV")
    ap.add_argument("--strict", action="store_true",
                    help="pre-submit: full checks (requires Release build)")
    ap.add_argument("--profile", action="store_true", help="run perf flamegraph after benchmark")
    ap.add_argument("--threads", type=int, default=None)
    ap.add_argument("--json", metavar="PATH", default=None, help="pass through to bench_tpcc.py")
    args, extra = ap.parse_known_args()

    tier_flags = sum(bool(x) for x in (args.quick, args.mid, args.finals))
    if tier_flags > 1:
        print("ERROR: --quick / --mid / --finals are mutually exclusive")
        return 2

    if args.generate_only:
        return subprocess.call(
            [sys.executable, os.path.join(ROOT, "tests/local/generate_tpcc_data.py"), "--scale", "full"]
        )

    tier = "quick" if args.quick else ("mid" if args.mid else ("finals" if args.finals else "oj"))
    _banner(tier)

    cmd = [sys.executable, os.path.join(ROOT, "tests/local/bench_tpcc.py")]
    if args.strict:
        cmd.append("--strict")
    if args.quick:
        cmd += ["--scale", "full", "--quick", "--rounds", "1"]
        if args.threads is not None:
            cmd += ["--threads", str(args.threads)]
        if not args.strict:
            cmd += ["--skip-crash"]
    elif args.mid:
        cmd += ["--scale", "full", "--mid"]
        if args.threads is not None:
            cmd += ["--threads", str(args.threads)]
        if not args.strict:
            cmd += ["--skip-crash"]
    elif args.finals:
        cmd += ["--scale", "full", "--finals"]
        if args.threads is not None:
            cmd += ["--threads", str(args.threads)]
        if not args.strict:
            cmd += ["--skip-crash"]
    else:
        # default ≈ finals window + optional --strict gates
        cmd += ["--scale", "full", "--finals"]
        if args.threads is not None:
            cmd += ["--threads", str(args.threads)]
        if not args.strict:
            cmd += ["--skip-crash"]
    if args.json:
        cmd += ["--json", args.json]
    cmd += extra

    print(">>>", " ".join(cmd))
    rc = subprocess.call(cmd, cwd=ROOT)
    if rc != 0:
        return rc

    if args.profile:
        prof = os.path.join(ROOT, "tests/prof/profile_tpcc.sh")
        if os.path.isfile(prof):
            if args.quick:
                flag = "--quick"
            elif args.mid:
                flag = "--measure 60"
            else:
                flag = "--measure 150"
            print("\n>>> profiling:", prof, flag)
            return subprocess.call(["bash", prof] + ([flag] if flag else []), cwd=ROOT)
    return rc


if __name__ == "__main__":
    sys.exit(main())
