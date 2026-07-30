#!/usr/bin/env python3
"""OJ Phase2+3 全流程本地门禁：一条命令在本地暴露 OJ 才会暴露的问题。

流程（对齐决赛评测管线；07-30 战役后全面加强，见 Docs/FinalCompetition/0730-OJ战役复盘.md）：
  1. 全新服务器（RLIMIT_AS 2GB 帽 + MALLOC_PERTURB_）+ wire 装载 W=50（≈OJ Phase2）
     ├ 装载预算 < 900s、装载后 VmSize < 帽 80%
     ├ 9 表 COUNT == CSV 行数；s_ytd 全 0、s_quantity∈[10,100]、o_ol_cnt∈[5,15]（Phase2 完整性）
     └ 装载后干净重启 → 行数保持（装载耐久性）
  2. 30s 预热 + 3×WINDOW 测量窗（32 客户端、HOTSPOT=2 确定性 160 槽轮盘、45/43/4/4/4）
  3. 每窗断言：无 [sql-error]、无 EXHAUSTED（帧耗尽=悬崖前兆）、进程存活、五家族均有提交；
     [pressure-abort]/[error-abort] 计数报告 + 阈值；fd 数窗前后增长 < 64
  4. 内存：VmSize 斜率（窗2→3 < SLOPE_MB_MAX）+ 后台采样峰值 RSS/VmSize < 帽 90%
  5. 静默一致性快照：七项 FLOAT32 聚合 + 四项计数（位精确）、账本（收紧容差）、关系不变量抽样
  6. kill -9 → 恢复 < 90s → 快照逐项位对比（OJ 崩后 0 ULP 同款）+ 账本 + 关系不变量
  7. churn 进行中 kill -9（在飞事务 undo 路径）→ 恢复 → 关系不变量 + 复写小压测
  8. 二次静默 kill -9 → 恢复幂等（计数/聚合位保持）

环境变量：
  OJ_GATE_W=50            仓数（数据集须已生成 build/tpccbench_data/full_w{W}_seed42）
  OJ_GATE_THREADS=32      客户端数
  OJ_GATE_WINDOW=150      每窗秒数（调小加快迭代,但斜率断言相应变松）
  OJ_GATE_SKIP_LOAD=1     复用已装载库（省 ~6min;正式验收前必须跑一次全新装载）
  OJ_GATE_SLOPE_MB=40     VmSize 斜率上限
  OJ_GATE_AS_MB=2048      服务器 RLIMIT_AS 帽（MB）
  OJ_GATE_MIDCRASH=0      跳过第 7/8 步（默认开启）
用法： python3 tests/local/oj_gate.py
"""
import os
import random
import signal
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, os.path.join(HERE, 'debug'))
from wirecli import Conn  # noqa: E402

W = int(os.environ.get('OJ_GATE_W', '50'))
THREADS = int(os.environ.get('OJ_GATE_THREADS', '32'))
WINDOW = int(os.environ.get('OJ_GATE_WINDOW', '150'))
SLOPE_MB_MAX = float(os.environ.get('OJ_GATE_SLOPE_MB', '40'))
AS_CAP_MB = int(os.environ.get('OJ_GATE_AS_MB', '2048'))
MIDCRASH = os.environ.get('OJ_GATE_MIDCRASH', '1') != '0'
DB = f'oj_gate_w{W}_db'
DBPATH = os.path.join(BUILD, DB)
LOG = os.path.join(BUILD, DB + '.server.log')
DATA = os.path.join(BUILD, 'tpccbench_data', f'full_w{W}_seed42')
STRESS = os.path.join(HERE, 'debug', 'wire_tpcc_stress2.py')
LOAD_BUDGET_S = 900          # OJ 装载 SQL 预算
ERROR_ABORT_MAX_PER_WIN = 200  # 降级中止的每窗阈值：偶发可容忍，大量=有实际缺陷

