#!/usr/bin/env bash
# 决赛提交前一键总门禁（07-30 战役后版本，见 Docs/FinalCompetition/0730-OJ战役复盘.md）
#
#   1. Release 构建
#   2. 官方功能门禁（tests/run_tests.py，11 项）
#   3. 一致性回归 C1/C2/H5
#   4. 定向复现器：自赋值功能矩阵（24 项）+ Delivery 扫描回环（90s 删除风暴）
#   5. 增强版 oj_gate：fresh W=50 装载门禁 + 3 窗资源/内存断言
#      + 崩前崩后七项聚合位对比 + churn 中途 kill -9 + 二次恢复幂等
#
# 用法：
#   bash tests/local/pre_oj_submit.sh          # 正式验收（全新 W=50 装载，~35min）
#   FAST=1 bash tests/local/pre_oj_submit.sh   # 快速迭代（复用已装载库 + 45s 窗）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "${ROOT}"

echo "=== [1/5] Release 构建 ==="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null
make -C build rmdb -j"$(nproc)"

pkill -x rmdb || true

echo "=== [2/5] 官方功能门禁（11 项） ==="
python3 tests/run_tests.py

echo "=== [3/5] 一致性回归 C1/C2/H5 ==="
python3 tests/local/consistency/run_consistency_regress.py --case C1 --case C2 --case H5

echo "=== [4/5] 定向复现器 ==="
python3 tests/local/debug/repro_selfassign.py
python3 tests/local/debug/delivery_scan_loop_repro.py 90 24

echo "=== [5/5] 增强版 oj_gate（W=50 全流程） ==="
if [[ "${FAST:-0}" == "1" ]]; then
    OJ_GATE_SKIP_LOAD=1 OJ_GATE_WINDOW=45 python3 tests/local/oj_gate.py
else
    python3 tests/local/oj_gate.py
fi

echo "=== 全部门禁通过，可以提交 ==="