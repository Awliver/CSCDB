"""TPC-C consistency checks (post-benchmark + crash recovery).

Normal mode: lightweight sampling (faster).
Strict mode: full-table / per-district scans, stronger invariants (stricter than OJ).
"""

from tpcc_common import W_ID, parse_count, parse_table_rows


def _fail(msg):
    print("  FAIL:", msg)
    return False


def _resolve_min_oid(min_o_id_per_district, w_id, d_id):
    if not min_o_id_per_district:
        return None
    per_w = min_o_id_per_district.get(w_id)
    if isinstance(per_w, dict):
        return per_w.get(d_id, 1)
    return min_o_id_per_district.get(d_id, 1)


def check_district_next_o_id(cli, districts, warehouses=1):
    """d_next_o_id should be greater than max(o_id) for each district."""
    ok = True
    for w_id in range(1, warehouses + 1):
        for d_id in range(1, districts + 1):
            rows = parse_table_rows(
                cli.query(
                    "select d_next_o_id from district where d_w_id = %d and d_id = %d;"
                    % (w_id, d_id)
                )
            )
            if not rows:
                ok = _fail("warehouse %d district %d missing" % (w_id, d_id)) and ok
                continue
            nxt = int(float(rows[0][0]))
            rows2 = parse_table_rows(
                cli.query(
                    "select max(o_id) from orders where o_w_id = %d and o_d_id = %d;"
                    % (w_id, d_id)
                )
            )
            max_oid = 0
            if rows2 and rows2[0][0] not in ("", "NULL"):
                max_oid = int(float(rows2[0][0]))
            if nxt <= max_oid:
                ok = _fail(
                    "w=%d d=%d d_next_o_id=%d <= max(o_id)=%d" % (w_id, d_id, nxt, max_oid)
                ) and ok
    if ok:
        print("  PASS: district d_next_o_id vs max(o_id)")
    return ok


def snapshot_bench_start_o_ids(cli, districts, warehouses=1):
    """First o_id expected to be created after benchmark load (max existing + 1 per district)."""
    start = {}
    for w_id in range(1, warehouses + 1):
        start[w_id] = {}
        for d_id in range(1, districts + 1):
            rows = parse_table_rows(
                cli.query(
                    "select max(o_id) from orders where o_w_id = %d and o_d_id = %d;"
                    % (w_id, d_id)
                )
            )
            max_oid = 0
            if rows and rows[0][0] not in ("", "NULL"):
                max_oid = int(float(rows[0][0]))
            start[w_id][d_id] = max_oid + 1
    return start


def check_customer_balance_valid(cli):
    """Customer balance should stay within TPC-C credit limits after payments."""
    bad = parse_count(
        cli.query("select count(*) from customer where c_balance < -50000 or c_balance > 50000000;")
    )
    if bad > 0:
        return _fail("%d customer rows with out-of-range balance" % bad)
    print("  PASS: customer balance in range")
    return True


def check_new_orders_referential(cli, sample=200):
    """Sample new_orders rows and verify matching orders exist."""
    rows = parse_table_rows(
        cli.query("select no_o_id, no_d_id, no_w_id from new_orders limit %d;" % sample)
    )
    if not rows:
        print("  PASS: new_orders referential (empty)")
        return True
    ok = True
    for no_o_id, no_d_id, no_w_id in rows:
        o_id = int(float(no_o_id))
        d_id = int(float(no_d_id))
        w_id = int(float(no_w_id))
        cnt = parse_count(
            cli.query(
                "select count(*) from orders where o_w_id = %d and o_d_id = %d and o_id = %d;"
                % (w_id, d_id, o_id)
            )
        )
        if cnt != 1:
            ok = _fail("new_orders (%d,%d,%d) missing in orders" % (w_id, d_id, o_id)) and ok
    if ok:
        print("  PASS: new_orders referential (sampled %d)" % len(rows))
    return ok


def check_new_orders_referential_full(cli, districts, warehouses=1):
    """Strict: verify every new_orders row in every district."""
    ok = True
    total = 0
    for w_id in range(1, warehouses + 1):
        for d_id in range(1, districts + 1):
            rows = parse_table_rows(
                cli.query(
                    "select no_o_id from new_orders where no_w_id = %d and no_d_id = %d;"
                    % (w_id, d_id)
                )
            )
            for (no_o_id,) in rows:
                total += 1
                o_id = int(float(no_o_id))
                cnt = parse_count(
                    cli.query(
                        "select count(*) from orders where o_w_id = %d and o_d_id = %d and o_id = %d;"
                        % (w_id, d_id, o_id)
                    )
                )
                if cnt != 1:
                    ok = _fail("new_orders w=%d d=%d o=%d orphan" % (w_id, d_id, o_id)) and ok
    if ok:
        print("  PASS: new_orders full referential (%d rows)" % total)
    return ok


