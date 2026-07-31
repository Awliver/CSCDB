#!/usr/bin/env python3
"""丢删除静默期取证：120s stress → 静默 → set-diff 找坏 oid → SELECT 触发 ACCEPT
事件拿 rid → CHAINSTATE 链/堆/deferred 现场 → CHAINGONE 归因该 rid 被谁摘链。
用法: python3 lost_delete_quiesce.py [stress_secs]
"""
import os, re, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, HERE)
from wirecli import Conn  # noqa: E402

W = int(os.environ.get('AB_W', '10'))
SECS = sys.argv[1] if len(sys.argv) > 1 else '120'
DB = f'oj_gate_w{W}_db'
LOG = os.path.join(BUILD, DB + '.quiesce.log')


def start_server():
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    time.sleep(1)
    env = dict(os.environ, RMDB_RING='1')
    logf = open(LOG, 'w')
    subprocess.Popen([os.path.join(BUILD, 'bin', 'rmdb'), DB], cwd=BUILD,
                     stdout=logf, stderr=subprocess.STDOUT,
                     stdin=subprocess.DEVNULL, start_new_session=True, env=env)
    for _ in range(200):
        time.sleep(2)
        try:
            c = Conn(); c.exec_stream('show tables;'); c.close(); return
        except Exception:
            pass
    raise RuntimeError('server not ready')


def main():
    start_server()
    env = dict(os.environ, SKIP_BOOTSTRAP='1', HOTSPOT='2', TPCC_W=str(W))
    print(f'== stress {SECS}s ==', flush=True)
    subprocess.run([sys.executable, '-u', os.path.join(HERE, 'wire_tpcc_stress2.py'),
                    SECS, '32'], env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=float(SECS) + 240)
    time.sleep(5)   # 静默：残余事务/清扫线程沉降
    c = Conn()
    def rows(sql):
        r = c.exec_stream(sql)
        return r[3] if r[0] == 'RESULT_END' and len(r) > 3 else []
    bad = []   # (w,d,oid)
    for w in range(1, W + 1):
        for d in range(1, 11):
            q = set(x[0] for x in rows(f'select no_o_id from new_orders where no_w_id = {w} and no_d_id = {d};'))
            c0 = set(x[0] for x in rows(f'select o_id from orders where o_w_id = {w} and o_d_id = {d} and o_carrier_id = 0;'))
            for oid in sorted(q - c0):
                bad.append((w, d, oid))
    print(f'bad entries after quiesce: {len(bad)} -> {bad[:12]}', flush=True)
    if not bad:
        subprocess.run(['pkill', '-x', 'rmdb'], check=False)
        print('RESULT: CLEAN', flush=True)
        return
    # 触发 ACCEPT 事件拿 rid（每个坏项做一次点查）
    for w, d, oid in bad[:8]:
        rows(f'select no_o_id from new_orders where no_w_id = {w} and no_d_id = {d} and no_o_id = {oid};')
    c.exec_stream('RINGDUMP')
    time.sleep(2)
    with open(LOG, errors='replace') as f:
        lines = [ln for ln in f if ln.startswith('[ring]')]
    # 每个坏 oid：最后一次 ACCEPT 的 rid = 当前索引项指向的槽位
    for w, d, oid in bad[:8]:
        rid = None
        for ln in lines:
            if f'ACCEPT' in ln and re.search(rf'\ba={oid}\b', ln):
                m = re.search(r'rid=\((\d+),(\d+)\)', ln)
                if m:
                    rid = (int(m.group(1)), int(m.group(2)))
        print(f'--- bad oid={oid} (w{w} d{d}) rid={rid} ---', flush=True)
        if rid is None:
            continue
        c.exec_stream(f'CHAINSTATE new_orders {rid[0]} {rid[1]}')
        # 该 rid 的全部环事件（生命史+谁摘的链）
        for ln in lines:
            if f'rid=({rid[0]},{rid[1]})' in ln:
                print('  ', ln.rstrip(), flush=True)
    time.sleep(1)
    with open(LOG, errors='replace') as f:
        for ln in f:
            if ln.startswith('[chainstate]'):
                print(ln.rstrip(), flush=True)
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    print('RESULT: DIRTY', flush=True)


if __name__ == '__main__':
    main()
