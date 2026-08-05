# 学习笔记：单表 SELECT 的完整执行链

> **学习日期**：2026-08-04  
> **目标**：以一条最简单的单表查询为例，说明 RMDB 如何从 SQL 文本走到结果输出；先建立正确的控制流模型，不深入 MVCC、SER、聚合、排序、连接和性能特化分支。

示例表：

```sql
create table t (id int, name char(20), age int);
create index t(id);
```

示例查询：

```sql
select * from t where id = 1;
```

---

## 1. 四种中间形态

同一条 SQL 在系统中会依次变成四种不同对象。每一层只做自己的事。

| 形态 | 例子 | 作用 | 是否读取表记录 |
|---|---|---|:---:|
| SQL 字符串 | `select * from t where id=1` | 用户输入 | 否 |
| AST | `SelectStmt`，含表、列、条件 | 保存语法结构 | 否 |
| `Query` | 已确认表/列/条件合法的查询描述 | 语义分析结果 | 否（只读元数据） |
| `Plan` | `ProjectionPlan → ScanPlan` | 决定执行策略 | 否 |
| `Executor` | `ProjectionExecutor → *ScanExecutor` | 真正逐行取记录 | 是 |

总链路：

```text
Client
  → run_sql_statement
  → Parser（fast parser 或 yyparse）
  → AST
  → Analyze::do_analyze
  → Query
  → Planner
  → Plan tree
  → Portal
  → Executor tree
  → QlManager::select_from
  → Client
```

---

## 2. 接收 SQL 与解析：先理解语句，不查数据

公共入口是 [`run_sql_statement`](../../src/rmdb.cpp)；当前 SQL 文本通过其 `sql` 参数传入。

执行开始时会先创建 `Context` 并通过 `SetTransaction` 绑定当前事务。`Context` 可以看作这条语句的工具包：其中带有事务、锁、日志和结果输出所需的对象。准备 `Context` 不代表读取表 `t`。

随后有两条解析路径：

```text
try_fast_parse_sql(sql)
  ├─ 成功：直接得到 AST
  └─ 不适用：yyparse() 用通用词法/语法规则得到 AST
```

`try_fast_parse_sql` 不适用不等于 SQL 非法；只是它不属于快解析器支持的格式。真正如 `selcect ...` 一类的拼写错误，会在 `yyparse()` 的语法分析阶段失败。

对示例 SQL，AST 可近似画为：

```text
SelectStmt
├─ tables: [t]
├─ columns: *
└─ condition: id = 1
```

AST 只回答“用户写了什么”，尚未确认表 `t` 或列 `id` 是否真的存在。

---

## 3. Analyze：AST 变为语义合法的 Query

入口为 [`Analyze::do_analyze`](../../src/analyze/analyze.cpp)。它发现 AST 是 `ast::SelectStmt` 后，依次完成以下工作。

### 3.1 表名与别名

`query->tables = x->tabs` 得到：

```text
query->tables = [t]
```

随后统一建立真实表名与别名的映射。当前 SQL 没有显式别名，但仍会保存等价映射：

```text
t → t
```

若是 `select a.id from t a`，映射才会包含 `a → t`。表别名处理并不会因 `SELECT *` 跳过。

### 3.2 `*` 展开为真实列

Analyze 从元数据取出 `t` 的列定义：

```text
all_cols = [t.id, t.name, t.age]
```

该项目中，普通 `SELECT *` 的 `x->cols` 和 `x->aggs` 都为空。因此：

```cpp
if (query->cols.empty() && x->aggs.empty()) {
    for (auto &col : all_cols) {
        query->cols.push_back({col.tab_name, col.name});
    }
}
```

将得到：

```text
query->cols = [t.id, t.name, t.age]
```

后面的模块不再需要特殊处理 `*`。`SELECT *` 与明确写出所有列，在 Analyze 结束后拥有相同的列列表。

如果用户明确写 `select name from t`，Analyze 会通过 `check_column` 检查列存在性、歧义和归属，并补全列信息。

### 3.3 WHERE 条件

```cpp
get_clause(x->conds, query->conds);
check_clause(query->tables, query->conds);
```

第一行将 AST 中的 `id = 1` 转为内部 `Condition`；第二行检查它能否在当前查询中成立，例如 `id` 是否存在、是否歧义、类型是否可比较。

此时结果可概括为：

```text
Query {
  tables = [t]
  cols   = [t.id, t.name, t.age]
  conds  = [t.id = 1]
}
```

Analyze 读取过表和列的**元数据**，但没有读取任何真实数据行。

---

## 4. Planner：Query 变为 Plan tree

Planner 的职责不是读数据，而是决定“执行时如何读”。对于单表查询，可将它分为逻辑优化和物理优化。

### 4.1 逻辑优化：尽早过滤

只涉及表 `t` 的条件 `t.id = 1` 可以在扫描 `t` 时立即生效，而不应等整表扫描结束后再过滤。这类“尽可能靠近数据源执行过滤”的思想称为选择/谓词下推。

```text
不理想：扫描 t 的全部行 → 上层过滤 id=1
理想：扫描 t 的过程中 → 立即过滤 id=1
```

### 4.2 物理优化：选择 ScanPlan

Planner 从 `query->conds` 中取出可在表 `t` 一侧执行的条件：

```text
curr_conds = [t.id = 1]
```

然后 `get_index_cols(t, curr_conds, index_col_names)` 检查元数据中有哪些索引，条件是否匹配索引的最左前缀。

对于 `index(id)` 与 `id=1`：

```text
index_exist     = true
index_col_names = [id]
```

因此建立：

```text
IndexScanPlan(table=t, conds=[id=1], index_cols=[id])
```

