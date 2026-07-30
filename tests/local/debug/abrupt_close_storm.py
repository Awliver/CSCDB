#!/usr/bin/env python3
"""连接收尾 abort 风暴复现器：开显式事务带写 → 不 commit 直接断连（RST）。

目标：触发 handle_wire_connection 尾部未保护的 txn_manager->abort()。若 abort()
抛异常（缓冲池满重试耗尽 / bad_alloc / 回滚路径缺陷）→ std::terminate → SIGABRT。
配合并发常规负载（另开 delivery_hammer/wire_tpcc_stress2）制造资源压力效果更好。
用法: python3 abrupt_close_storm.py [seconds] [threads]
"""
import os
import random
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from wire_client import WireClient  # noqa: E402
from tpcc_batch import install_prepare, S_BEGIN, S_UPD_D_YTD, S_UPD_W_YTD, S_INS_HIST  # noqa: E402

NW = int(os.environ.get("TPCC_W", "10"))
STOP = False
counts = {"cycles": 0, "batch_fail": 0}
clock = threading.Lock()


def worker(tid):
    rng = random.Random(9000 + tid)
    while not STOP:
        try:
            cli = WireClient(timeout=20)
            cli.exec_stream("set transaction isolation level snapshot isolation")
            install_prepare(cli)
            w = rng.randint(1, NW)
            d = rng.randint(1, 10)
            amt = round(rng.uniform(1.0, 100.0), 2)
            # 显式事务 + 多张表的写（含插入），不提交
            br = cli.exec_batch([
                (S_BEGIN, []),
                (S_UPD_W_YTD, [amt, w]),
                (S_UPD_D_YTD, [amt, w, d]),
                (S_INS_HIST, [rng.randint(1, 3000), d, w, d, w,
                              "2026-07-29 20:00:00", amt, "stormrow"]),
            ])
            if not br.ok:
                with clock:
                    counts["batch_fail"] += 1
            # 暴力断连：SO_LINGER=0 → RST，服务器在事务活跃状态下走收尾 abort
            cli.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                struct.pack("ii", 1, 0))
            cli.sock.close()
            with clock:
                counts["cycles"] += 1
        except Exception:
            with clock:
                counts["batch_fail"] += 1
            time.sleep(0.05)


if __name__ == "__main__":
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60
    nthreads = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    ths = [threading.Thread(target=worker, args=(i,)) for i in range(nthreads)]
    t0 = time.time()
    for t in ths:
        t.start()
    while time.time() - t0 < seconds:
        time.sleep(2)
        with clock:
            c = dict(counts)
        print("  t=%4.0fs %s" % (time.time() - t0, c), flush=True)
    STOP = True
    for t in ths:
        t.join(timeout=10)
    print("FINAL:", counts)
