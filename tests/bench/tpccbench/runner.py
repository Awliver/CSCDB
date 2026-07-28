"""Multi-client benchmark driver (pgbench-style).

- each worker: own connection, own RNG (shared spec C-constants), bound to a
  home warehouse round-robin
- shuffle-and-deal weighted deck -> exact mix per pass (spec 5.2.4.2)
- warmup phase not measured; measured window uses txn completion time
- tpmC = committed New-Order per minute of the measured window
- per-txn latency percentiles; p90 <= 5s flagged per spec 5.2.5.3
- aborts (deadlock/SI conflicts) are counted, not retried; errors collected
"""

import threading
import time

from . import workload
from .db import Client, ServerDied, StmtTimeout
from .tpcrand import TpccRandom, derive_c_run


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.data = {}          # txn -> {status -> n}
        self.lat = {}           # txn -> [ms] (committed only)
        self.errors = {}        # detail -> n (first 50 distinct)
        self.no_marks = []      # completion timestamps of committed new_orders

    def record(self, txn, status, ms, detail=""):
        with self.lock:
            d = self.data.setdefault(txn, {})
            d[status] = d.get(status, 0) + 1
            if status == "commit":
                self.lat.setdefault(txn, []).append(ms)
                if txn == "new_order":
                    self.no_marks.append(time.perf_counter())
            if status == "error" and detail and len(self.errors) < 50:
                self.errors[detail] = self.errors.get(detail, 0) + 1

    def committed(self, txn=None):
        with self.lock:
            if txn:
                return self.data.get(txn, {}).get("commit", 0)
            return sum(d.get("commit", 0) for d in self.data.values())


def _pct(sorted_ms, q):
    if not sorted_ms:
        return 0.0
    i = min(len(sorted_ms) - 1, int(q * len(sorted_ms)))
    return sorted_ms[i]


class Worker(threading.Thread):
    def __init__(self, idx, cfg, deck_weights, stats, stop_evt, seed, c_consts,
                 isolation, timeout, think_scale=1.0):
        super().__init__(daemon=True)
        self.idx = idx
        self.cfg = cfg
        self.stats = stats
        self.stop_evt = stop_evt
        self.rng = TpccRandom(seed + idx * 10007, *c_consts)
        self.deck_weights = deck_weights
        self.w_id = 1 + (idx % cfg.warehouses)
        self.isolation = isolation
        self.timeout = timeout
        self.think_scale = think_scale     # TPC-C keying+think 时间倍率；0=极限吞吐模式
        self.fatal = None

    def _pace(self, seconds):
        """按 stop_evt 分段睡眠，保证停测时能及时退出（think time 可达数十秒）。"""
        if seconds <= 0:
            return
        end = time.perf_counter() + seconds
        while not self.stop_evt.is_set():
            remain = end - time.perf_counter()
            if remain <= 0:
                return
            time.sleep(min(remain, 0.5))

    def _connect(self):
        cli = Client(timeout=self.timeout)
        if self.isolation == "si":
            cli.query("set transaction isolation level snapshot isolation")
        return cli

    def run(self):
        try:
            cli = self._connect()
        except OSError as e:
            self.fatal = "connect: %s" % e
            return
        deck = []
        while not self.stop_evt.is_set():
            if not deck:
                deck = self.rng.shuffled(workload.build_deck(self.deck_weights))
            name, fn = deck.pop()
            # 规范 5.2.5.4：事务前的 keying time（终端限流，不计入事务延迟）
            if self.think_scale > 0:
                self._pace(self.rng.keying_time(name) * self.think_scale)
                if self.stop_evt.is_set():
                    break
            t0 = time.perf_counter()
            try:
                status, detail = fn(cli, self.rng, self.cfg, self.w_id)
            except StmtTimeout as e:
                self.stats.record(name, "error", 0, "timeout: " + str(e)[:80])
                cli.close()
                try:
                    cli = self._connect()
                except OSError:
                    self.fatal = "reconnect failed after timeout"
                    return
                continue
            except ServerDied as e:
                cli.close()
                try:                       # server may just have reset this conn
                    cli = self._connect()
                    self.stats.record(name, "error", 0, "conn reset: " + str(e)[:80])
                    continue
                except OSError:
                    self.fatal = "server died: " + str(e)[:80]
                    self.stop_evt.set()
                    return
            ms = (time.perf_counter() - t0) * 1000
            self.stats.record(name, status, ms, detail)
            # 规范 5.2.5.4：事务提交后的 think time（指数分布）
            if self.think_scale > 0:
                self._pace(self.rng.think_time(name) * self.think_scale)
        cli.close()


