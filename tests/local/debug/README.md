# 本地调试与定向复现脚本

> 2026-07-30 起本目录**已纳入 Git 跟踪**（决赛期定向复现器是团队共享资产），旧"不纳入 Git"说明作废。

## 决赛期核心工具（07-30 战役新增；08-01 官方全流程 PASS，详见 `Docs/FinalCompetition/0801-OJ性能测评报告摘要.md`）

| 脚本 | 用途 |
|------|------|
| `crash_aggregate_check.py` | churn → kill -9 → 七项 FLOAT32 聚合位精确对比（OJ 崩后一致性同款检查），可多轮 |
| `crash_row_diff.py` | 崩前逐行快照 + 崩溃镜像留存 + 恢复后行级 diff 定位 |
| `delivery_scan_loop_repro.py` | 删除/插入风暴下扫描回环检测（重复行 = 缺陷本体） |
| `hotspot_selfassign_stress.py` | 热点自赋值锁行 EXEC_BATCH 压测（冲突/回滚/断连） |
| `stmt_variant_sweep.py` | 排名词典全语句 + 形态变体重放，断言无 ERROR 终结 |
| `repro_selfassign.py` | 自赋值双路径×四类型×冲突×回滚 24 项功能矩阵 |
| `wire_tpcc_stress2.py` | 五家族全负载压测（HOTSPOT=1/2 热点轮盘、SKIP_BOOTSTRAP 复用已装载库） |
| `delivery_hammer.py` / `stocklevel_hammer.py` / `abrupt_close_storm.py` | 单家族加浓 / 断连风暴定向复现 |

多数脚本支持 `RMDB_BUILD=<build目录>` 指定被测二进制（对照新旧版本）。服务端诊断行：`[sql-error]` `[pressure-abort]` `[error-abort]` `[bpm-pressure]` `[sweep-wm]` `[trace-row]`（配 `RMDB_TRACE_ROW="v0,v1"` 在恢复期追踪首两 int 列匹配的行）。

其余 `debug_*` / `probe_*` / `repro_*` 为历史单点复现（初赛~决赛各阶段），保留作参考。

**正式回归**请使用上级目录：

- `abort_index_cases.py`
- `load_tpcc_verify.py`
- `smoke_tpcc.py`
- `bench_tpcc_neworder.py`
- `tpcc_neworder_once.py`
