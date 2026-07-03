# tpccbench — RMDB 的 TPC-C 基准测试工具

仿照 pgbench / BenchBase 结构、按 **TPC-C 规范 v5.11** 实现的完整测试工具：
数据生成 → 装载校验 → 事务正确性 → 一致性检查 → ACID 测试 → 多客户端压测。

## 快速开始

```bash
# 完整流水线：生成(如缺失)+装载+smoke+一致性+压测+压测后一致性
python3 bench/tpccbench.py full --scale small --duration 60 --clients 8

# 分步
python3 bench/tpccbench.py gen   --scale full -W 1          # 生成规范数据 (W=1 全量)
python3 bench/tpccbench.py load  --scale full                # 建库+装载+建索引+行数校验
python3 bench/tpccbench.py smoke                             # 每种事务的精确副作用断言
python3 bench/tpccbench.py check --samples 16 --deep         # 一致性条件 C1..C12
python3 bench/tpccbench.py acid                              # 原子/隔离/持久性(kill -9)
python3 bench/tpccbench.py run --duration 300 --clients 8 --json out.json
```

## 与规范对齐的要点（旧 tests/local 工具缺失的）

| 项 | 规范条款 | 说明 |
|---|---|---|
| NURand 非均匀分布 | 4.3.2 | c_id NURand(1023)、i_id NURand(8191)、c_last NURand(255)+C 常量装载/运行期分离 |
| 1% NewOrder 故意回滚 | 2.4.1.4 | 用不存在的 item，必须零副作用 |
| 60% 按姓氏访问客户 | 2.5.2.2 | c_last 查询取按 c_first 排序的中间行 |
| s_ytd += qty | 2.4.2.2 | 旧工具错写成 += qty*price |
| 承运人/阈值随机 | 2.7/2.8 | carrier rand(1,10)、threshold rand(10,20) |
| 一致性条件 | 3.3.2 | C1-C9 全量聚合，C6/C7/C10/C12 抽样 |
| tpmC 口径 | 5.4 | 仅统计测量窗内 **提交成功** 的 New-Order |
| 延迟分位数 | 5.2.5.3 | p50/p90/p99，p90>5s 标记 |

## 方言妥协（引擎限制，已在代码注释标注）

- 无 NULL：`o_carrier_id=0`、`ol_delivery_d='PENDING'` 作哨兵
- 无 DISTINCT：stock_level 取 join 行客户端去重（结果等价）
- `c_data char(50)`（规范 300..500）
- Delivery 内联执行（多数实现同此，规范允许排队）
- 无键入/思考时间（pgbench 式极限吞吐模式）

## 已知引擎问题（本工具首次发现，2026-07-03）

1. **SI 提交可见性滞后/丢失**：SI 下已提交插入要等下一笔事务才可见，
   会话最后一笔**永久丢失**。复现：`tests/local/debug/repro_lost_insert.py N si pure`。
   SER 串行无此问题；baseline 0d36b94 同样存在（非 INLJ 回归）。
2. **并发 abort 残留**：4 客户端下（SER 亦然）出现孤儿 new_orders、
   丢失 orders、d_next_o_id 空洞（C3/C5/C6 FAIL，金额类 C1/C8/C9 PASS）。
3. **SI 快照读不稳定**：事务内重读看到他人已提交修改（`acid` I1 FAIL），
   实际隔离级别退化为 Read Committed。

## 规模预设

| scale | items | 区/仓 | 客户/区 | 订单/区 |
|---|---|---|---|---|
| full  | 100000 | 10 | 3000 | 3000 |
| small | 10000  | 10 | 300  | 300  |
| mini  | 1000   | 3  | 50   | 50   |

`--mix tpcc`（45/43/4/4/4，默认）、`--mix oj`（10/10/1/1/1，赛方口径）或自定义权重。
