#!/usr/bin/env python3
"""Finals-shaped TPC-C performance gate: SI, W=50, 32 clients, 30s + 3×150s."""

from __future__ import annotations

import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def main() -> int:
    cmd = [
        sys.executable, "tests/local/run_oj_perf_test.py",
        "--finals", "--strict", "--threads", "32",
        "--p50-latency-ms", "10", "--p99-latency-ms", "50",
    ]
    print(">>>", " ".join(cmd))
    return subprocess.call(cmd, cwd=ROOT)


if __name__ == "__main__":
    raise SystemExit(main())
