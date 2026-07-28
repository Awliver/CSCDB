"""tpccbench CLI.

  gen    generate spec-compliant CSV data
  load   fresh server + schema + load + indexes + row-count verify
  smoke  transaction correctness: exact side effects of each txn
  check  consistency conditions C1..C12
  acid   atomicity / isolation / durability tests
  run    multi-client benchmark (tpmC + latency percentiles)
  full   load -> smoke -> check -> run -> check

Typical:
  python3 tests/bench/tpccbench.py gen --scale small
  python3 tests/bench/tpccbench.py full --scale small --duration 60 --clients 8
"""

import argparse
import json
import os
import shutil
import sys
import time

from . import datagen, schema
from .acid import run_acid, run_durability
from .checks import run_checks
from .db import BUILD, Client, Server
from .runner import bench
from .verify import run_smoke
from .workload import MIX_PRESETS, Config

# scale presets: (items, districts/W, customers/D, orders/D)
SCALES = {
    "full":  (schema.ITEMS, 10, 3000, 3000),   # spec-exact, W from --warehouses
    "small": (10000, 10, 300, 300),            # ~1/10, quick but realistic shape
    "mini":  (1000, 3, 50, 50),                # smoke-test size
}


def data_dir(args):
    if args.data:
        return args.data
    return os.path.join(BUILD, "tpccbench_data",
                        "%s_w%d_seed%d" % (args.scale, args.warehouses, args.seed))


def cfg_from_manifest(m):
    return Config(warehouses=m["warehouses"], items=m["items"],
                  districts=m["districts_per_w"], customers=m["customers_per_d"])


def manifest_of_db(db_name):
    path = os.path.join(BUILD, db_name, "tpccbench_manifest.json")
    if not os.path.isfile(path):
        sys.exit("no tpccbench_manifest.json in db dir %s — run `load` first" % db_name)
    with open(path) as f:
        return json.load(f)


def _connect(isolation="si", timeout=600):
    cli = Client(timeout=timeout)
    if isolation == "si":
        cli.query("set transaction isolation level snapshot isolation")
    return cli


# ---- subcommands ------------------------------------------------------------------

def cmd_gen(args):
    d = data_dir(args)
    items, districts, customers, orders = SCALES[args.scale]
    print("generating %s data (W=%d) -> %s" % (args.scale, args.warehouses, d))
    datagen.generate(d, warehouses=args.warehouses, seed=args.seed, items=items,
                     districts=districts, customers=customers, orders=orders)
    return 0


def cmd_load(args, keep_server=False):
    d = data_dir(args)
    m = datagen.load_manifest(d)
    if m is None:
        print("data missing, generating first...")
        cmd_gen(args)
        m = datagen.load_manifest(d)

    print("loading %s into db '%s'..." % (d, args.db))
    server = Server(args.db, fresh=True).start()
    cli = Client(timeout=None)      # index build on big data can take minutes
    t0 = time.time()
    for _, ddl in schema.SCHEMA:
        cli.must(ddl)
    for tab in schema.LOAD_ORDER:
        path = os.path.join(d, tab + ".csv")
        cli.must("load %s into %s;" % (path, tab))
        print("  loaded %s" % tab, flush=True)
    for ddl in schema.INDEXES:
        cli.must(ddl)
    print("  indexes built (%.1fs total)" % (time.time() - t0))

    ok = True
    for tab, expect in m["counts"].items():
        _, rows = cli.rows("select count(*) from %s;" % tab)
        got = int(float(rows[0][0])) if rows else -1
        status = "PASS" if got == expect else "FAIL"
        ok = ok and got == expect
        print("  rowcount %-11s %d/%d %s" % (tab, got, expect, status))
    cli.close()

    shutil.copy(os.path.join(d, "manifest.json"),
                os.path.join(BUILD, args.db, "tpccbench_manifest.json"))
    if not ok:
        server.stop()
        sys.exit("LOAD: FAIL (row counts)")
    print("LOAD: PASS")
    if keep_server:
        return server
    server.stop()
    return 0


def _ensure_server(args):
    """Start server on an existing loaded db (no wipe)."""
    return Server(args.db, fresh=False).start()


def cmd_smoke(args):
    m = manifest_of_db(args.db)
    server = _ensure_server(args)
    try:
        cli = _connect(args.isolation)
        print("smoke: exact-effect checks per transaction")
        rep = run_smoke(cli, cfg_from_manifest(m), seed=args.seed)
        cli.close()
        print("SMOKE: %s" % ("PASS" if rep.ok else "FAIL"))
        return 0 if rep.ok else 1
    finally:
        server.stop()


def _checks_with_retry(cli, m, samples, deep=False):
    """跑一致性检查；失败则等待在途事务结清后重试一次（worker join 超时可能残留
    活跃事务，与检查并发造成瞬态计数偏差——真实损坏重试后仍 FAIL）。"""
    rep = run_checks(cli, m["warehouses"], m["districts_per_w"],
                     samples=samples, deep=deep)
    if not rep.ok:
        print("  (存在 FAIL，3s 后重试一次以排除在途事务瞬态)")
        time.sleep(3)
        rep = run_checks(cli, m["warehouses"], m["districts_per_w"],
                         samples=samples, deep=deep)
    return rep


def cmd_check(args):
    m = manifest_of_db(args.db)
    server = _ensure_server(args)
    try:
        cli = _connect(args.isolation)
        print("consistency conditions C1..C12 (samples=%d%s)"
              % (args.samples, ", deep" if args.deep else ""))
        rep = _checks_with_retry(cli, m, args.samples, deep=args.deep)
        cli.close()
        n_fail = sum(1 for _, ok, _ in rep.results if not ok)
        print("CHECK: %s (%d conditions, %d failed)"
              % ("PASS" if rep.ok else "FAIL", len(rep.results), n_fail))
        return 0 if rep.ok else 1
    finally:
        server.stop()


