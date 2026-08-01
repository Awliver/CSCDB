#!/usr/bin/env python3
"""Fast, non-durable local TPC-C iterations backed by a verified base database.

The normal benchmark deliberately creates and loads a fresh database.  That is
right for an OJ rehearsal, but on an HDD it makes a 20-second experiment spend
most of its time in LOAD and index construction.  This helper separates the
two jobs:

* ``prepare`` creates a clean base using the normal SQL/WAL path, then kills
  and restarts it once before declaring it reusable.
* ``run`` copies that base to a disposable writable work directory and invokes
  ``bench_tpcc.py --reuse-db`` against the copy.

Putting ``--work-root`` on a writable tmpfs makes the iterative path fast.
That mode is intentionally tagged ``fast-clone-copy`` in benchmark history;
it is not a durability test and ``bench_tpcc --strict`` rejects it.

Examples:
  python3 tests/local/tpcc_fast.py prepare --scale local
  python3 tests/local/tpcc_fast.py run --scale local -- \
      --finals --warmup 5 --measure 20 --rounds 1 --threads 32
  python3 tests/local/tpcc_fast.py run --scale local --work-root /mnt/nvme/rmdb-fast -- \
      --mid --warmup 10 --measure 45
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../.."))
BUILD = os.path.join(ROOT, "build")
BENCH = os.path.join(HERE, "bench_tpcc.py")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

from tpcc_common import (  # noqa: E402
    RmdbClient,
    bootstrap_tpcc,
    kill_rmdb,
    start_existing_rmdb,
    verify_load_counts,
)
from tpcc_scale import (  # noqa: E402
    data_dir_for_scale,
    ensure_scale_data,
    loads_for_scale,
    scale_profile,
)


def default_base(scale):
    return os.path.join(BUILD, "tpcc_fast_base_" + scale)


def metadata_path(base):
    return base + ".fast.json"


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _tree_size(path):
    total = 0
    for root, _, files in os.walk(path):
        for name in files:
            try:
                total += os.path.getsize(os.path.join(root, name))
            except OSError:
                pass
    return total


def _format_bytes(n):
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if n < 1024 or unit == "TiB":
            return "%.1f %s" % (n, unit)
        n /= 1024.0


def _base_path(value, scale):
    base = os.path.abspath(value or default_base(scale))
    # Base creation LOAD paths are relative to build/<db>, so keep it a direct
    # child of build.  Disposable clones may live anywhere writable.
    if os.path.dirname(base) != BUILD or not os.path.basename(base).startswith("tpcc_fast_base_"):
        raise ValueError("--base must be a direct build/tpcc_fast_base_* directory")
    return base


def _read_metadata(base):
    path = metadata_path(base)
    if not os.path.isfile(path):
        raise RuntimeError("base metadata missing: run tpcc_fast.py prepare first")
    with open(path) as f:
        return json.load(f)


def _assert_metadata(base, scale):
    meta = _read_metadata(base)
    if meta.get("scale") != scale:
        raise RuntimeError("base scale=%r, requested scale=%r" % (meta.get("scale"), scale))
    if not os.path.isdir(base):
        raise RuntimeError("base database directory missing: " + base)
    manifest = os.path.join(data_dir_for_scale(scale) or "", "manifest.json")
    if manifest and os.path.isfile(manifest):
        current = _sha256(manifest)
        if meta.get("csv_manifest_sha256") != current:
            raise RuntimeError("CSV manifest changed since base creation; rebuild the base with --force")
    return meta


def prepare(args):
    if args.scale not in ("local", "full"):
        raise RuntimeError("fast bases require --scale local or --scale full")
    base = _base_path(args.base, args.scale)
    meta_path = metadata_path(base)
    if os.path.exists(base) or os.path.exists(meta_path):
        if not args.force:
            _assert_metadata(base, args.scale)
            print("Base already verified:", base)
            return 0
        if os.path.isdir(base):
            shutil.rmtree(base)
        elif os.path.exists(base):
            raise RuntimeError("base target is not a directory: " + base)
        if os.path.exists(meta_path):
            os.unlink(meta_path)

    if not ensure_scale_data(args.scale, generate=not args.no_generate):
        raise RuntimeError(
            "CSV missing; run tests/local/generate_tpcc_data.py --scale %s" % args.scale
        )

    loads = loads_for_scale(args.scale)
    proc = None
    cli = None
    success = False
    try:
        print("== Build fast base ==")
        print("  scale:", scale_profile(args.scale)["label"])
        print("  base: ", base)
        proc, cli = bootstrap_tpcc(base, loads=loads, client_timeout=None)
        if not verify_load_counts(cli, loads):
            raise RuntimeError("base row-count validation failed")
        cli.close()
        cli = None

        # Deliberately use a SIGKILL/restart here.  A reusable base is only
        # accepted after its normal WAL recovery path can reconstruct it.
        kill_rmdb()
        proc = None
        proc, _ = start_existing_rmdb(base)
        cli = RmdbClient(timeout=None)
        if not verify_load_counts(cli, loads):
            raise RuntimeError("base recovery validation failed")
        cli.close()
        cli = None
        kill_rmdb()
        proc = None

        manifest = os.path.join(data_dir_for_scale(args.scale), "manifest.json")
        metadata = {
            "format": 1,
            "created_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "scale": args.scale,
            "csv_manifest_sha256": _sha256(manifest) if os.path.isfile(manifest) else None,
            "base_bytes": _tree_size(base),
            "expected_counts": {tab: expected for tab, _, expected in loads},
            "recovery_checked": True,
        }
        with open(meta_path, "w") as f:
            json.dump(metadata, f, indent=2, ensure_ascii=False)
        print("Base ready:", base, "(%s)" % _format_bytes(metadata["base_bytes"]))
        success = True
        return 0
    finally:
        if cli is not None:
            cli.close()
        if proc is not None and proc.poll() is None:
            proc.kill()
        kill_rmdb()
        if not success and os.path.isdir(base):
            shutil.rmtree(base)
            if os.path.exists(meta_path):
                os.unlink(meta_path)
        if not success and os.path.isfile(base + ".server.log"):
            os.unlink(base + ".server.log")


def _default_work_root():
    candidate = "/dev/shm/rmdb-fast"
    if os.path.isdir("/dev/shm") and os.access("/dev/shm", os.W_OK | os.X_OK):
        return candidate
    return "/tmp/rmdb-fast"


def _safe_work_root(value):
    root = os.path.abspath(value or os.environ.get("RMDB_FAST_WORK_ROOT") or _default_work_root())
    os.makedirs(root, exist_ok=True)
    if not os.path.isdir(root) or not os.access(root, os.W_OK | os.X_OK):
        raise RuntimeError("work root is not writable: " + root)
    return root


def _validate_forwarded_args(values):
    protected = ("--scale", "--db-name", "--db-path", "--reuse-db", "--strict", "--no-generate")
    for value in values:
        if any(value == opt or value.startswith(opt + "=") for opt in protected):
            raise RuntimeError("fast runner owns %s; remove it from arguments after --" % value)


def run(args):
    base = _base_path(args.base, args.scale)
    meta = _assert_metadata(base, args.scale)
    forwarded = list(args.bench_args)
    if forwarded and forwarded[0] == "--":
        forwarded.pop(0)
    _validate_forwarded_args(forwarded)

    work_root = _safe_work_root(args.work_root)
    required = int(meta.get("base_bytes", _tree_size(base)) * 2 + 512 * 1024 * 1024)
    free = shutil.disk_usage(work_root).free
    if free < required:
        raise RuntimeError(
            "work root has %s free; need at least %s for base + WAL headroom"
            % (_format_bytes(free), _format_bytes(required))
        )

    run_dir = tempfile.mkdtemp(prefix="tpcc_fast_", dir=work_root)
    shutil.rmtree(run_dir)
    keep = args.keep_run
    try:
        t0 = time.monotonic()
        print("== Clone fast base ==")
        print("  base:     ", base)
        print("  work root:", work_root)
        shutil.copytree(base, run_dir, copy_function=shutil.copy2)
        print("  clone:    ", run_dir, "(%.1fs)" % (time.monotonic() - t0))

        cmd = [
            sys.executable,
            BENCH,
            "--scale", args.scale,
            "--db-path", run_dir,
            "--reuse-db",
            "--no-generate",
        ] + forwarded
        env = dict(os.environ)
        env["RMDB_TEST_STORAGE_MODE"] = "fast-clone-copy"
        env["RMDB_TEST_BASE_ID"] = os.path.basename(base)
        print(">>>", " ".join(cmd), flush=True)
        return subprocess.call(cmd, cwd=ROOT, env=env)
    finally:
        if keep:
            print("Kept fast clone for debugging:", run_dir)
        elif os.path.isdir(run_dir):
            shutil.rmtree(run_dir)


def main():
    ap = argparse.ArgumentParser(description="Reusable-base fast TPC-C test helper")
    sub = ap.add_subparsers(dest="command", required=True)

    p_prepare = sub.add_parser("prepare", help="build and recovery-validate a reusable base")
    p_prepare.add_argument("--scale", choices=["local", "full"], default="local")
    p_prepare.add_argument("--base", default=None)
    p_prepare.add_argument("--force", action="store_true", help="replace only this tool's base directory")
    p_prepare.add_argument("--no-generate", action="store_true")
    p_prepare.set_defaults(func=prepare)

    p_run = sub.add_parser("run", help="clone a base, run bench_tpcc, then remove the clone")
    p_run.add_argument("--scale", choices=["local", "full"], default="local")
    p_run.add_argument("--base", default=None)
    p_run.add_argument("--work-root", default=None,
                       help="writable tmpfs/NVMe directory; defaults to /dev/shm when writable")
    p_run.add_argument("--keep-run", action="store_true", help="keep clone after the benchmark for debugging")
    p_run.add_argument("bench_args", nargs=argparse.REMAINDER,
                       help="arguments passed to bench_tpcc.py; put them after --")
    p_run.set_defaults(func=run)

    args = ap.parse_args()
    try:
        return args.func(args)
    except (OSError, RuntimeError, ValueError) as exc:
        print("ERROR:", exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
