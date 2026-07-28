#!/bin/bash
set -e
ROOT=/home/neo/CSC_DB/db2026
cd "$ROOT"
pkill -f 'bin/rmdb' 2>/dev/null || true
sleep 0.5
rm -rf build/sum_probe_db
mkdir -p build/sum_probe_db
./build/bin/rmdb build/sum_probe_db > build/sum_probe_db/rmdb.log 2>&1 &
RPID=$!
cleanup() { kill -9 $RPID 2>/dev/null || true; pkill -f 'bin/rmdb build/sum_probe' 2>/dev/null || true; }
trap cleanup EXIT
sleep 2
python3 tests/local/debug/probe_prepare.py | tee build/sum_probe_db/probe.out
echo PROBE_DONE
