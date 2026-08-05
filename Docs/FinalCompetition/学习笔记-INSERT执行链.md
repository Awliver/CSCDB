# 学习笔记：INSERT 的完整执行链

> **学习日期**：2026-08-05  
> **目标**：以普通单行 `INSERT` 为例，串起 RMDB 从 SQL 文本到堆记录、WAL 与索引项的主路径；暂不深入 MVCC 两阶段插入、SSI、B+ 树分裂和缓冲池实现细节。

示例：

```sql
create table t (id int, name char(8));
create index t(id);

insert into t values (1, 'neo');
```

---

## 1. 总调用链

```text
客户端发送 SQL
  → run_sql_statement
  → Parser（fast parser 或 yyparse）
  → AST: InsertStmt
  → Analyze::do_analyze
  → Query
  → Planner
  → DMLPlan(T_Insert)
  → Portal::start / Portal::run
  → InsertExecutor::Next
  → 唯一索引检查
  → 堆记录写入，得到 Rid
  → INSERT WAL
  → 索引插入 key → Rid
```

其中 AST、`Query` 与 `Plan` 都是描述对象；只有 Executor 会真正读写表记录。

---

## 2. Parser：SQL 变为 InsertStmt

入口是 `run_sql_statement`。系统会先调用 `try_fast_parse_sql`：简单的

```sql
insert into t values (1, 'neo');
```

可由 `try_fast_parse_insert` 直接构造 `ast::InsertStmt`。如果快解析器不支持当前写法，不代表 SQL 错误；系统会回退到 `yyparse()` 使用完整语法规则继续解析。

语法正确后，AST 至少保存以下信息：

```text
tab_name = t
cols     = []
vals     = [1, 'neo']
```

如果 SQL 显式给出列名列表：

```sql
insert into t (name, id) values ('neo', 1);
```

则 AST 为：

```text
cols = [name, id]
vals = ['neo', 1]
```

---

## 3. Analyze：用表元数据校验并规范化输入

`Analyze::do_analyze` 识别 `ast::InsertStmt` 后，会读取目标表的 `TabMeta`。

对于未写列名列表的语句，字面量默认按建表时的列顺序对应。对于显式列名列表，Analyze 会按照**表定义顺序**重排值。

例如表列顺序是 `[id, name]`，而用户写：

```sql
insert into t (name, id) values ('neo', 1);
```

Analyze 得到：

```text
raw_vals / query->values = [1, 'neo']
```

这样后续记录层按列 offset 写字节时，`id` 与 `name` 一定落在正确位置。

本阶段还会检查：

- 列名是否存在，列名数量是否与输入值数量一致；
- 每个输入值能否匹配目标列的类型；
- `INT → FLOAT` 的转换，以及数值可表示时的 `FLOAT → INT` 特判。

最终返回 `Query`。Analyze 不创建执行计划。

---

## 4. Planner 与 Portal：建立执行对象

Planner 将 `Query` 中已经校验好的表名和值封装为无子计划的 DML 计划：

```cpp
DMLPlan(T_Insert, nullptr, tab_name, query->values, {}, {})
```

普通 `INSERT VALUES` 不需要先扫描已有数据，因此没有子计划、没有 `WHERE` 条件，也没有 `SET` 子句。

Portal 从 `DMLPlan` 中取出表名和值，创建：

```text
InsertExecutor(sm_manager, tab_name, values, context)
```

并将它放入 `PortalStmt`。`Portal::run` 驱动执行器；INSERT 没有结果集，因此其 Portal 类型为 `PORTAL_DML_WITHOUT_SELECT`。

---

## 5. InsertExecutor：插入一行的主路径

`InsertExecutor::Next()` 是 INSERT 的核心执行函数。多行 INSERT 时，`values_` 是平铺数组；`Next()` 按**表列数**切分为多行，并逐行执行以下步骤。

