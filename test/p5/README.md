# P5 聚合函数与分组统计 - 测试集

## 快速开始

```bash
cd /home/neo/CSC_DB/db2026
python3 test/edge_cases/test_regression_p5.py
```

- 返回码 `0` = 全部通过（无新失败）
- 返回码 `1` = 有新失败（需要修复）

## 文件说明

| 文件 | 职责 | 使用时机 |
|------|------|----------|
| `test_regression_p5.py` | **核心回归测试**，覆盖聚合、GROUP BY、HAVING、ORDER BY、LIMIT、JOIN、健壮性、表别名、无AS别名。输出 PASS/FAIL/KNOWN 汇总。 | **每次修复后必跑** |
| `test_table_alias_comprehensive.py` | 表别名盲区测试（17 场景）。验证 `FROM table alias` 后 `alias.column` 是否正常工作。 | 修复表别名后验证 |
| `test_no_as_alias_comprehensive.py` | 无AS别名盲区测试（28 场景）。验证 `SELECT expr alias` 和 `ORDER BY alias` 是否正常工作。 | 修复无AS别名后验证 |
| `test_limit_agg.py` | LIMIT + 聚合基础测试（19 场景）。 | 修复 LIMIT 后验证 |
| `test_order_limit.py` | ORDER BY 聚合别名 + LIMIT 组合测试。 | 修复 ORDER BY 后验证 |

## 当前状态（基于最新代码）

```
test_regression_p5.py: 43/51 通过, 0 新失败, 8 已知盲区
```

### 已知盲区（8 个）

| 类别 | 数量 | 现象 | 根因 |
|------|------|------|------|
| 表别名不支持 | 7 | `SELECT a.id FROM t a` 返回空 | analyze/planner 未处理 `FROM table alias` 的别名映射 |
| 无AS列别名+ORDER BY | 1 | `SELECT id i FROM t ORDER BY i` 报错 `Column not found` | ORDER BY 查找时未识别无AS列别名 |

## 测试数据说明

`test_regression_p5.py` 内部自动完成建表、插数、测试、清理，无需手动准备数据。

涉及表：
- `grade` (course char(20), id int, score float) — 4 行数据
- `empty_grade` — 空表（测试空表默认值）
- `student` (id int, name char(10)) — 2 行数据
- `score` (student_id int, course char(10), score float) — 3 行数据

## 清理

如需删除本目录：

```bash
rm -rf test/edge_cases/
```
