#!/usr/bin/env python3
"""Delivery 丢删除最小复现器（07-31 fresh 门禁 3 FAIL 归因用）。

形态：orders.o_carrier_id 已置 1（配送成功提交）但同事务的 new_orders 删除丢失。
最小化：单 warehouse 单 district，D 个 deliverer（begin; min; delete; update carrier;
commit）× F 个 feeder（begin; insert orders; insert new_orders; commit），全局递增
o_id。结束后逐单核账：carrier=1 的订单不得在 new_orders；carrier=0 计数 == 队列长度。

用法: python3 lost_delete_repro.py [seconds] [deliverers] [feeders]  （默认 60 6 4）
自起全新服务器于 build/lost_del_db。
"""
import os, struct, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BUILD = os.path.join(ROOT, 'build')
sys.path.insert(0, HERE)
from wirecli import Conn  # noqa: E402

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 60
NDEL = int(sys.argv[2]) if len(sys.argv) > 2 else 6
NFEED = int(sys.argv[3]) if len(sys.argv) > 3 else 4
DB = 'lost_del_db'

SQL_INT = 1


def start_server():
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    time.sleep(1)
    subprocess.run(['rm', '-rf', os.path.join(BUILD, DB)], check=False)
    logf = open(os.path.join(BUILD, DB + '.log'), 'w')
    subprocess.Popen([os.path.join(BUILD, 'bin', 'rmdb'), DB], cwd=BUILD,
                     stdout=logf, stderr=subprocess.STDOUT,
                     stdin=subprocess.DEVNULL, start_new_session=True,
                     env=dict(os.environ))
    for _ in range(60):
        time.sleep(1)
        try:
            c = Conn(); c.exec_stream('show tables;'); c.close(); return
        except Exception:
            pass
    raise RuntimeError('server not ready')


NDIST = 2   # 每笔配送覆盖的 district 数（>1 才押"部分已应用后回滚"路径）


def bootstrap():
    c = Conn()
    c.exec_stream('create table orders (o_id int, o_d_id int, o_w_id int, o_carrier_id int);')
    c.exec_stream('create table new_orders (no_o_id int, no_d_id int, no_w_id int);')
    # d_next 计数行 + 热点库存行：feeder 在"插入之后"才撞库存冲突 → abort 留壳 +
    # d_next 回滚 → o_id 复用（决赛 NewOrder 词典顺序的本质形态）
    c.exec_stream('create table district_ctr (d_id int, next_oid int);')
    c.exec_stream('create table stock_hot (s_id int, qty int);')
    c.exec_stream('create index orders (o_w_id, o_d_id, o_id);')
    c.exec_stream('create index new_orders (no_w_id, no_d_id, no_o_id);')
    c.exec_stream('create index district_ctr (d_id);')
    c.exec_stream('create index stock_hot (s_id);')
    # 预灌每 district 100 单
    for d in range(1, NDIST + 1):
        for i in range(1, 101):
            c.exec_stream(f'insert into orders values ({i}, {d}, 1, 0);')
            c.exec_stream(f'insert into new_orders values ({i}, {d}, 1);')
        c.exec_stream(f'insert into district_ctr values ({d}, 101);')
    for s in range(1, 4):   # 3 个热点库存行，feeder 间高冲突
        c.exec_stream(f'insert into stock_hot values ({s}, 1000);')
    c.close()


oid_lock = threading.Lock()
next_oid = [201]
stats = {'deliver_ok': 0, 'deliver_abort': 0, 'feed_ok': 0, 'feed_abort': 0,
         'min_empty': 0, 'del_after_min': 0}
slock = threading.Lock()
STOP = False


def prep(c):
    c.exec_stream('set transaction isolation level snapshot isolation')
    c.prepare_set([
        (1, False, [], 'begin;'),
        (2, False, [], 'commit;'),
        (3, True,  [SQL_INT], 'select min(no_o_id) as mo from new_orders where no_d_id = $1 and no_w_id = 1;'),
        (4, False, [SQL_INT, SQL_INT], 'delete from new_orders where no_o_id = $1 and no_d_id = $2 and no_w_id = 1;'),
        (5, False, [SQL_INT, SQL_INT], 'update orders set o_carrier_id = 1 where o_id = $1 and o_d_id = $2 and o_w_id = 1;'),
        (6, False, [SQL_INT, SQL_INT], 'insert into orders values ($1, $2, 1, 0);'),
        (7, False, [SQL_INT, SQL_INT], 'insert into new_orders values ($1, $2, 1);'),
        (8, True,  [SQL_INT], 'select next_oid from district_ctr where d_id = $1;'),
        (9, False, [SQL_INT], 'update district_ctr set next_oid = next_oid + 1 where d_id = $1;'),
        (10, False, [SQL_INT], 'update stock_hot set qty = qty - 1 where s_id = $1;'),
    ])


