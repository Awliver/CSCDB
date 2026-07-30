#!/usr/bin/env python3
"""StockLevel COUNT(DISTINCT) 定向猛测器。

针对 OJ 反馈的失败形态:
  SELECT COUNT ( DISTINCT ( s_i_id ) ) FROM order_line , stock
  WHERE ol_w_id=? AND ol_d_id=? AND ol_o_id<? AND ol_o_id>=? AND s_w_id=?
    AND s_i_id=ol_i_id AND s_quantity<?;

策略:
  - 双括号/无括号两个词典变体 (43/44) 混跑
  - 参数分档: 正常(d_next_o_id 邻域) / 越界(远超最大订单) / 负下界 / 空区间 /
    巨区间(几乎全分区) / 阈值极端(0 与 100000)
  - 半数线程当 NewOrder/Delivery 搅动器:向被扫区间持续插入 order_line、
    回滚部分事务(制造 husk 清理并发)、删 new_orders —— 让 join 扫描与
    插入/回滚/物理清理竞争
  - 全程统计 ERROR;任何 ERROR 立即打印 diag + 参数,可选停下 (STOP_ON_ERR=1)

用法: SKIP_BOOTSTRAP=1 TPCC_W=10 python3 stocklevel_hammer.py [seconds] [threads]
      (服务器须已装载;TPCC_W 与数据一致)
"""
import os
import random
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wirecli import Conn, SQL_INT, SQL_FLOAT, SQL_CHAR  # noqa: E402
import wire_tpcc_stress2 as W2  # noqa: E402  # 复用 prepare 模板定义

NW = int(os.environ.get('TPCC_W', '10'))
STOP_ON_ERR = os.environ.get('STOP_ON_ERR') == '1'

STOP = False
stats = {'sl_ok': 0, 'sl_err': 0, 'sl_abort': 0, 'feed_ok': 0, 'feed_abort': 0,
         'feed_err': 0, 'transport': 0}
slock = threading.Lock()
errs = []

PREPARE = [
    (1, False, [], 'begin;'),
    (2, False, [], 'commit;'),
    (3, True, [SQL_INT, SQL_INT],
     'select d_next_o_id, d_tax from district where d_id = $1 and d_w_id = $2;'),
    (43, True, [SQL_INT] * 6,
     'select count(distinct s_i_id) as low_stock from order_line, stock '
     'where ol_w_id = $1 and ol_d_id = $2 and ol_o_id < $3 and ol_o_id >= $4 '
     'and s_w_id = $5 and s_i_id = ol_i_id and s_quantity < $6;'),
    (44, True, [SQL_INT] * 6,
     'select count(distinct (s_i_id)) as low_stock from order_line, stock '
     'where ol_w_id = $1 and ol_d_id = $2 and ol_o_id < $3 and ol_o_id >= $4 '
     'and s_w_id = $5 and s_i_id = ol_i_id and s_quantity < $6;'),
    # 搅动器语句
    (4, False, [SQL_INT, SQL_INT, SQL_INT],
     'update district set d_next_o_id = $1 where d_id = $2 and d_w_id = $3;'),
    (6, False, [SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_CHAR, SQL_INT, SQL_INT],
     'insert into orders values ($1, $2, $3, $4, $5, 0, $6, $7);'),
    (7, False, [SQL_INT, SQL_INT, SQL_INT],
     'insert into new_orders values ($1, $2, $3);'),
    (10, False, [SQL_INT, SQL_INT, SQL_INT],
     'update stock set s_quantity = s_quantity - $1 + 91, s_ytd = s_ytd + $1, '
     's_order_cnt = s_order_cnt + 1 where s_i_id = $2 and s_w_id = $3;'),
    (11, False, [SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_FLOAT, SQL_CHAR],
     "insert into order_line values ($1, $2, $3, $4, $5, $6, '', $7, 0.0, $8);"),
    (16, True, [SQL_INT, SQL_INT],
     'select min(no_o_id) as mo from new_orders where no_d_id = $1 and no_w_id = $2;'),
    (17, False, [SQL_INT, SQL_INT, SQL_INT],
     'delete from new_orders where no_o_id = $1 and no_d_id = $2 and no_w_id = $3;'),
    (99, False, [], 'abort;'),
]


def get_next_oid(c, w, d):
    r = c.exec_batch([(3, [('i', d), ('i', w)])])
    if r['status'] != 0 or not r['tail']:
        return None
    _, cnt = struct.unpack_from('>HI', r['tail'], 0)
    if not cnt or not r['tail'][6]:
        return None
    return struct.unpack_from('>i', r['tail'], 7)[0]


