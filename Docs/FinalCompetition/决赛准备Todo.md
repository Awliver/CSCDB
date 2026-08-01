# 决赛准备 Todo

> **⚡ 状态速览（2026-08-01）**：OJ **全 PASS**；最新有效排名 **37,248.4 NewOrder/min**（W=50、32 客户端、3×150s 中位数）。
> 正确性、COMMIT 持久化、恢复、W=50 装载与 I/O 诊断均已通过；装载 **830.06s**（9 表、25,050,594 行，满足 <900s 预算）。详见 [`08-01 OJ 性能测评报告摘要`](./0801-OJ性能测评报告摘要.md)。
> 07-30 首个有效成绩 **10,402 NewOrder/min** 及当日六份报告的根因链保留在 [`0730-OJ战役复盘`](./0730-OJ战役复盘.md)。当前主线为在完整门禁不退化的前提下继续排名优化，优先降低异常放弃率与尾延迟。
> 本文以下内容保留作检查清单；已被官方结果覆盖的条目已更新。

> **末次修订**：2026-08-01（最新官方全流程 PASS）
> **成绩结构**：必通过正确性门禁（过=Score **100**，不过=**0**）+ TPC-C 饱和负载 **NewOrder/min 中位数**排名（固定 **SI**）  
> **规范**：[`决赛赛题整理.md`](./决赛赛题整理.md) · PDF 原文见 `Docs/` 根目录  
> **初赛锚点**：OJ **3615.5** @ `5a5527f`（[`Optimize/12`](../Optimize/12.初赛冻结备忘.md)）· 已落地：[§5](../Optimize/5.已完成优化摘要.md)  
> **一致性明细**：[`一致性审计与回归用例`](./一致性审计与回归用例.md) · **优化现行裁决**：[`Optimize/14`](../Optimize/14.决赛优化状态与实施计划.md) · **活动队列**：[`Optimize/2`](../Optimize/2.待落地优化Todo.md)

---

## 0. 总原则

| 原则 | 说明 |
|------|------|
| 门禁优先于排名 | 任一必通过项不过 → 性能 **0**；先过功能 / COMMIT 持久化 / 恢复 / 在线一致性 |
| 排名固定 SI | TPC-C 连接用 SNAPSHOT ISOLATION；功能测仍须覆盖 SER（含语句级即时中止） |
| 性能只做可控增量 | 以 08-01 官方 PASS 为基线；适配 **W=50×32**；禁 `sleep_for` 类高风险；一次只交一个变量 |
| 合规 | 禁止表名 / SQL 硬编码旁路；ACID 不得为性能降级；禁改 `CMakeLists.txt` |
| 初赛成绩 | 仅作历史锚点；勿再向初赛 OJ 镜像刷榜；本机仅 `gitlab` |

### 当前阶段（08-01 官方 PASS 后）

```
正确性 / 持久化 / 恢复 / 装载门禁   已通过
W=50×32 三窗排名基线               37,248.4 NewOrder/min
后续工作                            H3/H4 正确性 → abort 归因 → 单变量排名优化
本地 W=5                           仅作趋势和定向定位
```

### 官方建议工作顺序

1. 正确性基线（五事务 + 回滚 + 恢复 + 大装载）
2. 访问路径（索引，避免无谓全表扫）
3. 并发（细粒度锁 / MVCC，保 SI 与热点更新）
4. 持久化（group commit 等，须可审计稳定化）
5. 减测量路径开销（禁关 WAL / 回滚 / 冲突检测）

---

## 1. P0 — 决赛正确性门禁（首要）

> 明细与复现 → [`一致性审计与回归用例`](./一致性审计与回归用例.md) · 赛题压缩 → [`决赛赛题整理`](./决赛赛题整理.md)

### 1.1 初赛遗留（已关闭）

| 序 | 项 | 状态 | 说明 |
|:--:|----|:----:|------|
| 1 | **C1** 堆先于 MVCC 链 | ✅ | `reserve` → `mvcc_insert` → `publish` |
| 2 | **C2** `pending` 未检 | ✅ | `mvcc_other_writer` 查 pending |
| 3 | **H5** `mvcc_on_` 缓存 | ✅ | 扫描重查 dirty；显式并发时 autocommit 写走 MVCC |
| 4 | C1/C2/H5 纳入门禁 | ✅ | 与 `run_tests` 并列 |

