## 2026年全国大学生计算机系统能力大赛 — 数据库管理系统设计赛

> 基于赛方 [RMDB 框架](https://gitlab.eduxiji.net/csc1/csc-db/db2026)，详见 [README_rmdb.md](README_rmdb.md)

## 队伍信息

| | |
|---|---|
| **队伍 ID** | T2026104879910631 |
| **队伍名称** | orzcle |
| **学校** | 华中科技大学 |
| **队员** | 王圣翊、李正文、甘可欣 |

## 项目状态（2026-07-05）

| 项 | 状态 |
|----|------|
| P1–P10 功能 | 全部完成 |
| OJ 最新提交 | AC |
| OJ median tpmC | 799.83 |
| 当前阶段 | 性能优化 |

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

## 构建与运行

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make rmdb -j$(nproc)
mkdir -p my_db && ./bin/rmdb my_db
```

默认监听端口 `8765`，客户端发送以 `\0` 结尾的 SQL 文本。框架说明见 `docs_rmdb/` 目录下的 PDF 文档。
