# RMDB 本地测试（`tests/`）

> 决赛主入口见 [`finals/README.md`](finals/README.md)：25 组 Wire-v3 功能门禁、
> W=50 Load Data、W=50×32 性能测试，以及非计分 TPC-C I/O Diagnostics。
> 下文保留初赛/开发期脚本说明，供单模块调试使用，不应替代决赛门禁。

本目录统一存放**功能回归、TPC-C 压测与性能剖析**（不上传远程）。

| 子目录 | 职责 |
|--------|------|
| **`run_tests.py`** | P2 官方 11 测试点（一键入口） |
| **`framework/`** | 各题目专项用例（P3–P7、EXPLAIN 等） |
| **`bench/`** | TPC-C 规范压测工具（tpccbench） |
| **`local/`** | 本地 OJ 模拟、快测、一致性检查 |
| **`prof/`** | perf 火焰图与宏观计数 |

## 前置条件

```bash
cd build && make rmdb -j$(nproc)
```

运行前确认 **8765 端口空闲**（残留 `rmdb` 会导致 `Bind error!`）：

```bash
ps aux | grep "[r]mdb" | awk '{print $2}' | xargs -r kill -9
```

## 快速开始

```bash
# P2 功能回归（11 个测试点，OJ 前置条件）
python3 tests/run_tests.py

# ★ 最接近线上 OJ 的本地验收（见下文「OJ 性能测试对齐」）
python3 tests/local/run_oj_perf_test.py --strict

# TPC-C 快速冒烟（日常迭代，不能代表排名 tpmC）
python3 tests/local/bench_tpcc_neworder.py --quick

# Delivery EXEC_BATCH ERROR 定向回归（当前 build/bin/rmdb，5 秒）
python3 tests/local/repro_delivery_batch_error.py --quick
```

### Delivery `BATCH_STATUS_ERROR` 定向回归

`repro_delivery_batch_error.py` 使用决赛 Wire v3 的 PREPARE_SET + EXEC_BATCH，
让并发 SI 会话在 Delivery 写入和订单行明细查询前同步汇合。默认断言：

- 服务进程在测量末尾仍存活；
- 合法的 prepared batch 不返回不可重试 `ERROR`；
- 至少一笔 Delivery 成功提交；
- 明细行数、`ORDER BY ol_number`、FLOAT32 金额位模式和配送时间均正确。

```bash
# 当前版本日常冒烟
python3 tests/local/repro_delivery_batch_error.py --quick

# 当前版本 32 客户端压力；可重复多轮提高并发缺陷命中率
python3 tests/local/repro_delivery_batch_error.py --rounds 3 --seconds 30

# 历史 ultra_test 复现模式；命中明细 SELECT 的 ERROR 才算复现成功
python3 tests/local/repro_delivery_batch_error.py --legacy-repro

# 指定不可变提交、自定义压力，并输出机器可读报告
python3 tests/local/repro_delivery_batch_error.py \
  --ref f01553f --expect target-error --frames 80 --seconds 90 \
  --json build/repro_delivery_batch_error/result.json
```

服务端完整日志保存在 `build/repro_delivery_batch_error/`；摘要会区分
`sql-error`、缓冲池等待/耗尽、`pressure-abort`、`error-abort` 和
`bad_alloc`。`TRANSACTION_ABORT` 在 SI 压力下允许重试，不等同于本测试针对的
不可重试 `ERROR`。

## OJ 性能测试对齐（题意模拟）

### 本地回归配置（2026-07-10）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| warehouse W | **5** | `tests/local/tpcc_scale.py` → `FULL_W` |
| 并发线程 | **16** | `bench_tpcc.py` / `run_oj_perf_test.py` |
| 数据 | `build/tpcc_data/full/` | `python3 tests/local/generate_tpcc_data.py --scale full` |

