#!/usr/bin/env python3
"""丢删除现行犯抓捕：RMDB_RING=1 起服 → 后台 stress → 3s 一次热点区差分核账，
一发现新 bad oid 立刻 RINGDUMP + 停止，打印该 oid/rid 的事件生命线。

用法: [归因 env] python3 lost_delete_watch.py [max_secs] [threads]
默认在 oj_gate_w10_db 上（AB_W 可改）。
"""
import os, re, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, HERE)
from wirecli import Conn  # noqa: E402

W = int(os.environ.get('AB_W', '10'))
MAXS = float(sys.argv[1]) if len(sys.argv) > 1 else 600
THREADS = sys.argv[2] if len(sys.argv) > 2 else '32'
DB = f'oj_gate_w{W}_db'
LOG = os.path.join(BUILD, DB + '.watch.log')
# 热点区（W=10 时 4 仓热点 d: 1->3 2->7 3->5 4->9），加两普通区
SUSPECTS = [(1, 3), (2, 7), (3, 5), (4, 9), (5, 1), (6, 5)]


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


def snap(c):
    out = {}
    for w, d in SUSPECTS:
        r = c.exec_stream(f'select no_o_id from new_orders where no_w_id = {w} and no_d_id = {d};')
        q = set(x[0] for x in (r[3] or []))
        r = c.exec_stream(f'select o_id from orders where o_w_id = {w} and o_d_id = {d} and o_carrier_id = 0;')
        c0 = set(x[0] for x in (r[3] or []))
        out[(w, d)] = q - c0            # 队列有、未配送集没有 = 坏项候选（含在飞误差）
    return out


def main():
    print(f'== watch: W={W} max={MAXS:.0f}s threads={THREADS} flags='
          f'{ {k: os.environ.get(k) for k in ("RMDB_NO_MIN_EARLYSTOP", "RMDB_NO_SCAN_FASTPATH", "RMDB_NO_UNPIN_FASTPATH")} } ==', flush=True)
    start_server()
    probe = Conn()
    # 三次采样并集作基线：索引扫描在并发下偶发漏行，单次基线漏掉的陈旧 extra
    # 会被后续探针误报为"新增"（上一轮 405067 即此假阳性形态）
    base = snap(probe)
    for _ in range(2):
        time.sleep(2)
        for k, v in snap(probe).items():
            base.setdefault(k, set()).update(v)
    print('baseline extras:', {k: len(v) for k, v in base.items() if v}, flush=True)

    env = dict(os.environ, SKIP_BOOTSTRAP='1', HOTSPOT='2', TPCC_W=str(W))
    stress = subprocess.Popen([sys.executable, '-u', os.path.join(HERE, 'wire_tpcc_stress2.py'),
                               str(int(MAXS)), THREADS], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    caught = None
    while time.time() - t0 < MAXS and stress.poll() is None:
        time.sleep(3)
        try:
            cur = snap(probe)
        except Exception:
            probe = Conn(); continue
        sus = {}
        for k, v in cur.items():
            new = v - base.get(k, set())
            if new:
                sus[k] = new
        if not sus:
            continue
        # 在飞误差豁免：同一 oid 连续两次出现才算（配送中的瞬时态 3s 内会自愈）
        time.sleep(3)
        cur2 = snap(probe)
        confirmed = {}
        for k, new in sus.items():
            still = new & cur2.get(k, set())
            if still:
                confirmed[k] = still
        if confirmed:
            caught = confirmed
            print(f'!! CAUGHT at t={time.time()-t0:.0f}s: {confirmed}', flush=True)
            probe.exec_stream('RINGDUMP')
            break
        # 未确认：把瞬时态并入基线避免反复告警
        for k, new in sus.items():
            base.setdefault(k, set()).update(new)
    stress.terminate()
    try:
        stress.wait(timeout=30)
    except Exception:
        stress.kill()
    if not caught:
        print('no incident within window', flush=True)
        subprocess.run(['pkill', '-x', 'rmdb'], check=False)
        return

    # 分析 ring dump：oid→rid 映射（TOMBSTONE/IXINS/ACCEPT 事件），随后 CHAINSTATE 现场取证
    time.sleep(2)
    with open(LOG, errors='replace') as f:
        lines = [ln for ln in f if ln.startswith('[ring]')]
    print(f'ring lines: {len(lines)}', flush=True)
    all_oids = sorted({o for v in caught.values() for o in v})
    probed = 0
    for oid in all_oids:
        rids = set()
        evs = []
        for ln in lines:
            if re.search(rf'\ba={oid}\b', ln):
                evs.append(ln.rstrip())
                m = re.search(r'rid=\((\d+),(\d+)\)', ln)
                if m and m.group(0) != 'rid=(0,0)':
                    rids.add((int(m.group(1)), int(m.group(2))))
        if not rids:
            continue
        probed += 1
        if probed > 6:
            break
        print(f'--- oid={oid} events={len(evs)} ---', flush=True)
        for e in evs[-6:]:
            print('  ', e, flush=True)
        for pg, sl in rids:
            for ln in lines:
                if f'rid=({pg},{sl})' in ln and f'a={oid}' not in ln:
                    print('  *', ln.rstrip(), flush=True)
            probe.exec_stream(f'CHAINSTATE new_orders {pg} {sl}')
    print(f'oid->rid mapped for {probed}/{len(all_oids)}', flush=True)
    time.sleep(1)
    # 服务器 stderr 里的 [chainstate] 行回捞
    with open(LOG, errors='replace') as f:
        for ln in f:
            if ln.startswith('[chainstate]'):
                print(ln.rstrip(), flush=True)
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)


if __name__ == '__main__':
    main()
