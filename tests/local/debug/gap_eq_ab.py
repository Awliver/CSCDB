#!/usr/bin/env python3
"""gap-EQ 扫描提前终止归因驱动（Docs/Optimize/14 §6.4 交接首项）。

同一触发态库（默认 oj_gate_w10_db）上零并发重放 gap-EQ 形态查询：
    select o_id, o_d_id from orders where o_w_id = W and o_id = X
（索引 (o_w_id,o_d_id,o_id)：前缀 EQ + 跳过 o_d_id + 残差 EQ —— 宽范围扫描）
真值 = 逐 district 全前缀点查之和（无 gap，不走宽扫描）。缺行/多行即触发。

用法:
    python3 gap_eq_ab.py probe            # 用当前已运行服务器直接探测（不重启）
    python3 gap_eq_ab.py run              # 默认环境（scan fastpath 关）重启+探测
    RMDB_SCAN_FASTPATH=1 python3 gap_eq_ab.py run   # 快路径开
环境: AB_W=10 仓数, AB_DB 覆盖库名。
"""
import os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, HERE)
from wirecli import Conn  # noqa: E402

W = int(os.environ.get('AB_W', '10'))
DB = os.environ.get('AB_DB', f'oj_gate_w{W}_db')


def start_server():
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    time.sleep(1)
    logf = open(os.path.join(BUILD, DB + '.gapab.log'), 'w')
    subprocess.Popen([os.path.join(BUILD, 'bin', 'rmdb'), DB], cwd=BUILD,
                     stdout=logf, stderr=subprocess.STDOUT,
                     stdin=subprocess.DEVNULL, start_new_session=True,
                     env=dict(os.environ))
    for _ in range(600):          # 恢复可达 6-8 分钟
        time.sleep(2)
        try:
            c = Conn(); c.exec_stream('show tables;'); c.close(); return
        except Exception:
            pass
    raise RuntimeError('server not ready')


def probe():
    c = Conn()

    def rows(sql):
        return c.exec_stream(sql)[3]

    def val(sql):
        return rows(sql)[0][0]

    # 探针集：每 (w,d) 取 max/min o_id 及若干中间值 —— 覆盖叶链首尾与中段
    probes = {}
    for w in range(1, W + 1):
        ids = set()
        for d in range(1, 11):
            mx = val(f'select max(o_id) as m from orders where o_w_id={w} and o_d_id={d};')
            mn = val(f'select min(o_id) as m from orders where o_w_id={w} and o_d_id={d};')
            if isinstance(mx, int):
                ids.update({mx, mx - 1, mx - 7, (mn + mx) // 2, mn, mn + 1, 3000, 2101, 977})
        probes[w] = sorted(x for x in ids if isinstance(x, int) and x > 0)

    bad = []
    n_q = 0
    for w, ids in probes.items():
        for oid in ids:
            gap = rows(f'select o_id, o_d_id from orders where o_w_id={w} and o_id={oid};')
            truth = []
            for d in range(1, 11):
                r = rows(f'select o_id, o_d_id from orders where o_w_id={w} '
                         f'and o_d_id={d} and o_id={oid};')
                truth.extend(r)
            n_q += 1
            if sorted(map(tuple, gap)) != sorted(map(tuple, truth)):
                bad.append((w, oid, len(gap), len(truth)))
                print(f'MISMATCH w={w} o_id={oid}: gap={len(gap)} rows, truth={len(truth)} rows',
                      flush=True)

    # new_orders 同形态（插删搅动集中地：墓碑/陈旧项密度最高）——
    # 探 min 附近 ±3（队首正是 Delivery 刚删过的地方）
    for w in range(1, W + 1):
        ids = set()
        for d in range(1, 11):
            mn = val(f'select min(no_o_id) as m from new_orders where no_w_id={w} and no_d_id={d};')
            mx = val(f'select max(no_o_id) as m from new_orders where no_w_id={w} and no_d_id={d};')
            if isinstance(mn, int):
                ids.update({mn, mn + 1, mn + 3, mx, mx - 1})
        for oid in sorted(x for x in ids if isinstance(x, int) and x > 0):
            gap = rows(f'select no_o_id, no_d_id from new_orders where no_w_id={w} and no_o_id={oid};')
            truth = []
            for d in range(1, 11):
                truth.extend(rows(f'select no_o_id, no_d_id from new_orders where no_w_id={w} '
                                  f'and no_d_id={d} and no_o_id={oid};'))
            n_q += 1
            if sorted(map(tuple, gap)) != sorted(map(tuple, truth)):
                bad.append((w, oid, len(gap), len(truth)))
                print(f'MISMATCH new_orders w={w} no_o_id={oid}: gap={len(gap)} truth={len(truth)}',
                      flush=True)
    c.close()
    return n_q, bad


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'run'
    flags = {k: os.environ.get(k) for k in
             ('RMDB_SCAN_FASTPATH', 'RMDB_NO_MIN_EARLYSTOP', 'RMDB_NO_UNPIN_FASTPATH')}
    print(f'== gap_eq_ab: db={DB} mode={mode} flags={flags} ==', flush=True)
    if mode == 'run':
        start_server()
        print('server ready', flush=True)
    n_q, bad = probe()
    print(f'probed {n_q} gap-EQ queries, mismatches={len(bad)}', flush=True)
    if bad:
        print('FAIL: early-termination reproduced', flush=True)
        sys.exit(1)
    print('PASS', flush=True)


if __name__ == '__main__':
    main()