最新 strict 实测（Release，360s×3）：median **3535.48** tpmC，NewOrder **0 abort**，一致性 + 崩溃恢复 **PASS**（详见 `Docs/Optimize/3.md` §17）。

依据 [`Docs/Analysis/TaskAnalysis/性能测试_TPC-C性能优化.md`](../Docs/Analysis/TaskAnalysis/性能测试_TPC-C性能优化.md)。

### 题目规定的完整流程

```
功能测试 PASS → load 九表 → 建主键索引 → SI + set output_file off
    → 30s 预热 + 360s 测量 × 3 轮 → 一致性检查 → kill -9 崩溃恢复
    → 以 3 轮 NewOrder 成功提交吞吐的中位数计 tpmC
```

任一正确性检查失败 → **性能得 0 分**，不参与排名。

| 题目要求 | 本地 `--strict` 实现 |
|---------|---------------------|
| 快照隔离（SI） | `bootstrap_tpcc` 发送 `set transaction isolation level snapshot isolation` |
| `set output_file off`（**无分号**） | bootstrap 阶段已发送 |
| `load file into table` 加载九表 | 相对 `build/` 的路径加载 CSV |
| 九表主键索引 | `tpcc_common.INDEXES` 八条 `create index` |
| 五种事务混合 | 10/23 NewOrder + 10/23 Payment + 1/23 OrderStatus/Delivery/StockLevel |
| 30s 预热 + 360s × 3 轮，中位 tpmC | `--warmup 30 --measure 360 --rounds 3`（默认） |
| W=5 全量数据（item 10 万/仓等） | `--scale full`（默认 W=5，见 `tpcc_scale.FULL_W`） |
| 功能测试全部通过 | `--strict` 先跑 P2 门禁（11 点） |
| 加载结果与标准一致 | 行数校验 + 内容 checksum / 抽样 |
| 压测后一致性 | `tpcc_consistency.py`（strict 全表/压测期订单校验） |
| 多 seed 并发压力 + 逐轮一致性 | `stress_consistency.py`（`--strict` 默认启用） |
| kill -9 后已提交事务可恢复 | `crash_recovery_post_benchmark` |

### 最符合题意、最能模拟线上的命令

**提交 OJ 前最终验收**（Release 构建 + 全量数据 + strict 全流程）：

```bash
# 1. 线上为优化构建，勿用 Debug / ASAN 测最终 tpmC
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make rmdb -j$(nproc)

# 2. 功能门禁（题目硬性前置，须 11/11 PASS）
python3 tests/run_tests.py

# 3. 首次生成 W=5 全量 CSV（约数分钟，之后可复用）
python3 tests/local/generate_tpcc_data.py --scale full

# 4. 完整 OJ 规格压测（推荐一键入口；默认 16 线程）
python3 tests/local/run_oj_perf_test.py --strict
```

等价于：

```bash
python3 tests/local/bench_tpcc.py \
  --scale full \
  --strict \
  --warmup 30 \
  --measure 360 \
  --rounds 3 \
  --threads 16
```

**通过标准**：输出中同时出现 `median tpmC (OJ metric): …` 与 `OVERALL: PASS`。

### 各命令与题意的符合度

| 命令 | 题意符合度 | 适用场景 |
|------|-----------|----------|
| `run_oj_perf_test.py --strict` | ★★★★★ | **提交前最终验收**，最接近线上 |
| `bench_tpcc.py --scale full --strict` | ★★★★★ | 同上，参数可手动微调 |
| `bench_tpcc.py --scale full`（无 strict） | ★★★★☆ | 只看 tpmC，跳过 P2 / 崩溃恢复 |
| `bench_tpcc_neworder.py --quick` | ★★☆☆☆ | 仅 NewOrder，无五类事务混合 |
| `bench_tpcc.py --scale mini --quick` | ★★☆☆☆ | 数据规模不对（Phase2 小 CSV） |

