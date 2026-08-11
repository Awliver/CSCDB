#!/usr/bin/env python3
"""Create one pristine W=50 database template for repeatable A/B runs."""

import argparse
import os
import sys

from tpcc_common import (
    BUILD,
    bootstrap_tpcc,
    exclusive_perf_lock,
    kill_rmdb,
    stop_rmdb,
    verify_load_counts,
)
from tpcc_scale import ensure_full_data, loads_for_scale


def main():
    parser = argparse.ArgumentParser(description="Prepare a reusable full TPC-C database")
    parser.add_argument("--db-name", default="tpcc_base_w50")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--no-generate", action="store_true")
    args = parser.parse_args()

    db_path = os.path.join(BUILD, args.db_name)
    metadata = os.path.join(db_path, "db.meta")
    if os.path.isfile(metadata) and not args.force:
        print("TPC-C base already exists:", db_path)
        return 0
    if not ensure_full_data(generate=not args.no_generate):
        print("Full data missing; run generate_tpcc_data.py --scale full")
        return 2

    os.environ.pop("RMDB_TPCC_BASE_DB", None)
    os.environ.pop("RMDB_TPCC_REUSE_DB", None)
    loads = loads_for_scale("full")
    proc = None
    cli = None
    with exclusive_perf_lock():
        try:
            proc, cli = bootstrap_tpcc(args.db_name, loads=loads, client_timeout=None)
            if not verify_load_counts(cli, loads):
                print("Base load count verification failed")
                return 1
            cli.close()
            cli = None
            stop_rmdb(proc, timeout=180)
            proc = None
            if not os.path.isfile(metadata):
                print("Base database has no db.meta after shutdown:", db_path)
                return 1
            print("TPC-C base ready:", db_path)
            return 0
        finally:
            if cli is not None:
                cli.close()
            stop_rmdb(proc)
            kill_rmdb()


if __name__ == "__main__":
    sys.exit(main())