def sl_worker(tid):
    rng = random.Random(tid)
    try:
        c = Conn()
        c.exec_stream('set transaction isolation level snapshot isolation')
        c.prepare_set(PREPARE)
        while not STOP:
            w = rng.randint(1, NW)
            d = rng.randint(1, 10)
            no = get_next_oid(c, d, w) or 3001
            mode = rng.random()
            if mode < 0.5:      # 词典正常形态：最近 20 笔
                hi, lo = no, no - 20
            elif mode < 0.62:   # 负/零下界
                hi, lo = 15, -5
            elif mode < 0.74:   # 越界高位（区间在未来）
                hi, lo = no + 10 ** 7, no + 10 ** 7 - 20
            elif mode < 0.86:   # 空区间（hi<=lo）
                hi, lo = no - 20, no
            else:               # 巨区间（近全分区，最大分配压力）
                hi, lo = no, 1
            th = rng.choice([0, 1, rng.randint(10, 20), 99999, 100000])
            sid = 43 if rng.random() < 0.5 else 44
            try:
                r = c.exec_batch([(sid, [('i', w), ('i', d), ('i', hi), ('i', lo),
                                         ('i', w), ('i', th)])])
                with slock:
                    if r['status'] == 0:
                        stats['sl_ok'] += 1
                    elif r['status'] == 1:
                        stats['sl_abort'] += 1
                    else:
                        stats['sl_err'] += 1
                        msg = (f"SL ERROR sid={sid} w={w} d={d} hi={hi} lo={lo} "
                               f"th={th}: {r['diag'][:200]}")
                        errs.append(msg)
                        print('!!', msg, flush=True)
                        if STOP_ON_ERR:
                            globals()['STOP'] = True
            except Exception as e:
                with slock:
                    stats['transport'] += 1
                    errs.append(f'SL TRANSPORT: {e!r}')
                return
    except Exception as e:
        with slock:
            stats['transport'] += 1
            errs.append(f'SL SETUP: {e!r}')


def feeder_worker(tid):
    """搅动器：在被扫描分区高频插入订单（30% 故意回滚制造 husk 清理），删最小 new_orders。"""
    rng = random.Random(9000 + tid)
    oid = 5_000_000 + tid * 200000
    try:
        c = Conn()
        c.exec_stream('set transaction isolation level snapshot isolation')
        c.prepare_set(PREPARE)
        while not STOP:
            w = rng.randint(1, NW)
            d = rng.randint(1, 10)
            oid += 1
            ops = [(1, []), (6, [('i', oid), ('i', d), ('i', w), ('i', rng.randint(1, 30)),
                                 ('s', '2026-07-29 21:00:00'), ('i', 5), ('i', 1)]),
                   (7, [('i', oid), ('i', d), ('i', w)])]
            for n in range(1, 6):
                it = rng.randint(1, 100000)
                ops += [(10, [('i', rng.randint(1, 10)), ('i', it), ('i', w)]),
                        (11, [('i', oid), ('i', d), ('i', w), ('i', n), ('i', it),
                              ('i', w), ('f', 5.0), ('s', 'dist')])]
            ops.append((99, []) if rng.random() < 0.3 else (2, []))
            try:
                r = c.exec_batch(ops)
                with slock:
                    if r['status'] == 0:
                        stats['feed_ok'] += 1
                    elif r['status'] == 1:
                        stats['feed_abort'] += 1
                    else:
                        stats['feed_err'] += 1
                        if len(errs) < 20:
                            errs.append(f"FEED ERROR: {r['diag'][:150]}")
                if rng.random() < 0.2:
                    mo = None
                    rr = c.exec_batch([(16, [('i', d), ('i', w)])])
                    if rr['status'] == 0 and rr['tail']:
                        _, cnt = struct.unpack_from('>HI', rr['tail'], 0)
                        if cnt and rr['tail'][6]:
                            mo = struct.unpack_from('>i', rr['tail'], 7)[0]
                    if mo:
                        c.exec_batch([(1, []), (17, [('i', mo), ('i', d), ('i', w)]), (2, [])])
            except Exception as e:
                with slock:
                    stats['transport'] += 1
                    errs.append(f'FEED TRANSPORT: {e!r}')
                return
    except Exception as e:
        with slock:
            stats['transport'] += 1
            errs.append(f'FEED SETUP: {e!r}')


if __name__ == '__main__':
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 120
    nthreads = int(sys.argv[2]) if len(sys.argv) > 2 else 24
    n_sl = max(1, nthreads // 2)
    ths = [threading.Thread(target=sl_worker, args=(i,)) for i in range(n_sl)]
    ths += [threading.Thread(target=feeder_worker, args=(i,))
            for i in range(nthreads - n_sl)]
    t0 = time.time()
    for t in ths:
        t.start()
    while time.time() - t0 < seconds and not STOP:
        time.sleep(2)
        alive = sum(t.is_alive() for t in ths)
        with slock:
            s = dict(stats)
        print(f'  t={time.time()-t0:4.0f}s alive={alive} {s}', flush=True)
        if alive == 0:
            break
    STOP = True
    for t in ths:
        t.join(timeout=15)
    print('FINAL:', stats)
    for e in errs[:20]:
        print('ERR:', e)
    sys.exit(1 if stats['sl_err'] or stats['transport'] else 0)
