#!/usr/bin/env python3
"""Generate TPC-C CSV fixtures for local fast tests and W=50 OJ rehearsal."""

import argparse
import csv
import hashlib
import json
import os
import random
import string
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(_HERE, "../.."))
BUILD = os.path.join(ROOT, "build")

from tpcc_scale import TABLE_ORDER, data_dir_for_scale, scale_profile  # noqa: E402
from tpcc_load_verify import TABLE_PK  # noqa: E402


def _rand_str(n, rng):
    return "".join(rng.choice(string.ascii_letters + string.digits) for _ in range(n))


def _write_csv(path, header, rows_iter, progress_every=0, label=""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    n = 0
    with open(path, "w", newline="") as f:
        f.write(header + "\n")
        for row in rows_iter:
            f.write(",".join(str(x) for x in row) + "\n")
            n += 1
            if progress_every and n % progress_every == 0:
                print("  %s: %d rows" % (label, n))
    return n


def _sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _sample_csv_rows(path, n=3):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if len(rows) <= n * 2:
        return rows
    mid = len(rows) // 2
    return rows[:n] + rows[mid: mid + n] + rows[-n:]


def _build_manifest_extras(out_dir):
    checksums = {}
    anchors = []
    for tab in TABLE_ORDER:
        path = os.path.join(out_dir, tab + ".csv")
        checksums[tab] = _sha256_file(path)
        pk = TABLE_PK[tab]
        for row in _sample_csv_rows(path, n=3):
            anchors.append({
                "table": tab,
                "pk": {c: row[c] for c in pk},
                "fields": {c: row[c] for c in row if c not in pk},
            })
    return checksums, anchors


def generate_scale(scale="full", seed=42):
    t0 = time.time()
    rng = random.Random(seed)
    profile = scale_profile(scale)
    warehouses = profile["warehouses"]
    districts = profile["districts"]
    items = profile["items"]
    customers_per_district = profile["customers_per_district"]
    orders_per_district = profile["orders_per_district"]
    new_orders_per_district = profile["new_orders_per_district"]
    out_dir = data_dir_for_scale(scale)
    if not out_dir:
        raise RuntimeError("scale=%s data dir not configured" % scale)
    os.makedirs(out_dir, exist_ok=True)
    entry_d = "2023-07-22 20:50:31"

    print("Generating warehouse (%d rows)..." % warehouses)
    _write_csv(
        os.path.join(out_dir, "warehouse.csv"),
        "w_id,w_name,w_street_1,w_street_2,w_city,w_state,w_zip,w_tax,w_ytd",
        (
            (w_id, _rand_str(10, rng), _rand_str(20, rng), _rand_str(20, rng),
             _rand_str(20, rng), "JY", _rand_str(9, rng), 0.125, 300000.0)
            for w_id in range(1, warehouses + 1)
        ),
    )

    print("Generating item (%d rows)..." % items)
    def item_rows():
        for i_id in range(1, items + 1):
            yield (i_id, rng.randint(1, 10000), _rand_str(24, rng),
                   round(rng.uniform(1.0, 100.0), 2), _rand_str(50, rng))
    _write_csv(
        os.path.join(out_dir, "item.csv"),
        "i_id,i_im_id,i_name,i_price,i_data",
        item_rows(), progress_every=20000, label="item",
    )

    print("Generating stock (%d rows)..." % (warehouses * items))
    def stock_rows():
        for w_id in range(1, warehouses + 1):
            for s_i_id in range(1, items + 1):
                dists = [_rand_str(24, rng) for _ in range(10)]
                yield (
                    s_i_id, w_id, rng.randint(10, 100),
                    *dists, 0.0, 0, 0, _rand_str(50, rng),
                )
    _write_csv(
        os.path.join(out_dir, "stock.csv"),
        "s_i_id,s_w_id,s_quantity,s_dist_01,s_dist_02,s_dist_03,s_dist_04,s_dist_05,"
        "s_dist_06,s_dist_07,s_dist_08,s_dist_09,s_dist_10,s_ytd,s_order_cnt,s_remote_cnt,s_data",
        stock_rows(), progress_every=20000, label="stock",
    )

    print("Generating district (%d rows)..." % (warehouses * districts))
    def district_rows():
        for w_id in range(1, warehouses + 1):
            for d_id in range(1, districts + 1):
                yield (
                    d_id, w_id, _rand_str(10, rng), _rand_str(20, rng), _rand_str(20, rng),
                    _rand_str(20, rng), "JY", _rand_str(9, rng), 0.125, 30000.0,
                    orders_per_district + 1,
                )
    _write_csv(
        os.path.join(out_dir, "district.csv"),
        "d_id,d_w_id,d_name,d_street_1,d_street_2,d_city,d_state,d_zip,d_tax,d_ytd,d_next_o_id",
        district_rows(),
    )

    print("Generating customer (%d rows)..." % (warehouses * districts * customers_per_district))
    def customer_rows():
        for w_id in range(1, warehouses + 1):
            for d_id in range(1, districts + 1):
                for c_id in range(1, customers_per_district + 1):
                    yield (
                        c_id, d_id, w_id, _rand_str(16, rng), "OE", "BARRBARRBARR",
                        _rand_str(20, rng), _rand_str(20, rng), _rand_str(20, rng),
                        "JY", _rand_str(9, rng), _rand_str(16, rng), entry_d,
                        "GC", 50000, round(rng.uniform(0.0, 0.5), 3),
                        round(rng.uniform(-10.0, 10.0), 2), 10.5, 1, 0, _rand_str(50, rng),
                    )
    _write_csv(
        os.path.join(out_dir, "customer.csv"),
        "c_id,c_d_id,c_w_id,c_first,c_middle,c_last,c_street_1,c_street_2,c_city,c_state,"
        "c_zip,c_phone,c_since,c_credit,c_credit_lim,c_discount,c_balance,c_ytd_payment,"
        "c_payment_cnt,c_delivery_cnt,c_data",
        customer_rows(), progress_every=10000, label="customer",
    )

    print("Generating history...")
    def history_rows():
        for w_id in range(1, warehouses + 1):
            for d_id in range(1, districts + 1):
                for c_id in range(1, customers_per_district + 1):
                    yield (
                        c_id, d_id, w_id, d_id, w_id, entry_d,
                        round(rng.uniform(10.0, 5000.0), 2), _rand_str(24, rng),
                    )
    _write_csv(
        os.path.join(out_dir, "history.csv"),
        "h_c_id,h_c_d_id,h_c_w_id,h_d_id,h_w_id,h_date,h_amount,h_data",
        history_rows(), progress_every=10000, label="history",
    )

    print("Generating orders + order_line...")
    ol_count = 0
    orders_path = os.path.join(out_dir, "orders.csv")
    ol_path = os.path.join(out_dir, "order_line.csv")
    with open(orders_path, "w", newline="") as fo, open(ol_path, "w", newline="") as fl:
        fo.write("o_id,o_d_id,o_w_id,o_c_id,o_entry_d,o_carrier_id,o_ol_cnt,o_all_local\n")
        fl.write(
            "ol_o_id,ol_d_id,ol_w_id,ol_number,ol_i_id,ol_supply_w_id,"
            "ol_delivery_d,ol_quantity,ol_amount,ol_dist_info\n"
        )
        for w_id in range(1, warehouses + 1):
            for d_id in range(1, districts + 1):
                for o_id in range(1, orders_per_district + 1):
                    c_id = rng.randint(1, customers_per_district)
                    ol_cnt = rng.randint(5, 15)
                    fo.write(
                        "%d,%d,%d,%d,'%s',%d,%d,1\n"
                        % (o_id, d_id, w_id, c_id, entry_d, rng.randint(1, 10), ol_cnt)
                    )
                    for ol_no in range(1, ol_cnt + 1):
                        i_id = rng.randint(1, items)
                        qty = rng.randint(1, 10)
                        amount = round(rng.uniform(1.0, 100.0) * qty, 2)
                        fl.write(
                            "%d,%d,%d,%d,%d,%d,'%s',%d,%.2f,'%s'\n"
                            % (o_id, d_id, w_id, ol_no, i_id, w_id, entry_d, qty, amount, _rand_str(24, rng))
                        )
                        ol_count += 1
                if d_id % 2 == 0:
                    print("  orders/order_line w=%d districts done: %d/%d, ol_rows=%d"
                          % (w_id, d_id, districts, ol_count))

    print("Generating new_orders...")
    def new_orders_rows():
        for w_id in range(1, warehouses + 1):
            for d_id in range(1, districts + 1):
                start = orders_per_district - new_orders_per_district + 1
                for o_id in range(start, orders_per_district + 1):
                    yield (o_id, d_id, w_id)
    _write_csv(
        os.path.join(out_dir, "new_orders.csv"),
        "no_o_id,no_d_id,no_w_id",
        new_orders_rows(),
    )

    csv_sha256, anchors = _build_manifest_extras(out_dir)
    manifest = {
        "scale": scale,
        "seed": seed,
        "warehouse": warehouses,
        "item": items,
        "stock": warehouses * items,
        "district": warehouses * districts,
        "customer": warehouses * districts * customers_per_district,
        "history": warehouses * districts * customers_per_district,
        "orders": warehouses * districts * orders_per_district,
        "new_orders": warehouses * districts * new_orders_per_district,
        "order_line": ol_count,
        "csv_sha256": csv_sha256,
        "anchors": anchors,
        "elapsed_sec": round(time.time() - t0, 1),
    }
    with open(os.path.join(out_dir, "manifest.json"), "w") as mf:
        json.dump(manifest, mf, indent=2)
    print("Done. manifest:", manifest)
    return manifest


def generate_full(seed=42):
    """Compatibility wrapper for existing callers."""
    return generate_scale("full", seed=seed)


def main():
    ap = argparse.ArgumentParser(description="Generate TPC-C CSV for local perf test")
    ap.add_argument("--scale", choices=["local", "full"], default="full")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--refresh-manifest", action="store_true",
                    help="recompute csv_sha256+anchors for the selected existing CSV (no regen)")
    args = ap.parse_args()
    if args.refresh_manifest:
        out_dir = data_dir_for_scale(args.scale)
        if not out_dir or not os.path.isdir(out_dir):
            print("TPC-C data dir missing for scale=%s" % args.scale)
            return 1
        csv_sha256, anchors = _build_manifest_extras(out_dir)
        mp = os.path.join(out_dir, "manifest.json")
        manifest = {}
        if os.path.isfile(mp):
            with open(mp) as f:
                manifest = json.load(f)
        manifest["csv_sha256"] = csv_sha256
        manifest["anchors"] = anchors
        with open(mp, "w") as f:
            json.dump(manifest, f, indent=2)
        print("Refreshed manifest:", mp, "anchors=%d" % len(anchors))
        return 0
    generate_scale(args.scale, seed=args.seed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
