# FinalCompetition（决赛赛题与主线）

> 决赛规范、正确性门禁、功能增量；**与性能档案 `Optimize/` 分离**。  
> 初赛已落地 → [`Optimize/5`](../Optimize/5.已完成优化摘要.md)  
> 最后更新：**2026-07-29**

---

## 索引

| 文档 | 状态 | 说明 |
|------|:----:|------|
| **[决赛赛题整理](./决赛赛题整理.md)** | 📘 规范 | 决赛 PDF（29 页）完整要点，对照官方全文 |
| **[决赛准备 Todo](./决赛准备Todo.md)** | 🎯 主线 | 门禁 → Wire v3 / SI·SER / COUNT(DISTINCT) / FLOAT32 → W=50×32 |
| [一致性审计与回归用例](./一致性审计与回归用例.md) | 🟢 P0 已修 | C1/C2/H5 已关闭；余下见文档 §三 |

性能侧候选（🧊）仍在 [`Optimize/2`](../Optimize/2.待落地优化Todo.md)，由「决赛准备 Todo」§3 引用，避免两处各写一套。

---

## 快速验证（正确性 P0 门禁）

```bash
# 应 100% PASS（勿用 --stability：该模式统计漏洞检出率）
python3 tests/local/consistency/run_consistency_regress.py \
  --case C1 --case C2 --case H5
python3 tests/run_tests.py
```

修复前曾 `--stability 10` → **100% detected**；修复后连跑 10 轮 → **100% PASS**（2026-07-14）。

完整复验（2026-07-27）：`run_tests` 11/11 + consistency P0 + `--mid`（**BATCH**）median **3578.46**（NewOrder 0 abort）→ 见 [`决赛准备Todo`](./决赛准备Todo.md) §1。

一键门禁：`bash tests/local/debug/run_full_local_gate.sh`  
OJ 形窗口：`python3 tests/local/run_oj_perf_test.py --finals`（150s×3 / 32；本地仍 W=5）

---

## 与测试代码关系

| 类型 | 路径 |
|------|------|
| 赛题 / 主线文档 | `Docs/FinalCompetition/` |
| 可执行回归 | `tests/local/consistency/` |

文档归档在 FinalCompetition；用例实现留在 `tests/`（可提交、可 CI）。
