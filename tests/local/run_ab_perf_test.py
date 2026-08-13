#!/usr/bin/env python3
"""Build two git refs and run equivalent local TPC-C measurements sequentially."""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BENCH = os.path.join(ROOT, "tests/local/bench_tpcc.py")
COMPARE = os.path.join(ROOT, "tests/local/compare_tpcc_results.py")


def output(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def ref_metadata(ref):
    return {
        "rev": output("git", "rev-parse", ref),
        "subject": output("git", "show", "-s", "--format=%s", ref),
        "commit_time": output("git", "show", "-s", "--format=%ci", ref),
    }


def run_logged(command, cwd, env, log_path):
    print(">>>", " ".join(command))
    with open(log_path, "w") as log:
        proc = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, bufsize=1)
        for line in proc.stdout:
            sys.stdout.write(line)
            log.write(line)
        return proc.wait()


def prepare_ref(ref, label, temp_root, jobs, shared_data=None):
    worktree = os.path.join(temp_root, label)
    build = os.path.join(worktree, "build")
    meta = ref_metadata(ref)
    subprocess.check_call(["git", "worktree", "add", "--detach", worktree, meta["rev"]], cwd=ROOT)
    subprocess.check_call([
        "cmake", "-S", worktree, "-B", build, "-DCMAKE_BUILD_TYPE=Release"
    ])
    subprocess.check_call(["cmake", "--build", build, "--target", "rmdb", "-j", str(jobs)])

    data_link = os.path.join(build, "tpcc_data")
    if shared_data and not os.path.exists(data_link):
        os.symlink(shared_data, data_link, target_is_directory=True)
    return {"ref": ref, "worktree": worktree, "build": build, "meta": meta}


def prepare_current_tree(jobs):
    """Build the current, possibly dirty tree without copying or cleaning it."""
    build = os.path.join(ROOT, "build")
    subprocess.check_call([
        "cmake", "-S", ROOT, "-B", build, "-DCMAKE_BUILD_TYPE=Release"
    ])
    subprocess.check_call(["cmake", "--build", build, "--target", "rmdb", "-j", str(jobs)])
    meta = ref_metadata("HEAD")
    meta["dirty"] = bool(output("git", "status", "--porcelain"))
    return {
        "ref": "WORKTREE",
        "worktree": None,
        "build": build,
        "meta": meta,
    }


def benchmark(prepared, label, args, result_dir):
    json_path = os.path.join(result_dir, label + ".json")
    log_path = os.path.join(result_dir, label + ".log")
    env = dict(os.environ)
    env.update({
        "RMDB_TEST_BUILD": prepared["build"],
        "RMDB_TEST_BINARY": os.path.join(prepared["build"], "bin/rmdb"),
        "RMDB_BENCH_GIT_REV": prepared["meta"]["rev"],
        "RMDB_BENCH_GIT_BRANCH": prepared["ref"],
        "RMDB_BENCH_GIT_SUBJECT": prepared["meta"]["subject"],
        "RMDB_BENCH_GIT_COMMIT_TIME": prepared["meta"]["commit_time"],
        "RMDB_BENCH_GIT_DIRTY": "1" if prepared["meta"].get("dirty") else "0",
    })
    command = [
        sys.executable,
        BENCH,
        "--scale", "full",
        "--" + args.tier,
        "--threads", str(args.threads),
        "--seed", str(args.seed),
        "--rounds", str(args.rounds),
        "--no-generate",
        "--no-save-history",
        "--skip-crash",
        "--skip-consistency",
        "--skip-p2",
        "--skip-load-content",
        "--skip-load-counts",
        "--skip-stress",
        "--diagnostics",
        "--json", json_path,
    ]
    if args.uniform_routing:
        command.append("--uniform-routing")
    if args.base_db:
        command += ["--base-db", args.base_db]
    if args.warmup is not None:
        command += ["--warmup", str(args.warmup)]
    if args.measure is not None:
        command += ["--measure", str(args.measure)]
    rc = run_logged(command, ROOT, env, log_path)
    if not os.path.isfile(json_path):
        raise RuntimeError("%s benchmark did not produce %s (rc=%d)" % (label, json_path, rc))
    with open(json_path) as stream:
        payload = json.load(stream)
    payload["ab_label"] = label
    payload["tested_ref"] = prepared["ref"]
    payload["benchmark_rc"] = rc
    with open(json_path, "w") as stream:
        json.dump(payload, stream, indent=2, ensure_ascii=False)
    return json_path, rc