> **`--strict` 比 OJ 更严**：额外检查 NewOrder abort 率、非 NewOrder 失败率、事务 mix、p99 延迟、多 seed 压力后一致性等。**轮间 tpmC 波动仅 WARN，不挡关**（OJ 无此项）。本地 strict 通过通常意味着线上正确性更稳；若仅因 WARN 项提示，不等于 OJ 必挂。

### tpmC 计算说明

与题目一致：**3 轮正式测量**（预热不计）中，**成功提交的 NewOrder 笔数** ÷ **该轮实际墙钟秒数** × 60，再取**中位数**。

```text
tpmC = new_order_ok / elapsed × 60
median tpmC = median(round1, round2, round3)
```

| 要点 | 说明 |
|------|------|
| 分子 | 仅 `commit` 成功的 NewOrder；abort/失败不计 |
| 分母 | 该轮 worker 启动到全部 join 的**实际 elapsed**（通常略大于 360s，如 360.6s） |
| 混合压测 | Payment 等占用 DB 但不进分子——与 OJ 一致 |
| 不可比场景 | `bench_tpcc_neworder.py`（仅 NewOrder）、`--quick`（15s）、`mini` 规模、Debug/ASAN |

本地 quick 的 tpmC（如 500+）与 OJ 正式成绩（如 40+）**公式相同、标尺不同**，勿直接对比绝对值。

### 一致性与随机错误检测

| 机制 | 作用 | strict |
|------|------|:--:|
| 压测后 `tpcc_consistency.py` | 终态不变量（引用完整性、`o_ol_cnt`、`w_ytd` 等） | 全表/压测期新建 order |
| 多 seed 压力（`stress_consistency.py`） | 不同随机调度下短 burst + **每轮后**再验一致性 | 默认 3×60s（full） |
| `check_other_fail_rate` | Payment/Delivery 等失败率过高时 FAIL | ≤5% |
| 崩溃恢复 | `kill -9` 后行数 + 一致性 | ✓ |

**局限**：仍为概率性检测——单次 PASS 不能数学保证无并发 bug。可提高信心：

```bash
# 独立多 seed 压力（不跑完整 19min 压测）
python3 tests/local/stress_consistency.py --scale full --seconds 60 --seeds 42,99,123,456 --strict

# 跳过 strict 附加压力（省 ~3min）
python3 tests/local/bench_tpcc.py --strict --skip-stress
```

并发下事务 **abort 在 SI 下属正常**，压力轮次的 burst fail 率**仅 WARN**（默认阈值 25%）；**一致性 FAIL 或服务器崩溃**才判失败。

### 分层测试策略（OJ 拟合）

| 档位 | 命令 | 耗时 | 用途 |
|------|------|------|------|
| **smoke** | `run_oj_perf_test.py --quick` | ~2 min | 改完代码看一眼（**非 OJ 360s 窗口**） |
| **trend** | `run_oj_perf_test.py --mid` | ~4 min | 日常跟踪 median tpmC（60s×3，比 quick 稳） |
| **OJ 预演** | `run_oj_perf_test.py --strict` | ~25 min | **提交前唯一可信** |
| 一键提交前 | `tests/local/pre_oj_submit.sh [threads]` | ~30 min | Release + P2 + strict + JSON |
| 标定线程数 | `python3 tests/local/scan_threads.py` | ~30 min | full + mid 扫 4–16 线程 |

数据：**官方未提供全量 CSV**，本地使用 `generate_tpcc_data.py` 自生成 full 数据（`build/tpcc_data/full/`），经 checksum/anchor 校验。

**禁止**用 `bench_tpcc_neworder.py` 或 mini 规模的 tpmC 判断 OJ 排名（无 Payment 混合 / 数据过小）。

```bash
# 日常性能迭代（Release 构建；默认 W=5、16 线程）
python3 tests/local/run_oj_perf_test.py --mid

# 提交 OJ 前（默认 16 线程）
bash tests/local/pre_oj_submit.sh
```

