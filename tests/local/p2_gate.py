"""P2 functional test gate — must pass before strict TPC-C perf (mirrors OJ prerequisite)."""

import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
P2_SCRIPT = os.path.join(ROOT, "tests", "run_tests.py")


def run_p2_functional_tests():
    """
    Run RMDB P2 automated tests (11 test points).
    Returns True on full pass.
    """
    script = P2_SCRIPT
    if not os.path.isfile(script):
        print("  FAIL: P2 test script not found at tests/run_tests.py")
        return False

    print("\n-- P2 functional tests (OJ prerequisite) --")
    print("  script:", script)
    rc = subprocess.call([sys.executable, script], cwd=ROOT)
    if rc == 0:
        print("  P2 GATE: PASS")
        return True
    print("  P2 GATE: FAIL (exit %d)" % rc)
    return False