TABLES = ('warehouse', 'district', 'customer', 'history', 'new_orders',
          'orders', 'order_line', 'item', 'stock')
# 崩前/崩后位精确对比集（OJ 崩后 44 项聚合的本地缩影；server-vs-server 全部要求位等）
AGG_SQLS = [
    ('SUM(w_ytd)', 'select sum(w_ytd) as v from warehouse;'),
    ('SUM(d_ytd)', 'select sum(d_ytd) as v from district;'),
    ('SUM(s_ytd)', 'select sum(s_ytd) as v from stock;'),
    ('SUM(c_ytd_payment)', 'select sum(c_ytd_payment) as v from customer;'),
    ('SUM(c_balance)', 'select sum(c_balance) as v from customer;'),
    ('SUM(h_amount)', 'select sum(h_amount) as v from history;'),
    ('SUM(ol_amount)', 'select sum(ol_amount) as v from order_line;'),
    ('COUNT(new_orders)', 'select count(*) as v from new_orders;'),
    ('COUNT(orders)', 'select count(*) as v from orders;'),
    ('COUNT(order_line)', 'select count(*) as v from order_line;'),
    ('SUM(s_order_cnt)', 'select sum(s_order_cnt) as v from stock;'),
]

failures = []


def check(name, ok, detail=''):
    print(f"  [{'PASS' if ok else 'FAIL'}] {name} {detail}", flush=True)
    if not ok:
        failures.append(f'{name} {detail}')


def proc_status_mb(pid, key):
    try:
        with open(f'/proc/{pid}/status') as f:
            for line in f:
                if line.startswith(key + ':'):
                    return int(line.split()[1]) // 1024
    except OSError:
        return None
    return None


def vmsize_mb(pid):
    return proc_status_mb(pid, 'VmSize')


def fd_count(pid):
    try:
        return len(os.listdir(f'/proc/{pid}/fd'))
    except OSError:
        return -1


def du_gb(path):
    try:
        out = subprocess.run(['du', '-sb', path], capture_output=True, text=True)
        return int(out.stdout.split()[0]) / (1 << 30)
    except Exception:
        return -1.0


class PeakSampler:
    """后台每秒采样 VmRSS/VmSize 峰值（评测的 Peak RSS 口径近似）。"""

    def __init__(self, pid):
        self.pid = pid
        self.rss = 0
        self.vsz = 0
        self._stop = False
        self._t = threading.Thread(target=self._run, daemon=True)
        self._t.start()

    def _run(self):
        while not self._stop:
            r = proc_status_mb(self.pid, 'VmRSS')
            v = proc_status_mb(self.pid, 'VmSize')
            if r:
                self.rss = max(self.rss, r)
            if v:
                self.vsz = max(self.vsz, v)
            time.sleep(1)

    def stop(self):
        self._stop = True


def start_server(fresh):
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    time.sleep(1)
    if fresh:
        subprocess.run(['rm', '-rf', DBPATH], check=False)
        os.makedirs(DBPATH, exist_ok=True)
    env = dict(os.environ, RMDB_MVCC_STATS='1', MALLOC_PERTURB_='165')
    logf = open(LOG, 'w')   # 每次启动截断：错误计数只统计本轮

    def limits():
        import resource
        cap = AS_CAP_MB * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (cap, cap))
        resource.setrlimit(resource.RLIMIT_CORE,
                           (resource.RLIM_INFINITY, resource.RLIM_INFINITY))
        os.setsid()

    proc = subprocess.Popen([os.path.join(BUILD, 'bin', 'rmdb'), DB], cwd=BUILD,
                            stdout=logf, stderr=subprocess.STDOUT, env=env,
                            stdin=subprocess.DEVNULL, preexec_fn=limits)
    # 就绪探测：端口可连 + show tables 成功（OJ 的就绪定义）
    t0 = time.time()
    while time.time() - t0 < 600:
        if proc.poll() is not None:
            raise RuntimeError('server died during startup/recovery')
        try:
            c = Conn()
            r = c.exec_stream('show tables;')
            c.close()
            if r[0] in ('RESULT_END', 'COMMAND_OK'):
                return proc, time.time() - t0
        except Exception:
            time.sleep(2)
    raise RuntimeError('server not ready in 600s')


