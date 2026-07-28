"""TPC-C load content verification (stricter than row-count-only OJ check)."""

import csv
import hashlib
import json
import os

from tpcc_common import BUILD, parse_table_rows
from tpcc_scale import TABLE_ORDER, data_dir_for_scale

# PK columns per table (CSV header names)
TABLE_PK = {
    "warehouse": ["w_id"],
    "item": ["i_id"],
    "stock": ["s_w_id", "s_i_id"],
    "district": ["d_w_id", "d_id"],
    "customer": ["c_w_id", "c_d_id", "c_id"],
    "history": ["h_c_id", "h_c_d_id", "h_c_w_id", "h_d_id", "h_w_id", "h_date"],
    "orders": ["o_w_id", "o_d_id", "o_id"],
    "new_orders": ["no_w_id", "no_d_id", "no_o_id"],
    "order_line": ["ol_w_id", "ol_d_id", "ol_o_id", "ol_number"],
}

FLOAT_COLS = {
    "warehouse": {"w_tax", "w_ytd"},
    "item": {"i_price"},
    "district": {"d_tax", "d_ytd"},
    "customer": {"c_discount", "c_balance", "c_ytd_payment"},
    "history": {"h_amount"},
    "order_line": {"ol_amount"},
    "stock": {"s_ytd"},
}

# mini official CSV is tiny — verify every row
MINI_FULL_TABLES = set(TABLE_PK.keys())


def _fail(msg):
    print("  FAIL:", msg)
    return False


def resolve_csv(load_relpath, db_name="tpcc_perf_db"):
    """CSV paths in load SQL are relative to build/<db_name>/ (rmdb cwd)."""
    return os.path.normpath(os.path.join(BUILD, db_name, load_relpath))


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_csv_rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def _norm_str(s):
    return (s or "").strip()


def _values_equal(col, csv_val, db_val, float_cols):
    cv = _norm_str(csv_val)
    dv = _norm_str(db_val)
    if col in float_cols:
        try:
            return abs(float(cv) - float(dv)) < 0.02
        except ValueError:
            return cv == dv
    # RMDB table output may truncate long char fields with "..."
    if dv.endswith("..."):
        return cv.startswith(dv[:-3])
    if cv.endswith("..."):
        return dv.startswith(cv[:-3])
    if cv == dv:
        return True
    # char(N) padding: accept if either is a prefix of the other
    return cv.startswith(dv) or dv.startswith(cv)


def _query_row(cli, table, pk_cols, row):
    where = " and ".join("%s = %s" % (c, _sql_literal(row[c])) for c in pk_cols)
    cols = [c for c in row.keys() if c not in pk_cols]
    if not cols:
        cols = pk_cols
    sel = ", ".join(cols)
    resp = cli.query("select %s from %s where %s;" % (sel, table, where))
    rows = parse_table_rows(resp)
    if not rows:
        return None, cols
    return rows[0], cols


def _sql_literal(val):
    v = _norm_str(val)
    try:
        float(v)
        return v
    except ValueError:
        pass
    if v.isdigit() or (v.startswith("-") and v[1:].isdigit()):
        return v
    return "'" + v.replace("'", "''") + "'"


def verify_rows_against_db(cli, table, csv_rows, pk_cols, float_cols, label=""):
    ok = True
    for i, row in enumerate(csv_rows):
        db_cells, cols = _query_row(cli, table, pk_cols, row)
        if db_cells is None:
            ok = _fail("%s row %d: not found in DB (pk=%s)" % (
                table, i + 1, {c: row[c] for c in pk_cols})) and ok
            continue
        for j, col in enumerate(cols):
            if col not in row:
                continue
            if j >= len(db_cells):
                ok = _fail("%s row %d: missing column %s in DB" % (table, i + 1, col)) and ok
                continue
            if not _values_equal(col, row[col], db_cells[j], float_cols):
                ok = _fail(
                    "%s pk=%s col %s: csv=%r db=%r"
                    % (table, {c: row[c] for c in pk_cols}, col, row[col], db_cells[j])
                ) and ok
    if ok and csv_rows:
        print("  PASS: %s content (%d rows)%s" % (table, len(csv_rows), label))
    return ok


def verify_csv_checksums(data_dir, manifest):
    expected = manifest.get("csv_sha256", {})
    if not expected:
        print("  SKIP: csv_sha256 not in manifest (regenerate with generate_tpcc_data.py)")
        return True
    ok = True
    for tab in TABLE_ORDER:
        path = os.path.join(data_dir, tab + ".csv")
        if tab not in expected:
            ok = _fail("manifest missing checksum for %s" % tab) and ok
            continue
        actual = sha256_file(path)
        if actual != expected[tab]:
            ok = _fail("%s csv checksum mismatch" % tab) and ok
    if ok:
        print("  PASS: CSV file checksums (%d tables)" % len(expected))
    return ok


def verify_manifest_anchors(cli, anchors):
    if not anchors:
        print("  SKIP: no anchors in manifest")
        return True
    ok = True
    for ent in anchors:
        table = ent["table"]
        pk_cols = TABLE_PK[table]
        row = dict(ent["pk"])
        row.update(ent.get("fields", {}))
        fcols = FLOAT_COLS.get(table, set())
        if not verify_rows_against_db(cli, table, [row], pk_cols, fcols, label=" [anchor]"):
            ok = False
    if ok:
        print("  PASS: manifest anchors (%d)" % len(anchors))
    return ok


def sample_rows_from_csv(path, count=5):
    rows = read_csv_rows(path)
    if len(rows) <= count * 2:
        return rows
    mid = len(rows) // 2
    picks = rows[:count] + rows[mid: mid + count] + rows[-count:]
    return picks


def verify_load_content(cli, scale_name, loads, strict=False, db_name="tpcc_perf_db"):
    """
    Verify loaded DB content matches CSV source.
    - mini: full row-by-row compare (all rows)
    - full+strict: csv checksums + manifest anchors + per-table CSV samples
    """
    print("\n-- load content verify%s --" % (" (STRICT)" if strict else ""))
    ok = True
    load_map = {tab: path for tab, path, _ in loads}

    if scale_name == "mini":
        for table in TABLE_ORDER:
            path = resolve_csv(load_map[table], db_name)
            rows = read_csv_rows(path)
            pk = TABLE_PK[table]
            fcols = FLOAT_COLS.get(table, set())
            ok = verify_rows_against_db(cli, table, rows, pk, fcols) and ok
        print("LOAD CONTENT:", "PASS" if ok else "FAIL")
        return ok

    data_dir = data_dir_for_scale("full")
    manifest_path = os.path.join(data_dir, "manifest.json")
    manifest = {}
    if os.path.isfile(manifest_path):
        with open(manifest_path) as f:
            manifest = json.load(f)

    if strict:
        ok = verify_csv_checksums(data_dir, manifest) and ok
        ok = verify_manifest_anchors(cli, manifest.get("anchors", [])) and ok

    # Sample-based compare for large tables (stricter than OJ count-only)
    sample_n = 10 if strict else 3
    for table in TABLE_ORDER:
        path = resolve_csv(load_map[table], db_name)
        if not os.path.isfile(path):
            ok = _fail("csv missing: " + path) and ok
            continue
        rows = sample_rows_from_csv(path, sample_n)
        pk = TABLE_PK[table]
        fcols = FLOAT_COLS.get(table, set())
        ok = verify_rows_against_db(
            cli, table, rows, pk, fcols, label=" [sample %d]" % len(rows)
        ) and ok

    print("LOAD CONTENT:", "PASS" if ok else "FAIL")
    return ok