```text
1. 按各列的 offset / len 将 Value 拼为 RmRecord。
2. 对表的索引构造 key，查询 B+ 树，进行唯一键冲突检查。
3. 记录层写入 RmRecord，获得 Rid。
4. 追加 INSERT WAL，并更新事务的 prev_lsn。
5. 对每一个索引写入 key → Rid。
```

唯一键检查发生在写堆记录之前。若 B+ 树中已经有同 key 且有效、对当前事务可见的记录，则返回 `failure`；不会调用 `fh_->insert_record(...)`，也不会新增索引项。

```sql
insert into t values (1, 'neo');
insert into t values (1, 'other'); -- failure
```

第二条语句的关键路径为：

```text
构造 key(id=1)
  → ih->get_value(key, &existing, ...)
  → 找到已有 Rid
  → 唯一键冲突
  → failure
```

MVCC 路径会使用 `reserve_insert_slot → mvcc_insert → publish_insert_slot`，以确保未提交插入在 bitmap 发布前先登记版本链；该分支留待事务/MVCC 学习阶段展开。

---

## 6. 记录层：Rid 如何产生

普通路径进入 `RmFileHandle::insert_record`：

```text
1. 获取 op_latch_，保护插入页缓存、bitmap 与空闲槽分配。
2. ensure_insert_page_cached()：复用有空槽的当前页，或取空闲页/创建新页。
3. find_free_slot_on_cached_page()：在 bitmap 中找到一个空槽 slot_no。
4. 计算页内地址：slots + slot_no × record_size。
5. memcpy 写入记录字节；Bitmap::set 标记槽位已占用；num_records++。
6. 返回 Rid{page_no, slot_no}。
```

`op_latch_` 防止并发线程分到同一槽位，不负责唯一键冲突；唯一性由 Executor 的索引检查处理。

当页刚好写满时，它会从空闲页链中移出，并取消当前插入页缓存，以便下一次插入重新选择页面。

---

## 7. 索引层：key → Rid

插入索引时，Executor 调用：

```cpp
ih->insert_entry(key.data(), rid_, context_->txn_);
```

最小理解链为：

```text
find_leaf_page(key)
  → 从根节点沿内部节点下降，找到目标叶子页
  → lower_bound(key) 找到叶内插入位置
  → insert_pairs 移动后续项
  → 写入 key → Rid，num_key++
```

叶子页满时，才需要 `split`：原叶保留左半，新叶获得右半，然后将新叶首 key 写入父节点作为导航信息。B+ 树的分裂、pin/unpin 和顺序插入缓存属于后续进阶内容，不是当前主线。

---

## 8. 今日完成情况

- [x] 串起 INSERT 的 Parser、Analyze、Planner、Portal、Executor 主路径；
- [x] 理解显式列名列表为何要重排为表定义顺序；
- [x] 理解 `Rid = {page_no, slot_no}` 的来源及堆记录写入过程；
- [x] 理解索引保存 `key → Rid`，以及重复键为何在写堆记录前失败；
- [x] 区分 `op_latch_` 的槽位分配保护与唯一索引冲突检查；
- [ ] 未展开 MVCC/SER、WAL 持久化、B+ 树分裂与 Buffer Pool 细节。

## 9. 明日计划（2026-08-06）

主题：**Projection 与唯一键失败路径（D2：Q3–Q4）**。

1. 以 `select name from t where id = 1;` 为例，复习 `ProjectionPlan → ProjectionExecutor`：投影如何从子执行器当前记录中取列，并按新的列顺序拼出输出记录。
2. 对比 `SELECT *` 与只选择一列时，`Query::cols`、`ProjectionPlan::sel_cols_` 与输出 tuple 布局的差别。
3. 复看 INSERT 的唯一键失败路径：`get_value` 查到既有 key 后，为何不调用 `insert_record` / `insert_entry`。
4. 用两组最小 SQL 验收：

   ```sql
   select name from t where id = 1;
   insert into t values (1, 'neo');
   insert into t values (1, 'other'); -- 应失败且不留下新记录
   ```

明日结束时应能回答：投影为什么不改变底层表记录、重复键失败后为何不产生新的堆记录或索引项。
