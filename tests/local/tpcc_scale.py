"""TPC-C data scale profiles (mini = legacy P2 CSV, full = finals W=50)."""

import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(_HERE, "../.."))
BUILD = os.path.abspath(os.environ.get("RMDB_TEST_BUILD", os.path.join(ROOT, "build")))
OFFICIAL = os.path.join(ROOT, "src", "test", "performance_test", "table_data")

# 决赛正式规格：50 仓、每仓 10 district、每 district 3000 客户/订单。
# ``full`` 必须保持该规模；日常小规模调试请显式使用 ``mini``，不要把 W=5
# 伪装成 OJ 预演。
FULL_W = 50
FULL_DISTRICTS = 10
FULL_ITEMS = 100000
FULL_CUSTOMERS_PER_DISTRICT = 3000
FULL_ORDERS_PER_DISTRICT = 3000
FULL_NEW_ORDERS_PER_DISTRICT = 900

TABLE_ORDER = [
    "warehouse", "item", "stock", "district", "customer",
    "history", "orders", "new_orders", "order_line",
]

SCALES = {
    "mini": {
        "label": "OJ Phase-2 mini CSV",
        "warehouses": 1,
        "districts": 3,
        "items": 10,
        "customers_per_district": 10,
        "orders_per_district": 10,
        "min_ol_cnt": 5,
        "max_ol_cnt": 15,
        "loads": [
            ("warehouse", OFFICIAL + "/warehouse.csv"),
            ("item", OFFICIAL + "/item.csv"),
            ("stock", OFFICIAL + "/stock.csv"),
            ("district", OFFICIAL + "/district.csv"),
            ("customer", OFFICIAL + "/customer.csv"),
            ("history", OFFICIAL + "/history.csv"),
            ("orders", OFFICIAL + "/orders.csv"),
            ("new_orders", OFFICIAL + "/new_orders.csv"),
            ("order_line", OFFICIAL + "/order_line.csv"),
        ],
        "expected": {
            "warehouse": 1,
            "item": 10,
            "stock": 10,
            "district": 3,
            "customer": 30,
            "history": 30,
            "orders": 30,
            "new_orders": 18,
            "order_line": 300,
        },
    },
    "full": {
        "label": "OJ perf spec W=%d" % FULL_W,
        "warehouses": FULL_W,
        "districts": FULL_DISTRICTS,
        "items": FULL_ITEMS,
        "customers_per_district": FULL_CUSTOMERS_PER_DISTRICT,
        "orders_per_district": FULL_ORDERS_PER_DISTRICT,
        "min_ol_cnt": 5,
        "max_ol_cnt": 15,
        "data_subdir": "tpcc_data/full",
        "expected": {
            "warehouse": FULL_W,
            "item": FULL_ITEMS,
            "stock": FULL_W * FULL_ITEMS,
            "district": FULL_W * FULL_DISTRICTS,
            "customer": FULL_W * FULL_DISTRICTS * FULL_CUSTOMERS_PER_DISTRICT,
            "history": FULL_W * FULL_DISTRICTS * FULL_CUSTOMERS_PER_DISTRICT,
            "orders": FULL_W * FULL_DISTRICTS * FULL_ORDERS_PER_DISTRICT,
            "new_orders": FULL_W * FULL_DISTRICTS * FULL_NEW_ORDERS_PER_DISTRICT,
            "order_line": 0,  # filled from manifest.json after generation
        },
    },
}


def scale_profile(name):
    if name not in SCALES:
        raise ValueError("unknown scale %r, choose from %s" % (name, list(SCALES)))
    return SCALES[name]


def tpcc_runtime_scale(name):
    p = scale_profile(name)
    return {
        "warehouses": p["warehouses"],
        "districts": p["districts"],
        "customers_per_district": p["customers_per_district"],
        "items": p["items"],
        "min_ol_cnt": p["min_ol_cnt"],
        "max_ol_cnt": p["max_ol_cnt"],
    }


def data_dir_for_scale(name):
    p = scale_profile(name)
    sub = p.get("data_subdir")
    if not sub:
        return None
    return os.path.join(BUILD, sub)


def expected_counts(name):
    p = scale_profile(name)
    exp = dict(p["expected"])
    if name == "full":
        mp = os.path.join(data_dir_for_scale(name) or "", "manifest.json")
        if os.path.isfile(mp):
            with open(mp) as f:
                exp["order_line"] = json.load(f)["order_line"]
    return exp


def loads_for_scale(name):
    p = scale_profile(name)
    exp = expected_counts(name)
    if "loads" in p:
        return [(tab, path, exp[tab]) for tab, path in p["loads"]]
    data_dir = data_dir_for_scale(name)
    return [
        (tab, os.path.join(data_dir, tab + ".csv"), exp[tab])
        for tab in TABLE_ORDER
    ]


def full_data_ready():
    d = data_dir_for_scale("full")
    if not d or not os.path.isdir(d):
        return False
    mp = os.path.join(d, "manifest.json")
    if not os.path.isfile(mp):
        return False
    try:
        with open(mp) as f:
            manifest = json.load(f)
        wh = manifest.get("warehouse", manifest.get("warehouses"))
        if wh != FULL_W:
            return False
    except (OSError, ValueError, TypeError):
        return False
    for tab in TABLE_ORDER:
        if not os.path.isfile(os.path.join(d, tab + ".csv")):
            return False
    return True


def ensure_full_data(generate=True):
    if full_data_ready():
        return True
    if not generate:
        return False
    import subprocess

    script = os.path.join(_HERE, "generate_tpcc_data.py")
    print("Full TPC-C CSV not found; generating (may take several minutes)...")
    subprocess.check_call([sys.executable, script, "--scale", "full"])
    return full_data_ready()
