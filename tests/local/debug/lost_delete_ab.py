#!/usr/bin/env python3
"""丢删除回归 A/B 归因驱动：已装载 W=50 门禁库上跑一窗 stress，按 district 差分
count(new_orders)-count(o_carrier_id=0)，只报告【新增】的不一致。

用法: RMDB_NO_MIN_EARLYSTOP=1 ... python3 lost_delete_ab.py [window_secs] [threads]
"""
import os, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, HERE)
from wirecli import Conn  # noqa: E402

W = int(os.environ.get("AB_W", "50"))
WINDOW = sys.argv[1] if len(sys.argv) > 1 else '150'
THREADS = sys.argv[2] if len(sys.argv) > 2 else '32'
DB = f'oj_gate_w{W}_db'


def start_server():
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    time.sleep(1)
    logf = open(os.path.join(BUILD, DB + '.ab.log'), 'w')
    subprocess.Popen([os.path.join(BUILD, 'bin', 'rmdb'), DB], cwd=BUILD,
                     stdout=logf, stderr=subprocess.STDOUT,
                     stdin=subprocess.DEVNULL, start_new_session=True,
                     env=dict(os.environ))
    for _ in range(200):
        time.sleep(2)
        try:
            c = Conn(); c.exec_stream('show tables;'); c.close(); return
        except Exception:
            pass
    raise RuntimeError('server not ready')


def sweep():
    c = Conn()
    def val(sql):
        r = c.exec_stream(sql)
        return r[3][0][0]
    m = {}
    for w in range(1, W + 1):
        for d in range(1, 11):
            n1 = val(f'select count(*) as n from new_orders where no_w_id = {w} and no_d_id = {d};')
            n2 = val(f'select count(*) as n from orders where o_w_id = {w} and o_d_id = {d} and o_carrier_id = 0;')
            if n1 != n2:
                m[(w, d)] = n1 - n2
    c.close()
    return m


def main():
    flags = {k: os.environ.get(k) for k in
             ('RMDB_NO_MIN_EARLYSTOP', 'RMDB_NO_SCAN_FASTPATH', 'RMDB_NO_UNPIN_FASTPATH')}
    print(f'== lost_delete A/B: window={WINDOW}s threads={THREADS} flags={flags} ==', flush=True)
    start_server()
    base = sweep()
    print(f'baseline mismatched districts: {len(base)}', flush=True)
    env = dict(os.environ, SKIP_BOOTSTRAP='1', HOTSPOT='2', TPCC_W=str(W))
    r = subprocess.run([sys.executable, '-u', os.path.join(HERE, 'wire_tpcc_stress2.py'),
                        WINDOW, THREADS], env=env, capture_output=True, text=True,
                       timeout=float(WINDOW) + 180)
    for ln in r.stdout.splitlines():
        if ln.startswith('FINAL:') or ln.strip().startswith(('neworder:', 'delivery:')):
            print(' ', ln, flush=True)
    after = sweep()
    new = {k: v for k, v in after.items() if base.get(k, 0) != v}
    print(f'after: mismatched={len(after)} NEW/changed={len(new)}: '
          f'{dict(list(new.items())[:20])}', flush=True)
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    print('AB_RESULT:', 'DIRTY' if new else 'CLEAN', flush=True)


if __name__ == '__main__':
    main()
