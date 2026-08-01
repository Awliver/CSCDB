# FinalCompetition（决赛赛题与主线）

> 决赛规范、正确性门禁、功能增量；**与性能档案 `Optimize/` 分离**。  
> 优化现行裁决 → [`Optimize/14`](../Optimize/14.决赛优化状态与实施计划.md) · 已落地 → [`Optimize/5`](../Optimize/5.已完成优化摘要.md)
> 最后更新：**2026-08-01**（OJ 全 PASS，Ranking **37,248.4 NewOrder/min**）

---

## 索引

| 文档 | 状态 | 说明 |
|------|:----:|------|
| **[08-01 OJ 性能测评报告摘要](./0801-OJ性能测评报告摘要.md)** | ✅ 最新 PASS | W=50 装载 830.06s；32 客户端三窗中位 **37,248.4 NewOrder/min**；恢复 / I/O 诊断均通过 |
| **[0730 OJ 战役复盘](./0730-OJ战役复盘.md)** | ✅ 已 PASS | 当日六份报告完整因果链、修复、方法论、排名优化下一步 |
| **[决赛赛题整理](./决赛赛题整理.md)** | 📘 规范 | 决赛 PDF（29 页）完整要点，对照官方全文 |
| **[决赛准备 Todo](./决赛准备Todo.md)** | 🎯 主线 | 门禁 → Wire v3 / SI·SER / COUNT(DISTINCT) / FLOAT32 → W=50×32 |
| **[Optimize/14 优化状态与实施计划](../Optimize/14.决赛优化状态与实施计划.md)** | 🎯 现行裁决 | 无效项清退、有限项冻结、H3/H4 与条件式性能实验 |
| [一致性审计与回归用例](./一致性审计与回归用例.md) | 🟢 P0 已修 | C1/C2/H5 已关闭；余下见文档 §三 |

优化状态、实现步骤和验收门槛只在 [`Optimize/14`](../Optimize/14.决赛优化状态与实施计划.md) 维护；[`Optimize/2`](../Optimize/2.待落地优化Todo.md) 仅保留活动队列，避免两处各写一套。

---

## 快速验证（正确性 P0 门禁）

```bash
# 应 100% PASS（勿用 --stability：该模式统计漏洞检出率）
python3 tests/local/consistency/run_consistency_regress.py \
  --case C1 --case C2 --case H5
python3 tests/run_tests.py
```

## 提交前总门禁（07-30 后一律用这个）

```bash
bash tests/local/pre_oj_submit.sh          # 正式验收：fresh W=50 全流程（~35min）
FAST=1 bash tests/local/pre_oj_submit.sh   # 快速迭代：复用已装载库 + 45s 窗
```

覆盖：Release 构建 → 功能 11 项 → 一致性 C1/C2/H5 → 自赋值矩阵/扫描回环复现器 →
增强版 oj_gate（装载预算/完整性/装载崩溃耐久、EXHAUSTED=0、降级中止阈值、
峰值 RSS/fd/磁盘水位、崩前崩后**七项 FLOAT32 聚合位对比**、关系不变量、
churn 中途 kill -9、二次恢复幂等）。

最新官方复测（2026-08-01）：**全 PASS**，W=50 装载 **830.06s**（9 表、25,050,594 行），
32 客户端三轮排名中位 **37,248.4 NewOrder/min**。详见
[08-01 OJ 性能测评报告摘要](./0801-OJ性能测评报告摘要.md)。

修复前曾 `--stability 10` → **100% detected**；修复后连跑 10 轮 → **100% PASS**（2026-07-14）。

完整复验（2026-07-27）：`run_tests` 11/11 + consistency P0 + `--mid`（**BATCH**）median **3578.46**（NewOrder 0 abort）→ 见 [`决赛准备Todo`](./决赛准备Todo.md) §1。

一键门禁：`bash tests/local/debug/run_full_local_gate.sh`  
OJ 形窗口：`python3 tests/local/run_oj_perf_test.py --finals`（150s×3 / 32；本地仍 W=5）

---

## 与测试代码关系

| 类型 | 路径 |
|------|------|
| 赛题 / 主线文档 | `Docs/FinalCompetition/` |
| 可执行回归 | `tests/local/consistency/` |

文档归档在 FinalCompetition；用例实现留在 `tests/`（可提交、可 CI）。
