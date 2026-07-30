"""带外部账本的 MIN canary（模拟 OJ 'MIN was not the earliest new_orders key' 判定）。

单 mutator 顺序删除 d=5 的最小键（客户端账本 LO_ACKED = 最近一次已确认删除的键）；
多 reader 并发 get_min(d=5)，断言 v ∈ [快照下界, LO_ACKED+1]。
v 超前（漏键）时轮询等 LO_ACKED 追平：追平延迟 > LAG_MS 判为 BUG
（合法的 commit可见-未ack 间隙是 µs 级；漏键窗口 = fsync 组提交窗口 ms 级）。
用 RMDB_GROUP_COMMIT_WINDOW_US=3000 放大窗口。
"""
import sys, os, subprocess, time, socket, struct, threading
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.abspath(__file__)))
from wirecli import Conn, SQL_INT

BIN = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../build/bin/rmdb'))
DUR = float(os.environ.get('DUR', '30'))
LAG_MS = 1.5
MAXB = int(os.environ.get("MAXB", "10"))
NREAD = int(os.environ.get('NREAD', '3'))

subprocess.run(['pkill', '-x', 'rmdb'], capture_output=True); time.sleep(0.3)
subprocess.run(['rm', '-rf', 'mc5_db'])
logf = open('mc5_trace.log', 'w')
p = subprocess.Popen([BIN, 'mc5_db'], stdout=subprocess.DEVNULL, stderr=logf, start_new_session=True)
for _ in range(50):
    try: socket.create_connection(('127.0.0.1', 8765), timeout=0.3).close(); break
    except OSError: time.sleep(0.2)

boot = Conn()
boot.exec_stream('set transaction isolation level snapshot isolation')
boot.exec_stream('create table new_orders (no_o_id int, no_d_id int, no_w_id int);')
boot.exec_stream('create index new_orders (no_w_id, no_d_id, no_o_id);')

def prep(c):
    c.exec_stream('set transaction isolation level snapshot isolation')
    c.prepare_set([
        (1, True,  [SQL_INT], 'select min(no_o_id) as mo from new_orders where no_d_id = $1 and no_w_id = 1;'),
        (2, False, [], 'begin;'), (3, False, [], 'commit;'),
        (4, False, [SQL_INT, SQL_INT], 'insert into new_orders values ($1, $2, 1);'),
        (5, False, [SQL_INT, SQL_INT], 'delete from new_orders where no_o_id = $1 and no_d_id = $2 and no_w_id = 1;'),
    ])

# 预置 d=5 键 1..N（单事务批量，一次 fsync）
N = int(os.environ.get('NKEYS', '300'))
prep(boot)
boot.exec_stream('begin;')
for v in range(1, N + 1):
    rr = boot.exec_stream(f'insert into new_orders values ({v}, 5, 1);')
    assert rr[0] != 'ERROR', rr
boot.exec_stream('commit;')
print(f'populated {N} keys', flush=True)

def get_min(c, d):
    r = c.exec_batch([(1, [('i', d)])])
    if r['status'] != 0: return ('ERR', r['diag'][:40])
    _, cnt = struct.unpack_from('>HI', r['tail'], 0)
    if cnt == 0 or not r['tail'][6]: return None
    return struct.unpack_from('>i', r['tail'], 7)[0]

STOP = False
LO_ACKED = 0          # 最近一次已确认删除的键（0 = 尚无）
bugs = []

def mutator():
    global LO_ACKED, STOP
    c = Conn(); prep(c)
    k = 1
    while not STOP:
        r = c.exec_batch([(2, []), (5, [('i', k), ('i', 5)]), (3, [])])
        if r['status'] == 0:
            LO_ACKED = k
            k += 1
            # 尾部补充，维持种群规模（独立事务；账本只随删除推进）
            if os.environ.get('NOINS') != '1':
                c.exec_batch([(2, []), (4, [('i', k + N - 1), ('i', 5)]), (3, [])])
        # 失败（冲突等）则重试同一键
    STOP = True

import random
def reader(tid):
    c = Conn(); prep(c)
    while not STOP and len(bugs) < MAXB:
        time.sleep(random.random() * 0.0012)   # 制造 active_rts_ 安静瞬间
        lo_before = LO_ACKED
        v = get_min(c, 5)
        if v is None or isinstance(v, tuple) or v == 0: continue
        a = LO_ACKED
        if v < lo_before + 1:
            bugs.append((tid, 'STALE', v, lo_before, a))
            print(f'<<<< BUG STALE tid={tid} v={v} lo_before={lo_before} acked={a}', flush=True)
            continue
        if v > a + 1:
            # 漏键嫌疑：先倾倒 ring（时间最敏感），再窗口内现场取证
            missed = a + 1
            t0 = time.perf_counter()
            if os.environ.get('RMDB_RING'):
                c.exec_stream('RINGDUMP;')
            r_idx = c.exec_stream(f'select no_o_id from new_orders where no_o_id = {missed} and no_d_id = 5 and no_w_id = 1;')
            idx_hit = r_idx[0] == 'RESULT_END' and len(r_idx[3]) > 0
            r_seq = c.exec_stream(f'select no_d_id from new_orders where no_o_id = {missed};')
            seq_hit = r_seq[0] == 'RESULT_END' and len(r_seq[3]) > 0
            t_probe = (time.perf_counter() - t0) * 1000
            while LO_ACKED < v - 1 and (time.perf_counter() - t0) * 1000 < 20:
                time.sleep(0.0002)
            lag = (time.perf_counter() - t0) * 1000
            # 判定校准（2026-07-30）：lag 时钟含探针耗时（RINGDUMP + 两条 wire 点查）。
            # root_latch 改写者优先后，点查偶排写者队列后，探针尾延迟可达数 ms，
            # 单靠 lag>LAG_MS 会把"删除已提交、ack 晚到"的合法时序判成 bug
            # （假阳性签名：idx=False seq=False 且 lag≈t_probe）。真吞行的证据是
            # 键仍在表中（idx/seq 命中），或 20ms 硬停滞（账本追不平）。
            if lag > LAG_MS and (idx_hit or seq_hit or lag >= 20):
                bugs.append((tid, 'SKIP', v, a, lag))
                print(f'<<<< BUG SKIP tid={tid} min={v} ledger-min={missed} lag={lag:.2f}ms '
                      f'probe(idx={idx_hit} seq={seq_hit} t={t_probe:.2f}ms)', flush=True)

ths = [threading.Thread(target=mutator, daemon=True)] + \
      [threading.Thread(target=reader, args=(i,), daemon=True) for i in range(NREAD)]
t_start = time.time()
for t in ths: t.start()
while time.time() - t_start < DUR and not STOP and len(bugs) < MAXB:
    time.sleep(0.5)
STOP = True; time.sleep(0.5)
print(f'deleted up to {LO_ACKED}, bugs={len(bugs)}')
print('RESULT:', 'FAIL' if bugs else 'PASS')
subprocess.run(['pkill', '-x', 'rmdb'], capture_output=True)
