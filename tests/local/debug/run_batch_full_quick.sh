#!/bin/bash
set -e
ROOT=/home/neo/CSC_DB/db2026
cd "$ROOT"
pkill -f 'bin/rmdb' 2>/dev/null || true
sleep 0.5
cd tests/local
python3 bench_tpcc.py --scale full --quick --skip-crash --skip-consistency --skip-p2 --skip-load-content --skip-stress --threads 8 --no-save-history --json ../../build/batch_full_quick.json 2>&1 | tee ../../build/batch_full_quick.log
echo BENCH_EXIT:${PIPESTATUS[0]}
pkill -f 'bin/rmdb' 2>/dev/null || true
