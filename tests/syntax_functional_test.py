#!/usr/bin/env python3
"""Unified functional suite for SQL query-expression and predicate syntax."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / "tests" / "local"
DEFAULT_PREDICATE_ROWS = 1_000_000
DEFAULT_UNION_ROWS = 1_000_000
DEFAULT_DIFF_ROWS = 2_000
DEFAULT_DIFF_CASES = 100
DEFAULT_SEEDS = "78,91,11451419"
DEFAULT_UNION_DIFF_CASES = 40
DEFAULT_UNION_SEEDS = "42,99,20260812"
QUICK_PREDICATE_ROWS = 10_000
QUICK_UNION_ROWS = 10_000
QUICK_DIFF_ROWS = 500
QUICK_DIFF_CASES = 25
QUICK_SEEDS = "42"
QUICK_UNION_DIFF_CASES = 8
QUICK_UNION_SEEDS = "42"


@dataclass(frozen=True)
class Component:
    name: str
    command: tuple[str, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--quick",
        action="store_true",
        help="run the minimum large-data gate and a smaller random differential set",
    )
    parser.add_argument(
        "--component",
        choices=(
            "all",
            "parser",
            "boolean",
            "union",
            "union-differential",
            "predicates",
            "differential",
        ),
        default="all",
        help="run the complete suite or one internal component (default: all)",
    )
    parser.add_argument("--predicate-rows", type=int)
    parser.add_argument("--union-rows", type=int)
    parser.add_argument("--diff-rows", type=int)
    parser.add_argument("--cases-per-seed", type=int)
    parser.add_argument("--seeds")
    parser.add_argument("--union-cases-per-seed", type=int)
    parser.add_argument("--union-seeds")
    parser.add_argument(
        "--postgres-dsn",
        default=os.environ.get("RMDB_POSTGRES_DSN", ""),
        help="reuse an existing PostgreSQL instance instead of a temporary cluster",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path(os.environ.get("RMDB_BUILD_DIR", ROOT / "build")),
        help="RMDB build directory (default: RMDB_BUILD_DIR or ./build)",
    )
    args = parser.parse_args()

    if args.predicate_rows is None:
        args.predicate_rows = (
            QUICK_PREDICATE_ROWS if args.quick else DEFAULT_PREDICATE_ROWS
        )
    if args.diff_rows is None:
        args.diff_rows = QUICK_DIFF_ROWS if args.quick else DEFAULT_DIFF_ROWS
    if args.union_rows is None:
        args.union_rows = QUICK_UNION_ROWS if args.quick else DEFAULT_UNION_ROWS
    if args.cases_per_seed is None:
        args.cases_per_seed = (
            QUICK_DIFF_CASES if args.quick else DEFAULT_DIFF_CASES
        )
    if args.seeds is None:
        args.seeds = QUICK_SEEDS if args.quick else DEFAULT_SEEDS
    if args.union_cases_per_seed is None:
        args.union_cases_per_seed = (
            QUICK_UNION_DIFF_CASES if args.quick else DEFAULT_UNION_DIFF_CASES
        )
    if args.union_seeds is None:
        args.union_seeds = QUICK_UNION_SEEDS if args.quick else DEFAULT_UNION_SEEDS

    if args.predicate_rows < 10_000:
        parser.error("--predicate-rows must be at least 10000")
    if args.diff_rows < 100:
        parser.error("--diff-rows must be at least 100")
    if args.union_rows < 10_000:
        parser.error("--union-rows must be at least 10000")
    if args.union_rows > 5_000_000:
        parser.error("--union-rows must not exceed 5000000")
    if args.cases_per_seed < 1:
        parser.error("--cases-per-seed must be positive")
    if args.union_cases_per_seed < 1:
        parser.error("--union-cases-per-seed must be positive")
    return args


def build_components(args: argparse.Namespace) -> list[Component]:
    python = sys.executable
    components = [
        Component(
            "parser and AST unit tests",
            (str(args.build_dir.resolve() / "bin" / "test_parser"),),
        ),
        Component(
            "boolean expression tree",
            (python, "-B", str(LOCAL / "boolean_expression_gate.py")),
        ),
        Component(
            "UNION query expressions",
            (
                python,
                "-B",
                str(LOCAL / "union_query_expression_gate.py"),
                "--rows",
                str(args.union_rows),
            ),
        ),
        Component(
            "random UNION SQL/PostgreSQL four-way differential gate",
            (
                python,
                "-B",
                str(LOCAL / "postgresql_union_differential.py"),
                "--rows",
                str(args.union_rows),
                "--cases-per-seed",
                str(args.union_cases_per_seed),
                "--seeds",
                args.union_seeds,
            ),
        ),
        Component(
            "LIKE/BETWEEN/EXISTS/IN deterministic gate",
            (
                python,
                "-B",
                str(LOCAL / "predicate_keywords_gate.py"),
                "--rows",
                str(args.predicate_rows),
            ),
        ),
        Component(
            "random SQL/PostgreSQL differential gate",
            (
                python,
                "-B",
                str(LOCAL / "postgresql_predicate_differential.py"),
                "--rows",
                str(args.diff_rows),
                "--cases-per-seed",
                str(args.cases_per_seed),
                "--seeds",
                args.seeds,
            ),
        ),
    ]
    if args.postgres_dsn:
        for index in (3, 5):
            differential = components[index]
            components[index] = Component(
                differential.name,
                (*differential.command, "--postgres-dsn", args.postgres_dsn),
            )
    if args.component == "all":
        return components
    index = {
        "parser": 0,
        "boolean": 1,
        "union": 2,
        "union-differential": 3,
        "predicates": 4,
        "differential": 5,
    }[args.component]
    return [components[index]]


def run_component(component: Component, env: dict[str, str]) -> float:
    print(f"\n{'=' * 72}\n[SUITE] {component.name}\n{'=' * 72}", flush=True)
    started = time.monotonic()
    result = subprocess.run(component.command, cwd=ROOT, env=env, check=False)
    elapsed = time.monotonic() - started
    if result.returncode != 0:
        raise RuntimeError(
            f"{component.name} failed with exit code {result.returncode}"
        )
    print(f"[SUITE PASS] {component.name} ({elapsed:.2f}s)", flush=True)
    return elapsed


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    server = build_dir / "bin" / "rmdb"
    parser_test = build_dir / "bin" / "test_parser"
    if not server.is_file():
        print(f"[FATAL] server binary not found: {server}", file=sys.stderr)
        return 2
    if not parser_test.is_file():
        print(f"[FATAL] parser test binary not found: {parser_test}", file=sys.stderr)
        return 2

    env = os.environ.copy()
    env["RMDB_BUILD_DIR"] = str(build_dir)
    components = build_components(args)
    total_started = time.monotonic()
    completed: list[tuple[str, float]] = []
    try:
        for component in components:
            completed.append((component.name, run_component(component, env)))
    except (RuntimeError, KeyboardInterrupt) as exc:
        print(f"\n[SYNTAX SUITE FAILED] {exc}", file=sys.stderr)
        return 1

    total_elapsed = time.monotonic() - total_started
    print("\nSyntax functional correctness summary:")
    for name, elapsed in completed:
        print(f"  PASS  {name} ({elapsed:.2f}s)")
    print(f"\nALL {len(completed)}/{len(components)} COMPONENTS PASSED ({total_elapsed:.2f}s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
