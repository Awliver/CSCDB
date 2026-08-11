# Docs 文档索引（本地，不上传远程）

> 队伍 **orzcle** · RMDB 2026 · **决赛阶段**  
> 最后更新：**2026-08-11**
> **决赛规范**：[FinalCompetition/决赛赛题整理](./FinalCompetition/决赛赛题整理.md) · **主线 Todo**：[FinalCompetition/决赛准备](./FinalCompetition/决赛准备Todo.md)  
> **现行优化裁决**：[Optimize/14](./Optimize/14.决赛优化状态与实施计划.md) · 初赛锚点：[Optimize/12](./Optimize/12.初赛冻结备忘.md) · 已落地：[Optimize/5](./Optimize/5.已完成优化摘要.md)

---

## 先读什么

| 顺序 | 文档 | 用途 |
|:--:|------|------|
| 1 | **[决赛赛题整理](./FinalCompetition/决赛赛题整理.md)** | **决赛 PDF 完整要点（先读）** |
| 2 | **[决赛准备 Todo](./FinalCompetition/决赛准备Todo.md)** | **决赛主线：门禁 → 增量 → 排名** |
| 3 | **[线下赛与答辩准备计划](./FinalCompetition/线下赛与答辩准备计划.md)** | **当前执行：架构定稿、文档归档、无 AI 功能题与多轮答辩** |
| 4 | [08-03 OJ 报告摘要](./FinalCompetition/0803-OJ性能测评报告摘要.md) | **最新 `c446377` 全 PASS · 40,264.8；新的正确性与性能锚点** |
| 5 | [一致性审计](./FinalCompetition/一致性审计与回归用例.md) | 本地 C1–M8 回归与定位档案 |
| 6 | **[14.决赛优化状态与实施计划](./Optimize/14.决赛优化状态与实施计划.md)** | **无效项清退、有限项冻结、活动项计划与量化验收** |
| 7 | **[15.P-A1 归因取证](./Optimize/15.P-A1归因取证.md)** | **稳定 reason token、确定性闭环、W=5 开关 A/B 与 W=50 冻结口径** |
| 8 | **[5.已完成优化分类总表](./Optimize/5.已完成优化摘要.md)** | 当前 HEAD 按层归类的落地清单 |
| 9 | [0.优化索引](./Optimize/0.优化索引.md) | 现状一页 + 文档地图 |
| — | [12.初赛冻结](./Optimize/12.初赛冻结备忘.md) | 初赛历史锚点（档案） |
| — | [9.OJ 成绩](./Optimize/9.OJ成绩与提交记录.md) | 初赛交卷与成绩锚点 |
| — | [3.落地日志](./Optimize/3.方案A-C优化记录与tpmC评估.md) | tpmC / profile 权威数字 |
| — | [13.P0 后 mid 收尾](./Optimize/13.P0正确性后性能复验收尾.md) | 初赛三轮完整门禁 · ~3970 / abort 0 |
| — | [08-01 OJ 报告摘要](./FinalCompetition/0801-OJ性能测评报告摘要.md) | H3 冻结历史样本：`bc2c208` 全 PASS · **34,592** |
| — | [2.Todo](./Optimize/2.待落地优化Todo.md) | 决赛性能队列（H3/H4 与 P-A1 已冻结） |
| — | [1](./Optimize/1.TPC-C性能优化分析.md) · [4](./Optimize/4.S5-WAL组提交深化.md) · [11](./Optimize/11.WAL四项优化实现设计.md) | 分析 / WAL 专题 |
| — | [6.搁置](./Optimize/6.搁置与低优先级方案.md) · [7.外部参考](./Optimize/7.外部参考-DuckDB与ClickHouse.md) · [8.火焰图](./Optimize/8.火焰图实测与性能对比分析.md) | 备忘 |
| — | [10.注释](./Optimize/10.代码注释与人味化备忘.md) | 改 `src/` 纪律 |
| — | [Analysis/ProjectAnalysis](./Analysis/ProjectAnalysis/README.md) | 代码定位 / 陷阱 / 调用链；含 [JOIN 扩展架构](./Analysis/ProjectAnalysis/7.JOIN扩展架构.md) |