**门禁命令**：

```bash
python3 tests/local/consistency/run_consistency_regress.py \
  --case C1 --case C2 --case H5
python3 tests/run_tests.py
```

### 1.2 决赛必通过增量（相对初赛）

| 序 | 项 | 状态 | 说明 |
|:--:|----|:----:|------|
| 1 | **Wire Protocol v3** | ✅ | 握手 `RMDB` major=3；`EXEC_STREAM`/`PREPARE_SET`/`EXEC_BATCH`（含 `AUTO_ABORT` 回滚）已实测；`src/common/wire_protocol.h` + `rmdb.cpp` |
| 2 | **可配置 SI / SER** | ✅ | 会话级 `SET TRANSACTION ISOLATION LEVEL …`（NUL 与 Wire 均支持）；默认 SER，排名前显式设 SI；见 `rmdb.cpp: parse_set_isolation` |
| 3 | **SSI 语句级即时中止** | ✅ | 主路径（SeqScan/IndexScan/INLJ/Update/Insert/Delete）已在**语句执行中**即时 `TRANSACTION_ABORT`，非拖到 COMMIT；H3 已补齐 INLJ 右表实例化谓词与命中 RID 跟踪 |
| 4 | **原生 COUNT(DISTINCT)** | ✅ | `COUNT(DISTINCT col)` 与 `COUNT(DISTINCT (col))` 均支持（lex/yacc/AST/Analyze/AggExecutor）；已改造 `tpcc_transactions.py`/`tpccbench/workload.py` 的 StockLevel 为单条原生查询，去掉客户端去重 |
| 5 | **FLOAT32 规则** | 🟡 | binary32 存储；Wire v3 按位传输（0 ULP，已实测 bit pattern）；**SUM 已修复为 binary64 累加 + 一次舍回 binary32**（`executor_aggregation.h`，实测规避了旧版 float 逐行累加漂移）；relative `col ± lit` 走引擎内单次运算；本地文本协议（legacy NUL）固定 6 位小数是官方 P2 评测 `output.txt` 既定格式，不可改；**2026-07-27 晚新增**：`wire_param_literal`（`rmdb.cpp`）对 NaN/±Inf 位模式主动 `WireProtocolError` 拒绝（受控 `BATCH_STATUS_ERROR`），修复评测报告 "Float Precision" Server ERROR |
| 6 | **COMMIT 可审计 fsync** | ✅ | `commit()` 写 `CommitLogRecord` 后 `wait_for_persist` 阻塞至 `fdatasync(db.log)` 完成才 ACK；`transaction_manager.cpp`/`log_manager.cpp`/`disk_manager.cpp` |
| 7 | **崩溃恢复** | ✅ | 启动 `analyze → undo(未提交) → redo(已提交)`；`recovery/log_recovery.*`；`tests/local/bench_tpcc.py::crash_recovery_post_benchmark` 等已覆盖 kill -9 抽检 |
| 8 | **五事务 + 在线一致性** | ✅ | NewOrder/Payment/OrderStatus/Delivery/StockLevel 均已实现并本地跑通；StockLevel 已切至原生 `COUNT(DISTINCT)` |

> 上表按 2026-08-01 的代码与官方结果同步；🟡 = 主路径已实现且官方门禁已通过，但本地审计仍记录未逐一关闭的边角路径（详见 §2 P1）。

### P0 本地复验（初赛收尾，参考）

| 日期 | 项 | 结果 |
|------|----|------|
| 07-14 | 单次 mid | median **3943.73** · NewOrder 0 fail · 功能/一致性 PASS |
| **07-15** | **三轮完整门禁** | 功能×3 · 一致性×3 · mid 均值 **~3970** · NewOrder abort **0%** |
| — | 收尾文档 | [`Optimize/13`](../Optimize/13.P0正确性后性能复验收尾.md) |

### 决赛本地复验（Wire BATCH）

