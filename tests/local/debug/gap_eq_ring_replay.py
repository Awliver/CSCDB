#!/usr/bin/env python3
"""对单条 new_orders gap-EQ 失败查询做 ring 取证重放。

用法: python3 gap_eq_ring_replay.py <w> <oid>
前提: 服务器以 RMDB_RING=1 启动;失败形态为 gap 查询漏行(truth 点查可见)。
输出: gap/点查结果 + 服务器日志尾部与 oid 相关的 ring 事件。
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from wirecli import Conn  # noqa: E402

w, oid = int(sys.argv[1]), int(sys.argv[2])
c = Conn()

gap = c.exec_stream(f'select no_o_id, no_d_id from new_orders where no_w_id={w} and no_o_id={oid};')[3]
print(f'gap rows: {gap}', flush=True)
truth = []
for d in range(1, 11):
    truth += c.exec_stream(f'select no_o_id, no_d_id from new_orders where no_w_id={w} '
                           f'and no_d_id={d} and no_o_id={oid};')[3]
print(f'truth rows: {truth}', flush=True)
c.exec_stream('RINGDUMP')
c.close()
print('ring dumped to server stderr; grep server log for events', flush=True)