### 分层测试策略（旧表）

| 阶段 | 命令 | 用途 |
|------|------|------|
| 日常改代码 | `python3 tests/run_tests.py` | 功能不退化 |
| 性能迭代 | `python3 tests/local/run_oj_perf_test.py --mid` | 看 tpmC 趋势（full 60s×3） |
| **提交前验收** | `bash tests/local/pre_oj_submit.sh` | **模拟线上** |
| 找瓶颈 | `bash tests/prof/profile_tpcc.sh --scale full --measure 60` | 火焰图 |

性能剖析用 **RelWithDebInfo + `-fno-omit-frame-pointer`** 编译；**最终 tpmC 用 Release**。

### 容易偏离题意的点

1. **隔离级别**：题目要求**快照隔离（SI）**，不是默认 SER。勿为性能擅自改默认隔离级别。
2. **数据规模**：`mini` 是 Phase2 小 CSV（item=10），与性能题全量无关；**排名必须用 `full`**（本地默认 **W=5**）。
3. **事务混合**：OJ 跑五类事务混合，Payment 等会争用缓冲池 / MVCC；只测 NewOrder 的 tpmC **偏高**，不能代表真实排名。
4. **测量窗口不可改**：360s × 3 轮是固定规格；被提前中断应优化性能，而非缩短 `--measure`。
5. **并发度**：题目未写死线程数；本地默认 **16** 客户端（`bench_tpcc.py` / `run_oj_perf_test.py`）。可用 `scan_threads.py` 标定 sweet spot。
6. **编译模式**：Debug / ASAN 的 tpmC 不可与 Release 成绩对比。

## 目录结构

```
tests/
├── run_tests.py          # P2 功能回归（11 测试点）
├── framework/            # 专项用例（aggregates / explain / p3 …）
├── bench/                # tpccbench 规范压测
├── local/                # 本地 TPC-C / OJ 模拟
│   ├── run_oj_perf_test.py
│   ├── bench_tpcc.py
│   └── debug/            # 临时脚本（gitignore）
└── prof/                 # 火焰图 / perf stat
```

## 主要脚本说明

### `run_tests.py`

P2 功能测试入口。自动起停 `rmdb`、发送 SQL、比对 `output.txt`。

- 通过：退出码 `0`，11/11 PASS
- 失败：退出码 `1`，打印逐行 diff

### `local/bench_tpcc.py`

对齐 OJ Phase 3 性能测试规格：

- 9 表 + 主键索引，默认 **SI** 隔离、`output_file` 关闭
- 事务混合：NewOrder / Payment / 其余三类按官方比例
- **30s 预热 + 360s 测量 × 3 轮**，取 NewOrder **中位 tpmC**
- 默认 **16** 并发客户端（worker 按线程 round-robin 绑定 home warehouse）

常用参数：

| 参数 | 说明 |
|------|------|
| `--scale mini\|full` | mini 用官方小 CSV；full 需先生成全量数据 |
| `--quick` | 3s 预热 + 15s 测量，1 轮（smoke，非 OJ 窗口） |
| `--mid` | 30s 预热 + 60s 测量 × 3 轮，中位 tpmC（日常趋势） |
| `--strict` | P2 门禁、加载内容校验、更强一致性、多 seed 压力、崩溃恢复 |
| `--json PATH` | 额外再写一份结果 JSON（默认已自动写入 `build/bench_history/`） |
| `--no-save-history` | 关闭自动历史落盘 |
| `--threads N` | 并发客户端数（默认 **16**，最大 32） |
| `--seed N` | 主压测随机种子（默认 42；每轮 +1000） |
| `--stress-seeds` | 压力轮 seed 列表（strict 默认 `99,123,456` full / `99,123` quick） |
| `--stress-seconds` | 每轮压力时长（strict 默认 60s full / 15s quick） |
| `--skip-stress` | 跳过多 seed 一致性压力 |
| `--max-abort-rate` | strict：NewOrder abort 率上限（默认 1%） |
| `--max-other-fail-rate` | strict：非 NewOrder 失败率上限（默认 5%） |
| `--max-round-spread` | strict：轮间 tpmC spread 警告阈值（默认 0.50，**不 FAIL**） |
| `--skip-crash` / `--skip-consistency` / `--skip-p2` | 非 strict 模式下跳过对应检查 |
| `--client-timeout N` | 单条 SQL 的 socket 读超时（秒）；`0` 表示不限时；默认 `max(2×measure, 600)` |