def cleanup_worktree(path):
    if not path:
        return
    subprocess.run(["git", "worktree", "remove", "--force", path], cwd=ROOT, check=False)


def main():
    parser = argparse.ArgumentParser(description="A/B TPC-C benchmark for two git refs")
    parser.add_argument("--baseline-ref", required=True, help="known-good git ref")
    parser.add_argument("--candidate-ref", default="HEAD", help="candidate git ref (default HEAD)")
    parser.add_argument(
        "--candidate-working-tree", action="store_true",
        help="build and measure the current possibly-uncommitted working tree",
    )
    parser.add_argument("--tier", choices=["quick", "mid", "finals"], default="mid")
    parser.add_argument("--threads", type=int, default=32)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--warmup", type=float, default=None)
    parser.add_argument("--measure", type=float, default=None)
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--output-dir", default=None)
    parser.add_argument("--base-db", default=None,
                        help="pristine preloaded W=50 database (strongly recommended)")
    parser.add_argument("--candidate-first", action="store_true")
    parser.add_argument("--keep-worktrees", action="store_true")
    parser.add_argument("--uniform-routing", action="store_true",
                        help="diagnostic only: disable finals 160-slot hotspot routing")
    parser.add_argument("--require-pass", action="store_true")
    parser.add_argument("--min-tpmc-gain-pct", type=float, default=None)
    parser.add_argument("--max-abort-rate-increase-pp", type=float, default=None)
    parser.add_argument("--max-p99-increase-pct", type=float, default=None)
    args = parser.parse_args()

    shared_data = os.path.join(ROOT, "build", "tpcc_data")
    if not args.base_db and not os.path.isdir(os.path.join(shared_data, "full")):
        print("Full data is missing. Run: python3 tests/local/generate_tpcc_data.py --scale full")
        return 2
    if not os.path.isdir(os.path.join(shared_data, "full")):
        shared_data = None
    if args.base_db:
        args.base_db = os.path.abspath(args.base_db)
        if not os.path.isfile(os.path.join(args.base_db, "db.meta")):
            print("Invalid --base-db (db.meta missing):", args.base_db)
            return 2

    result_dir = args.output_dir or os.path.join(
        ROOT, "build", "ab_results", time.strftime("%Y%m%d_%H%M%S"))
    os.makedirs(result_dir, exist_ok=True)
    temp_root = tempfile.mkdtemp(prefix="rmdb-ab-")
    prepared = {}
    try:
        prepared["baseline"] = prepare_ref(
            args.baseline_ref, "baseline", temp_root, args.jobs, shared_data)
        if args.candidate_working_tree:
            prepared["candidate"] = prepare_current_tree(args.jobs)
        else:
            prepared["candidate"] = prepare_ref(
                args.candidate_ref, "candidate", temp_root, args.jobs, shared_data)

        order = ["candidate", "baseline"] if args.candidate_first else ["baseline", "candidate"]
        results = {}
        return_codes = {}
        for label in order:
            print("\n" + "=" * 78)
            print("A/B RUN:", label, prepared[label]["ref"], prepared[label]["meta"]["rev"][:12])
            print("=" * 78)
            results[label], return_codes[label] = benchmark(prepared[label], label, args, result_dir)

        compare_cmd = [sys.executable, COMPARE, results["baseline"], results["candidate"]]
        if args.require_pass:
            compare_cmd.append("--require-pass")
        if args.min_tpmc_gain_pct is not None:
            compare_cmd += ["--min-tpmc-gain-pct", str(args.min_tpmc_gain_pct)]
        if args.max_abort_rate_increase_pp is not None:
            compare_cmd += ["--max-abort-rate-increase-pp", str(args.max_abort_rate_increase_pp)]
        if args.max_p99_increase_pct is not None:
            compare_cmd += ["--max-p99-increase-pct", str(args.max_p99_increase_pct)]
        compare_rc = subprocess.call(compare_cmd, cwd=ROOT)
        print("\nA/B artifacts:", result_dir)
        if any(return_codes.values()) and args.require_pass:
            return 1
        return compare_rc
    finally:
        if args.keep_worktrees:
            print("Kept worktrees under", temp_root)
        else:
            cleanup_worktree(prepared.get("candidate", {}).get("worktree"))
            cleanup_worktree(prepared.get("baseline", {}).get("worktree"))
            shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
