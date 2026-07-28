#!/usr/bin/env python3
"""Non-scoring finals TPC-C I/O diagnostic: 10s warmup plus 60s syscall trace."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from datetime import datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
OUT = os.path.join(ROOT, "build", "io_diagnostics")


def main() -> int:
    strace = shutil.which("strace")
    if not strace:
        print("SKIP: strace is not installed; finals I/O diagnostics are warning-only.")
        return 0
    os.makedirs(OUT, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    trace = os.path.join(OUT, "tpcc_io_%s.strace" % stamp)
    workload = [
        sys.executable, "tests/local/bench_tpcc.py", "--scale", "full",
        "--warmup", "10", "--measure", "60", "--rounds", "1", "--threads", "32",
        "--skip-p2", "--skip-consistency", "--skip-crash",
    ]
    cmd = [
        strace, "-ff", "-ttt", "-o", trace,
        "-e", "trace=write,pwrite64,fdatasync,fsync,sync,syncfs,openat,close",
    ] + workload
    print(">>>", " ".join(cmd))
    rc = subprocess.call(cmd, cwd=ROOT)
    print("TPC-C I/O DIAGNOSTICS: %s (trace prefix %s)" %
          ("WARN" if rc else "COMPLETE", trace))
    # Diagnostics are explicitly non-scoring.  Preserve the workload exit code
    # in the log but do not turn trace-environment limitations into a gate fail.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