def cmd_acid(args):
    manifest_of_db(args.db)
    server = _ensure_server(args)
    try:
        print("ACID: atomicity + isolation (%s)" % args.isolation)
        rep1 = run_acid(isolation=args.isolation)
        print("ACID: durability (kill -9 + recovery)")
        rep2 = run_durability(server, isolation=args.isolation)
        ok = rep1.ok and rep2.ok
        print("ACID: %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    finally:
        server.stop()


def _parse_mix(s):
    if s in MIX_PRESETS:
        return MIX_PRESETS[s]
    parts = tuple(int(x) for x in s.split(","))
    if len(parts) != 5 or sum(parts) <= 0:
        sys.exit("--mix must be 'tpcc', 'oj' or 5 comma-separated weights")
    return parts


def cmd_run(args):
    m = manifest_of_db(args.db)
    server = _ensure_server(args)
    try:
        result = bench(cfg_from_manifest(m), duration=args.duration, warmup=args.warmup,
                       clients=args.clients, mix=_parse_mix(args.mix),
                       isolation=args.isolation, seed=args.seed,
                       c_last_load=m.get("c_last_load"), progress=args.progress,
                       server_alive=server.alive, timeout=args.timeout,
                       think_scale=args.think)
        if args.json:
            with open(args.json, "w") as f:
                json.dump(result, f, indent=2)
            print("  json -> %s" % args.json)
        return 1 if result["fatal"] else 0
    finally:
        server.stop()


def cmd_full(args):
    server = cmd_load(args, keep_server=True)
    m = manifest_of_db(args.db)
    cfg = cfg_from_manifest(m)
    rc = 0
    try:
        cli = _connect(args.isolation)
        print("\n== smoke ==")
        rep = run_smoke(cli, cfg, seed=args.seed)
        rc |= 0 if rep.ok else 1
        print("\n== consistency (pre-benchmark) ==")
        repc = run_checks(cli, m["warehouses"], m["districts_per_w"], samples=args.samples)
        rc |= 0 if repc.ok else 1
        cli.close()

        print("\n== benchmark ==")
        result = bench(cfg, duration=args.duration, warmup=args.warmup,
                       clients=args.clients, mix=_parse_mix(args.mix),
                       isolation=args.isolation, seed=args.seed,
                       c_last_load=m.get("c_last_load"), progress=args.progress,
                       server_alive=server.alive, timeout=args.timeout,
                       think_scale=args.think)
        rc |= 1 if result["fatal"] else 0

        print("\n== consistency (post-benchmark) ==")
        cli = _connect(args.isolation)
        repc = _checks_with_retry(cli, m, args.samples)
        rc |= 0 if repc.ok else 1
        cli.close()
        if args.json:
            with open(args.json, "w") as f:
                json.dump(result, f, indent=2)
        print("\nFULL: %s" % ("PASS" if rc == 0 else "FAIL"))
        return rc
    finally:
        server.stop()


# ---- argparse ------------------------------------------------------------------------

def main(argv=None):
    p = argparse.ArgumentParser(prog="tpccbench", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp, db=True):
        sp.add_argument("--scale", choices=SCALES, default="small")
        sp.add_argument("--warehouses", "-W", type=int, default=1)
        sp.add_argument("--seed", type=int, default=42)
        sp.add_argument("--data", help="data dir (default: build/tpccbench_data/<auto>)")
        if db:
            sp.add_argument("--db", default="tpccbench_db")
            sp.add_argument("--isolation", choices=["si", "default"], default="si")

    sp = sub.add_parser("gen", help="generate CSV data")
    common(sp, db=False)

    sp = sub.add_parser("load", help="fresh load into a db")
    common(sp)

    sp = sub.add_parser("smoke", help="transaction exact-effect tests")
    common(sp)

    sp = sub.add_parser("check", help="consistency conditions C1..C12")
    common(sp)
    sp.add_argument("--samples", type=int, default=8)
    sp.add_argument("--deep", action="store_true", help="also C10/C12 (per-customer scans)")

    sp = sub.add_parser("acid", help="ACID tests (includes kill -9 durability)")
    common(sp)

    for name in ("run", "full"):
        sp = sub.add_parser(name, help="benchmark" if name == "run" else "load+verify+bench+verify")
        common(sp)
        sp.add_argument("--duration", type=int, default=60)
        sp.add_argument("--warmup", type=int, default=10)
        sp.add_argument("--clients", "-c", type=int, default=8)
        sp.add_argument("--mix", default="tpcc", help="tpcc | oj | 5 weights (e.g. 45,43,4,4,4)")
        sp.add_argument("--progress", type=int, default=5)
        sp.add_argument("--timeout", type=int, default=60, help="per-statement client timeout (s)")
        sp.add_argument("--json", help="write result json")
        sp.add_argument("--samples", type=int, default=8)
        # TPC-C 规范 5.2.5.4 keying+think 时间倍率。默认 1.0 = 完整规范限流（tpmC 落到真实
        # 数十量级，与 OJ 同数量级）；0 = 极限吞吐模式（不限流，原 pgbench 式高吞吐）。
        sp.add_argument("--think", type=float, default=1.0,
                        help="TPC-C keying+think time scale (1.0=spec, 0=max-throughput)")

    args = p.parse_args(argv)
    return {"gen": cmd_gen, "load": cmd_load, "smoke": cmd_smoke, "check": cmd_check,
            "acid": cmd_acid, "run": cmd_run, "full": cmd_full}[args.cmd](args)
