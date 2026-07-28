#!/usr/bin/env python3
"""Finals Load Data gate (W=50, CREATE×9 → INDEX×10 → LOAD×9 → verification).

The server-side load and verification logic is deliberately reused from the
TPC-C harness so the same Wire-v3 path is exercised as the performance test.
The one-second workload is only a trigger for bootstrap/load verification; it
is not a performance result.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def main() -> int:
    start = time.monotonic()
    generate = subprocess.call(
        [sys.executable, "tests/local/generate_tpcc_data.py", "--scale", "full"], cwd=ROOT)
    if generate:
        return generate
    # The harness performs CREATE/INDEX/LOAD, row counts, CSV samples/anchors and
    # one correctly committed mixed workload before it returns.
    cmd = [
        sys.executable, "tests/local/bench_tpcc.py", "--scale", "full",
        "--warmup", "0", "--measure", "1", "--rounds", "1", "--threads", "1",
        "--skip-p2", "--skip-crash", "--skip-consistency",
    ]
    rc = subprocess.call(cmd, cwd=ROOT)
    elapsed = time.monotonic() - start
    print("LOAD DATA: %s (%.1fs; finals budget 900s)" % ("PASS" if rc == 0 else "FAIL", elapsed))
    if elapsed > 900:
        print("LOAD DATA: FAIL (exceeded 900s finals budget)")
        return 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
