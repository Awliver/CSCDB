# RMDB 功能测试用例（`test/`）

本目录存放 **P2 官方功能回归**与各题目**专项测试脚本**。本地一键入口推荐使用 [`tests/run_tests.py`](../tests/README.md)（转发到本目录的 `run_tests.py`）。

## 前置条件

```bash
cd build && make rmdb -j$(nproc)
```

测试通过 TCP（`127.0.0.1:8765`）与 `rmdb` 通信：SQL 以 `\0` 结尾；`select` 结果写入数据库目录下的 `output.txt` 再比对。

## 快速开始

```bash
# P2 官方 11 测试点（推荐路径）
python3 tests/run_tests.py
# 或直接
python3 test/run_tests.py

# P5 聚合回归（51 场景，改聚合相关代码后必跑）
python3 test/edge_cases/test_regression_p5.py

# P6 Union
python3 test/p6_union_test.py

# P4 EXPLAIN（单文件示例）
python3 test/explain/test_official_exact.py
```

## 核心入口：`run_tests.py`

自动完成：清理旧库 → 启动 `rmdb` → 逐条发 SQL → 读 `output.txt` → 与期望逐行比对 → 汇总 PASS/FAIL。

### 11 个测试点

| # | 名称 | 数据库目录 | 覆盖能力 |
|---|------|------------|----------|
| 1 | 尝试建表 | `tp1_db` | CREATE / DROP / SHOW TABLES |
| 2 | 单表插入与条件查询 | `tp2_db` | INSERT、WHERE、投影 |
| 3 | 单表更新与条件查询 | `tp3_db` | UPDATE、条件更新 |
| 4 | 单表删除与条件查询 | `tp4_db` | DELETE |
| 5 | 连接查询 | `tp5_db` | JOIN（含无序结果块比对） |
| 6 | 单独使用聚合函数 | `tp6_db` | COUNT / SUM / AVG / MIN / MAX |
| 7 | 聚合函数加分组统计 | `tp7_db` | GROUP BY、HAVING |
| 8 | 健壮性测试 | `tp8_db` | 非法 SQL、空表、类型边界 |
| 9 | 边界情况测试 | `tp9_db` | 复杂 WHERE、多条件 |
| 10 | JOIN 聚合测试 | `tp10_db` | JOIN + 聚合组合 |
| 11 | 大规模聚合综合测试 | `tp11_db` | 大数据量聚合、多表 JOIN |

- 全部通过：退出码 `0`
- 任一失败：退出码 `1`，打印期望/实际 diff

数据库与 `output.txt` 生成在 `build/test_dbs/<name>/`（脚本在 `build/` 下启动服务器）。

## 目录结构（按题目 / 主题）

```
test/
├── run_tests.py           # P2 官方 11 点（主回归）
├── p6_union_test.py       # P6 Union 算子
├── aggregates/            # P5 聚合专项（5 个脚本）
├── aliases/               # 表别名 / 无 AS 列别名（3 个）
├── edge_cases/            # 边界、格式、P5 回归等（11 个）
├── explain/               # P4 EXPLAIN 输出格式（20 个）
├── gaps/                  # 已知未支持语法探测（3 个）
├── joins/                 # P7 JOIN 语法（3 个）
├── massive/               # 大规模聚合压力（10 个）
├── p3/                    # P3 唯一索引（9 个）
├── p5/                    # P5 说明文档（README.md）
└── test_db* / test_idx*   # 历史调试残留的数据库目录（可忽略或删除）
```

### 各子目录用途

