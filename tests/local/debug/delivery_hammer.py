#!/usr/bin/env python3
"""Delivery 加浓定向压测：排名词典形态（ORDER BY..LIMIT 1 + 10 district 三段 batch）。

针对 OJ Phase3 measurement Delivery SIGABRT 的复现器。默认混合 50% Delivery /
40% NewOrder（喂 new_orders）/ 10% Payment。SKIP_BOOTSTRAP=1 直连已装载库。
用法: python3 delivery_hammer.py [seconds] [threads]   (需服务器已启动+已装载)
"""
import os
import random
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from wire_client import WireClient  # noqa: E402
from tpcc_batch import install_prepare, run_txn_batch  # noqa: E402

NW = int(os.environ.get("TPCC_W", "10"))
SCALE = {
    "warehouses": NW,
    "districts": 10,
    "customers_per_district": 3000,
    "items": 100000,
    "min_ol_cnt": 5,
    "max_ol_cnt": 15,
}

STOP = False
stats = {"ok": 0, "abort": 0, "err": 0, "transport": 0}
slock = threading.Lock()
errs = []


def worker(tid):
    rng = random.Random(1000 + tid)
    try:
        cli = WireClient(timeout=30)
        cli.exec_stream("set transaction isolation level snapshot isolation")
        install_prepare(cli)
        scale = dict(SCALE)
        scale["w_id"] = (tid % NW) + 1
        while not STOP:
            k = rng.random()
            if k < 0.5:
                txn = "delivery"
            elif k < 0.9:
                txn = "new_order"
            else:
                txn = "payment"
            try:
                ok, err = run_txn_batch(cli, rng, scale, txn)
                with slock:
                    if ok:
                        stats["ok"] += 1
                    elif "abort" in err:
                        stats["abort"] += 1
                    else:
                        stats["err"] += 1
                        if len(errs) < 12:
                            errs.append("%s: %s" % (txn, err))
            except Exception as e:
                with slock:
                    stats["transport"] += 1
                    if len(errs) < 12:
                        errs.append("TRANSPORT %s: %r" % (txn, e))
                return
    except Exception as e:
        with slock:
            stats["transport"] += 1
            if len(errs) < 12:
                errs.append("SETUP: %r" % (e,))


if __name__ == "__main__":
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 120
    nthreads = int(sys.argv[2]) if len(sys.argv) > 2 else 32
    ths = [threading.Thread(target=worker, args=(i,)) for i in range(nthreads)]
    t0 = time.time()
    for t in ths:
        t.start()
    while time.time() - t0 < seconds:
        time.sleep(2)
        alive = sum(t.is_alive() for t in ths)
        with slock:
            s = dict(stats)
        print("  t=%4.0fs alive=%d %s" % (time.time() - t0, alive, s), flush=True)
        if alive == 0:
            break
    STOP = True
    for t in ths:
        t.join(timeout=15)
    print("FINAL:", stats)
    for e in errs:
        print("ERR:", e)
