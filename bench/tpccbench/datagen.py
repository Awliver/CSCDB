"""TPC-C initial population generator (spec clause 4.3.3) -> CSV files.

Deviations from spec (RMDB dialect constraints, see schema.py):
  - c_data char(50) instead of 300..500 chars
  - o_carrier_id NULL -> 0; ol_delivery_d NULL -> 'PENDING'
  - --items/--customers/--orders allow downscaling for quick local runs
    (full spec values are the defaults)
"""

import json
import os
import time

from . import schema
from .tpcrand import TpccRandom, lastname


def _w(f, *cells):
    f.write(",".join(str(c) for c in cells) + "\n")


def generate(out_dir, warehouses=1, seed=42,
             items=schema.ITEMS,
             districts=schema.DISTRICTS_PER_W,
             customers=schema.CUSTOMERS_PER_D,
             orders=schema.ORDERS_PER_D,
             quiet=False):
    """Write all 9 CSVs + manifest.json into out_dir. Returns manifest dict."""
    t0 = time.time()
    os.makedirs(out_dir, exist_ok=True)
    r = TpccRandom(seed)
    now = time.strftime("%Y-%m-%d %H:%M:%S")
    # undelivered tail scales with order count (spec: last 30% ≈ 900/3000)
    first_undelivered = max(1, orders - max(1, orders * 30 // 100) + 1)

    counts = {}

    def log(msg):
        if not quiet:
            print("  " + msg, flush=True)

    # ---- item ------------------------------------------------------------------
    with open(os.path.join(out_dir, "item.csv"), "w") as f:
        f.write(schema.CSV_HEADER["item"] + "\n")
        for i_id in range(1, items + 1):
            _w(f, i_id, r.randint(1, 10000), r.astring(14, 24),
               "%.2f" % r.money(1.00, 100.00), r.data_string(26, 50))
    counts["item"] = items
    log("item: %d rows" % items)

    # ---- per-warehouse tables ---------------------------------------------------
    fw = open(os.path.join(out_dir, "warehouse.csv"), "w")
    fd = open(os.path.join(out_dir, "district.csv"), "w")
    fc = open(os.path.join(out_dir, "customer.csv"), "w")
    fh = open(os.path.join(out_dir, "history.csv"), "w")
    fo = open(os.path.join(out_dir, "orders.csv"), "w")
    fn = open(os.path.join(out_dir, "new_orders.csv"), "w")
    fl = open(os.path.join(out_dir, "order_line.csv"), "w")
    fs = open(os.path.join(out_dir, "stock.csv"), "w")
    for f, tab in ((fw, "warehouse"), (fd, "district"), (fc, "customer"), (fh, "history"),
                   (fo, "orders"), (fn, "new_orders"), (fl, "order_line"), (fs, "stock")):
        f.write(schema.CSV_HEADER[tab] + "\n")

    n_c = n_h = n_o = n_no = n_ol = n_s = 0
    # spec: w_ytd=300000, d_ytd=30000 — these equal 10.00 * customer count so the
    # C8/C9 consistency identities (ytd == sum(h_amount)) hold; keep the identity
    # when downscaling instead of hardcoding the full-scale numbers
    d_ytd0 = 10.0 * customers
    w_ytd0 = d_ytd0 * districts
    for w_id in range(1, warehouses + 1):
        _w(fw, w_id, r.astring(6, 10), r.astring(10, 20), r.astring(10, 20),
           r.astring(10, 20), r.astring(2, 2).upper(), r.zip_code(),
           "%.4f" % r.uniform(0.0, 0.2), "%.2f" % w_ytd0)

        # stock: 100k per warehouse
        for i_id in range(1, items + 1):
            dists = [r.astring(24, 24) for _ in range(10)]
            _w(fs, i_id, w_id, r.randint(10, 100), *dists,
               "0.00", 0, 0, r.data_string(26, 50))
        n_s += items

        for d_id in range(1, districts + 1):
            # district: d_ytd matches sum(h_amount); d_next_o_id = orders+1
            _w(fd, d_id, w_id, r.astring(6, 10), r.astring(10, 20), r.astring(10, 20),
               r.astring(10, 20), r.astring(2, 2).upper(), r.zip_code(),
               "%.4f" % r.uniform(0.0, 0.2), "%.2f" % d_ytd0, orders + 1)

            # customers: first min(1000, customers) get sequential last names,
            # the rest NURand-distributed (spec 4.3.3.1)
            for c_id in range(1, customers + 1):
                if c_id <= 1000:
                    c_last = lastname((c_id - 1) % 1000)
                else:
                    c_last = r.nurand_c_last()
                c_credit = "BC" if r.randint(1, 100) <= 10 else "GC"
                _w(fc, c_id, d_id, w_id, r.astring(8, 16), "OE", c_last,
                   r.astring(10, 20), r.astring(10, 20), r.astring(10, 20),
                   r.astring(2, 2).upper(), r.zip_code(), r.nstring(16, 16), now,
                   c_credit, 50000, "%.4f" % r.uniform(0.0, 0.5),
                   "-10.00", "10.00", 1, 0, r.data_string(30, 50, original_pct=0))
                # history: one row per customer, h_amount = 10.00
                _w(fh, c_id, d_id, w_id, d_id, w_id, now, "10.00", r.astring(12, 24))
            n_c += customers
            n_h += customers

            # orders: o_c_id is a permutation of customer ids (spec)
            perm = r.shuffled(range(1, customers + 1))
            for o_id in range(1, orders + 1):
                o_c_id = perm[(o_id - 1) % customers]
                ol_cnt = r.randint(5, 15)
                delivered = o_id < first_undelivered
                carrier = r.randint(1, 10) if delivered else schema.CARRIER_NULL
                _w(fo, o_id, d_id, w_id, o_c_id, now, carrier, ol_cnt, 1)
                if not delivered:
                    _w(fn, o_id, d_id, w_id)
                    n_no += 1
                for ol_no in range(1, ol_cnt + 1):
                    if delivered:
                        dd, amount = now, "0.00"
                    else:
                        dd, amount = schema.DELIVERY_D_NULL, "%.2f" % r.money(0.01, 9999.99)
                    _w(fl, o_id, d_id, w_id, ol_no, r.randint(1, items), w_id,
                       dd, 5, amount, r.astring(24, 24))
                    n_ol += 1
            n_o += orders
        log("warehouse %d/%d done" % (w_id, warehouses))

    for f in (fw, fd, fc, fh, fo, fn, fl, fs):
        f.close()

    counts.update({
        "warehouse": warehouses,
        "district": warehouses * districts,
        "customer": n_c, "history": n_h, "orders": n_o,
        "new_orders": n_no, "order_line": n_ol, "stock": n_s,
    })
    manifest = {
        "generated_at": now,
        "seed": seed,
        "warehouses": warehouses,
        "districts_per_w": districts,
        "customers_per_d": customers,
        "orders_per_d": orders,
        "items": items,
        "first_undelivered_o_id": first_undelivered,
        "c_last_load": r.c_last,
        "counts": counts,
        "elapsed_sec": round(time.time() - t0, 1),
    }
    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    log("done in %.1fs -> %s" % (manifest["elapsed_sec"], out_dir))
    return manifest


def load_manifest(data_dir):
    path = os.path.join(data_dir, "manifest.json")
    if not os.path.isfile(path):
        return None
    with open(path) as f:
        return json.load(f)
