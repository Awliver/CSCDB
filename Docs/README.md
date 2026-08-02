# Docs 文档索引（本地，不上传远程）

> 队伍 **orzcle** · RMDB 2026 · **决赛阶段**  
> 最后更新：**2026-08-01**
> **决赛规范**：[FinalCompetition/决赛赛题整理](./FinalCompetition/决赛赛题整理.md) · **主线 Todo**：[FinalCompetition/决赛准备](./FinalCompetition/决赛准备Todo.md)  
> **现行优化裁决**：[Optimize/14](./Optimize/14.决赛优化状态与实施计划.md) · 初赛锚点：[Optimize/12](./Optimize/12.初赛冻结备忘.md) · 已落地：[Optimize/5](./Optimize/5.已完成优化摘要.md)

---

## 先读什么

| 顺序 | 文档 | 用途 |
|:--:|------|------|
| 1 | **[决赛赛题整理](./FinalCompetition/决赛赛题整理.md)** | **决赛 PDF 完整要点（先读）** |
| 2 | **[决赛准备 Todo](./FinalCompetition/决赛准备Todo.md)** | **决赛主线：门禁 → 增量 → 排名** |
| 3 | [08-01 OJ 报告摘要](./FinalCompetition/0801-OJ性能测评报告摘要.md) | **最新 `bc2c208` 全 PASS · 34,592；性能锚点 37,248.4** |
| 4 | [一致性审计](./FinalCompetition/一致性审计与回归用例.md) | 本地 C1–M8 回归与定位档案 |
| 5 | **[14.决赛优化状态与实施计划](./Optimize/14.决赛优化状态与实施计划.md)** | **无效项清退、有限项冻结、活动项计划与量化验收** |
| 6 | **[5.已完成优化分类总表](./Optimize/5.已完成优化摘要.md)** | 当前 HEAD 按层归类的落地清单 |
| 7 | [0.优化索引](./Optimize/0.优化索引.md) | 现状一页 + 文档地图 |
| — | [12.初赛冻结](./Optimize/12.初赛冻结备忘.md) | 初赛历史锚点（档案） |
| — | [9.OJ 成绩](./Optimize/9.OJ成绩与提交记录.md) | 初赛交卷与成绩锚点 |
| — | [3.落地日志](./Optimize/3.方案A-C优化记录与tpmC评估.md) | tpmC / profile 权威数字 |
| — | [13.P0 后 mid 收尾](./Optimize/13.P0正确性后性能复验收尾.md) | 初赛三轮完整门禁 · ~3970 / abort 0 |
| — | [08-01 OJ 报告摘要](./FinalCompetition/0801-OJ性能测评报告摘要.md) | **最新正式结果**：`bc2c208` 全 PASS · **34,592**；回退锚点 **37,248.4** |
| — | [2.Todo](./Optimize/2.待落地优化Todo.md) | 决赛活动队列（H3 已冻结；H4 → 归因 → 条件式性能实验） |
| — | [1](./Optimize/1.TPC-C性能优化分析.md) · [4](./Optimize/4.S5-WAL组提交深化.md) · [11](./Optimize/11.WAL四项优化实现设计.md) | 分析 / WAL 专题 |
| — | [6.搁置](./Optimize/6.搁置与低优先级方案.md) · [7.外部参考](./Optimize/7.外部参考-DuckDB与ClickHouse.md) · [8.火焰图](./Optimize/8.火焰图实测与性能对比分析.md) | 备忘 |
| — | [10.注释](./Optimize/10.代码注释与人味化备忘.md) | 改 `src/` 纪律 |
| — | [Analysis/ProjectAnalysis](./Analysis/ProjectAnalysis/README.md) | 代码定位 / 陷阱 / 调用链 |

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
│   ├── 0801-OJ性能测评报告摘要.md ← 最新 PASS + 性能回退锚点
│   └── 一致性审计与回归用例.md
├── Optimize/                 初赛性能档案 + 决赛现行裁决（0–14）
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
│   └── 14.决赛优化状态与实施计划.md ← 现行裁决与验收方案
├── Analysis/
└── RMDB/
```

---

## 当前状态（2026-08-01 · 决赛）

| 项 | 状态 |
|----|------|
| 初赛 P1–P10 | ✅ |
| **初赛 OJ 锚点** | **`5a5527f` · AC · 3615.5 tpmC**（独享机，历史） |
| 本地 / gitlab | HEAD `bc2c208`（H3 冻结；源码提交 `d39fcdb`） |
| 本机远端 | 仅 **`gitlab`** |
| **阶段** | **决赛**：正确性门禁 → Wire v3 / SI·SER / COUNT(DISTINCT) / FLOAT32 → W=50×32 |
| **最新官方评测** | `bc2c208` **PASS**；**34,592 NewOrder/min**；装载 **711.11s**；性能锚点仍为 `c524ff8` **37,248.4** |
| 正确性残口 | H3 INLJ SSI 已冻结；剩余 H4 `active_rts_` 锁域，完成前不启动排名改造 |
| 优化现行裁决 | **[§14](./Optimize/14.决赛优化状态与实施计划.md)**；已落地见 [§5](./Optimize/5.已完成优化摘要.md) |

---

## 常用命令

```bash
cd build && make rmdb -j$(nproc)
python3 tests/run_tests.py
python3 tests/local/consistency/run_consistency_regress.py \
  --case C1 --case C2 --case H5
# 决赛形本地门禁：W=50 形数据 / Wire v3 / SI / 32 客户端
python3 tests/finals/performance_test.py
python3 tests/local/bench_tpcc.py --quick --threads 16
python3 tests/local/run_oj_perf_test.py --strict   # W=5 趋势；决赛正式规格为 W=50×32
bash tests/prof/profile_tpcc.sh --measure 60
```

工作流：`AGENTS.md`（仅本机，见项目注意）。
