# 功能测试用例（`tests/framework/`）

P2 官方回归入口：[`tests/run_tests.py`](../run_tests.py)  
本目录存放各题目**专项测试脚本**（P3–P7、EXPLAIN 等）。

## 快速开始

```bash
# P2 官方 11 测试点
python3 tests/run_tests.py

# P5 聚合回归
python3 tests/framework/edge_cases/test_regression_p5.py

# P6 Union
python3 tests/framework/p6_union_test.py

# P4 EXPLAIN
python3 tests/framework/explain/test_official_exact.py

# JOIN 线下验收（严格校验 NLJ/INLJ 与扩展 JOIN 的 EXPLAIN）
python3 tests/framework/explain/test_join_acceptance.py
```

## 目录概览

| 目录 | 说明 |
|------|------|
| `aggregates/` | P5 聚合 |
| `explain/` | P4 EXPLAIN |
| `edge_cases/` | 边界与回归 |
| `joins/` | P7 连接 |
| `explain/test_join_acceptance.py` | JOIN 精确计划与运行时行数门禁 |
| `p3/` | 索引相关 |
| `test_db7` … | P2 测例数据库快照 |

详见各子目录脚本内注释。
