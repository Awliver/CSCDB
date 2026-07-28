# 一致性回归测试集

> **待办归档**：[Docs/Todo/一致性审计与回归用例.md](../../Docs/Todo/一致性审计与回归用例.md) · 目录 [Docs/Todo/README.md](../../Docs/Todo/README.md)

针对 [一致性审计](../../Docs/Analysis/ProjectAnalysis/5.陷阱清单与编程契约.md) 中 **C1–M8** 类漏洞设计的本地复现/回归用例。与 `tests/local/debug/`（gitignore、临时脚本）不同，本目录**可提交**，作为修复前后的门禁。

## 快速开始

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make rmdb -j$(nproc)
ps aux | grep '[r]mdb' | awk '{print $2}' | xargs -r kill -9

# 日常：确定性用例 (~1–3 min)
python3 tests/local/consistency/run_consistency_regress.py --quick

# 单点复现
python3 tests/local/consistency/run_consistency_regress.py --case C2

# 偶发类：重复 10 次观察 flaky 率
python3 tests/local/consistency/run_consistency_regress.py --case C3 --repeat 10

# 修复前：漏洞检出率（应 100% detected）
python3 tests/local/consistency/run_consistency_regress.py --case C1 --case C2 --case H5 --stability 10

# 修复后：应 PASS（勿用 --stability 判成功）
python3 tests/local/consistency/run_consistency_regress.py --case C1 --case C2 --case H5

# 全量（含 TPC-C strict、长 stress，~15–30 min）
python3 tests/local/consistency/run_consistency_regress.py
```

## 用例 ↔ 审计映射

| ID | 审计项 | 类型 | 机制 | 通过标准 |
|----|--------|------|------|----------|
| **C1** | C1 堆先于 MVCC 链 | 确定性 | 6 读者 + 3 写者循环未提交 INSERT | 读者不得见未提交行 |
| **C1b** | C1 补充 | 确定性 | 读者先 BEGIN 快照，写者 INSERT 未提交 | 读者 count=0 |
| **C2** | C2 `pending` 未检 | 确定性 | T1 语句结束仍持 txn，T2 同键 INSERT | T2 必须 abort，仅 1 行 |
| **C3** | C3 Cleaner TOCTOU | flaky | 2000 行 × 8 线程随机 UPDATE | `sum(v)` 等于 commit 次数，无 crash |
| **C4** | C4 log-after-dirty | flaky | 200 次 commit 后 SIGKILL 重启 | 重启后 `v` 与 kill 前一致 |
| **H1** | H1 `maintain_parent` | 慢 | 2500 次索引 insert/delete | 无 crash，索引 probe 成功 |
| **H2** | H2 `coalesce` IX_NO_PAGE | 确定性 | 30 轮 40 行删空 | 无 crash，每轮 count=0 |
| **H3** | H3 INLJ 缺 SSI | flaky | SER join + 4 路 inner UPDATE | 无 crash，join 行数稳定 |
| **H4** | H4 `ser_finish` 无锁 | flaky | 16×40 SER 事务 | 无 crash / server error |
| **H5** | H5 `mvcc_on_` 缓存 | 确定性 | `BEGIN` → 外连 autocommit UPDATE → `SELECT` | 不得见 post-snapshot 值 |
| **M1** | M1 双 BEGIN | 确定性 | 4 线程 `begin;begin;` 双表插入 | 两表各 4 行 |
| **M6** | M6 `output_file off` | 确定性 | `set output_file off` + SELECT | `output.txt` 大小为 0 |
| **M7** | M7 ytd drift | flaky | 8×25 Payment mini TPC-C | warehouse/district ytd 增量差 ≤1 |
| **INT** | 集成 | flaky | 8 线程 SI mini burst + strict | `run_consistency_checks` PASS |

### 标签

- `mvcc` — C1/C1b/C2/H5
- `wal` / `bpm` — C3/C4
- `index` — H1/H2
- `ssi` — H3/H4
- `meta` — M1/M6（非数据一致性，工具/元数据）
- `tpcc` / `integration` — M7/INT

## 设计原则

1. **确定性优先**：C1/C2/H5/H2/C1b 不依赖调度运气，适合 CI / 修 bug 前后对比。
2. **flaky 用 `--repeat`**：C3/C4/H3/H4/M7/INT 标注 `flaky=True`。
3. **失败语义**：
   - **数据 bug**：C/H 系列 FAIL → 应修代码。
   - **已知缺口**：M6 FAIL 表示 `set output_file off` 未实现（文档/测试不一致），修 server 或改测试预期。
4. **与现有脚本关系**：
   - `tests/local/stress_consistency.py` — 多 seed 泛化压力
   - `tests/local/debug/si_consistency_repro.py` — 同类 INT 的早期版本
   - 本套件 — **按 audit ID 定点复现**

## 修复验证工作流

```bash
# 1. 修复前：记录 baseline
python3 tests/local/consistency/run_consistency_regress.py --quick --repeat 3 | tee /tmp/cons_before.log

# 2. 改代码 + 编译
cd build && make rmdb -j$(nproc)

# 3. 修复后：同一命令应全 PASS（或 flaky 通过率上升）
python3 tests/local/consistency/run_consistency_regress.py --quick --repeat 3 | tee /tmp/cons_after.log

# 4. 功能回归
python3 tests/run_tests.py
```

## 文件结构

```
tests/local/consistency/
  harness.py                 # 启停库、多客户端、Barrier
  cases.py                   # 各用例实现 + ALL_CASES 注册表
  run_consistency_regress.py # CLI 入口
  README.md                  # 本文件
```

## 当前预期（2026-07-14）

| 用例 | 结果 | 说明 |
|------|------|------|
| **C1** | **PASS ×10** | `reserve` → `mvcc_insert` → `publish` |
| **C2** | **PASS ×10** | `mvcc_other_writer` 查 pending |
| **H5** | **PASS ×10** | 重查 dirty + 显式并发时 autocommit 走 MVCC；目标 id=75 |
| C1b | PASS | 双 txn 快照隔离正常路径 |
| M6 | XFAIL | API 缺口 |

**C2 触发条件**：`pre_dirty_table()` + 两连接均 `BEGIN` 后再 INSERT（禁用 SI fast path）。

**H5 触发条件**：种子 auto-commit（初始 `any_mvcc_dirty_=false`）；读者 `BEGIN` 后、写者提交 UPDATE 后、读者再 `SELECT` 同键。

**注意**：`--stability N` 统计的是**漏洞检出率**（修前应为 100% detected）。修后请用普通跑法验 PASS。

**2026-07-14 完整复验**：功能 11/11 + C1/C2/H5 PASS + `--mid` median **3943.73**（NewOrder 0 abort）→ 见 Docs/Todo/决赛准备Todo §1。
