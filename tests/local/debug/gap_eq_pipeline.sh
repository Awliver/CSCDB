#!/usr/bin/env bash
# gap-EQ 漏行复现流水线（复刻 2026-07-31 107例失败轮）:
#   全新触发态副本 → 恢复 → 探测 → 120s×32 搅动 → 静默探测
# 用法: bash gap_eq_pipeline.sh <标签> [SERVER_ENV...]
#   例: bash gap_eq_pipeline.sh fpON RMDB_SCAN_FASTPATH=1
set -e
ROOT=/home/smart/workspace/2026/db2026
BUILD=$ROOT/build
TAG=$1; shift
DB=oj_gate_w10_trig

pkill -x rmdb || true; sleep 1
rm -rf $BUILD/$DB
cp -a $BUILD/oj_gate_w10_db.bak0731 $BUILD/$DB

cd $BUILD && env "$@" nohup bin/rmdb $DB > $DB.$TAG.log 2>&1 &
cd $ROOT
until python3 -c "import sys; sys.path.insert(0,'tests/local/debug'); from wirecli import Conn; c=Conn(); c.exec_stream('show tables;'); c.close()" 2>/dev/null; do sleep 3; done
echo "[$TAG] server ready"

AB_W=10 AB_DB=$DB python3 tests/local/debug/gap_eq_ab.py probe 2>&1 | tail -2 | sed "s/^/[$TAG pre] /"
SKIP_BOOTSTRAP=1 TPCC_W=10 HOTSPOT=2 timeout 400 python3 tests/local/debug/wire_tpcc_stress2.py 120 32 2>&1 | grep FINAL | sed "s/^/[$TAG] /"
AB_W=10 AB_DB=$DB python3 tests/local/debug/gap_eq_ab.py probe 2>&1 | grep -E "MISMATCH|probed|FAIL|PASS" | tail -8 | sed "s/^/[$TAG post] /"
grep -c "bpm-heal" $BUILD/$DB.$TAG.log | sed "s/^/[$TAG] bpm-heal=/" || true