若没有匹配索引，或事务语义要求不能走该索引，则建立：

```text
SeqScanPlan(table=t, conds=[id=1])
```

**“表上有索引”不等于一定走索引**；必须是当前条件可用的索引。

### 4.3 最终的 Plan tree

Planner 最终会在扫描节点上包一层 `ProjectionPlan`，负责声明要输出的列。`SELECT *` 虽然输出全部列，也会保留此节点以维持统一结构。

有可用索引时：

```text
DMLPlan(T_select)
└─ ProjectionPlan(output: t.id, t.name, t.age)
   └─ IndexScanPlan(table: t, condition: id=1, index: id)
```

无可用索引时，只有最底层变为 `SeqScanPlan`。

---

## 5. Portal：Plan tree 变为 Executor tree

[`Portal::convert_plan_executor`](../../src/portal.h) 递归地把每一个 Plan 节点变为对应的 Executor 对象：

```text
ProjectionPlan → ProjectionExecutor
SeqScanPlan    → SeqScanExecutor
IndexScanPlan  → IndexScanExecutor
```

因此当前有索引的 Executor tree 是：

```text
ProjectionExecutor
└─ IndexScanExecutor
```

无索引时是：

```text
ProjectionExecutor
└─ SeqScanExecutor
```

`ProjectionExecutor` 构造时接收的是**子 Executor 对象**，不是子 Executor 的全部结果。父 Executor 持有子 Executor；当它需要一行数据时，才向子 Executor 索取当前行。

---

## 6. Executor 的流水线模型

普通扫描不会把所有满足条件的记录一次性收集起来。每个 Executor 只保存当前记录及自身状态。

| 组件 | 保存的主要状态 |
|---|---|
| `RmScan` | 当前物理位置 `Rid(page_no, slot_no)` |
| `SeqScanExecutor` | 当前完整匹配记录 `cur_rec_` |
| `IndexScanExecutor` | 当前索引位置/Rid 与当前完整记录所需状态 |
| `ProjectionExecutor` | 当前投影后的记录 `cur_rec_` |

统一迭代接口：

```cpp
beginTuple();  // 定位第一条匹配记录
while (!is_end()) {
    Next();       // 交出当前记录
    nextTuple();  // 前进并定位下一条匹配记录
}
```

对表中 `[id=5]、[id=1]、[id=1]` 三条记录：

```text
beginTuple → 跳过 id=5，停在第一条 id=1
Next       → 交出第一条 id=1
nextTuple  → 先离开当前行，再停在第二条 id=1
Next       → 交出第二条 id=1
nextTuple  → 到达结束
```

`nextTuple()` 必须先前进，否则会重复命中和输出当前记录。

`ORDER BY`、聚合和部分 Join 需要见到更多数据，可能缓存大量记录；但 `Scan + Projection` 是逐行流式处理。

---

## 7. 两种扫描方式的差别

### 7.1 SeqScan：顺序扫描

```text
RmScan 从表的第一个有效 Rid 开始
→ 取当前行字节
→ 判断 WHERE id=1
→ 不匹配则前进
→ 匹配则作为当前结果交给上层
```

它会查看所有可能的记录，过滤发生在扫描过程中。

### 7.2 IndexScan：索引扫描

`IndexScanExecutor::beginTuple()` 会：

```text
取得 t 上 index(id) 的 B+ 树句柄
→ 将 id=1 构造成索引的查找范围
→ IxScan 在 B+ 树中定位 key=1 的索引项
→ 得到该索引项对应的 Rid
→ 根据 Rid 回表读取完整记录
→ 交给 ProjectionExecutor
```

可以画为：

```text
B+ Tree:  key=1  → Rid(8, 3)
                         ↓
表文件 t：第 8 页、第 3 槽 → [id=1, name=Alice, age=20]
```

索引通常保存的是 **key → Rid**，而不是完整记录，因此 IndexScan 仍需要按 Rid 回表。若索引键不唯一，一个 key 可以产生多个 Rid，Executor 会通过 `nextTuple()` 继续产生后续记录。

两者对上层提供的是相同格式的完整记录，所以：

```text
SeqScanExecutor   ─┐
                   ├→ ProjectionExecutor → 输出
IndexScanExecutor ─┘
```

Projection 和输出层不需要知道底层究竟采用哪种扫描方式。

---

## 8. 可用于复述的总结

> Client 发来 SQL 后，RMDB 先将字符串解析为 AST，再通过 Analyze 验证表、列和 WHERE 条件，并生成 Query。Planner 将 Query 组织成 Plan tree：单表过滤条件会尽量下推到扫描节点，并按索引可用性选择 IndexScanPlan 或 SeqScanPlan，再由 ProjectionPlan 声明输出列。Portal 把 Plan tree 转为对应 Executor tree。运行时，扫描 Executor 逐条产生符合条件的完整记录；IndexScan 通过 B+ 树由 key 找到 Rid 后回表，SeqScan 则逐行检查；ProjectionExecutor 最后保留 SELECT 指定的字段并将结果交给输出层。

---

## 9. 本节自测

- [ ] `yyparse` 与 `do_analyze` 分别检查语法还是语义？
- [ ] 为什么 `SELECT *` 在 Analyze 后不再保留 `*`？
- [ ] 什么条件下 Planner 才选择 `IndexScanPlan`？
- [ ] `Plan` 和 `Executor` 的区别是什么？
- [ ] `IndexScan` 为什么得到 Rid 后仍要回表？
- [ ] `ProjectionExecutor` 持有的是子 Executor 还是全部结果集？
- [ ] 为什么简单 `Scan + Projection` 不需要保存全部结果？
