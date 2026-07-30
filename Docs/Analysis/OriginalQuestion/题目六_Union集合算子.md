6. 2026题目六：Union 集合算子
前置题目：存储管理、B+树索引、查询执行、聚合函数与分组统计

题目种类：基础功能

推荐知识点：Union 集合算子

大赛框架：https://gitlab.eduxiji.net/csc1/csc-db/db2026/-/tree/main/rmdb

1 赛题背景
集合操作是 SQL 语言的核心功能之一。Union 算子用于合并多个分支查询的元组集合并去除重复元组，是大数据分析、多源数据融合等场景的关键算子。本赛题要求支持 Union 操作，并对结果集排序。

2 题目总览
本题要求参赛者在 RMDB 中实现 Union 算子，并针对查询间数据模式一致性、数据类型转换等场景完善健壮性保证机制，同时支持对 Union 的输出执行 ORDER BY 整体排序。

3 题目 6.1：Union 算子实现与 ORDER BY 支持
3.1 题目描述
本小题要求实现 Union 算子，并支持对 Union 结果进行整体排序。

记该物理算子为 U，U 包含了 n 个子节点构成的列表 {R1, R2, ..., Rn}（其中 n ≥ 2）。U 遍历所有子节点，收集全部元组后去除重复元组（所有对应列的值均相等），最终返回去重后的元组集合。

ORDER BY 子句作用于 Union 算子的最终输出，需支持多列排序与 ASC/DESC 指定。排序键限定在 Union 输出列的子集（默认引用第一个子节点的列名）。

测试用例会将 Union 结果作为 FROM 子句中的派生表使用，形式如下：

SELECT * FROM (<select_stmt> UNION <select_stmt> [UNION <select_stmt> ...]) AS alias
ORDER BY ...
参赛者需支持上述派生表语法，至少覆盖 Union 结果作为外层 SELECT * 数据源的场景；当外层带有 ORDER BY 时，排序作用于 Union 的整体输出，列名按照 Union 输出列解析。

3.2 提示
可将 U 视作多叉树结构进行处理，也可以将 U 转换为左深、右深结构处理。

去重可基于哈希表实现，也可基于排序实现。

Union 的输出列名默认继承自第一个子节点 R1 的列名。

对于多列排序，需按排序键列表顺序依次比较；未显式指定时默认按 ASC 处理。

3.3 测试示例
CREATE TABLE orders1 (
    order_id INT,
    amount FLOAT,
    region CHAR(10)
);

CREATE TABLE orders2 (
    order_id INT,
    amount FLOAT,
    region CHAR(10)
);

CREATE TABLE orders3 (
    order_id INT,
    amount FLOAT,
    region CHAR(10)
);

INSERT INTO orders1 VALUES (1, 150.0, 'Beijing');
INSERT INTO orders1 VALUES (2, 230.5, 'Shanghai');
INSERT INTO orders1 VALUES (3, 89.99, 'Guangzhou');

INSERT INTO orders2 VALUES (1, 150.0, 'Beijing');
INSERT INTO orders2 VALUES (4, 120.0, 'Shenzhen');
INSERT INTO orders2 VALUES (5, 560.0, 'Chengdu');

INSERT INTO orders3 VALUES (1, 150.0, 'Beijing');
INSERT INTO orders3 VALUES (6, 199.99, 'Wuhan');

SELECT * FROM
    (SELECT * FROM orders1
     UNION
     SELECT * FROM orders2
     UNION
     SELECT * FROM orders3) AS all_orders
ORDER BY amount DESC;
预期结果：

| order_id | amount | region |
| 5 | 560 | Chengdu |
| 2 | 230.5 | Shanghai |
| 6 | 199.99 | Wuhan |
| 1 | 150 | Beijing |
| 4 | 120 | Shenzhen |
| 3 | 89.99 | Guangzhou |
4 题目 6.2：类型系统兼容性与异常处理
4.1 题目描述
Union 的语义正确性依赖于分支查询间的数据模式兼容性约束。本小题要求在查询分析阶段实现完备的数据模式检验机制：严格保证各分支查询返回的列数量一致，且对应位置列的数据类型满足兼容性判定 (类型相同或支持隐式转换)；在执行阶段，Union 算子需执行统一的数据类型提升策略，将相应列的数据类型转换为公共超类型，确保各分支查询的异构元组最终遵循统一的数据模式。

4.2 提示
公共超类型是指能够无损容纳所有分支查询对应列数据类型的最通用类型。对于本小题，只需要考虑下面两种情况。

数据类型提升与对应的公共超类型：

INT 与 FLOAT ⇒ FLOAT

CHAR(n) 与 CHAR(m) ⇒ CHAR(max(n, m))，按照字符串实际长度输出即可，无需增添空格。

4.3 测试示例
CREATE TABLE orders1 (
    order_id INT,
    amount INT,
    region CHAR(10)
);

CREATE TABLE orders2 (
    order_id INT,
    amount FLOAT,
    region CHAR(20)
);

-- 错误示例 1：列数量不一致
SELECT * FROM
    (SELECT amount FROM orders1
     UNION
     SELECT * FROM orders2
    ) AS all_orders;

-- 错误示例 2：数据类型不兼容
SELECT * FROM
    (SELECT amount FROM orders1
     UNION
     SELECT region FROM orders2
    ) AS all_orders;

-- 错误示例 3：ORDER BY 出现未知列
SELECT * FROM
    (SELECT amount FROM orders1
     UNION
     SELECT amount FROM orders2
    ) AS all_orders
ORDER BY order_id ASC;

-- 类型提升测试示例
-- amount 列：INT 与 FLOAT 提升为 FLOAT
-- region 列：CHAR(10) 与 CHAR(20) 提升为 CHAR(20)
-- 预期结果的第一行（虚线部分）数据模式无需返回。
SELECT * FROM
    (SELECT * FROM orders1
     UNION
     SELECT * FROM orders2
    ) AS all_orders
ORDER BY amount DESC, order_id ASC;
错误示例预期结果：

错误示例 1 → failure

错误示例 2 → failure

错误示例 3 → failure

类型提升测试预期结果：

数据模式：INT | FLOAT | CHAR(20)

| order_id | amount | region |
| ... | ... | ... |
| ... | ... | ... |
| ... | ... | ... |
| ... | ... | ... |
| ... | ... | ... |\
