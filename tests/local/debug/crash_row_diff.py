#!/usr/bin/env python3
"""s_ytd 崩溃漂移逐行定位器。

churn → 静默 → 全量快照 stock (s_w_id,s_i_id,s_ytd位模式,s_quantity,s_order_cnt,s_remote_cnt)
→ 保存崩溃镜像 → kill -9 → 恢复 → 重扫 → 逐行 diff → 打印漂移行的前后完整值。
用法: python3 crash_row_diff.py [churn_secs] [threads]
"""
import os, sys, subprocess, time, struct

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BUILD = os.path.join(REPO, "build")
DB = "stress2_w10_db"
DBDIR = os.path.join(BUILD, DB)
IMG = os.path.join(BUILD, DB + ".crashimg")
LOG = os.path.join(BUILD, DB + ".server.log")
CHURN = float(sys.argv[1]) if len(sys.argv) > 1 else 90
THREADS = int(sys.argv[2]) if len(sys.argv) > 2 else 32
sys.path.insert(0, os.path.join(REPO, "tests", "local"))
from wire_client import WireClient, send_frame, recv_frame, TAG_EXEC_STREAM

def start_server():
    subprocess.run(["pkill", "-x", "rmdb"], check=False)
    time.sleep(1)
    subprocess.Popen(f"cd {BUILD} && setsid ./bin/rmdb {DB} </dev/null >> {LOG} 2>&1 &", shell=True)
    for _ in range(180):
        time.sleep(1)
        try:
            c = WireClient(timeout=10)
            send_frame(c.sock, TAG_EXEC_STREAM, b"show tables;")
            while True:
                tag, _f, _p = recv_frame(c.sock)
                if tag in (0x10, 0x11):
                    c.close(); return True
                if tag == 0x13:
                    break
            c.close()
        except (OSError, ConnectionError):
            continue
    return False

def scan_stock():
    """全表扫 stock，返回 {(w,i): (ytd位模式hex, qty, ocnt, rcnt)}。"""
    c = WireClient(timeout=600)
    sql = b"select s_w_id, s_i_id, s_ytd, s_quantity, s_order_cnt, s_remote_cnt from stock;"
    send_frame(c.sock, TAG_EXEC_STREAM, sql)
    rows = {}
    while True:
        tag, _f, p = recv_frame(c.sock)
        if tag == 0x02:
            off = 0
            vals = []
            for k in range(6):
                assert p[off] == 1; off += 1
                vals.append(p[off:off+4]); off += 4
            w = struct.unpack(">i", vals[0])[0]
            i = struct.unpack(">i", vals[1])[0]
            rows[(w, i)] = (vals[2].hex(),
                            struct.unpack(">i", vals[3])[0],
                            struct.unpack(">i", vals[4])[0],
                            struct.unpack(">i", vals[5])[0])
        elif tag in (0x11, 0x10):
            break
        elif tag == 0x13:
            raise RuntimeError("scan ERROR: " + p.decode(errors="replace"))
    c.close()
    return rows

print(f"=== churn {CHURN}s x{THREADS} ===", flush=True)
assert start_server()
env = dict(os.environ, TPCC_W="10", HOTSPOT="2", SKIP_BOOTSTRAP="1",
           TPCC_DATA=os.path.join(BUILD, "tpccbench_data", "full_w10_seed42"))
r = subprocess.run([sys.executable, "-u",
                    os.path.join(REPO, "tests", "local", "debug", "wire_tpcc_stress2.py"),
                    str(CHURN), str(THREADS)], env=env, capture_output=True, text=True, timeout=CHURN+600)
print("churn:", (r.stdout.strip().splitlines()[-6:][0] if r.stdout else r.stderr[-200:]), flush=True)
time.sleep(3)
print("pre-crash full stock scan...", flush=True)
pre = scan_stock()
print(f"  {len(pre)} rows", flush=True)

subprocess.run(["pkill", "-9", "-x", "rmdb"], check=False)
time.sleep(2)
subprocess.run(["rm", "-rf", IMG], check=False)
subprocess.run(["cp", "-a", DBDIR, IMG], check=True)
print("crash image saved:", IMG, flush=True)

assert start_server(), "recovery failed"
print("post-crash full stock scan...", flush=True)
post = scan_stock()

diffs = []
for k, v in pre.items():
    pv = post.get(k)
    if pv != v:
        diffs.append((k, v, pv))
print(f"\n=== diff rows: {len(diffs)} ===")
for k, v, pv in diffs[:20]:
    def dec(t):
        if t is None: return "MISSING"
        f = struct.unpack(">f", bytes.fromhex(t[0]))[0]
        return f"ytd={t[0]}({f}) qty={t[1]} ocnt={t[2]} rcnt={t[3]}"
    print(f"  stock(w={k[0]}, i={k[1]}):\n    pre : {dec(v)}\n    post: {dec(pv)}")
subprocess.run(["pkill", "-x", "rmdb"], check=False)
sys.exit(1 if diffs else 0)
