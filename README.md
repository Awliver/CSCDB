## 2026年全国大学生计算机系统能力大赛 — 数据库管理系统设计赛

> 基于赛方 [RMDB 框架](https://gitlab.eduxiji.net/csc1/csc-db/db2026)，详见 [README_rmdb.md](README_rmdb.md)

## 队伍信息

| | |
|---|---|
| **队伍 ID** | T2026104879910631 |
| **队伍名称** | orzcle |
| **学校** | 华中科技大学 |
| **队员** | 王圣翊、李正文、甘可欣 |

## 项目状态（2026-08-03）

| 项 | 状态 |
|----|------|
| 初赛 P1–P10 | 全部完成 · OJ AC |
| 初赛性能锚点 | `5a5527f` · 3615.5 tpmC（历史） |
| **当前阶段** | **决赛** |
| 决赛成绩结构 | 正确性门禁 100/0 + NewOrder/min 排名（SI） |
| **最新决赛 OJ** | `c446377` **全 PASS · 40,264.8 NewOrder/min**；较旧锚点 +8.10%，已升级为新的正确性与性能锚点 |
| **当前工作** | P-A1 实现与 W=5 单变量取证完成；W=50 开启态终验因本机资源约束冻结，转入架构定稿与线下答辩准备 |
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
| P7 | NLJ / INLJ | Done |
| P8 | 事务控制 | Done |
| P9 | 隔离级别 | Done |
| P10 | 故障恢复 | Done |

## 架构概览

```
Parser → Analyze → Optimizer → Portal → Execution
Transaction / Lock / Log / Recovery
B+ Index · Slotted-Page Record · Buffer Pool · Disk
```

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

优化须落实在通用的索引、执行器、事务、缓存及 WAL 等机制中，不得针对评测固定表名/SQL 编写专用旁路，不得改变 SQL 语义；不得为性能降低 ACID。