**仓库可见文件约束**：`notes/personal/项目注意.md`。

---

## 目录结构

```
Docs/
├── README.md                 ← 本文件
├── FinalCompetition/              决赛赛题 + 主线 Todo
│   ├── README.md
│   ├── 决赛赛题整理.md          ← 决赛 PDF 完整要点（先读）
│   ├── 决赛准备Todo.md          ← 决赛主线
│   ├── 线下赛与答辩准备计划.md    ← 当前执行计划
│   ├── 0803-OJ性能测评报告摘要.md ← 最新 P-A1 提交 PASS + 新性能锚点
│   ├── 0802-OJ性能测评报告摘要.md ← H4 冻结历史样本
│   ├── 0801-OJ性能测评报告摘要.md ← H3 冻结历史样本 + 性能锚点
│   └── 一致性审计与回归用例.md
├── Optimize/                 初赛性能档案 + 决赛现行裁决（0–15）
│   ├── 0.优化索引.md
│   ├── 1.TPC-C性能优化分析.md
│   ├── 2.待落地优化Todo.md      ← 决赛活动队列
│   ├── 3.方案A-C优化记录与tpmC评估.md
│   ├── 4.S5-WAL组提交深化.md
│   ├── 5.已完成优化摘要.md      ← 分类总表（初赛）
│   ├── 6.搁置与低优先级方案.md
│   ├── 7.外部参考-DuckDB与ClickHouse.md
│   ├── 8.火焰图实测与性能对比分析.md
│   ├── 9.OJ成绩与提交记录.md
│   ├── 10.代码注释与人味化备忘.md
│   ├── 11.WAL四项优化实现设计.md
│   ├── 12.初赛冻结备忘.md       ← 历史档案
│   ├── 13.P0正确性后性能复验收尾.md
│   ├── 14.决赛优化状态与实施计划.md ← 现行裁决与验收方案
│   └── 15.P-A1归因取证.md       ← 诊断闭环与单变量证据
├── Analysis/
└── RMDB/
```

---

## 当前状态（2026-08-03 · 决赛）

| 项 | 状态 |
|----|------|
| 初赛 P1–P10 | ✅ |
| **初赛 OJ 锚点** | **`5a5527f` · AC · 3615.5 tpmC**（独享机，历史） |
| 本地 / gitlab | HEAD `c446377`（P-A1；含 H3/H4 冻结修复） |
| 本机远端 | 仅 **`gitlab`** |
| **阶段** | **决赛**：正确性门禁 → Wire v3 / SI·SER / COUNT(DISTINCT) / FLOAT32 → W=50×32 |
| **最新官方评测** | `c446377` **PASS**；**40,264.8 NewOrder/min**；装载 **620.61s**；升级为新性能锚点 |
| 正确性状态 | H3/H4 已冻结；P-A1 实现与 W=5 取证完成，W=50 原因分布终验因资源约束冻结 |
| 优化现行裁决 | **[§14](./Optimize/14.决赛优化状态与实施计划.md)**；已落地见 [§5](./Optimize/5.已完成优化摘要.md) |

---

## 常用命令

```bash
cd build && make rmdb -j$(nproc)
python3 tests/run_tests.py
python3 tests/local/consistency/run_consistency_regress.py \
  --case C1 --case C2 --case H3 --case H4 --case H5
# 决赛形本地门禁：W=50 形数据 / Wire v3 / SI / 32 客户端
python3 tests/finals/performance_test.py
python3 tests/local/bench_tpcc.py --quick --threads 16
python3 tests/local/run_oj_perf_test.py --mid --scale local  # W=5 趋势
python3 tests/local/run_oj_perf_test.py --finals             # W=50×32 本地正式形
bash tests/prof/profile_tpcc.sh --measure 60
```

工作流：`AGENTS.md`（仅本机，见项目注意）。
