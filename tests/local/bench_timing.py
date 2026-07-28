"""Shared timing utilities for SQL / transaction micro-benchmarks."""

import statistics
import time


def percentile(sorted_vals, p):
    """Linear-interpolation percentile on a pre-sorted list."""
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    k = (len(sorted_vals) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(sorted_vals) - 1)
    if f == c:
        return sorted_vals[f]
    return sorted_vals[f] + (sorted_vals[c] - sorted_vals[f]) * (k - f)


class LatStats:
    """Wall-clock latency stats in seconds."""

    __slots__ = ("name", "samples")

    def __init__(self, name=""):
        self.name = name
        self.samples = []

    def add(self, seconds):
        if seconds >= 0:
            self.samples.append(seconds)

    def merge(self, other):
        self.samples.extend(other.samples)

    def count(self):
        return len(self.samples)

    def empty(self):
        return not self.samples

    def summary_ms(self):
        if not self.samples:
            return {
                "count": 0,
                "mean": 0.0,
                "p50": 0.0,
                "p95": 0.0,
                "p99": 0.0,
                "min": 0.0,
                "max": 0.0,
                "stdev": 0.0,
            }
        s = sorted(self.samples)
        mean = statistics.mean(s)
        stdev = statistics.pstdev(s) if len(s) > 1 else 0.0
        return {
            "count": len(s),
            "mean": mean * 1000.0,
            "p50": percentile(s, 50) * 1000.0,
            "p95": percentile(s, 95) * 1000.0,
            "p99": percentile(s, 99) * 1000.0,
            "min": s[0] * 1000.0,
            "max": s[-1] * 1000.0,
            "stdev": stdev * 1000.0,
        }

    def format_line(self, width=28):
        s = self.summary_ms()
        if s["count"] == 0:
            return "%-*s  %6d  (no samples)" % (width, self.name, 0)
        return (
            "%-*s  %6d  %8.3f  %8.3f  %8.3f  %8.3f  %8.3f"
            % (
                width,
                self.name,
                s["count"],
                s["mean"],
                s["p50"],
                s["p95"],
                s["p99"],
                s["max"],
            )
        )


def print_stats_table(title, stats_list, extra_cols=None):
    """Print a fixed-width latency table (times in ms)."""
    print("\n=== %s ===" % title)
    header = "%-28s  %6s  %8s  %8s  %8s  %8s  %8s" % (
        "name",
        "count",
        "mean",
        "p50",
        "p95",
        "p99",
        "max",
    )
    print(header)
    print("-" * len(header))
    for st in stats_list:
        print(st.format_line())
    if extra_cols:
        print(extra_cols)


def classify_tpcc_sql(sql):
    """Map a TPC-C SQL string to a short step label for profiling."""
    s = sql.strip().lower().rstrip(";")
    if s == "begin":
        return "begin"
    if s == "commit":
        return "commit"
    if s == "abort":
        return "abort"
    if s.startswith("select"):
        if " from district" in s:
            return "select_district"
        if " from warehouse" in s:
            return "select_warehouse"
        if " from customer" in s:
            return "select_customer"
        if " from stock" in s:
            return "select_stock"
        if " from item" in s:
            return "select_item"
        if " from orders" in s:
            return "select_orders"
        if " from order_line" in s:
            if "count(" in s or "sum(" in s:
                return "agg_order_line"
            return "select_order_line"
        if " from new_orders" in s:
            return "select_new_orders"
        if "order_line" in s and "stock" in s:
            return "agg_stock_level"
        if "count(" in s or "sum(" in s:
            return "aggregate"
        return "select_other"
    if s.startswith("update"):
        if " district" in s:
            return "update_district"
        if " warehouse" in s:
            return "update_warehouse"
        if " customer" in s:
            return "update_customer"
        if " stock" in s:
            return "update_stock"
        if " orders" in s:
            return "update_orders"
        if " order_line" in s:
            return "update_order_line"
        return "update_other"
    if s.startswith("insert"):
        if " history" in s:
            return "insert_history"
        if " order_line" in s:
            return "insert_order_line"
        if " orders" in s:
            return "insert_orders"
        if " new_orders" in s:
            return "insert_new_orders"
        return "insert_other"
    if s.startswith("delete"):
        return "delete"
    return "other"


class InstrumentedClient:
    """Wrap RmdbClient; record per-query latency with TPC-C step labels."""

    def __init__(self, cli):
        self._cli = cli
        self.steps = []  # list of (label, seconds)
        self.last_txn_steps = []

    def clear_steps(self):
        self.steps = []
        self.last_txn_steps = []

    def take_txn_steps(self):
        """Snapshot and clear steps for one transaction."""
        snap = list(self.steps)
        self.last_txn_steps = snap
        self.steps = []
        return snap

    def query(self, sql):
        label = classify_tpcc_sql(sql)
        t0 = time.perf_counter()
        r = self._cli.query(sql)
        dt = time.perf_counter() - t0
        self.steps.append((label, dt))
        return r

    def query_ok(self, sql):
        label = classify_tpcc_sql(sql)
        t0 = time.perf_counter()
        ok, r = self._cli.query_ok(sql)
        dt = time.perf_counter() - t0
        self.steps.append((label, dt))
        return ok, r

    def close(self):
        self._cli.close()


class StepAggregator:
    """Aggregate per-step latencies across many transactions."""

    def __init__(self):
        self.by_step = {}
        self.total = LatStats("txn_total")

    def add_txn(self, step_list):
        txn_dt = 0.0
        for label, dt in step_list:
            txn_dt += dt
            bucket = self.by_step.setdefault(label, LatStats(label))
            bucket.add(dt)
        self.total.add(txn_dt)

    def ordered_steps(self):
        """Prefer TPC-C logical order in report."""
        order = [
            "begin",
            "select_warehouse",
            "select_district",
            "update_district",
            "insert_orders",
            "insert_new_orders",
            "select_item",
            "select_stock",
            "update_stock",
            "insert_order_line",
            "select_customer",
            "update_warehouse",
            "update_customer",
            "insert_history",
            "select_orders",
            "select_order_line",
            "select_new_orders",
            "delete",
            "update_orders",
            "update_order_line",
            "agg_order_line",
            "agg_stock_level",
            "aggregate",
            "commit",
            "abort",
        ]
        seen = set(self.by_step)
        out = []
        for name in order:
            if name in self.by_step:
                out.append(self.by_step[name])
                seen.discard(name)
        for name in sorted(seen):
            out.append(self.by_step[name])
        return out

    def print_breakdown(self, title):
        steps = self.ordered_steps()
        total_mean = self.total.summary_ms()["mean"]
        print_stats_table(title, steps)
        if total_mean > 0:
            print("\n  step share of txn mean latency:")
            for st in steps:
                sm = st.summary_ms()
                if sm["count"] == 0:
                    continue
                share = sm["mean"] / total_mean * 100.0
                print("    %-24s %6.1f%%  (mean %.3f ms)" % (st.name, share, sm["mean"]))
        sm = self.total.summary_ms()
        print(
            "\n  txn_total: count=%d mean=%.3fms p50=%.3fms p95=%.3fms"
            % (sm["count"], sm["mean"], sm["p50"], sm["p95"])
        )


def run_timed_loop(fn, warmup, iterations, label=""):
    """Run fn() for warmup then collect per-iteration latencies (seconds)."""
    stats = LatStats(label)
    for _ in range(warmup):
        fn()
    for _ in range(iterations):
        t0 = time.perf_counter()
        fn()
        stats.add(time.perf_counter() - t0)
    return stats
