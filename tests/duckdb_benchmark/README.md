# DuckDB benchmark 的 RMDB 兼容迁移

本目录把 DuckDB 的 interpreted benchmark 格式迁移到 RMDB 的 Wire Protocol v3
测试路径。上游基线是
[`duckdb/duckdb@56b0c8f`](https://github.com/duckdb/duckdb/tree/56b0c8f6b2326a11267b199b0b34969efefd7c47/benchmark)，
完整提交号见 `UPSTREAM_COMMIT`，上游代码与 benchmark 资产采用 MIT License，副本见
`LICENSE.duckdb`。

## 快速运行

先构建现有 RMDB，不需要修改 CMake：

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make rmdb -j$(nproc)
cd ..
```

列出或运行用例：

```bash
python3 tests/duckdb_benchmark/runner.py --list
python3 tests/duckdb_benchmark/runner.py
python3 tests/duckdb_benchmark/runner.py 'aggregate|index' --timed-runs 3
python3 tests/duckdb_benchmark/runner.py 'index/point' --query
```

运行器为每个用例创建独立临时数据库，依次完成：

1. 启动 `build/bin/rmdb` 并进行 Wire v3 握手；
2. 执行 `load` DDL；
3. 根据 `generate` 生成无表头 CSV，再走 RMDB 原生 `LOAD`；
4. 执行 `postload`（主要用于 LOAD 后建索引）；
5. 先核对 `result`，再做 1 次预热和默认 5 次计时；
6. 执行每轮 `cleanup`，关闭服务器并删除临时数据库。

输出与 DuckDB runner 一样使用 `name/run/timing` 三列。`--out FILE` 只写耗时，
`--json FILE` 写入含上游提交号的结构化结果。端口 `8765` 已被占用时，运行器会拒绝
杀掉现有进程并给出错误；可用 `--keep-db` 保留临时数据库和 `server.log`。

## 已迁移范围

上游该提交含 1187 个 `.benchmark`，其中 385 个位于 `benchmark/micro`。RMDB 当前仅有
`INT/FLOAT/CHAR`、基础 DDL/DML、等值/范围条件、聚合、排序、Limit、Union 和
NLJ/INLJ，无法原样执行依赖 DuckDB 扩展能力的全部用例。本目录选择 10 个与现有内核
能力相符的 micro 用例：

| 目录 | 用例 | RMDB 覆盖 |
|---|---|---|
| aggregate | `simple_aggregate` | 无分组 SUM |
| aggregate | `simple_group` | GROUP BY + ORDER BY |
| aggregate | `simple_distinct` | 原生 COUNT(DISTINCT) |
| filter | `parallel_complex_filter` | 整数范围过滤 |
| index/create | `create_art_sorted` | 唯一索引构建 |
| index/point | `point_query_with_art` | 索引点查 |
| index/range | `wide_range_query_with_art` | 索引范围统计 |
| join | `hashjoin_highcardinality` | 高基数等值连接、分组和 Limit |
| limit | `parallel_limit` | 过滤、排序和 Limit |
| order | `orderby` | 双整数全表排序 |

文件保持 DuckDB 的 `name/group/load/run/result/cleanup` 段，并增加两个适配段：

- `generate`：`表名 行数 表达式...`，支持 `row`、`row+N`、`row-N`、`row%N`、
  `row*N%M` 和 `const:N`；
- `postload`：在 CSV 装载后执行的 SQL，避免 RMDB 的批量 LOAD 绕过预建索引维护。

## 方言和规模调整

- DuckDB 的 `CREATE TABLE AS SELECT ... FROM range(...)` 被替换为动态 CSV；
- `CREATE INDEX name ON table USING ART(col)` 映射为 `CREATE INDEX table(col)`；
- DuckDB 的非唯一 ART 用例只选择/生成唯一键，以符合 RMDB 当前唯一索引语义；
- `IN (subquery)`、`INNER JOIN ... ON` 等未支持语法改写为当前 RMDB 可表达的等价或
  明确缩小的过滤/逗号连接；
- 10M～100M 行的上游规模缩到 20K～100K 行，使它们适合作为本地回归和趋势测试；
- 大结果排序增加 `LIMIT`，避免把 Wire 结果传输成本误当成排序算子成本。

因此这些结果用于 RMDB 内部回归和同版本 A/B，不应与 DuckDB 官方数字横向比较。
DATE/TIMESTAMP/DECIMAL、NULL、CTE/子查询、窗口函数、复杂类型、CSV/Parquet 扫描、
扩展函数以及 TPC-H/TPC-DS 等用例仍明确不在当前迁移范围；待对应 SQL 能力加入后，
可继续沿用同一 runner 增补用例。