```bash
# 首次 full 压测前生成数据（约数分钟）
python3 tests/local/generate_tpcc_data.py --scale full

# 完整 OJ 规格（结束后自动写入 build/bench_history/）
python3 tests/local/bench_tpcc.py --scale full --rounds 3

# 开发期快测（full 数据，非 OJ 360s 窗口）
python3 tests/local/run_oj_perf_test.py --quick

# 日常趋势（60s x 3 中位 tpmC）
python3 tests/local/run_oj_perf_test.py --mid

# 查看历史记录
python3 tests/local/list_bench_history.py
python3 tests/local/list_bench_history.py --latest

# 提交 OJ 前
bash tests/local/pre_oj_submit.sh
```

### 自动历史记录（`build/bench_history/`）

每次 `bench_tpcc.py` / `run_oj_perf_test.py` 结束（**PASS 或 FAIL**）默认落盘：

| 文件 | 内容 |
|------|------|
| `YYYYMMDD_HHMMSS_<tier>_wN_tN_….json` | 单次完整结果 + git 版本 |
| `LATEST.json` | 最近一次 |
| `index.jsonl` | 一行一条摘要，便于扫趋势 |

记录字段含：`median_tpmc`、`tpms`、`overall` / `fail_stage`、`warehouses`、`threads`、`build_type`、`git_rev` / `git_branch` / `git_dirty` / `git_subject`。

关闭：`--no-save-history`。额外拷贝：`--json PATH`。

### `local/stress_consistency.py`

独立于完整压测的**多 seed 并发压力 + 逐轮一致性**工具；也被 `bench_tpcc.py --strict` 在主压测后调用。

```bash
python3 tests/local/stress_consistency.py --scale mini --seconds 30 --seeds 42,99,123 --strict
```

### `local/bench_tpcc_neworder.py`

只跑 **NewOrder** 事务的 tpmC 快测，适合迭代性能优化时频繁回归。参数 `--quick` / `--warmup` / `--measure` / `--rounds` 与完整压测类似。

### `local/run_oj_perf_test.py`

一键串联：可选 P2 门禁 → 确保 CSV 数据 → `bench_tpcc.py` → 可选火焰图。

```bash
python3 tests/local/run_oj_perf_test.py --quick          # 快测
python3 tests/local/run_oj_perf_test.py --strict         # 严格全量
python3 tests/local/run_oj_perf_test.py --generate-only    # 仅生成 CSV
python3 tests/local/run_oj_perf_test.py --profile          # 压测后跑 perf 火焰图
```

### `local/p2_gate.py`

被 `bench_tpcc.py --strict` 调用，确保性能测试前 P2 功能 11 点全部通过。

### `framework/`

各题目专项用例（P3 索引、P4 EXPLAIN、P5 聚合、P6 Union、P7 Join 等）。入口示例见 [`framework/README.md`](framework/README.md)。

### `bench/tpccbench.py`

规范 TPC-C 压测 CLI（gen / load / smoke / check / acid / run）。与 `local/bench_tpcc.py`（OJ 模拟）互补，详见 [`bench/tpccbench/README.md`](bench/tpccbench/README.md)。

## 性能剖析

详见 [`prof/README.md`](prof/README.md)。简要命令：

