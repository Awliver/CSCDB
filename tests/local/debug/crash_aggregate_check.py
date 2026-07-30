#!/usr/bin/env python3
"""崩溃前后七项 FLOAT32 聚合位精确对比（对应 07-30 16:47 OJ 判负 SUM(s_ytd) 0 ULP）。

流程：起服务器（已装载库）→ stress2 SKIP_BOOTSTRAP churn → 静默 → 快照聚合位模式
→ kill -9 → 重启恢复 → 重读 → 逐项对比。可循环多轮。
用法: python3 crash_aggregate_check.py [churn_secs] [rounds] [threads]
前提: build/stress2_w10_db 已存在（wire_tpcc_stress2 bootstrap 过）。
"""
import os, sys, subprocess, time, struct, socket

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BUILD = os.path.join(REPO, "build")
DB = "stress2_w10_db"
LOG = os.path.join(BUILD, DB + ".server.log")
CHURN = float(sys.argv[1]) if len(sys.argv) > 1 else 90
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 2
THREADS = int(sys.argv[3]) if len(sys.argv) > 3 else 32
sys.path.insert(0, os.path.join(REPO, "tests", "local"))
from wire_client import WireClient, send_frame, recv_frame, TAG_EXEC_STREAM

AGGS = [
    ("SUM(w_ytd)",          "select sum(w_ytd) from warehouse;"),
    ("SUM(d_ytd)",          "select sum(d_ytd) from district;"),
    ("SUM(s_ytd)",          "select sum(s_ytd) from stock;"),
    ("SUM(c_ytd_payment)",  "select sum(c_ytd_payment) from customer;"),
    ("SUM(c_balance)",      "select sum(c_balance) from customer;"),
    ("SUM(h_amount)",       "select sum(h_amount) from history;"),
    ("SUM(ol_amount)",      "select sum(ol_amount) from order_line;"),
]
INTS = [
    ("COUNT(new_orders)",   "select count(*) from new_orders;"),
    ("COUNT(orders)",       "select count(*) from orders;"),
    ("COUNT(order_line)",   "select count(*) from order_line;"),
    ("SUM(s_order_cnt)",    "select sum(s_order_cnt) from stock;"),
]

def start_server():
    subprocess.run(["pkill", "-x", "rmdb"], check=False)
    time.sleep(1)
    subprocess.Popen(f"cd {BUILD} && setsid ./bin/rmdb {DB} </dev/null >> {LOG} 2>&1 &",
                     shell=True)
    # 测活：show tables 完整成功
    for _ in range(180):
        time.sleep(1)
        try:
            c = WireClient(timeout=10)
            send_frame(c.sock, TAG_EXEC_STREAM, b"show tables;")
            while True:
                tag, _f, _p = recv_frame(c.sock)
                if tag in (0x10, 0x11):
                    c.close()
                    return True
                if tag == 0x13:
                    break
            c.close()
        except (OSError, ConnectionError):
            continue
    return False

def raw_cells(sql):
    """返回首行首列的原始 payload 字节（float 取位模式）。"""
    c = WireClient(timeout=300)
    send_frame(c.sock, TAG_EXEC_STREAM, sql.encode())
    row = None
    while True:
        tag, _f, payload = recv_frame(c.sock)
        if tag == 0x02 and row is None:
            row = payload
        elif tag in (0x11, 0x10):
            break
        elif tag == 0x13:
            c.close()
            raise RuntimeError("ERROR: " + payload.decode(errors="replace"))
    c.close()
    return row

def snapshot():
    snap = {}
    for name, sql in AGGS + INTS:
        snap[name] = raw_cells(sql)
    return snap

def fmt(v):
    if v is None: return "None"
    return v.hex()

fails = 0
for rnd in range(1, ROUNDS + 1):
    print(f"=== round {rnd}: churn {CHURN}s x{THREADS} ===", flush=True)
    assert start_server(), "server not ready"
    env = dict(os.environ, TPCC_W="10", HOTSPOT="2", SKIP_BOOTSTRAP="1",
               TPCC_DATA=os.path.join(BUILD, "tpccbench_data", "full_w10_seed42"))
    r = subprocess.run([sys.executable, "-u",
                        os.path.join(REPO, "tests", "local", "debug", "wire_tpcc_stress2.py"),
                        str(CHURN), str(THREADS)], env=env, capture_output=True, text=True, timeout=CHURN+600)
    print("  churn tail:", r.stdout.strip().splitlines()[-6] if r.stdout else r.stderr[-200:], flush=True)
    time.sleep(3)   # 静默
    pre = snapshot()
    subprocess.run(["pkill", "-9", "-x", "rmdb"], check=False)
    time.sleep(2)
    t0 = time.time()
    assert start_server(), "server not ready after crash"
    print(f"  recovered in {time.time()-t0:.0f}s", flush=True)
    post = snapshot()
    for name, _sql in AGGS + INTS:
        a, b = pre[name], post[name]
        status = "OK " if a == b else "MISMATCH"
        if a != b:
            fails += 1
        print(f"  [{status}] {name}: pre={fmt(a)} post={fmt(b)}", flush=True)
    if fails:
        break

subprocess.run(["pkill", "-x", "rmdb"], check=False)
print("\nRESULT:", "FAIL" if fails else "PASS", f"(mismatches={fails})")
sys.exit(1 if fails else 0)
