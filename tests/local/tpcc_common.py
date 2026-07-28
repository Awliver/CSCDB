"""Shared TPC-C helpers for local OJ-style tests (schema / load / indexes).

默认使用决赛 Wire Protocol v3（见 wire_client.py）；与评测测活/功能路径一致。
"""

import os
import shutil
import subprocess
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from wire_client import (  # noqa: E402
    HOST,
    PORT,
    WireClient,
    wait_ready,
)

ROOT = os.path.abspath(os.path.join(_HERE, "../.."))
BUILD = os.path.join(ROOT, "build")
RMDB = os.path.join(BUILD, "bin/rmdb")

SCHEMA = [
    "create table warehouse (w_id int, w_name char(10), w_street_1 char(20), w_street_2 char(20), w_city char(20), w_state char(2), w_zip char(9), w_tax float, w_ytd float);",
    "create table district (d_id int, d_w_id int, d_name char(10), d_street_1 char(20), d_street_2 char(20), d_city char(20), d_state char(2), d_zip char(9), d_tax float, d_ytd float, d_next_o_id int);",
    "create table customer (c_id int, c_d_id int, c_w_id int, c_first char(16), c_middle char(2), c_last char(16), c_street_1 char(20), c_street_2 char(20), c_city char(20), c_state char(2), c_zip char(9), c_phone char(16), c_since char(30), c_credit char(2), c_credit_lim int, c_discount float, c_balance float, c_ytd_payment float, c_payment_cnt int, c_delivery_cnt int, c_data char(50));",
    "create table history (h_c_id int, h_c_d_id int, h_c_w_id int, h_d_id int, h_w_id int, h_date char(19), h_amount float, h_data char(24));",
    "create table new_orders (no_o_id int, no_d_id int, no_w_id int);",
    "create table orders (o_id int, o_d_id int, o_w_id int, o_c_id int, o_entry_d char(19), o_carrier_id int, o_ol_cnt int, o_all_local int);",
    "create table order_line (ol_o_id int, ol_d_id int, ol_w_id int, ol_number int, ol_i_id int, ol_supply_w_id int, ol_delivery_d char(30), ol_quantity int, ol_amount float, ol_dist_info char(24));",
    "create table item (i_id int, i_im_id int, i_name char(24), i_price float, i_data char(50));",
    "create table stock (s_i_id int, s_w_id int, s_quantity int, s_dist_01 char(24), s_dist_02 char(24), s_dist_03 char(24), s_dist_04 char(24), s_dist_05 char(24), s_dist_06 char(24), s_dist_07 char(24), s_dist_08 char(24), s_dist_09 char(24), s_dist_10 char(24), s_ytd float, s_order_cnt int, s_remote_cnt int, s_data char(50));",
]

INDEXES = [
    "create index warehouse (w_id);",
    "create index district (d_w_id, d_id);",
    "create index customer (c_w_id, c_d_id, c_id);",
    # 决赛 Load Data 规定的姓氏查找访问路径（Payment）。
    "create index customer (c_w_id, c_d_id, c_last, c_id);",
    "create index item (i_id);",
    "create index stock (s_w_id, s_i_id);",
    "create index orders (o_w_id, o_d_id, o_id);",
    # 决赛 Load Data 规定的客户最近订单访问路径（OrderStatus）。
    "create index orders (o_w_id, o_d_id, o_c_id, o_id);",
    "create index new_orders (no_w_id, no_d_id, no_o_id);",
    "create index order_line (ol_w_id, ol_d_id, ol_o_id, ol_number);",
]

# Default mini CSV (OJ Phase-2) load expectations
LOADS = [
    ("warehouse", "../../src/test/performance_test/table_data/warehouse.csv", 1),
    ("item", "../../src/test/performance_test/table_data/item.csv", 10),
    ("stock", "../../src/test/performance_test/table_data/stock.csv", 10),
    ("district", "../../src/test/performance_test/table_data/district.csv", 3),
    ("customer", "../../src/test/performance_test/table_data/customer.csv", 30),
    ("history", "../../src/test/performance_test/table_data/history.csv", 30),
    ("orders", "../../src/test/performance_test/table_data/orders.csv", 30),
    ("new_orders", "../../src/test/performance_test/table_data/new_orders.csv", 18),
    ("order_line", "../../src/test/performance_test/table_data/order_line.csv", 300),
]

TPCC_SCALE = {
    "warehouses": 5,
    "districts": 3,
    "customers_per_district": 10,
    "items": 10,
    "min_ol_cnt": 5,
    "max_ol_cnt": 15,
}

W_ID = 1
ENTRY_D = "2023-07-22 20:50:31"