| 日期 | 项 | 结果 |
|------|----|------|
| **07-27** | 完整门禁 | `run_tests` **11/11** · C1/C2/H5 **3/3** · mid（BATCH + 45/43/4/4/4）median **3578.46** · NewOrder **0 fail** |
| 同日对照 | STREAM mid（改造前） | ~2141（EXEC_STREAM 文本往返，不可与 BATCH 比） |
| 日志/JSON | — | `build/full_local_gate_20260727_173123.log` · `build/oj_mid_batch_20260727_173123.json` |
| **07-27 晚** | 官方评测门禁 FAIL → 修复 SI 陈旧写 abort + FLOAT NaN/Inf 拒绝 | `run_tests` **11/11** · 一致性 **15/15**（quick）+ **C3/M7/H1/H4** 单独复验全过 · 新增 **SI1/F2** 用例 PASS · mid median **3772.23**（NewOrder ok=11322 fail=702，无回归） |
| **08-01** | **官方全流程评测** | **PASS**；25 功能 + 5 恢复用例、COMMIT 持久化 32/32；W=50 9 表 **25,050,594** 行装载 **830.06s**；32 客户端 3×150s 中位 **37,248.4 NewOrder/min**；崩后 44 聚合 + 5,974 关系检查及 I/O 诊断均 PASS |

> mid 仍为 **W=5 / 16 线程 / 60s×3** 趋势窗；`--finals` 为 150s×3 / 32 客户端（本地仍 W=5 CSV，非 OJ W=50）。

---

## 2. P1 — 正确性扩展

| 序 | 项 | 状态 | 来源 |
|:--:|----|:----:|------|
| 1 | H3 INLJ 缺 SSI · H4 `ser_finish` 无锁读 | 🟡 | H3 ✅（08-01）；H4 ⬜；一致性 §三 |
| 2 | C3 Cleaner TOCTOU · C4 脏页/日志序 · C5 `page_lsn` | ⬜ | 一致性 §三 |
| 3 | H1/H2 索引边角 | ⬜ | 一致性 §三 |
| 4 | 装载 900s 预算 + 动态 `order_line` | ✅ | 官方 08-01：W=50、9 表 25,050,594 行，**830.06s**；`order_line` 已通过全量装载与恢复检查 |
| 5 | 同步陷阱文档过时条目 | ✅ | §四 + ProjectAnalysis/5（部分） |
| 6 | 本地 TPC-C Payment 绝对写回（legacy 文本） | ✅ | **BATCH 路径已改相对更新**（`w_ytd`/`d_ytd`/`c_balance`/`c_ytd_payment`/`s_ytd` 等）；`--stream` 旧路径仍可能绝对写，日常勿用 |

---

## 3. P2 — 优化活动队列（官方基线后 · W=50×32）

> 08-01 官方全流程已通过；后续仅可在保持该基线的前提下启动。裁决、实现和量化门槛 → [`Optimize/14`](../Optimize/14.决赛优化状态与实施计划.md)；队列 → [`Optimize/2`](../Optimize/2.待落地优化Todo.md)。
> 正式规格：**50 仓 × 32 客户端** · 预热 30s · **3×150s** · 隔离 **SI**  
> 本地 W=5 脚本仅作趋势，**不能替代**决赛自检。

| 序 | 项 | 状态 | 备注 |
|:--:|----|:----:|------|
| 1 | H3 INLJ 补 SSI 内表谓词/RID 跟踪 | ✅ | 确定性三事务 H3：危险 INLJ SELECT 当前语句 abort；H3S 并发 PASS |
| 2 | H4 `ser_finish` 使用锁内 watermark | 🔴 | 消除对 `active_rts_` 的无锁读取，补高重复/TSan |
| 3 | abort 事务族×语句×原因归因 | 🟠 | 两次 W=50 形；Top-3 原因覆盖 ≥80% |
| 4 | district/stock 冲突窗口收缩 | 🧪 | 仅当相关冲突占 abort ≥30% 或贡献 p99 ≥25% |
| 5 | PREPARE_SET 分析/元数据缓存 | 🧪 | 先证明持锁后解析/计划开销显著；一次缓存一种语句形态 |
| 6 | P3 字节/等待者阈值组提交 | 🧪 | 仅当三窗 `commit_waits/fsync≤2` 且 commit p95 占比 ≥20%；禁固定 sleep |
| 7 | P2 `UPDATE_DELTA` / SER 谓词预编译 | 🧊 已集成 | 当前 HEAD 已包含，不再列为待上线项 |
| 8 | 本地 TPC-C 走 OJ BATCH 热路径 | ✅ | `tpcc_batch.py` + 默认 `PREPARE_SET+EXEC_BATCH`；`--stream` 仅 A/B |

**每项上线前**：

