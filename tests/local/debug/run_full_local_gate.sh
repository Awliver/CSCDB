#!/bin/bash
# 完整本地门禁：功能 + 一致性 + mid（BATCH / 决赛混合比）
set -o pipefail
ROOT=/home/neo/CSC_DB/db2026
cd "$ROOT"
TS=$(date +%Y%m%d_%H%M%S)
LOG=build/full_local_gate_${TS}.log
JSON=build/oj_mid_batch_${TS}.json

pkill -f 'bin/rmdb' 2>/dev/null || true
sleep 0.5

{
  echo "======== FULL LOCAL GATE $TS ========"
  echo "log=$LOG json=$JSON"
  echo

  echo "---- [1/3] run_tests.py ----"
  python3 tests/run_tests.py
  RC1=$?
  echo "run_tests exit=$RC1"
  echo

  echo "---- [2/3] consistency core + finals regressions ----"
  python3 tests/local/consistency/run_consistency_regress.py \
    --case C1 --case C2 --case H5 \
    --case F1 --case F2 \
    --case SI1 --case SI2 --case SI3 --case C8
  RC2=$?
  echo "consistency exit=$RC2"
  echo

  echo "---- [3/3] mid BATCH (60s x 3) ----"
  python3 tests/local/run_oj_perf_test.py --mid --json "$JSON"
  RC3=$?
  echo "mid exit=$RC3"
  echo

  echo "======== SUMMARY ========"
  echo "run_tests=$RC1 consistency=$RC2 mid=$RC3"
  if [ "$RC1" -eq 0 ] && [ "$RC2" -eq 0 ] && [ "$RC3" -eq 0 ]; then
    echo "OVERALL_GATE: PASS"
    exit 0
  else
    echo "OVERALL_GATE: FAIL"
    exit 1
  fi
} 2>&1 | tee "$LOG"
