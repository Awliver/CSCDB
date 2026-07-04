# tpccbench — RMDB 的 TPC-C 基准测试工具

> ⚠️ **单实例约定**：本工具（与 tests/ 下所有脚本一致）启动服务器前会
> `pkill -9 -f bin/rmdb` 清场，且所有测试共用端口 8765。**不要并行运行
> 两个测试/基准**——后启动的会直接杀死先前的服务器（表现为静默崩溃）。

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

## keying / think time（`--think`，默认开启）

TPC-C 规范 5.2.5.4 要求每个终端在事务前有 **keying time**（固定输入时间）、提交后有
**think time**（指数分布思考时间），把吞吐限流到规范上限。**这是 tpmC 落在真实数十
量级的根本原因**——OJ 也是这么测的（OJ 实测 ~53）。

- `--think 1.0`（默认）：完整规范限流。W=1、8 客户端下 tpmC ≈ 10~50，与 OJ 同数量级。
  要逼近 OJ 的绝对值，按规范用 `--clients`＝10×仓库数、跑满 `--duration 360`。
- `--think 0`：关闭限流 = **极限吞吐模式**（旧 pgbench 式）。tpmC 会虚高几百倍
  （20000+），**只适合做引擎内部优化的相对对比，不代表真实 TPC-C 成绩**。

keying/think 时间不计入事务延迟统计（p50/p90/p99 仍是纯事务响应时间）。

## 本工具发现并已修复的引擎 bug（2026-07-03 发现，07-04 修复）

历史现象（三个表象，同源）：SI 提交行"丢失/滞后一拍"、并发下孤儿 new_orders /
orders 空洞（C2/C3/C5/C6 FAIL）、SI 快照读不稳定（acid I1 FAIL）。

根因与修复（5 处，见 git log）：
1. **IndexScan 无 MVCC 可见性**（主根因）：直接读堆，读不到本事务链上未提交写
   （版本化 update 只写链、commit 才物化堆）→ district 计数器重读回旧值 →
   o_id 差一 → 唯一键静默 "failure" → 丢单/孤儿行/空洞。修复：IndexScan 与
   SeqScan 同样走 `mvcc_read` 重建可见版本。INLJ 内表读同修。
2. **district 计数器钳制误伤 payment**：三处钳制（update 执行器 MVCC/快路径、
   commit）对 `proposed == visible`（payment 不改计数器）也 +1 → 每笔 payment
   幽灵递增 d_next_o_id。修复：仅 `proposed < visible` 时钳制。
3. **rebase_write_delta 基底错误**：以事务旧快照为基底，未修改列覆盖他人已
   提交增量（new_order 全行镜像覆盖 payment 的 d_ytd → C1/C9 丢钱）。
   修复：基底改为 latest_rec。
4. **IxScan 无限绕环**：叶链经 header 页成环，定位式 end_ 被并发分裂打失效后
   扫描绕环不停（stock_level 卡 15 分钟）。修复：page_no_valid 排除 header 页。
5. **IndexScan 越界返回**：stale end_ 下"范围吸收"优化返回越界行。修复：值条件
   始终逐行 eval、EQ 前缀检查始终开启。

修复后验证：`full`（SI/SER × 4/8 客户端）全 PASS、`acid` 7 项全 PASS、
`repro_lost_insert 200 si` 0 丢失、C5 定向压测 40 轮 OK、官方 P2 回归 11/11。

## 规模预设

| scale | items | 区/仓 | 客户/区 | 订单/区 |
|---|---|---|---|---|
| full  | 100000 | 10 | 3000 | 3000 |
| small | 10000  | 10 | 300  | 300  |
| mini  | 1000   | 3  | 50   | 50   |

`--mix tpcc`（45/43/4/4/4，默认）、`--mix oj`（10/10/1/1/1，赛方口径）或自定义权重。