```bash
cd build && make rmdb -j$(nproc)
python3 tests/run_tests.py
python3 tests/local/consistency/run_consistency_regress.py
# 趋势（BATCH + 决赛混合比，W=5）：
python3 tests/local/run_oj_perf_test.py --mid
# OJ 形窗口（仍 W=5）：150s×3 / 32 客户端
python3 tests/local/run_oj_perf_test.py --finals
# 上线仍须 fresh W=50 形完整门禁；W=5 只能筛掉明显退化
```
---

## 4. P3 — 工程与交卷节奏

| 序 | 项 | 状态 | 说明 |
|:--:|----|:----:|------|
| 1 | 门禁脚本化（Release + 功能 + consistency + mid） | ✅ | `tests/local/debug/run_full_local_gate.sh`（2026-07-27 实测 PASS） |
| 2 | 决赛形 harness（Wire BATCH / SI / 150s×32 / 混合比） | ✅ | 官方 08-01 已完成 W=50、32 客户端、3×150s 与 50/50 仓库覆盖；本地 `--finals` 继续用于回归 |
| 3 | 对照 PDF「参赛提交前自检清单」逐项勾选 | ⬜ | 正式交卷前逐项复核 |
| 4 | 维护 Optimize §14 / §2 / §5 / §9 与本 Todo 状态 | ✅ | 2026-08-01 已同步现行裁决、队列、落地表与官方基线 |
| 5 | M1–M7 等 Medium 项按需排期 | ⬜ | 仅在 P0/P1 关闭后按风险排期 |

---

## 5. 验收总清单（决赛交卷前）

- [x] C1/C2/H5 连跑全 PASS（2026-07-14，初赛收尾；2026-07-27 复验仍 3/3）
- [x] `tests/run_tests.py` 全过（11/11；2026-07-27 复验仍 11/11）
- [x] Wire v3 握手 + EXEC_STREAM / PREPARE_SET / EXEC_BATCH 可用（2026-07-27 实测：握手/流式/预编译批处理/AUTO_ABORT 回滚均通过）
- [x] SI / SER 可配置；SSI 语句级即时中止（含 INLJ 右表实例化谓词与命中 RID；08-01 H3/H3S PASS）
- [x] 原生 `COUNT(DISTINCT)`（两种写法；StockLevel 已切原生单条查询）
- [x] FLOAT32 规则：0 ULP（Wire v3 位级）/ SUM 一次舍入（2026-07-27 修复并实测规避漂移）/ NaN·Inf 参数受控拒绝（2026-07-27 晚修复）
- [x] SI 陈旧写：`mvcc_write`/`mvcc_write_col_delta`/`mvcc_write_col_patch` 检测到快照之后有新提交版本时**无条件 abort**，不再变基合并（2026-07-27 晚修复，此前会静默 rebase 导致 COMMAND_OK 应为 TRANSACTION_ABORT）
- [x] COMMIT ACK 前可审计 WAL 稳定化（`wait_for_persist` → `fdatasync`）
- [x] kill -9 恢复抽检通过（既有 `RecoveryManager` + 本地回归脚本）
- [x] 五事务正确；在线一致性 + 恢复后一致性（本地 W=5 已覆盖）
- [x] 本地排名路径 `PREPARE_SET + EXEC_BATCH`（`tpcc_batch.py`；附件 A §7 批次边界；相对 FLOAT 更新）
- [x] 本地 mid 完整门禁 PASS（2026-07-27：11/11 + C1/C2/H5 + mid median **3578.46**，NewOrder 0 fail）
- [x] 装载正式数据在 900s 内完成（官方 2026-08-01：W=50、**830.06s**）
- [x] （排名）W=50×32 / SI / 3×150s 可跑通且仓库覆盖门禁过（官方 2026-08-01：3/3 窗、32 客户端、50/50 覆盖）
- [x] 无表名/SQL 硬编码旁路；未改 CMake；ACID 未降级
- [ ] 推送目标确认（本机 `gitlab`；正式交卷按赛方要求）

---

## 6. 修订记录