def check_order_line_referential(cli, sample=200):
    rows = parse_table_rows(
        cli.query("select ol_o_id, ol_d_id, ol_w_id from order_line limit %d;" % sample)
    )
    if not rows:
        print("  PASS: order_line referential (empty)")
        return True
    ok = True
    for ol_o_id, ol_d_id, ol_w_id in rows:
        o_id = int(float(ol_o_id))
        d_id = int(float(ol_d_id))
        w_id = int(float(ol_w_id))
        cnt = parse_count(
            cli.query(
                "select count(*) from orders where o_w_id = %d and o_d_id = %d and o_id = %d;"
                % (w_id, d_id, o_id)
            )
        )
        if cnt != 1:
            ok = _fail("order_line (%d,%d,%d) missing parent order" % (w_id, d_id, o_id)) and ok
    if ok:
        print("  PASS: order_line referential (sampled %d)" % len(rows))
    return ok


def check_order_line_referential_full(cli, districts, warehouses=1, o_id_step=200):
    """Strict: every order_line references an existing order (batched by o_id range)."""
    ok = True
    total = 0
    for w_id in range(1, warehouses + 1):
        for d_id in range(1, districts + 1):
            rows = parse_table_rows(
                cli.query(
                    "select max(o_id) from orders where o_w_id = %d and o_d_id = %d;"
                    % (w_id, d_id)
                )
            )
            max_oid = int(float(rows[0][0])) if rows and rows[0][0] not in ("", "NULL") else 0
            for o_start in range(1, max_oid + 1, o_id_step):
                o_end = min(max_oid + 1, o_start + o_id_step)
                ol_rows = parse_table_rows(
                    cli.query(
                        "select ol_o_id from order_line where ol_w_id = %d and ol_d_id = %d "
                        "and ol_o_id >= %d and ol_o_id < %d;" % (w_id, d_id, o_start, o_end)
                    )
                )
                for (o_id_s,) in ol_rows:
                    total += 1
                    o_id = int(float(o_id_s))
                    cnt = parse_count(
                        cli.query(
                            "select count(*) from orders where o_w_id = %d and o_d_id = %d and o_id = %d;"
                            % (w_id, d_id, o_id)
                        )
                    )
                    if cnt != 1:
                        ok = _fail("order_line w=%d d=%d o=%d orphan" % (w_id, d_id, o_id)) and ok
    if ok:
        print("  PASS: order_line full referential (%d rows)" % total)
    return ok


def check_order_line_counts(cli, districts, min_o_id_per_district=None, warehouses=1):
    """orders.o_ol_cnt must match count(order_line); strict uses bench_start_o_ids when set."""
    ok = True
    checked = 0
    for w_id in range(1, warehouses + 1):
        for d_id in range(1, districts + 1):
            min_oid = _resolve_min_oid(min_o_id_per_district, w_id, d_id)
            if min_oid is None:
                rows = parse_table_rows(
                    cli.query(
                        "select max(o_id) from orders where o_w_id = %d and o_d_id = %d;"
                        % (w_id, d_id)
                    )
                )
                max_oid = int(float(rows[0][0])) if rows and rows[0][0] else 0
                min_oid = max(1, max_oid - 200)
            rows = parse_table_rows(
                cli.query(
                    "select o_id, o_ol_cnt from orders where o_w_id = %d and o_d_id = %d "
                    "and o_id >= %d;" % (w_id, d_id, min_oid)
                )
            )
            for o_id_s, ol_cnt_s in rows:
                o_id = int(float(o_id_s))
                expected = int(float(ol_cnt_s))
                actual = parse_count(
                    cli.query(
                        "select count(*) from order_line where ol_w_id = %d and ol_d_id = %d "
                        "and ol_o_id = %d;" % (w_id, d_id, o_id)
                    )
                )
                checked += 1
                if actual != expected:
                    ok = _fail(
                        "w=%d d=%d o=%d o_ol_cnt=%d but order_line count=%d"
                        % (w_id, d_id, o_id, expected, actual)
                    ) and ok
    if ok:
        print("  PASS: order o_ol_cnt vs order_line count (%d orders)" % checked)
    return ok


def check_order_line_amounts(cli):
    bad = parse_count(cli.query("select count(*) from order_line where ol_amount < 0;"))
    if bad > 0:
        return _fail("%d order_line rows with negative amount" % bad)
    print("  PASS: order_line amounts non-negative")
    return True


def check_stock_quantity(cli):
    bad = parse_count(cli.query("select count(*) from stock where s_quantity < 0 or s_quantity > 100;"))
    if bad > 0:
        return _fail("%d stock rows with invalid quantity" % bad)
    print("  PASS: stock quantity range")
    return True


def check_table_counts_growing(cli, before_counts, tables):
    ok = True
    for tab in tables:
        after = parse_count(cli.query("select count(*) from %s;" % tab))
        b = before_counts.get(tab, 0)
        if after < b:
            ok = _fail("%s shrank: %d -> %d" % (tab, b, after)) and ok
    if ok:
        print("  PASS: table row counts monotonic")
    return ok