def parse_mins(r, ndist):
    """b1 结果：ndist 个单列结果集，返回 {district: min_oid}"""
    out = {}
    t = r.get('tail') or b''
    off = 0
    for i in range(r.get('n_results', 0)):
        if off + 6 > len(t):
            break
        opi, cnt = struct.unpack_from('>HI', t, off); off += 6
        v = None
        for _ in range(cnt):
            present = t[off]; off += 1
            if present:
                v = struct.unpack_from('>i', t, off)[0]; off += 4
        # opi = 批内 op 下标；op0 是 begin，district d 的 MIN 是 op d
        if v is not None and v > 0 and 1 <= opi <= ndist:
            out[opi] = v
    return out


def deliverer(tid):
    c = None
    while not STOP:
        try:
            if c is None:
                c = Conn(); prep(c)
            r = c.exec_batch([(1, [])] + [(3, [('i', d)]) for d in range(1, NDIST + 1)])
            if r['status'] != 0:
                with slock: stats['deliver_abort'] += 1
                continue
            mins = parse_mins(r, NDIST)
            if not mins:
                with slock: stats['min_empty'] += 1
                c.exec_batch([(2, [])])
                continue
            ops = []
            for d, mo in mins.items():
                ops += [(4, [('i', mo), ('i', d)]), (5, [('i', mo), ('i', d)])]
            ops.append((2, []))
            r2 = c.exec_batch(ops)
            with slock:
                if r2['status'] == 0: stats['deliver_ok'] += 1
                else: stats['deliver_abort'] += 1
        except Exception:
            try:
                if c: c.close()
            except Exception: pass
            c = None
            time.sleep(0.05)


def feeder(tid):
    import random
    rng = random.Random(1000 + tid)
    c = None
    while not STOP:
        try:
            if c is None:
                c = Conn(); prep(c)
            d = rng.randint(1, NDIST)
            # 词典形态：先读 d_next → 占号 → 插 orders/new_orders → 最后撞热点库存
            # （冲突点在插入之后 → abort 留壳 + d_next 回滚 → o_id 复用）
            r1 = c.exec_batch([(1, []), (8, [('i', d)]), (9, [('i', d)])])
            if r1['status'] != 0:
                with slock: stats['feed_abort'] += 1
                continue
            oid = None
            t = r1.get('tail') or b''
            if len(t) >= 11:
                _, cnt = struct.unpack_from('>HI', t, 0)
                if cnt >= 1 and t[6] == 1:
                    oid = struct.unpack_from('>i', t, 7)[0]
            if oid is None:
                c.exec_batch([(2, [])])
                continue
            s = rng.randint(1, 3)
            r2 = c.exec_batch([(6, [('i', oid), ('i', d)]),
                               (7, [('i', oid), ('i', d)]),
                               (10, [('i', s)]), (2, [])])
            with slock:
                if r2['status'] == 0: stats['feed_ok'] += 1
                else: stats['feed_abort'] += 1
        except Exception:
            try:
                if c: c.close()
            except Exception: pass
            c = None
            time.sleep(0.05)


def main():
    global STOP
    print(f'== lost_delete_repro: {SECS:.0f}s deliverers={NDEL} feeders={NFEED} '
          f'MIN_ES_OFF={os.environ.get("RMDB_NO_MIN_EARLYSTOP")} '
          f'FASTPATH_OFF={os.environ.get("RMDB_NO_SCAN_FASTPATH")} ==', flush=True)
    start_server()
    bootstrap()
    ths = [threading.Thread(target=deliverer, args=(i,)) for i in range(NDEL)] + \
          [threading.Thread(target=feeder, args=(i,)) for i in range(NFEED)]
    for t in ths: t.start()
    t0 = time.time()
    while time.time() - t0 < SECS:
        time.sleep(5)
        with slock:
            print(f'  t={time.time()-t0:4.0f}s {dict(stats)}', flush=True)
    STOP = True
    for t in ths: t.join(timeout=10)

    # 核账（逐 district）
    c = Conn()
    def rows(sql):
        r = c.exec_stream(sql)
        return r[3] if r[0] == 'RESULT_END' and len(r) > 3 else []
    fail = False
    for d in range(1, NDIST + 1):
        q = rows(f'select no_o_id from new_orders where no_w_id = 1 and no_d_id = {d};')
        queue = sorted(x[0] for x in q)
        dups = sorted(set(x for i, x in enumerate(queue) if i and queue[i-1] == x))
        if dups:
            print(f'd={d} DUP queue entries: {dups[:10]}', flush=True)
            fail = True
        bad = []
        for oid in queue:
            rr = rows(f'select o_carrier_id from orders where o_w_id = 1 and o_d_id = {d} and o_id = {oid};')
            if not rr:
                bad.append((oid, 'ORDER_MISSING'))
            elif rr[0][0] != 0:
                bad.append((oid, f'carrier={rr[0][0]}'))
        n_c0 = rows(f'select count(*) as n from orders where o_w_id = 1 and o_d_id = {d} and o_carrier_id = 0;')[0][0]
        print(f'd={d} queue={len(queue)} carrier0={n_c0} bad_entries={bad[:10]}', flush=True)
        if bad or n_c0 != len(queue):
            fail = True
    c.close()
    subprocess.run(['pkill', '-x', 'rmdb'], check=False)
    print('RESULT:', 'FAIL' if fail else 'PASS', flush=True)
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
