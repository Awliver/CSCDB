"""Wire-protocol concurrent TPC-C-ish stress: PREPARE_SET + EXEC_BATCH, five families."""
import sys, os, threading, random, time, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wirecli import Conn, SQL_INT, SQL_FLOAT, SQL_CHAR

DATA = os.environ.get('TPCC_DATA', '/home/smart/workspace/2026/db2026/build/tpccbench_data/full_w1_seed42')
NW = int(os.environ.get('TPCC_W', '1'))
SCHEMAS = [
 "create table warehouse (w_id int, w_name char(10), w_street_1 char(20), w_street_2 char(20), w_city char(20), w_state char(2), w_zip char(9), w_tax float, w_ytd float);",
 "create table district (d_id int, d_w_id int, d_name char(10), d_street_1 char(20), d_street_2 char(20), d_city char(20), d_state char(2), d_zip char(9), d_tax float, d_ytd float, d_next_o_id int);",
 "create table customer (c_id int, c_d_id int, c_w_id int, c_first char(16), c_middle char(2), c_last char(16), c_street_1 char(20), c_street_2 char(20), c_city char(20), c_state char(2), c_zip char(9), c_phone char(16), c_since char(30), c_credit char(2), c_credit_lim int, c_discount float, c_balance float, c_ytd_payment float, c_payment_cnt int, c_delivery_cnt int, c_data char(50));",
 "create table history (h_c_id int, h_c_d_id int, h_c_w_id int, h_d_id int, h_w_id int, h_date char(19), h_amount float, h_data char(24));",
 "create table new_orders (no_o_id int, no_d_id int, no_w_id int);",
 "create table orders (o_id int, o_d_id int, o_w_id int, o_c_id int, o_entry_d char(19), o_carrier_id int, o_ol_cnt int, o_all_local int);",
 "create table order_line (ol_o_id int, ol_d_id int, ol_w_id int, ol_number int, ol_i_id int, ol_supply_w_id int, ol_delivery_d char(30), ol_quantity int, ol_amount float, ol_dist_info char(24));",
 "create table item (i_id int, i_im_id int, i_name char(24), i_price float, i_data char(50));",
 "create table stock (s_i_id int, s_w_id int, s_quantity int, s_dist_01 char(24), s_dist_02 char(24), s_dist_03 char(24), s_dist_04 char(24), s_dist_05 char(24), s_dist_06 char(24), s_dist_07 char(24), s_dist_08 char(24), s_dist_09 char(24), s_dist_10 char(24), s_ytd float, s_order_cnt int, s_remote_cnt int, s_data char(50));",
]
INDEXES = [
 "create index warehouse (w_id);", "create index district (d_w_id, d_id);",
 "create index customer (c_w_id, c_d_id, c_id);", "create index item (i_id);",
 "create index stock (s_w_id, s_i_id);", "create index orders (o_w_id, o_d_id, o_id);",
 "create index new_orders (no_w_id, no_d_id, no_o_id);",
 "create index order_line (ol_w_id, ol_d_id, ol_o_id, ol_number);",
]

def bootstrap():
    c = Conn()
    c.exec_stream('set transaction isolation level snapshot isolation')
    for s in SCHEMAS:
        assert c.exec_stream(s)[0] == 'COMMAND_OK', s
    for s in INDEXES:  # 索引先建，与 OJ Phase2 顺序一致
        assert c.exec_stream(s)[0] == 'COMMAND_OK', s
    for t in ['warehouse','district','customer','history','new_orders','orders','order_line','item','stock']:
        r = c.exec_stream(f'load {DATA}/{t}.csv into {t};')
        assert r[0] == 'COMMAND_OK', (t, r)
    r = c.exec_stream('select count(*) as c from stock;')
    print('bootstrap done, stock rows:', r[3])
    c.close()