def benchmark_client_timeout(measure_sec, explicit=None):
    """Socket read timeout for TPC-C workers: must exceed slowest single txn."""
    if explicit is not None:
        return explicit if explicit > 0 else None
    # Default: 2× measure window, at least 10 min (covers OJ 360s rounds on slow DB)
    return max(int(measure_sec * 2), 600)


class RmdbClient(WireClient):
    """Wire v3 客户端；保留 RmdbClient 名称以兼容既有脚本。"""

    def __init__(self, host=HOST, port=PORT, timeout=120):
        super().__init__(host=host, port=port, timeout=timeout)


def parse_count(resp):
    rows = parse_table_rows(resp)
    if rows:
        for cell in rows[0]:
            cell = cell.replace(",", "")
            if cell.isdigit() or (cell.replace(".", "", 1).isdigit() and cell.count(".") <= 1):
                return int(float(cell))
    for line in resp.splitlines():
        line = line.strip()
        if not line or line.startswith("+"):
            continue
        if line.lower().startswith("total"):
            continue
        parts = [p.strip() for p in line.split("|") if p.strip()]
        for p in parts:
            p = p.replace(",", "")
            if p.isdigit() or (p.replace(".", "", 1).isdigit() and p.count(".") <= 1):
                if not any(ch.isalpha() for ch in p):
                    return int(float(p))
    return 0


def _is_header_row(cells):
    """True if row looks like a column header (no numeric values)."""
    has_value = False
    for c in cells:
        c = c.strip().replace(",", "")
        if not c:
            continue
        has_value = True
        try:
            float(c)
            return False
        except ValueError:
            pass
    return has_value


def parse_table_rows(resp):
    """Return data rows (list of cell strings) from RMDB table output."""
    rows = []
    for line in resp.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        inner = line.strip("|")
        cells = [c.strip() for c in inner.split("|")]
        if not cells or _is_header_row(cells):
            continue
        rows.append(cells)
    return rows


def kill_rmdb():
    subprocess.run(["pkill", "-9", "-f", "bin/rmdb"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)


def start_rmdb(db_name, log_path=None):
    if not os.path.isfile(RMDB):
        raise FileNotFoundError(RMDB + " not found; run: cd build && make rmdb -j4")
    kill_rmdb()
    dbpath = os.path.join(BUILD, db_name)
    if os.path.isdir(dbpath):
        shutil.rmtree(dbpath)
    os.makedirs(dbpath, exist_ok=True)
    stdout = open(log_path, "w") if log_path else subprocess.DEVNULL
    proc = subprocess.Popen([RMDB, db_name], cwd=BUILD, stdout=stdout, stderr=subprocess.STDOUT)
    wait_ready(timeout=60.0)
    return proc, dbpath


def bootstrap_tpcc(db_name="tpcc_bench_db", with_indexes=True, loads=None, client_timeout=None):
    """Create schema, load CSV, PK indexes, set SI. Returns (proc, client)."""
    proc, _ = start_rmdb(db_name)
    cli = None
    for _ in range(60):
        try:
            cli = RmdbClient(timeout=client_timeout)
            break
        except (ConnectionRefusedError, OSError, RuntimeError):
            time.sleep(1)
    if cli is None:
        proc.kill()
        kill_rmdb()
        raise RuntimeError("rmdb not accepting connections after 60s")
    for sql in SCHEMA:
        ok, r = cli.query_ok(sql)
        if not ok:
            cli.close()
            proc.kill()
            kill_rmdb()
            raise RuntimeError("schema failed: " + r)
    load_list = loads if loads is not None else LOADS
    for tab, path, _ in load_list:
        ok, r = cli.query_ok("load " + path + " into " + tab + ";")
        if not ok:
            cli.close()
            proc.kill()
            kill_rmdb()
            raise RuntimeError("load failed " + tab + ": " + r)
    if with_indexes:
        for sql in INDEXES:
            ok, r = cli.query_ok(sql)
            if not ok:
                cli.close()
                proc.kill()
                kill_rmdb()
                raise RuntimeError("index failed: " + r)
    cli.query("set transaction isolation level snapshot isolation")
    # 决赛不发送 SET OUTPUT_FILE；Wire 路径本就不写 output.txt
    return proc, cli


def verify_load_counts(cli, loads=None):
    ok = True
    load_list = loads if loads is not None else LOADS
    for tab, _, exp in load_list:
        cnt = parse_count(cli.query("select count(*) from " + tab + ";"))
        passed = cnt == exp
        print("  load %s: %d/%d %s" % (tab, cnt, exp, "PASS" if passed else "FAIL"))
        ok = ok and passed
    return ok