def bench(cfg, duration, warmup=10, clients=8, mix=(45, 43, 4, 4, 4),
          isolation="si", seed=42, c_last_load=None, progress=5,
          server_alive=None, timeout=60, think_scale=1.0):
    """Run the benchmark; returns result dict."""
    rng = TpccRandom(seed)
    c_last_run = derive_c_run(c_last_load, rng.rng) if c_last_load is not None else rng.c_last
    c_consts = (c_last_run, rng.c_id, rng.c_ol_i_id)

    stats = Stats()
    stop = threading.Event()
    workers = [Worker(i, cfg, mix, stats, stop, seed, c_consts, isolation, timeout, think_scale)
               for i in range(clients)]

    mode = ("think=%.2gx(TPC-C规范限流)" % think_scale) if think_scale > 0 else "极限吞吐(无think)"
    print("tpccbench run: W=%d clients=%d mix=%s isolation=%s warmup=%ds measure=%ds %s"
          % (cfg.warehouses, clients, "/".join(map(str, mix)), isolation, warmup, duration, mode),
          flush=True)
    for w in workers:
        w.start()

    # ---- warmup ----------------------------------------------------------------
    t_end = time.time() + warmup
    while time.time() < t_end and not stop.is_set():
        time.sleep(0.5)
    with stats.lock:                       # reset counters, keep connections hot
        stats.data.clear(), stats.lat.clear(), stats.errors.clear()
        stats.no_marks.clear()

    # ---- measured window ---------------------------------------------------------
    t0 = time.perf_counter()
    t_end = time.time() + duration
    last_no, last_t = 0, t0
    while time.time() < t_end and not stop.is_set():
        time.sleep(min(progress, max(0.5, t_end - time.time())))
        if server_alive and not server_alive():
            print("!! server process died — aborting benchmark", flush=True)
            stop.set()
            break
        now = time.perf_counter()
        cur = stats.committed("new_order")
        inst = (cur - last_no) / max(1e-9, now - last_t) * 60
        print("  progress %4.0fs  committed=%d  inst-tpmC=%.0f"
              % (now - t0, stats.committed(), inst), flush=True)
        last_no, last_t = cur, now
    measured = time.perf_counter() - t0
    stop.set()
    for w in workers:
        w.join(timeout=max(10, timeout))

    # ---- report -----------------------------------------------------------------------
    fatal = [w.fatal for w in workers if w.fatal]
    no_commits = stats.committed("new_order")
    tpmc = no_commits / (measured / 60) if measured > 0 else 0.0

    print("\n=== tpccbench result (measured %.1fs) ===" % measured)
    print("%-13s %9s %9s %9s %9s %9s | %8s %8s %8s %8s" %
          ("txn", "commit", "abort", "rollbck", "error", "total",
           "avg-ms", "p50", "p90", "p99"))
    order = ["new_order", "payment", "order_status", "delivery", "stock_level"]
    totals = {"commit": 0, "abort": 0, "rollback": 0, "error": 0}
    for name in order:
        d = stats.data.get(name, {})
        lat = sorted(stats.lat.get(name, []))
        n_c, n_a = d.get("commit", 0), d.get("abort", 0)
        n_r, n_e = d.get("rollback", 0), d.get("error", 0)
        for k, v in (("commit", n_c), ("abort", n_a), ("rollback", n_r), ("error", n_e)):
            totals[k] += v
        avg = sum(lat) / len(lat) if lat else 0.0
        flag = "  << p90 > 5s!" if _pct(lat, 0.90) > 5000 else ""
        print("%-13s %9d %9d %9d %9d %9d | %8.1f %8.1f %8.1f %8.1f%s" %
              (name, n_c, n_a, n_r, n_e, n_c + n_a + n_r + n_e,
               avg, _pct(lat, 0.50), _pct(lat, 0.90), _pct(lat, 0.99), flag))
    grand = sum(totals.values())
    print("-" * 100)
    print("total committed=%d abort=%d rollback=%d error=%d (%d txns, %.1f tps)"
          % (totals["commit"], totals["abort"], totals["rollback"], totals["error"],
             grand, grand / measured if measured else 0))
    print("\n  tpmC (committed New-Order / min): %.1f" % tpmc)
    if totals["abort"] + totals["commit"] > 0:
        print("  system abort rate: %.1f%%"
              % (100.0 * totals["abort"] / (totals["abort"] + totals["commit"])))
    if stats.errors:
        print("  DISTINCT ERRORS (%d):" % len(stats.errors))
        for msg, n in sorted(stats.errors.items(), key=lambda kv: -kv[1])[:10]:
            print("    %4dx %s" % (n, msg))
    if fatal:
        print("  FATAL: %s" % "; ".join(fatal))

    return {
        "tpmC": round(tpmc, 1),
        "measured_sec": round(measured, 1),
        "clients": clients,
        "mix": list(mix),
        "isolation": isolation,
        "think_scale": think_scale,
        "totals": totals,
        "fatal": fatal,
        "per_txn": {
            name: {
                "counts": stats.data.get(name, {}),
                "lat_ms": {
                    "avg": round(sum(stats.lat.get(name, [0])) /
                                 max(1, len(stats.lat.get(name, []))), 2),
                    "p50": round(_pct(sorted(stats.lat.get(name, [])), 0.50), 2),
                    "p90": round(_pct(sorted(stats.lat.get(name, [])), 0.90), 2),
                    "p99": round(_pct(sorted(stats.lat.get(name, [])), 0.99), 2),
                },
            } for name in order
        },
        "errors": stats.errors,
    }