def wire_query(c, sql):
    r = c.exec_stream(sql)
    assert r[0] == 'RESULT_END', (sql, r)
    return r[3]


def cell(v):
    return v[1] if isinstance(v, tuple) else v


def scalar(c, sql):
    return cell(wire_query(c, sql)[0][0])


def log_count(pattern):
    try:
        out = subprocess.run(['grep', '-ac', pattern, LOG],
                             capture_output=True, text=True)
        return int(out.stdout.strip() or 0)
    except Exception:
        return -1


def sql_error_count():
    return log_count(r'\[sql-error\]')


def agg_snapshot(c):
    """七项 FLOAT32 聚合 + 四项计数。float32→python float 无损，== 即位精确比较。"""
    return {name: scalar(c, sql) for name, sql in AGG_SQLS}


def compare_snapshots(pre, post, tag):
    for name in pre:
        a, b = pre[name], post[name]
        check(f'{tag} {name} bit-exact', a == b, f'({a} vs {b})' if a != b else f'({a})')


def relational_invariants(c, tag, rng):
    """跨表同事务不变量：对任意已提交前缀成立，churn 中途崩溃后也必须成立。"""
    no_cnt = scalar(c, 'select count(*) as v from new_orders;')
    carrier0 = scalar(c, 'select count(*) as v from orders where o_carrier_id = 0;')
    check(f'{tag} count(o_carrier_id=0)==count(new_orders)', carrier0 == no_cnt,
          f'({carrier0} vs {no_cnt})')
    # 注：d_next_o_id-1==max(o_id) 是 OJ 语义，但本地 datagen 的 o_id 编号与
    # d_next_o_id 不同源，本地不成立，勿加（07-30 实测会误报）。
    for _ in range(3):
        w = rng.randint(1, W)
        d = rng.randint(1, 10)
        # 抽样最新订单：o_ol_cnt == 明细行数（NewOrder 原子性——插单与明细同事务）
        mx = scalar(c, f'select max(o_id) as v from orders where o_w_id={w} and o_d_id={d};')
        cnt = scalar(c, f'select o_ol_cnt from orders where o_w_id={w} and o_d_id={d} and o_id={mx};')
        ol = scalar(c, f'select count(*) as v from order_line '
                       f'where ol_w_id={w} and ol_d_id={d} and ol_o_id={mx};')
        check(f'{tag} o_ol_cnt==count(order_line) w={w} d={d} o={mx}', cnt == ol,
              f'({cnt} vs {ol})')
        # 队首未配送单必须存在于 orders 且 carrier==0（Delivery 原子性：删队列+置 carrier 同事务）
        mn = scalar(c, f'select min(no_o_id) as v from new_orders '
                       f'where no_w_id={w} and no_d_id={d};')
        if mn is not None:
            hit = scalar(c, f'select count(*) as v from orders '
                            f'where o_w_id={w} and o_d_id={d} and o_id={mn};')
            car = scalar(c, f'select o_carrier_id from orders '
                            f'where o_w_id={w} and o_d_id={d} and o_id={mn};')
            check(f'{tag} queue-head order exists & undelivered w={w} d={d}',
                  hit == 1 and car == 0, f'(hit={hit} carrier={car})')


