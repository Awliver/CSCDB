## 2026年全国大学生计算机系统能力大赛 — 数据库管理系统设计赛

> 基于赛方 [RMDB 框架](https://gitlab.eduxiji.net/csc1/csc-db/db2026)，详见 [README_rmdb.md](README_rmdb.md)

## 队伍信息

| | |
|---|---|
| **队伍 ID** | T2026104879910631 |
| **队伍名称** | orzcle |
| **学校** | 华中科技大学 |
| **队员** | 王圣翊、李正文、甘可欣 |

## 项目状态（2026-08-11）

| 项 | 状态 |
|----|------|
| 初赛 P1–P10 | 全部完成 · OJ AC |
| 初赛性能锚点 | `5a5527f` · 3615.5 tpmC（历史） |
| **当前阶段** | **决赛** |
| 决赛成绩结构 | 正确性门禁 100/0 + NewOrder/min 排名（SI） |
| **性能锚点** | `c446377` **全 PASS · 40,264.8 NewOrder/min**；较旧锚点 +8.10%，仍为性能回退基线 |
| **最新决赛 OJ** | 2026-08-05 12:24 全流程 PASS · **29,650.4 NewOrder/min** · abort-rate 0.17%；用于记录“压低 abort 但损失并行度”的反例，不替换性能锚点 |
| **当前工作** | JOIN 扩展主线完成并补齐架构文档；唯一索引的重复键/唯一性语义作为下一条独立改动推进 |
| **JOIN 扩展** | 基本 JOIN 已通过 OJ；结构化 JOIN Tree 与 LEFT/RIGHT/FULL NULL 扩展已通过本地完整功能测试 12/12，OJ 未专项覆盖新增语义 |
| 本机 Git 远端 | 仅 `gitlab`（教育平台） |

主线文档：[`Docs/FinalCompetition/决赛赛题整理.md`](Docs/FinalCompetition/决赛赛题整理.md) · [`Docs/FinalCompetition/决赛准备Todo.md`](Docs/FinalCompetition/决赛准备Todo.md) · [`Docs/FinalCompetition/线下赛与答辩准备计划.md`](Docs/FinalCompetition/线下赛与答辩准备计划.md)

## 系统模块

| 题号 | 题目 | 状态 |
| ---- | ---- | ---- |
| P1 | 存储管理 | Done |
| P2 | 查询执行 | Done |
| P3 | 唯一索引 | Done |
| P4 | 查询优化 | Done |
| P5 | 聚合 | Done |
| P6 | Union 算子 | Done |
| P7 | JOIN Tree / NLJ / INLJ / 外连接 | Done（基本 JOIN OJ；扩展本地回归） |
| P8 | 事务控制 | Done |
| P9 | 隔离级别 | Done |
| P10 | 故障恢复 | Done |

## 架构概览

```
Parser → Analyze → Optimizer → Portal → Execution
Transaction / Lock / Log / Recovery
B+ Index · Slotted-Page Record · Buffer Pool · Disk
```

JOIN 的跨层架构、优化边界和 NULL 协议见 [`Docs/Analysis/ProjectAnalysis/7.JOIN扩展架构.md`](Docs/Analysis/ProjectAnalysis/7.JOIN扩展架构.md)。

## 构建与运行

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make rmdb -j$(nproc)
mkdir -p my_db && ./bin/rmdb my_db
```

默认监听端口 `8765`。决赛正式测评走 **Wire Protocol v3**；本地初赛遗留回归仍可用 NUL 结尾 SQL。框架说明见 `docs_rmdb/`。

## 提交约束

**禁止修改任何 `CMakeLists.txt` 文件**（含根目录、`src/`、`src/parser/`、`rmdb_client/` 等）。OJ 评测使用赛方提供的原始构建脚本；改动 CMake 可能导致编译失败、链接错误或评测环境不一致。性能优化请只改 `src/` 等业务代码，通过 `cmake` 命令行参数调整构建类型（如 `-DCMAKE_BUILD_TYPE=Release`），勿改 CMake 文件本身。

优化须落实在通用的索引、执行器、事务、缓存及 WAL 等机制中，不得改变 SQL 语义；不得为性能降低 ACID。

**禁止任何硬编码或特判旁路**：不得以公开或猜测的表名、列名、索引名、CSV/数据库文件名、SQL 文本/前缀、prepared-statement ID、字面量、仓库/客户/商品 ID、客户端编号、随机种子、装载顺序、固定数据分布或测试阶段作为优化分支条件；也不得探测或依赖评测资产。允许的优化必须由运行时 schema、类型、索引元数据和协议结构**通用推导**，并对任意等价的不透明标识符与合法输入保持相同行为。

**性能目标**：所有优化都必须先满足上述比赛规则、项目规范、Wire 语义与 ACID 门禁；在此前提下，唯一的性能目标是尽可能提高正式 W=50×32、3×150s 测量的 **median NewOrder/min（Rank）**。`abort-rate`、延迟、CPU、I/O 等均为约束或诊断指标，不能替代 Rank，更不得为了改善其中任一指标而接受 Rank 下降。