| 日期 | 内容 |
|------|------|
| 2026-07-11 | 初版：决赛准备主线（正确性优先 + 可控性能 + 门禁） |
| 2026-07-14 | P0 C1/C2/H5 关闭；更新门禁命令与验收勾选 |
| 2026-07-14 | 完整本地复验：功能 + 一致性 + mid **3943.73**；勾选 NewOrder 0 abort |
| 2026-07-15 | 三轮完整门禁；mid 均值 **~3970**；收尾 [`Optimize/13`](../Optimize/13.P0正确性后性能复验收尾.md) |
| **2026-07-27** | **初赛结束**：切入决赛成绩结构 / Wire v3 / SI·SER / COUNT(DISTINCT) / FLOAT32 / W=50×32 |
| **2026-07-27** | §1.2 八项门禁落地：**Wire v3**（`wire_protocol.h` + `rmdb.cpp`，握手/EXEC_STREAM/PREPARE_SET/EXEC_BATCH 已实测）、**原生 COUNT(DISTINCT)**（lex/yacc/AST/Analyze/AggExecutor）、**SUM FLOAT32 单次舍入修复**（`executor_aggregation.h`：`float_sum` 改 `double` 累加，输出前一次 cast 回 float，实测规避漂移）、**StockLevel 改为单条原生 `COUNT(DISTINCT (col))`**（`tests/local/tpcc_transactions.py` + `tests/bench/tpccbench/workload.py`，移除客户端去重）；核对确认 SI/SER、SSI 语句级中止、COMMIT `fdatasync`、崩溃恢复、五事务在代码中均已有主体实现，更新状态标记与验收清单；`tests/run_tests.py`（11/11）与一致性 C1/C2/H5（3/3）复验通过 |
| **2026-07-27** | **本地 TPC-C 对齐 OJ**：`tpcc_batch.py`（PREPARE/BATCH + §7 批次边界 + 相对更新）；默认混合比 45/43/4/4/4；新增 `--finals`（150s×3 / 32）；`--stream` / `--legacy-mix` 作对照。完整门禁 PASS：功能 11/11 · C1/C2/H5 · mid BATCH median **3578.46**（NewOrder 0 fail；other_fail 42 多为 Delivery wait-die） |
| **2026-07-27 晚** | **官方评测门禁 FAIL 修复**：官方性能测评报告 3 项功能失败——`Float Precision`（Server ERROR）、`Transaction Commit Index`（typed 结果不匹配/幻行）、`Snapshot Isolation Model`（陈旧快照写未按 SI 规范 abort，观测到 COMMAND_OK）。根因定位：①`transaction_manager.cpp` 的 `mvcc_write`/`mvcc_write_col_delta`/`mvcc_write_col_patch` 在检测到 `ch.hist.back().commit_ts > txn->get_read_ts()`（快照之后已有新提交版本）时，此前会调用 `rebase_write_delta`/`apply_col_patch_rebase` 把本次写变基合并到最新版本继续放行，违反决赛赛题整理 §5.3"SI 陈旧写必须 TRANSACTION_ABORT"；②`rmdb.cpp` 的 `wire_param_literal` 用 `%.9g` 序列化 FLOAT32 时未防护 NaN/±Inf，生成的 `"nan"/"inf"` 文本无法被词法分析器识别，回填 SQL 后触发不可控 Server ERROR。**修复**：三个 `mvcc_write*` 函数陈旧写分支改为无条件 `return false`（触发 `TransactionAbortException`），移除已死的 `rebase_write_delta`/`apply_col_patch_rebase`；`wire_param_literal` 增加 `std::isfinite` 检查，非 finite 值主动抛 `WireProtocolError`（受控 `BATCH_STATUS_ERROR`，不落入 lexer 崩溃路径）。新增本地回归 `SI1`（陈旧写必 abort）、`F2`（inf/nan 参数拒绝）纳入 `tests/local/consistency/cases.py`。验证：`run_tests.py` 11/11、一致性 quick 15/15 + C3/M7/H1/H4 单独复验全过、mid 门禁 median **3772.23**（较修复前 3578.46 无回归，NewOrder ok=11322 fail=702，一致性检查全 PASS） |
| **2026-08-01** | **官方全流程 PASS**：功能 25 项、恢复 5 项、COMMIT 持久化 32/32 均通过；W=50 九表 25,050,594 行装载 **830.06s**（<900s）；32 客户端 3×150s 三轮 **37,166.4 / 37,248.4 / 38,150.0**，排名中位 **37,248.4 NewOrder/min**；崩后 44 项聚合、5,974 项关系检查与非排名 I/O 诊断均 PASS。完整数字见 [`08-01 OJ 性能测评报告摘要`](./0801-OJ性能测评报告摘要.md)。 |