def run_stress(seconds, tag):
    env = dict(os.environ, SKIP_BOOTSTRAP='1', HOTSPOT='2', TPCC_W=str(W),
               TPCC_DATA=DATA)
    out = subprocess.run([sys.executable, '-u', STRESS, str(seconds), str(THREADS)],
                         env=env, capture_output=True, text=True,
                         timeout=seconds + 120)
    lines = out.stdout.strip().splitlines()
    fam = {}
    agg = None
    for ln in lines:
        ln = ln.strip()
        if ln.startswith('FINAL:'):
            agg = ln
        for f in ('neworder', 'payment', 'delivery', 'orderstatus', 'stocklevel'):
            if ln.startswith(f + ':'):
                fam[f] = ln
    print(f'  [{tag}] {agg}')
    for f, ln in fam.items():
        print(f'    {ln}')
    return agg, fam, '\n'.join(lines[-6:])


def ledger_checks(c, tag, warehouses):
    """W_YTD == sum(D_YTD)（Payment 每笔同事务更新两处）。
    两侧舍入路径不同（逐笔 f32 累加 vs 一次求和），不能位等；
    容差收紧为相对 1e-4（07-30 前是 1%，过松）。"""
    for w in warehouses:
        wy = scalar(c, f'select w_ytd from warehouse where w_id = {w};')
        dy = scalar(c, f'select sum(d_ytd) as s from district where d_w_id = {w};')
        ok = abs(wy - dy) <= max(1e-4 * abs(wy), 1.0)
        check(f'{tag} ledger w_ytd==sum(d_ytd) w={w}', ok, f'({wy:.2f} vs {dy:.2f})')


def family_gate(fam, tag):
    for f in ('neworder', 'payment', 'delivery', 'orderstatus', 'stocklevel'):
        ln = fam.get(f, '')
        ok_n = 0
        for part in ln.split():
            if part.startswith('ok='):
                ok_n = int(part[3:])
        check(f'{tag} family {f} committed>0', ok_n > 0, f'({ln.strip()})')


def table_counts(c):
    return {t: scalar(c, f'select count(*) as v from {t};') for t in TABLES}


def csv_rows(table):
    path = os.path.join(DATA, table + '.csv')
    try:
        out = subprocess.run(['wc', '-l', path], capture_output=True, text=True)
        return int(out.stdout.split()[0]) - 1   # 减表头
    except Exception:
        return -1


def load_integrity_checks(c):
    """Phase2 装载完整性抽样（对齐 OJ 公开校验语义）。"""
    for t in TABLES:
        n = scalar(c, f'select count(*) as v from {t};')
        exp = csv_rows(t)
        check(f'load count({t})==csv', n == exp, f'({n} vs {exp})')
    check('load sum(s_ytd)==0', scalar(c, 'select sum(s_ytd) as v from stock;') == 0.0)
    qmin = scalar(c, 'select min(s_quantity) as v from stock;')
    qmax = scalar(c, 'select max(s_quantity) as v from stock;')
    check('load s_quantity in [10,100]', 10 <= qmin and qmax <= 100, f'({qmin}..{qmax})')
    omin = scalar(c, 'select min(o_ol_cnt) as v from orders;')
    omax = scalar(c, 'select max(o_ol_cnt) as v from orders;')
    check('load o_ol_cnt in [5,15]', 5 <= omin and omax <= 15, f'({omin}..{omax})')
    no_cnt = scalar(c, 'select count(*) as v from new_orders;')
    carrier0 = scalar(c, 'select count(*) as v from orders where o_carrier_id = 0;')
    check('load count(o_carrier_id=0)==count(new_orders)', carrier0 == no_cnt,
          f'({carrier0} vs {no_cnt})')


def resource_checks(tag, sampler):
    exhausted = log_count('EXHAUSTED')
    check(f'{tag} no frame-wait EXHAUSTED', exhausted == 0, f'(count={exhausted})')
    pa = log_count(r'\[pressure-abort\]')
    ea = log_count(r'\[error-abort\]')
    if pa or ea:
        print(f'  [WARN] {tag} degraded aborts so far: pressure={pa} error={ea}', flush=True)
    check(f'{tag} error-abort under threshold', ea <= ERROR_ABORT_MAX_PER_WIN * 3,
          f'(count={ea})')
    if sampler is not None:
        check(f'{tag} peak VmSize < {int(AS_CAP_MB * 0.9)}MB', sampler.vsz < AS_CAP_MB * 0.9,
              f'(peak vsz={sampler.vsz}MB rss={sampler.rss}MB)')


