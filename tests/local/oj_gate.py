#!/usr/bin/env python3
"""OJ Phase2+3 全流程本地门禁：一条命令在本地暴露 OJ 才会暴露的问题。

流程（对齐决赛评测管线）：
  1. 全新服务器（RLIMIT_AS 2GB 帽 + MALLOC_PERTURB_）+ wire 装载 W=50（≈OJ Phase2）
  2. 30s 预热 + 3×150s 测量窗（32 客户端、HOTSPOT=2 确定性 160 槽轮盘、45/43/4/4/4）
  3. 每窗断言：无 [sql-error]、进程存活、五家族均有成功提交、Delivery 实际处理过订单
  4. 内存斜率断言：窗 2→3 的 VmSize 增长 < SLOPE_MB_MAX（默认 40MB）——
     本地吞吐低撞不到帽，斜率>0 就是泄漏，比"等 bad_alloc"灵敏数倍
  5. 一致性账本：W_YTD == sum(D_YTD)（Payment 原子性）、orders/new_orders 计数关系
  6. kill -9 崩溃 + 恢复：就绪时间 < 90s（OJ 预算）、已提交数据不丢、账本仍平

环境变量：
  OJ_GATE_W=50            仓数（数据集须已生成 build/tpccbench_data/full_w{W}_seed42）
  OJ_GATE_THREADS=32      客户端数
  OJ_GATE_WINDOW=150      每窗秒数（调小加快迭代,但斜率断言相应变松）
  OJ_GATE_SKIP_LOAD=1     复用已装载库（省 ~6min;正式验收前必须跑一次全新装载）
  OJ_GATE_SLOPE_MB=40     斜率上限
用法： python3 tests/local/oj_gate.py
"""
import os
import random
import signal
import struct
import subprocess
import sys
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
DB = f'oj_gate_w{W}_db'
DBPATH = os.path.join(BUILD, DB)
LOG = os.path.join(BUILD, DB + '.server.log')
DATA = os.path.join(BUILD, 'tpccbench_data', f'full_w{W}_seed42')
STRESS = os.path.join(HERE, 'debug', 'wire_tpcc_stress2.py')

failures = []


def check(name, ok, detail=''):
    print(f"  [{'PASS' if ok else 'FAIL'}] {name} {detail}", flush=True)
    if not ok:
        failures.append(f'{name} {detail}')


def vmsize_mb(pid):
    try:
        with open(f'/proc/{pid}/status') as f:
            for line in f:
                if line.startswith('VmSize:'):
                    return int(line.split()[1]) // 1024
    except OSError:
        return None
    return None


def start_server(fresh):
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    time.sleep(1)
    if fresh:
        subprocess.run(['rm', '-rf', DBPATH], check=False)
        os.makedirs(DBPATH, exist_ok=True)
    env = dict(os.environ, RMDB_MVCC_STATS='1', MALLOC_PERTURB_='165')
    logf = open(LOG, 'w')   # 每次启动截断：sql-error 计数只统计本轮

    def limits():
        import resource
        cap = 2048 * 1024 * 1024
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


def sql_error_count():
    try:
        out = subprocess.run(['grep', '-c', r'\[sql-error\]', LOG],
                             capture_output=True, text=True)
        return int(out.stdout.strip() or 0)
    except Exception:
        return -1


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
    """W_YTD == sum(D_YTD)（Payment 每笔同事务更新两处）——决赛账本核心不变量"""
    for w in warehouses:
        wy = cell(wire_query(c, f'select w_ytd from warehouse where w_id = {w};')[0][0])
        rows = wire_query(c, f'select sum(d_ytd) as s from district where d_w_id = {w};')
        dy = cell(rows[0][0])
        ok = abs(wy - dy) <= max(1e-2 * abs(wy), 1.0)   # float 累加容差
        check(f'{tag} ledger w_ytd==sum(d_ytd) w={w}', ok, f'({wy:.2f} vs {dy:.2f})')