STOP = False
stats = {'ok':0, 'abort':0, 'err':0, 'transport':0}
FAMS = ['neworder', 'payment', 'delivery', 'orderstatus', 'stocklevel']
for _f in FAMS:
    stats[_f + '_ok'] = 0
    stats[_f + '_abort'] = 0
    stats[_f + '_err'] = 0
slock = threading.Lock()
errs = []


def bump(fam, status, diag=None):
    """按家族记账（调用方须持 slock）。status: 0 ok / 1 abort / 其他 err"""
    if status == 0:
        stats['ok'] += 1
        stats[fam + '_ok'] += 1
    elif status == 1:
        stats['abort'] += 1
        stats[fam + '_abort'] += 1
    else:
        stats['err'] += 1
        stats[fam + '_err'] += 1
        if diag and len(errs) < 12:
            errs.append(f'{fam}: {diag[:200]}')


def walk_results(r, float_ops=frozenset()):
    """解析 BATCH_RESULT tail 为 {op_index: [cell,...]}。仅适用于单列结果集
    （int/float 各 present(1B)+值(4B)，present=0 无值字节——见 wire_protocol.h:464）。"""
    out = {}
    t = r['tail']
    off = 0
    for _ in range(r['n_results']):
        opi, cnt = struct.unpack_from('>HI', t, off)
        off += 6
        rows = []
        for _i in range(cnt):
            present = t[off]
            off += 1
            if present:
                fmt = '>f' if opi in float_ops else '>i'
                rows.append(struct.unpack_from(fmt, t, off)[0])
                off += 4
            else:
                rows.append(None)
        out[opi] = rows
    return out

