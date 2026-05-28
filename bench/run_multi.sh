#!/bin/bash
# Run benchmark with 5 seeds on current build. Usage: ./run_multi.sh <label> <scale>
set -e
LABEL=${1:-run}
SCALE=${2:-10000}
PROJ=~/projects/db2026

cd $PROJ/build
for seed in 1 2 3 4 5; do
    echo "=== $LABEL seed=$seed ==="
    pkill -9 rmdb 2>/dev/null || true
    rm -rf test_db
    ./bin/rmdb test_db > /tmp/server_$LABEL_$seed.log 2>&1 &
    sleep 1
    python3 $PROJ/bench/benchmark_v2.py --label ${LABEL}_seed${seed} --scale $SCALE --seed $seed --out /tmp/${LABEL}_seed${seed}.json
    pkill -INT rmdb || true
    sleep 1
done
echo ""
echo "=== Done. Run: ==="
echo "  python3 $PROJ/bench/multi_seed_compare.py '/tmp/baseline_seed*.json' '/tmp/${LABEL}_seed*.json'"