def family_gate(fam, tag):
    for f in ('neworder', 'payment', 'delivery', 'orderstatus', 'stocklevel'):
        ln = fam.get(f, '')
        ok_n = 0
        for part in ln.split():
            if part.startswith('ok='):
                ok_n = int(part[3:])
        check(f'{tag} family {f} committed>0', ok_n > 0, f'({ln.strip()})')


def main():
    fresh = os.environ.get('OJ_GATE_SKIP_LOAD') != '1'
    print(f'== OJ gate: W={W} threads={THREADS} window={WINDOW}s fresh_load={fresh} ==',
          flush=True)
    if fresh and not os.path.isdir(DATA):
        print(f'dataset missing: {DATA}\n  generate: python3 -c "import sys; '
              f"sys.path.insert(0,'bench'); from tpccbench.datagen import generate; "
              f"generate('{DATA}', warehouses={W}, seed=42)\"")
        sys.exit(2)

    proc, ready_s = start_server(fresh)
    print(f'server up pid={proc.pid} ready in {ready_s:.0f}s', flush=True)

    if fresh:
        t0 = time.time()
        env = dict(os.environ, TPCC_DATA=DATA, TPCC_W=str(W))
        r = subprocess.run([sys.executable, '-c',
                            f"import sys; sys.path.insert(0, r'{os.path.join(HERE, 'debug')}');"
                            "import wire_tpcc_stress2 as s; s.bootstrap()"],
                           env=env, capture_output=True, text=True, timeout=1800)
        print(r.stdout.strip()[-200:])
        check('phase2 load', r.returncode == 0, f'({time.time()-t0:.0f}s)')
        vm = vmsize_mb(proc.pid)
        print(f'  post-load VmSize={vm}MB')

    # ---- Phase3: 预热 + 3×150s 窗口 ----
    run_stress(30, 'warmup')
    check('warmup no sql-error', sql_error_count() == 0, f'(count={sql_error_count()})')
    vm_marks = []
    pre_counts = None
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
        # 取帧近失（>100ms 等待）是"接近 ERROR 悬崖"的前导指标，只报不判
        try:
            near = subprocess.run(['grep', '-c', 'bpm-pressure', LOG],
                                  capture_output=True, text=True)
            n = int(near.stdout.strip() or 0)
            if n:
                print(f'  [WARN] window{win} bpm-pressure events so far: {n}', flush=True)
        except Exception:
            pass
        check(f'window{win} no transport fail', agg is not None and "'transport': 0" in agg,
              f'({agg})')
        family_gate(fam, f'window{win}')
        print(f'  window{win} VmSize {vm0} -> {vm1} MB', flush=True)

    if proc.poll() is None and len(vm_marks) == 3:
        slope = vm_marks[2][1] - vm_marks[1][1]
        check('VmSize slope window2->3', slope < SLOPE_MB_MAX, f'({slope:+d}MB)')

    # ---- 一致性 + 崩溃恢复 ----
    if proc.poll() is None:
        rng = random.Random(7)
        ws = sorted(rng.sample(range(1, W + 1), min(5, W)))
        c = Conn()
        ledger_checks(c, 'pre-crash', ws)
        pre_counts = {t: cell(wire_query(c, f'select count(*) as c from {t};')[0][0])
                      for t in ('orders', 'history')}
        c.close()

        os.kill(proc.pid, signal.SIGKILL)
        proc.wait()
        print('  killed -9, restarting...', flush=True)
        t0 = time.time()
        proc2, ready_s = start_server(fresh=False)
        check('post-crash ready < 90s', ready_s < 90, f'({ready_s:.0f}s)')
        c = Conn()
        ledger_checks(c, 'post-crash', ws)
        for t, n in pre_counts.items():
            n2 = cell(wire_query(c, f'select count(*) as c from {t};')[0][0])
            check(f'post-crash {t} rows kept', n2 >= n, f'({n} -> {n2})')
        c.close()
        subprocess.run(['pkill', '-x', 'rmdb'], check=False)

    print('\n== RESULT:', 'PASS' if not failures else f'FAIL ({len(failures)})', '==')
    for f in failures:
        print('  FAIL:', f)
    sys.exit(0 if not failures else 1)


if __name__ == '__main__':
    main()