```bash
bash tests/prof/install_deps.sh
bash tests/prof/profile_tpcc.sh --quick    # → build/prof_out/<ts>/flamegraph.svg
bash tests/prof/perf_stat_tpcc.sh --quick
```

建议用 **RelWithDebInfo + `-fno-omit-frame-pointer`** 编译，火焰图栈才完整。

## 调试脚本

`tests/local/debug/` 存放临时复现脚本（MVCC、SSI、崩溃、索引 abort 等），**不纳入 Git**。正式回归请用上文列出的 `local/` 根目录脚本。

## 常见问题

| 现象 | 处理 |
|------|------|
| `Bind error!` | 杀残留 `rmdb`，等待端口释放 |
| `output.txt` 为空 | 服务端可能已 crash；用 ASAN 编译复现 |
| `Table already exists` | 清理 `build/test_dbs/<db_name>/` 后重跑 |
| full 压测缺 CSV | `python3 tests/local/generate_tpcc_data.py --scale full` |
| `OVERALL: FAIL (strict measure checks)` | 查 NewOrder abort、other fail、mix、p99；**非** tpmC spread WARN |
| `OVERALL: FAIL (multi-seed stress)` | 压力后一致性失败或压测中服务器崩溃 |
| `WARN: round tpmC unstable` | 仅提示，不挡关；三轮波动大时参考 |
| `WARN: burst fail rate` | SI 并发 abort 常见，看一致性是否 PASS |

### 压测被提前中断

常见有两类原因，处理方式不同：

#### 1. 本地脚本：单条 SQL 超时（旧默认 120s）

若单条 NewOrder 在慢机器上超过 **120s**，socket 会抛超时，worker 提前退出，表现为某轮 `elapsed` 远小于 360s、或大量 `fail`。

**已修复**：压测脚本默认使用 `max(2×measure, 600)` 秒超时；加载阶段不限时。仍不够时可：

```bash
python3 tests/local/bench_tpcc.py --client-timeout 0   # 完全取消 socket 超时
```

#### 2. 总时长预算不够（OJ 评测端或本地 `--strict` 全流程）

完整流程大致耗时：

| 阶段 | 约耗时 |
|------|--------|
| P2 功能 11 点（`--strict`） | 1–3 min |
| full 数据加载 + 索引 | 5–30+ min（视性能） |
| 预热 30s + 测量 360s × 3 轮 | **≈ 19 min**（固定） |
| 多 seed 一致性压力（`--strict`） | **≈ 3 min**（full 默认 3×60s） |
| 一致性 + 崩溃恢复（`--strict`） | 2–10 min |

**OJ 评测端**有总 wall-clock 上限，数据库越慢越容易在 2/3 轮测量或后续检查前被强制断开——**无法改 OJ 客户端时限**，只能提升吞吐，让固定 360s×3 窗口内跑完且留出加载/检查时间。

**本地开发**不必每次跑满 OJ 规格，用缩短参数验证逻辑即可：

```bash
# 快测（推荐日常迭代）
python3 tests/local/bench_tpcc_neworder.py --quick

# 自定义：1 轮、60s 测量、跳过崩溃恢复
python3 tests/local/bench_tpcc.py --scale mini --rounds 1 --measure 60 \
  --skip-crash --skip-consistency --skip-p2 --skip-load-content

# 仅看 tpmC 趋势，不要开 --strict（strict 会强制跑 P2 + 崩溃恢复）
python3 tests/local/bench_tpcc.py --scale mini --quick --threads 2
```

发布 OJ 前再用完整规格 + `--strict` 做最终验收。

## 推荐回归顺序

```
P2 回归 (tests/run_tests.py)
  → 专项用例 (tests/framework/<topic>/)
  → TPC-C 快测 (tests/local/bench_tpcc_neworder.py --quick)
  → OJ 模拟 (tests/local/run_oj_perf_test.py --strict)
```

规范 TPC-C 工具见 [`bench/tpccbench/README.md`](bench/tpccbench/README.md)。
