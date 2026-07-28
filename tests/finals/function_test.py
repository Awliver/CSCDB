#!/usr/bin/env python3
"""Finals Wire-v3 functional gate: 25 locally derived test groups.

The former P2/framework collection remains available for focused debugging.  This
runner is the finals-facing entry point: it keeps the 11 P2 SQL groups, then
adds Wire, FLOAT32, SI/SER, MVCC rollback and index/snapshot regressions that
the finals specification requires. The OJ publishes only three names from its
25 hidden functional cases, so these are a transparent local derivation—not a
claim to reproduce its hidden catalog one-for-one.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
PYTHON = sys.executable

P2_GROUPS = (
    "尝试建表",
    "单表插入与条件查询",
    "单表更新与条件查询",
    "单表删除与条件查询",
    "连接查询",
    "单独使用聚合函数",
    "聚合函数加分组统计",
    "健壮性测试",
    "边界情况测试",
    "JOIN聚合测试",
    "大规模聚合综合测试",
)


@dataclass(frozen=True)
class Check:
    test_id: str
    title: str
    consistency_case: str | None = None


EXTRA_CHECKS = (
    Check("F12", "FLOAT32 scientific-notation Wire bind", "F1"),
    Check("F13", "FLOAT32 NaN/Inf query-parameter semantics", "F2"),
    Check("F14", "SI stale write abort", "SI1"),
    Check("F15", "SI active write immediate abort", "SI2"),
    Check("F16", "SI delete snapshot/index lifetime", "SI3"),
    Check("F17", "delete-reinsert rollback visibility", "C8"),
    Check("F18", "uncommitted insert visibility race", "C1"),
    Check("F19", "pending same-key insert conflict", "C2"),
    Check("F20", "snapshot scan MVCC cache", "H5"),
    Check("F21", "compound rollback insert/update", "C5"),
    Check("F22", "compound rollback update/update", "C6"),
    Check("F23", "compound rollback insert/delete", "C7"),
    Check("F24", "SER INLJ rw-dependency smoke", "H3"),
    Check("F25", "SER metadata/GC concurrency", "H4"),
)


def run(cmd: list[str]) -> tuple[int, str]:
    proc = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    return proc.returncode, proc.stdout


def run_p2() -> dict[str, bool]:
    rc, out = run([PYTHON, "tests/run_tests.py"])
    status = {}
    for name in P2_GROUPS:
        status[name] = rc == 0 and bool(re.search(r"\[PASS\]\s+" + re.escape(name), out))
    if rc != 0:
        print(out, end="" if out.endswith("\n") else "\n")
    return status


def run_case(case_id: str) -> tuple[bool, str]:
    rc, out = run([
        PYTHON, "tests/local/consistency/run_consistency_regress.py",
        "--case", case_id,
    ])
    summary = next((line.strip() for line in reversed(out.splitlines())
                    if line.startswith("SUMMARY:")), "no summary")
    if rc:
        failures = [line.strip() for line in out.splitlines() if line.strip().startswith(case_id + ":")]
        summary = failures[-1] if failures else summary
    return rc == 0, summary


def main() -> int:
    parser = argparse.ArgumentParser(description="25-group finals functional gate")
    parser.add_argument("--list", action="store_true", help="list checks without running")
    parser.add_argument("--only", action="append", default=[], metavar="ID",
                        help="run only a test id, e.g. P2-01 or F15")
    args = parser.parse_args()

    checks = [Check("P2-%02d" % (i + 1), name) for i, name in enumerate(P2_GROUPS)]
    checks.extend(EXTRA_CHECKS)
    if args.list:
        for c in checks:
            print("%-5s %s" % (c.test_id, c.title))
        return 0
    selected = {v.upper() for v in args.only}
    if selected:
        checks = [c for c in checks if c.test_id.upper() in selected]
        if not checks:
            parser.error("--only did not select a known test")

    p2_needed = any(c.test_id.startswith("P2-") for c in checks)
    p2 = run_p2() if p2_needed else {}
    passed = 0
    for check in checks:
        if check.test_id.startswith("P2-"):
            ok = p2[check.title]
            detail = "P2 Wire EXEC_STREAM"
        else:
            ok, detail = run_case(check.consistency_case or "")
        print("[%s] %-5s %s — %s" % ("PASS" if ok else "FAIL", check.test_id, check.title, detail))
        passed += int(ok)
    print("FUNCTION TEST: %d/%d passed" % (passed, len(checks)))
    return 0 if passed == len(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