def main():
    fresh = os.environ.get('OJ_GATE_SKIP_LOAD') != '1'
    print(f'== OJ gate: W={W} threads={THREADS} window={WINDOW}s fresh_load={fresh} '
          f'as_cap={AS_CAP_MB}MB midcrash={MIDCRASH} ==', flush=True)
    if fresh and not os.path.isdir(DATA):
        print(f'dataset missing: {DATA}\n  generate: python3 -c "import sys; '
              f"sys.path.insert(0,'bench'); from tpccbench.datagen import generate; "
              f"generate('{DATA}', warehouses={W}, seed=42)\"")
        sys.exit(2)

    proc, ready_s = start_server(fresh)
    print(f'server up pid={proc.pid} ready in {ready_s:.0f}s', flush=True)

    # ---- Phase2: 装载 + 完整性 + 装载耐久性 ----
    if fresh:
        t0 = time.time()
        env = dict(os.environ, TPCC_DATA=DATA, TPCC_W=str(W))
        r = subprocess.run([sys.executable, '-c',
                            f"import sys; sys.path.insert(0, r'{os.path.join(HERE, 'debug')}');"
                            "import wire_tpcc_stress2 as s; s.bootstrap()"],
                           env=env, capture_output=True, text=True, timeout=1800)
        print(r.stdout.strip()[-200:])
        load_s = time.time() - t0
        check('phase2 load ok', r.returncode == 0, f'({load_s:.0f}s)')
        check(f'phase2 load < {LOAD_BUDGET_S}s', load_s < LOAD_BUDGET_S, f'({load_s:.0f}s)')
        vm = vmsize_mb(proc.pid)
        check(f'post-load VmSize < {int(AS_CAP_MB * 0.8)}MB',
              vm is not None and vm < AS_CAP_MB * 0.8, f'({vm}MB)')
        c = Conn()
        load_integrity_checks(c)
        counts0 = table_counts(c)
        c.close()
        # 装载耐久性：装载后立即 kill -9 重启，行数必须不变（LOAD 检查点 + 恢复正确性；
        # W=50 曾因日志 IO 32 位截断在此丢数 GB 日志——用崩溃重启而非干净退出，更严）
        os.kill(proc.pid, signal.SIGKILL)
        proc.wait()
        proc, ready_s = start_server(fresh=False)
        check('post-load restart ready < 90s', ready_s < 90, f'({ready_s:.0f}s)')
        c = Conn()
        counts1 = table_counts(c)
        c.close()
        for t in TABLES:
            check(f'post-load restart count({t}) kept', counts0[t] == counts1[t],
                  f'({counts0[t]} -> {counts1[t]})')

    sampler = PeakSampler(proc.pid)

    # ---- Phase3: 预热 + 3×WINDOW 窗口 ----
    fd0 = fd_count(proc.pid)
    run_stress(30, 'warmup')
    check('warmup no sql-error', sql_error_count() == 0, f'(count={sql_error_count()})')
    vm_marks = []
    for win in (1, 2, 3):
        vm0 = vmsize_mb(proc.pid)
        agg, fam, tail = run_stress(WINDOW, f'window{win}')
        vm1 = vmsize_mb(proc.pid)
        vm_marks.append((vm0, vm1))
        alive = proc.poll() is None
        check(f'window{win} server alive', alive)
        if not alive:
            break
        check(f'window{win} no sql-error', sql_error_count() == 0,
              f'(count={sql_error_count()})')
        check(f'window{win} no transport fail', agg is not None and "'transport': 0" in agg,
              f'({agg})')
        family_gate(fam, f'window{win}')
        resource_checks(f'window{win}', sampler)
        print(f'  window{win} VmSize {vm0} -> {vm1} MB', flush=True)

    if proc.poll() is None and len(vm_marks) == 3:
        slope = vm_marks[2][1] - vm_marks[1][1]
        check('VmSize slope window2->3', slope < SLOPE_MB_MAX, f'({slope:+d}MB)')
        fd1 = fd_count(proc.pid)
        check('fd count stable', fd0 < 0 or fd1 < 0 or fd1 - fd0 < 64, f'({fd0} -> {fd1})')
        disk = du_gb(DBPATH)
        check('db disk < 25GB', 0 <= disk < 25, f'({disk:.1f}GB)')

    # ---- 静默一致性快照 + 崩溃恢复（位精确） ----
    rng = random.Random(7)
    pre_agg = None
    if proc.poll() is None:
        time.sleep(3)   # 静默
        ws = sorted(rng.sample(range(1, W + 1), min(5, W)))
        c = Conn()
        ledger_checks(c, 'pre-crash', ws)
        relational_invariants(c, 'pre-crash', rng)
        pre_agg = agg_snapshot(c)
        pre_counts = table_counts(c)
        c.close()

        os.kill(proc.pid, signal.SIGKILL)
        proc.wait()
        sampler.stop()
        print('  killed -9, restarting...', flush=True)
        proc, ready_s = start_server(fresh=False)
        check('post-crash ready < 90s', ready_s < 90, f'({ready_s:.0f}s)')
        c = Conn()
        compare_snapshots(pre_agg, agg_snapshot(c), 'post-crash')
        ledger_checks(c, 'post-crash', ws)
        relational_invariants(c, 'post-crash', rng)
        for t in TABLES:
            n2 = scalar(c, f'select count(*) as v from {t};')
            check(f'post-crash count({t}) kept', n2 == pre_counts[t],
                  f'({pre_counts[t]} -> {n2})')
        c.close()

    # ---- churn 中途 kill -9（在飞事务 undo 路径）+ 二次恢复幂等 ----
    if MIDCRASH and proc.poll() is None:
        print('  mid-churn crash: stress up, killing server mid-flight...', flush=True)
        env = dict(os.environ, SKIP_BOOTSTRAP='1', HOTSPOT='2', TPCC_W=str(W),
                   TPCC_DATA=DATA)
        stress = subprocess.Popen([sys.executable, '-u', STRESS, '60', str(THREADS)],
                                  env=env, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
        time.sleep(20)                       # 让在飞事务堆起来
        os.kill(proc.pid, signal.SIGKILL)    # 服务器先死，客户端全部在飞
        proc.wait()
        time.sleep(2)
        stress.terminate()
        try:
            stress.wait(timeout=30)
        except subprocess.TimeoutExpired:
            stress.kill()
        proc, ready_s = start_server(fresh=False)
        check('mid-churn crash ready < 90s', ready_s < 90, f'({ready_s:.0f}s)')
        c = Conn()
        relational_invariants(c, 'mid-churn-crash', rng)
        c.close()
        # 恢复后必须还能写：小压测五家族过一遍
        agg, fam, _ = run_stress(30, 'post-recovery-write')
        family_gate(fam, 'post-recovery')
        # 二次静默崩溃：恢复幂等（无新写入时聚合/计数位保持）
        time.sleep(3)
        c = Conn()
        pre2 = agg_snapshot(c)
        c.close()
        os.kill(proc.pid, signal.SIGKILL)
        proc.wait()
        proc, ready_s = start_server(fresh=False)
        check('second crash ready < 90s', ready_s < 90, f'({ready_s:.0f}s)')
        c = Conn()
        compare_snapshots(pre2, agg_snapshot(c), 'second-crash')
        c.close()

    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    print('\n== RESULT:', 'PASS' if not failures else f'FAIL ({len(failures)})', '==')
    for f in failures:
        print('  FAIL:', f)
    sys.exit(0 if not failures else 1)


if __name__ == '__main__':
    main()
