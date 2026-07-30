# 决赛本地测试入口

此目录以 `Docs/FinalCompetition/决赛赛题整理.md` 为准：Wire v3、SI、50 仓、32 客户端、
30 秒预热、3×150 秒测量、45/43/4/4/4 事务混合。旧的 P2 / 专项脚本没有删除，
仍保留在 `tests/run_tests.py`、`tests/framework/` 和 `test.local.bak/`，供定位单模块
问题；它们不再是决赛提交前的主入口。

## 四个入口

```bash
# 本地派生的 25 个功能组：11 个 P2 SQL 组 + 14 个 Wire/MVCC/SI/SER 决赛回归组
python3 tests/finals/function_test.py

# 生成 W=50 规范形 CSV，执行 CREATE/INDEX/LOAD/行数/抽样校验，并检查 900 秒预算
python3 tests/finals/load_data_test.py

# SI、PREPARE_SET+EXEC_BATCH、50 仓、32 客户端、30s + 3×150s、恢复与一致性
python3 tests/finals/performance_test.py

# 非计分：10s 预热 + 60s strace I/O 诊断；无 strace 时显式 SKIP
python3 tests/finals/tpcc_io_diagnostics.py
```

## 本地派生的 25 个功能组

| 范围 | 编号 | 覆盖 |
|---|---|---|
| P2 Wire SQL 回归 | P2-01～P2-11 | DDL、DML、查询、连接、聚合、分组、边界与大聚合 |
| FLOAT32 Wire | F12～F13 | 科学计数法、NaN/Inf 查询参数的受控语义 |
| Snapshot Isolation | F14～F17 | 陈旧写、活跃写、删除快照/索引、删后重插回滚 |
| MVCC 回归 | F18～F23 | 未提交可见性、pending 冲突、扫描缓存、三种复合回滚 |
| Serializable | F24～F25 | INLJ rw 依赖和 SER 元数据 GC 并发 |

`function_test.py --list` 列出所有编号；`--only F15` 可只运行一个功能组。

OJ 报告的精确计数是 **25 functional + 5 recovery + 1 durability audit**，但公开报告仅披露
`Float Precision`、`Transaction Commit Index`、`Snapshot Isolation Model` 三个功能名。
所以这里的 25 组由 PDF 规范和已观测失败场景派生，不能宣称与 OJ 未公开的 25 个名称一一
对应；恢复 5 项与 COMMIT durability audit 仍应作为独立门禁实现和报告。

## 保留与淘汰原则

- 保留：现有 Wire-v3 P2、`tests/local/consistency/`、TPC-C 数据生成/校验、BATCH
  压测和 `tests/prof/` 工具。
- 保留但降级为调试资产：`test.local.bak/` 与 `tests/framework/` 的 NUL/`output.txt`
  初赛专项脚本，以及 `tests/local/debug/` 临时复现脚本。
- 不把仅 NewOrder、W=5、EXEC_STREAM A/B、旧 10/23 事务混合当作 OJ 成绩；它们仍可
  用于性能定位，但不属于本目录的最终门禁。

`full` 数据由本地生成器生成官方行数和表顺序的 W=50 形数据；官方全量 CSV 未随赛题
提供，因此它用于协议、装载预算和一致性拟合，不能冒充赛方隐藏数据。