| 目录 | 题号 | 脚本数 | 说明 |
|------|------|--------|------|
| `p3/` | P3 唯一索引 | 9 | 索引 + DML、parser 盲区、fuzz |
| `aggregates/` | P5 | 5 | 聚合函数、LIMIT+聚合、NULL 处理 |
| `edge_cases/` | P2/P5 | 11 | **`test_regression_p5.py` 为 P5 核心回归** |
| `explain/` | P4 | 20 | EXPLAIN 计划格式、官方样例对齐 |
| `joins/` | P7 | 3 | JOIN ON、ORDER BY + JOIN |
| `massive/` | P5 | 10 | 大规模 GROUP BY / ORDER BY / LIMIT 组合 |
| `aliases/` | P5/P7 | 3 | `FROM t alias`、`SELECT expr alias` |
| `gaps/` | — | 3 | HAVING/ORDER BY 等已知缺口探测 |

各脚本均为**独立可执行**：内部自建库、插数、断言、清理；多数返回 `0`/`1` 表示通过/失败。

## 推荐工作流

### 改存储 / 缓冲 / 记录层

```bash
python3 tests/run_tests.py
```

### 改查询执行 / 聚合 / JOIN

```bash
python3 tests/run_tests.py
python3 test/edge_cases/test_regression_p5.py
# 若动 JOIN：
python3 test/joins/test_join_on.py
```

### 改唯一索引（P3）

```bash
python3 test/p3/test_p3_targeted.py
python3 test/p3/test_p3_fuzz.py
```

### 改 EXPLAIN / 优化器（P4）

```bash
python3 test/explain/test_official_exact.py
python3 test/explain/test_official.py
```

### 改 Union（P6）

```bash
python3 test/p6_union_test.py
```

### 性能优化阶段（TPC-C）

P2 的 11 测试点是 **OJ 性能测试的硬性前置**（题目规定：功能测试须全部通过，否则性能得 0 分）。完整 OJ 对齐说明见 [`tests/README.md` →「OJ 性能测试对齐」](../tests/README.md)。

```bash
# 日常：功能不退化
python3 tests/run_tests.py

# 日常：快看 tpmC 趋势（不能代表排名）
python3 tests/local/bench_tpcc_neworder.py --quick

# 提交前：模拟线上 OJ（Release 构建 + full 数据 + strict）
python3 tests/local/run_oj_perf_test.py --strict
```

`test/` 目录本身不含 TPC-C 压测脚本；性能测试统一在 `tests/local/`。

## P5 专项说明

[`p5/README.md`](p5/README.md) 记录 P5 聚合测试集细节与已知盲区。核心脚本：

```bash
python3 test/edge_cases/test_regression_p5.py   # 每次改聚合后必跑
python3 test/aliases/test_table_alias_comprehensive.py
python3 test/aliases/test_no_as_alias_comprehensive.py
```

## `test_db*` 目录

`test_db7` … `test_db12`、`test_idx*`、`test_verify` 等为开发期手动调试留下的**数据库文件**，不是自动化脚本。可安全删除；自动化测试会在 `build/test_dbs/` 下重新建库。

## 编写新用例

1. 参考同目录现有脚本：`BUILD_DIR`、`PORT=8765`、`send_sql`、起停服务器模式一致。
2. 优先放入对应题目子目录；跨题回归放 `edge_cases/`。
3. 使用 `os.path.join(..., "build")` 相对项目根路径，避免硬编码绝对路径。
4. 断言 `select` 结果时读 `output.txt` 或直接解析 socket 回复（表格行以 `|` 开头）。

## 常见问题

| 现象 | 处理 |
|------|------|
| 连接超时 | 确认 `build/bin/rmdb` 已编译；端口 8765 未被占用 |
| 期望行数不一致 | 对比浮点格式（如 `90.500000`）、列顺序、表头 |
| `test_db*` 干扰 | 删除对应目录；脚本使用 `build/test_dbs/` |
| P4 官方格式 | 参考 `explain/test_official*.py` 的制表符缩进与 `type=` 行 |

## 与 `tests/` 的关系

| 需求 | 使用 |
|------|------|
| P2 11 点功能回归 | `tests/run_tests.py` 或 `test/run_tests.py` |
| 单题深度用例 | `test/<topic>/test_*.py` |
| TPC-C / 性能 / 剖析 | [`tests/`](../tests/README.md) 下 `local/`、`prof/` |
