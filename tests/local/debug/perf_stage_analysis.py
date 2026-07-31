#!/usr/bin/env python3
"""R3 塌陷 / Delivery 尾延迟定量分析驱动器（07-30 PASS 后排名优化第一步）。

在已装载 W 库上重放 3×WINDOW 测量窗，同时采集四路证据：
  1. 每窗家族吞吐 + 家族×批次延迟分布（stress2 LAT 行）
  2. 旁路探针（独立连接，每 5s）：热点区 Delivery MIN 语句延迟曲线
     + 非热点区 MIN + 点查对照——若 MIN 曲线随时间上扬而点查平坦，即队首爬行定罪
  3. 服务端 mvcc-stats 序列（chains/deferred/delkeys/rss/vsz）按窗切分
  4. [anchor-seek]/[bpm-pressure]/[sweep-wm] 计数按窗切分
用法: python3 perf_stage_analysis.py [window_secs] [threads]   （默认 150 32）
前提: build/oj_gate_w50_db 已装载。
"""
import os, re, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, HERE)
from wirecli import Conn  # noqa: E402

W = int(os.environ.get('TPCC_W', '50'))
WINDOW = float(sys.argv[1]) if len(sys.argv) > 1 else 150
THREADS = int(sys.argv[2]) if len(sys.argv) > 2 else 32
DB = f'oj_gate_w{W}_db'
LOG = os.path.join(BUILD, DB + '.server.log')
DATA = os.path.join(BUILD, 'tpccbench_data', f'full_w{W}_seed42')
STRESS = os.path.join(HERE, 'wire_tpcc_stress2.py')

PROBES = [
    ('hot_min',  'select no_o_id from new_orders where no_w_id = 1 and no_d_id = 3 '
                 'order by no_o_id asc limit 1;'),
    ('cold_min', 'select no_o_id from new_orders where no_w_id = 25 and no_d_id = 5 '
                 'order by no_o_id asc limit 1;'),
    ('point_ctl', 'select c_balance from customer where c_w_id = 1 and c_d_id = 3 '
                  'and c_id = 1;'),
]

probe_rows = []   # (t_rel, name, ms)
stop_probe = False


def probe_loop(t0):
    c = None
    while not stop_probe:
        try:
            if c is None:
                c = Conn()
            for name, sql in PROBES:
                s = time.time()
                r = c.exec_stream(sql)
                ms = (time.time() - s) * 1000
                if r[0] == 'RESULT_END':
                    probe_rows.append((time.time() - t0, name, ms))
        except Exception:
            try:
                if c: c.close()
            except Exception:
                pass
            c = None
        time.sleep(5)


def start_server():
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    time.sleep(1)
    env = dict(os.environ, RMDB_MVCC_STATS='1')
    logf = open(LOG, 'w')
    subprocess.Popen([os.path.join(BUILD, 'bin', 'rmdb'), DB], cwd=BUILD,
                     stdout=logf, stderr=subprocess.STDOUT, env=env,
                     stdin=subprocess.DEVNULL, start_new_session=True)
    for _ in range(180):
        time.sleep(2)
        try:
            c = Conn()
            r = c.exec_stream('show tables;')
            c.close()
            if r[0] in ('RESULT_END', 'COMMAND_OK'):
                return
        except Exception:
            pass
    raise RuntimeError('server not ready')


def log_size():
    try:
        return os.path.getsize(LOG)
    except OSError:
        return 0


def parse_segment(seg):
    stats = {'anchor_seek': 0, 'bpm_pressure': 0}
    mvcc = []
    for ln in seg.splitlines():
        if '[anchor-seek]' in ln:
            m = re.search(r'count=(\d+)', ln)
            if m:
                stats['anchor_seek'] = max(stats['anchor_seek'], int(m.group(1)))
        elif 'bpm-pressure' in ln:
            stats['bpm_pressure'] += 1
        elif '[mvcc-stats]' in ln:
            kv = dict(re.findall(r'(\w+)=([\d.]+)', ln))
            mvcc.append(kv)
    return stats, mvcc


def probe_summary(t_lo, t_hi):
    out = {}
    for name, _sql in PROBES:
        xs = sorted(ms for t, n, ms in probe_rows if n == name and t_lo <= t < t_hi)
        if xs:
            out[name] = (xs[len(xs) // 2], xs[-1])   # (p50, max)
    return out


def main():
    print(f'== perf stage analysis: W={W} window={WINDOW:.0f}s x3 threads={THREADS} ==', flush=True)
    start_server()
    t0 = time.time()
    th = threading.Thread(target=probe_loop, args=(t0,), daemon=True)
    th.start()

    env = dict(os.environ, SKIP_BOOTSTRAP='1', HOTSPOT='2', TPCC_W=str(W), TPCC_DATA=DATA)
    marks = [log_size()]
    win_out = []
    win_bounds = []
    for win in (1, 2, 3):
        ws = time.time() - t0
        r = subprocess.run([sys.executable, '-u', STRESS, str(WINDOW), str(THREADS)],
                           env=env, capture_output=True, text=True, timeout=WINDOW + 120)
        win_bounds.append((ws, time.time() - t0))
        marks.append(log_size())
        keep = [ln for ln in r.stdout.splitlines()
                if ln.startswith(('FINAL:', 'LAT ')) or ln.strip().startswith(
                    ('neworder:', 'payment:', 'delivery:', 'orderstatus:', 'stocklevel:'))]
        win_out.append(keep)
        print(f'-- window{win} done --', flush=True)

    global stop_probe
    stop_probe = True
    time.sleep(1)

    with open(LOG, errors='replace') as f:
        log = f.read()

    print('\n================ 报告 ================')
    prev_seek = 0
    for win in (1, 2, 3):
        seg = log[marks[win - 1]:marks[win]]
        stats, mvcc = parse_segment(seg)
        print(f'\n--- window{win} ---')
        for ln in win_out[win - 1]:
            print(' ', ln)
        seek_delta = stats['anchor_seek'] - prev_seek if stats['anchor_seek'] else 0
        prev_seek = max(prev_seek, stats['anchor_seek'])
        print(f'  server: anchor-seek≈{stats["anchor_seek"]}(+{seek_delta}) '
              f'bpm-pressure={stats["bpm_pressure"]}')
        if mvcc:
            a, b = mvcc[0], mvcc[-1]
            keys = ('chains', 'vers', 'rwrites', 'delkeys', 'deferred', 'rss_mb', 'vsz_mb')
            print('  mvcc  first:', {k: a.get(k) for k in keys if k in a})
            print('  mvcc  last :', {k: b.get(k) for k in keys if k in b})
        lo, hi = win_bounds[win - 1]
        ps = probe_summary(lo, hi)
        for name in ('hot_min', 'cold_min', 'point_ctl'):
            if name in ps:
                print(f'  probe {name}: p50={ps[name][0]:.1f}ms max={ps[name][1]:.1f}ms')
        # 窗内前/后半段探针对比：上扬斜率
        mid = (lo + hi) / 2
        for name in ('hot_min', 'point_ctl'):
            fst = probe_summary(lo, mid).get(name)
            snd = probe_summary(mid, hi).get(name)
            if fst and snd:
                print(f'  probe {name} first-half p50={fst[0]:.1f}ms -> second-half p50={snd[0]:.1f}ms')

    subprocess.run(['pkill', '-x', 'rmdb'], check=False)


if __name__ == '__main__':
    main()