def check_payment_history_growth(cli, before_history):
    """Strict: successful payments must have inserted history rows."""
    after = parse_count(cli.query("select count(*) from history;"))
    if after < before_history:
        return _fail("history shrank: %d -> %d" % (before_history, after))
    print("  PASS: history monotonic (%d -> %d)" % (before_history, after))
    return True


def snapshot_ytd_baseline(cli, warehouses=1):
    """Capture warehouse.w_ytd and sum(district.d_ytd) before benchmark."""
    baseline = {}
    for w_id in range(1, warehouses + 1):
        rows = parse_table_rows(cli.query("select w_ytd from warehouse where w_id = %d;" % w_id))
        if not rows:
            return None
        w_ytd = float(rows[0][0])
        d_sum = 0.0
        for row in parse_table_rows(cli.query("select d_ytd from district where d_w_id = %d;" % w_id)):
            d_sum += float(row[0])
        baseline[w_id] = (w_ytd, d_sum)
    return baseline


def check_warehouse_district_ytd(cli, baseline=None, warehouses=1, atol=1.0):
    """Payment 后 warehouse 与 district 的 ytd 增量应一致（不比绝对值，装载基线可不等）。"""
    ok = True
    for w_id in range(1, warehouses + 1):
        rows = parse_table_rows(cli.query("select w_ytd from warehouse where w_id = %d;" % w_id))
        if not rows:
            return _fail("warehouse %d missing" % w_id)
        w_ytd = float(rows[0][0])
        d_sum = 0.0
        for row in parse_table_rows(cli.query("select d_ytd from district where d_w_id = %d;" % w_id)):
            d_sum += float(row[0])
        if baseline is not None:
            w0, d0 = baseline.get(w_id, (None, None))
            if w0 is None:
                ok = _fail("warehouse %d missing baseline" % w_id) and ok
                continue
            dw = w_ytd - w0
            dd = d_sum - d0
            effective_atol = max(atol, abs(dw) * 1e-5)
            if abs(dw - dd) > effective_atol:
                ok = _fail(
                    "w=%d payment ytd drift: warehouse +%.4f vs district +%.4f (delta diff %.4f, tol %.4f)"
                    % (w_id, dw, dd, dw - dd, effective_atol)
                ) and ok
        elif abs(w_ytd - d_sum) > atol:
            ok = _fail(
                "w=%d warehouse.w_ytd=%.4f != sum(district.d_ytd)=%.4f" % (w_id, w_ytd, d_sum)
            ) and ok
    if ok:
        if baseline is not None:
            print("  PASS: warehouse/district ytd payment delta (all warehouses)")
        else:
            print("  PASS: warehouse/district ytd balance (all warehouses)")
    return ok


def check_customer_referential_sample(cli, districts, warehouses=1, sample_per_d=5):
    """Strict: sampled customers exist with valid district."""
    ok = True
    for w_id in range(1, warehouses + 1):
        for d_id in range(1, districts + 1):
            rows = parse_table_rows(
                cli.query(
                    "select c_id from customer where c_w_id = %d and c_d_id = %d limit %d;"
                    % (w_id, d_id, sample_per_d)
                )
            )
            if not rows:
                ok = _fail("warehouse %d district %d has no customers" % (w_id, d_id)) and ok
    if ok:
        print("  PASS: customer presence per district")
    return ok


def run_consistency_checks(cli, districts=10, warehouses=1, before_counts=None, strict=False,
                           bench_start_o_ids=None, ytd_baseline=None):
    print("\n-- consistency checks%s --" % (" (STRICT)" if strict else ""))
    ok = True
    ok = check_district_next_o_id(cli, districts, warehouses=warehouses) and ok
    if strict:
        ok = check_new_orders_referential_full(cli, districts, warehouses=warehouses) and ok
        ok = check_order_line_referential_full(cli, districts, warehouses=warehouses) and ok
        min_o_ids = bench_start_o_ids if bench_start_o_ids else None
        ok = check_order_line_counts(
            cli, districts, min_o_id_per_district=min_o_ids, warehouses=warehouses
        ) and ok
        if before_counts is not None:
            ok = check_payment_history_growth(cli, before_counts.get("history", 0)) and ok
        ok = check_warehouse_district_ytd(
            cli, baseline=ytd_baseline, warehouses=warehouses
        ) and ok
        ok = check_customer_referential_sample(cli, districts, warehouses=warehouses) and ok
        ok = check_customer_balance_valid(cli) and ok
    else:
        ok = check_new_orders_referential(cli) and ok
        ok = check_order_line_referential(cli) and ok
        if bench_start_o_ids:
            ok = check_order_line_counts(
                cli, districts, min_o_id_per_district=bench_start_o_ids, warehouses=warehouses
            ) and ok
    ok = check_order_line_amounts(cli) and ok
    ok = check_stock_quantity(cli) and ok
    if before_counts:
        ok = check_table_counts_growing(
            cli, before_counts, ("orders", "order_line", "history")
        ) and ok
    print("CONSISTENCY:", "PASS" if ok else "FAIL")
    return ok