def worker(tid):
    global STOP
    rng = random.Random(tid)
    try:
        c = Conn()
        c.exec_stream('set transaction isolation level snapshot isolation')
        c.prepare_set([
            (1, False, [], 'begin;'),
            (2, False, [], 'commit;'),
            (3, True,  [SQL_INT, SQL_INT], 'select d_next_o_id, d_tax from district where d_id = $1 and d_w_id = $2;'),
            (4, False, [SQL_INT, SQL_INT, SQL_INT], 'update district set d_next_o_id = $1 where d_id = $2 and d_w_id = $3;'),
            (5, True,  [SQL_INT, SQL_INT, SQL_INT], 'select c_discount, c_last, c_credit from customer where c_w_id = $1 and c_d_id = $2 and c_id = $3;'),
            (6, False, [SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_CHAR, SQL_INT, SQL_INT], 'insert into orders values ($1, $2, $3, $4, $5, 0, $6, $7);'),
            (7, False, [SQL_INT, SQL_INT, SQL_INT], 'insert into new_orders values ($1, $2, $3);'),
            (8, True,  [SQL_INT], 'select i_price, i_name, i_data from item where i_id = $1;'),
            (9, True,  [SQL_INT, SQL_INT], 'select s_quantity, s_dist_01, s_ytd, s_order_cnt, s_remote_cnt from stock where s_i_id = $1 and s_w_id = $2;'),
            (10, False, [SQL_INT, SQL_INT, SQL_INT], 'update stock set s_quantity = s_quantity - $1 + 91, s_ytd = s_ytd + $1, s_order_cnt = s_order_cnt + 1 where s_i_id = $2 and s_w_id = $3;'),
            (11, False, [SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_FLOAT, SQL_CHAR], 'insert into order_line values ($1, $2, $3, $4, $5, $6, \'\', $7, 0.0, $8);'),
            (12, False, [SQL_FLOAT, SQL_INT], 'update warehouse set w_ytd = w_ytd + $1 where w_id = $2;'),
            (13, False, [SQL_FLOAT, SQL_INT, SQL_INT], 'update district set d_ytd = d_ytd + $1 where d_id = $2 and d_w_id = $3;'),
            (14, False, [SQL_FLOAT, SQL_INT, SQL_INT, SQL_INT], 'update customer set c_balance = c_balance - $1, c_ytd_payment = c_ytd_payment + $1, c_payment_cnt = c_payment_cnt + 1 where c_w_id = $2 and c_d_id = $3 and c_id = $4;'),
            (15, False, [SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_CHAR, SQL_FLOAT, SQL_CHAR], 'insert into history values ($1, $2, $3, $4, $5, $6, $7, $8);'),
            (16, True,  [SQL_INT, SQL_INT], 'select min(no_o_id) as mo from new_orders where no_d_id = $1 and no_w_id = $2;'),
            (17, False, [SQL_INT, SQL_INT, SQL_INT], 'delete from new_orders where no_o_id = $1 and no_d_id = $2 and no_w_id = $3;'),
            (18, False, [SQL_INT, SQL_INT, SQL_INT, SQL_INT], 'update orders set o_carrier_id = $1 where o_id = $2 and o_d_id = $3 and o_w_id = $4;'),
            (19, True,  [SQL_INT, SQL_INT, SQL_INT], 'select o_id, o_carrier_id, o_entry_d from orders where o_w_id = $1 and o_d_id = $2 and o_c_id = $3;'),
            (20, True,  [SQL_INT, SQL_INT, SQL_INT, SQL_INT], 'select count(*) as low_stock from stock, order_line where ol_w_id = $1 and ol_d_id = $2 and ol_o_id > $3 and s_i_id = ol_i_id and s_w_id = ol_w_id and s_quantity < $4;'),
            (21, True,  [SQL_INT, SQL_INT, SQL_INT], 'select c_discount, c_last, c_credit, w_tax from customer, warehouse where w_id = $1 and c_w_id = w_id and c_d_id = $2 and c_id = $3;'),
            (22, True,  [SQL_INT, SQL_INT, SQL_CHAR], 'select count(c_id) as count_c_id from customer where c_w_id = $1 and c_d_id = $2 and c_last = $3;'),
            (23, True,  [SQL_INT, SQL_INT, SQL_CHAR], 'select c_balance, c_first, c_middle, c_last from customer where c_w_id = $1 and c_d_id = $2 and c_last = $3 order by c_first;'),
            (24, False, [SQL_CHAR, SQL_INT, SQL_INT, SQL_INT], 'update order_line set ol_delivery_d = $1 where ol_o_id = $2 and ol_d_id = $3 and ol_w_id = $4;'),
            (25, True,  [SQL_INT, SQL_INT], 'select sum(ol_amount) as sum_amount from order_line where ol_o_id = $1 and ol_d_id = $2;'),
            (26, True,  [SQL_INT, SQL_INT, SQL_INT], 'select o_c_id from orders where o_id = $1 and o_d_id = $2 and o_w_id = $3;'),
            (27, False, [SQL_FLOAT, SQL_INT, SQL_INT, SQL_INT], 'update customer set c_balance = $1, c_delivery_cnt = c_delivery_cnt + 1 where c_id = $2 and c_d_id = $3 and c_w_id = $4;'),
            (28, True,  [SQL_INT, SQL_INT], 'select ol_i_id from order_line where ol_w_id = $1 and ol_d_id = $2 and ol_o_id < 4000 and ol_o_id >= 3980;'),
            (29, False, [SQL_INT, SQL_INT], 'update district set d_next_o_id = d_next_o_id where d_id = $1 and d_w_id = $2;'),
            (30, False, [SQL_INT, SQL_INT], 'update stock set s_quantity = s_quantity where s_i_id = $1 and s_w_id = $2;'),
            (31, False, [SQL_INT, SQL_INT, SQL_INT], 'update customer set c_balance = c_balance where c_w_id = $1 and c_d_id = $2 and c_id = $3;'),
            # --- OJ 独有形态补齐 ---
            (40, True, [SQL_INT, SQL_INT, SQL_CHAR],
             'select c_id, c_first, c_middle, c_last, c_balance, c_credit, c_since from customer '
             'where c_w_id = $1 and c_d_id = $2 and c_last = $3 order by c_first;'),
            (41, True, [SQL_INT, SQL_INT, SQL_INT],
             'select o_id, o_carrier_id, o_entry_d from orders '
             'where o_w_id = $1 and o_d_id = $2 and o_c_id = $3 order by o_id desc;'),
            (42, True, [SQL_INT, SQL_INT, SQL_INT],
             'select ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, ol_delivery_d from order_line '
             'where ol_w_id = $1 and ol_d_id = $2 and ol_o_id = $3;'),
            (43, True, [SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT],
             'select count(distinct s_i_id) as low_stock from order_line, stock '
             'where ol_w_id = $1 and ol_d_id = $2 and ol_o_id < $3 and ol_o_id >= $4 '
             'and s_w_id = $5 and s_i_id = ol_i_id and s_quantity < $6;'),
            (44, True, [SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT, SQL_INT],
             'select count(distinct (s_i_id)) as low_stock from order_line, stock '
             'where ol_w_id = $1 and ol_d_id = $2 and ol_o_id < $3 and ol_o_id >= $4 '
             'and s_w_id = $5 and s_i_id = ol_i_id and s_quantity < $6;'),
        ])
        oid = 4000 + tid * 100000
        home_w = (tid % NW) + 1
        # 决赛负载拓扑：
        #  HOTSPOT=1 概率近似——4 热点仓 ~65% 流量、热点仓内 65% 打单一热点 district；
        #  HOTSPOT=2 赛题原文确定性 160 槽轮盘——4 热点仓各 26 槽、其余仓 ≥1 槽，
        #    slot_index(client, txn_no) = (client + 32*(txn_no%5) + 13*(txn_no//5)) % 160，
        #    冲突时序结构与真实评测一致（13 与 160 互素，5 波覆盖全部槽）
        hotspot = os.environ.get('HOTSPOT', '0') if NW >= 4 else '0'
        hot_ws = [1, 2, 3, 4]
        hot_d = {1: 3, 2: 7, 3: 5, 4: 9}
        slots = None
        if hotspot == '2':
            slots = []
            for hw in hot_ws:
                slots += [hw] * 26                    # 4×26 = 104 热点槽（65%）
            others = [x for x in range(1, NW + 1) if x not in hot_ws]
            i = 0
            while len(slots) < 160:                    # 其余 56 槽轮转铺给非热点仓
                slots.append(others[i % len(others)])
                i += 1
        txn_no = 0
        while not STOP:
            if hotspot == '2':
                slot = (tid + 32 * (txn_no % 5) + 13 * (txn_no // 5)) % 160
                txn_no += 1
                w = slots[slot]
                if w in hot_ws:
                    d = hot_d[w] if rng.random() < 0.65 else rng.randint(1, 10)
                else:
                    d = rng.randint(1, 10)
            elif hotspot == '1':
                if rng.random() < 0.65:
                    w = rng.choice(hot_ws)
                    d = hot_d[w] if rng.random() < 0.65 else rng.randint(1, 10)
                else:
                    w = rng.randint(1, NW)
                    d = rng.randint(1, 10)
            else:
                w = home_w
                d = rng.randint(1, 10)
            cust = rng.randint(1, 30)
            kind = rng.random()
            # MIX_SL：StockLevel 加浓（0~1，抢占混合尾部；诊断 COUNT DISTINCT join 专用）
            sl_share = float(os.environ.get('MIX_SL', '0'))
            if sl_share > 0 and kind > 1.0 - sl_share:
                kind = 0.99  # 落入 StockLevel 分支
            oid += 1
            if kind < 0.45:  # NewOrder
                fam = 'neworder'
                ops = [(1, []), (21, [('i', w), ('i', d), ('i', cust)]),
                       (29, [('i', d), ('i', w)]),
                       (3, [('i', d), ('i', w)]), (4, [('i', oid), ('i', d), ('i', w)]),
                       (5, [('i', w), ('i', d), ('i', cust)]),
                       (6, [('i', oid), ('i', d), ('i', w), ('i', cust), ('s', '2026-07-28 09:00:00'), ('i', 5), ('i', 1)]),
                       (7, [('i', oid), ('i', d), ('i', w)])]
                for n in range(1, 6):
                    # 决赛拓扑：每条明细 25% 取自 24 个热点商品（stock 行冲突 →
                    # NewOrder abort 的主力）；远程供货仓 8%（赛题值,非老 TPC-C 1%）
                    if hotspot != '0' and rng.random() < 0.25:
                        it = 4200 + 137 * rng.randint(0, 23)
                    else:
                        it = rng.randint(1, 100000)
                    qty = rng.randint(1, 10)
                    remote_p = 0.08 if hotspot != '0' else 0.01
                    sw = w if rng.random() > remote_p or NW == 1 else (w % NW) + 1
                    ops += [(8, [('i', it)]), (30, [('i', it), ('i', sw)]),
                            (9, [('i', it), ('i', sw)]),
                            (10, [('i', qty), ('i', it), ('i', sw)]),
                            (11, [('i', oid), ('i', d), ('i', w), ('i', n), ('i', it), ('i', sw), ('f', float(qty)), ('s', 'dist')])]
                ops.append((2, []))
            elif kind < 0.9:  # Payment（60% 按姓氏：中位客户，ORDER BY c_first）
                fam = 'payment'
                amt = round(rng.uniform(1.0, 5000.0), 2)
                lastn = rng.choice(['BARBARBAR','BAROUGHTABLE','BARABLEPRI','OUGHTBARBAR','ABLEPRIBAR'])
                # 决赛拓扑：30% 远程客户仓（客户行与本仓 warehouse/district 行分属两仓）
                cw = w
                if hotspot != '0' and NW > 1 and rng.random() < 0.30:
                    cw = rng.randint(1, NW)
                byname = [(22, [('i', cw), ('i', d), ('s', lastn)]),
                          (40, [('i', cw), ('i', d), ('s', lastn)]),
                          (23, [('i', cw), ('i', d), ('s', lastn)])] if rng.random() < 0.6 else []
                ops = [(1, [])] + byname + [
                       (12, [('f', amt), ('i', w)]), (13, [('f', amt), ('i', d), ('i', w)]),
                       (31, [('i', cw), ('i', d), ('i', cust)]),
                       (14, [('f', amt), ('i', cw), ('i', d), ('i', cust)]),
                       (15, [('i', cust), ('i', d), ('i', cw), ('i', d), ('i', w), ('s', '2026-07-28 09:00:00'), ('f', amt), ('s', 'hist')]),
                       (2, [])]
            elif kind < 0.95:  # Delivery（词典形态：一笔覆盖全部 10 个 district，三段 batch）
                fam = 'delivery'
                r = c.exec_batch([(1, [])] +
                                 [(16, [('i', dd), ('i', w)]) for dd in range(1, 11)])
                if r['status'] != 0:
                    with slock:
                        bump(fam, r['status'])
                    continue
                claimed = []   # (district, min_oid)
                for opi, rows in walk_results(r).items():
                    if opi >= 1 and rows and rows[0] is not None and rows[0] > 0:
                        claimed.append((opi, rows[0]))
                if not claimed:
                    r3 = c.exec_batch([(2, [])])
                    with slock:
                        bump(fam, r3['status'])
                    continue
                ops2 = []
                sum_idx, cid_idx = {}, {}
                for dd, mo in claimed:
                    ops2 += [(17, [('i', mo), ('i', dd), ('i', w)]),
                             (18, [('i', rng.randint(1, 10)), ('i', mo), ('i', dd), ('i', w)]),
                             (24, [('s', '2026-07-28 09:30:00'), ('i', mo), ('i', dd), ('i', w)])]
                    sum_idx[dd] = len(ops2); ops2.append((25, [('i', mo), ('i', dd)]))
                    cid_idx[dd] = len(ops2); ops2.append((26, [('i', mo), ('i', dd), ('i', w)]))
                r2 = c.exec_batch(ops2)
                if r2['status'] != 0:
                    with slock:
                        bump(fam, r2['status'])
                    continue
                res2 = walk_results(r2, float_ops={sum_idx[dd] for dd, _ in claimed})
                ops3 = []
                for dd, _mo in claimed:
                    total = (res2.get(sum_idx[dd]) or [None])[0]
                    cid = (res2.get(cid_idx[dd]) or [None])[0]
                    ops3.append((27, [('f', float(total or 0.0)),
                                      ('i', int(cid or cust)), ('i', dd), ('i', w)]))
                ops3.append((2, []))
                ops = ops3
            elif kind < 0.98:  # OrderStatus：客户 → 最近订单(ORDER BY DESC) → 明细
                fam = 'orderstatus'
                r = c.exec_batch([(41, [('i', w), ('i', d), ('i', cust)])])
                latest = None
                if r['status'] == 0 and r['tail']:
                    _, cnt = struct.unpack_from('>HI', r['tail'], 0)
                    if cnt and r['tail'][6]:
                        latest = struct.unpack_from('>i', r['tail'], 7)[0]
                ops = [(19, [('i', w), ('i', d), ('i', cust)])]
                if latest is not None:
                    ops = [(42, [('i', w), ('i', d), ('i', latest)])] + ops
            else:  # StockLevel：next order → 官方 DISTINCT 连接（双括号变体混跑）
                fam = 'stocklevel'
                r = c.exec_batch([(3, [('i', d), ('i', w)])])
                no = None
                if r['status'] == 0 and r['tail']:
                    _, cnt = struct.unpack_from('>HI', r['tail'], 0)
                    if cnt and r['tail'][6]:
                        no = struct.unpack_from('>i', r['tail'], 7)[0]
                if no is None:
                    continue
                sid = 43 if rng.random() < 0.5 else 44
                ops = [(sid, [('i', w), ('i', d), ('i', no), ('i', no - 20), ('i', w), ('i', rng.randint(10, 20))])]
            try:
                _t0 = time.time()
                r = c.exec_batch(ops)
                _dt = time.time() - _t0
                if _dt > 5:
                    with slock:
                        errs.append(f'SLOW {_dt:.1f}s tid={tid} ops={[o[0] for o in ops]}')
                        print(f'!! SLOW batch {_dt:.1f}s tid={tid} stmts={[o[0] for o in ops]}', flush=True)
                with slock:
                    bump(fam, r['status'], r.get('diag'))
            except Exception as e:
                with slock:
                    stats['transport'] += 1
                    if len(errs) < 8: errs.append(f'TRANSPORT: {e}')
                return
    except Exception as e:
        with slock:
            stats['transport'] += 1
            if len(errs) < 8: errs.append(f'SETUP: {e}')

if __name__ == '__main__':
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 30
    nthreads = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    if os.environ.get('SKIP_BOOTSTRAP') != '1':
        bootstrap()
    ths = [threading.Thread(target=worker, args=(i,)) for i in range(nthreads)]
    t0 = time.time()
    for t in ths: t.start()
    while time.time() - t0 < seconds:
        time.sleep(1)
        alive = sum(t.is_alive() for t in ths)
        with slock:
            s = {k: stats[k] for k in ('ok', 'abort', 'err', 'transport')}
        print(f'  t={time.time()-t0:4.0f}s alive={alive} {s}')
        if alive == 0:
            break
    STOP = True
    for t in ths: t.join(timeout=10)
    print('FINAL:', {k: stats[k] for k in ('ok', 'abort', 'err', 'transport')})
    for f in FAMS:
        print(f"  {f}: ok={stats[f+'_ok']} abort={stats[f+'_abort']} err={stats[f+'_err']}")
    for e in errs: print('ERR:', e)
