#!/usr/bin/env bash
# Profile TPC-C benchmark with perf + FlameGraph.
#
# Usage:
#   tests/prof/profile_tpcc.sh [--quick]
#   tests/prof/profile_tpcc.sh --measure 120
#
# Output: build/prof_out/<timestamp>/flamegraph.svg, perf.data, bench.log

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build"
PROF_DIR="$(dirname "$0")"
OUT_BASE="$BUILD/prof_out"
TS="$(date +%Y%m%d_%H%M%S)"
OUT="$OUT_BASE/$TS"
FG="$ROOT/tools/FlameGraph"

WARMUP=5
MEASURE=60
THREADS=16
SCALE=full
QUICK=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --quick) QUICK=1; WARMUP=3; MEASURE=15 ;;
    --warmup) WARMUP="$2"; shift ;;
    --measure) MEASURE="$2"; shift ;;
    --threads) THREADS="$2"; shift ;;
    --scale) SCALE="$2"; shift ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
  shift
done

if [[ -f "$PROF_DIR/config.env" ]]; then
  # shellcheck disable=SC1090
  source "$PROF_DIR/config.env"
fi

mkdir -p "$OUT"
echo "Output: $OUT"

if [[ ! -x "$BUILD/bin/rmdb" ]]; then
  echo "Build rmdb first: cd build && make rmdb -j\$(nproc)"
  exit 1
fi

# Kill stale servers
pkill -9 -f bin/rmdb 2>/dev/null || true
sleep 0.5

RMDB_PID=""
cleanup() {
  if [[ -n "$RMDB_PID" ]] && kill -0 "$RMDB_PID" 2>/dev/null; then
    kill -9 "$RMDB_PID" 2>/dev/null || true
  fi
  pkill -9 -f bin/rmdb 2>/dev/null || true
}
trap cleanup EXIT

echo "==> Starting benchmark under perf record..."
cd "$ROOT"
perf record -F "${PERF_FREQ:-997}" -g -o "$OUT/perf.data" -- \
  python3 tests/local/bench_tpcc.py \
    --scale "$SCALE" \
    --warmup "$WARMUP" \
    --measure "$MEASURE" \
    --rounds 1 \
    --threads "$THREADS" \
    --skip-crash \
    --skip-consistency \
    2>&1 | tee "$OUT/bench.log" || true

echo "==> Generating flamegraph..."
if [[ ! -d "$FG" ]]; then
  echo "FlameGraph not found. Run: bash tests/prof/install_deps.sh"
  exit 1
fi

perf script -i "$OUT/perf.data" 2>/dev/null | \
  "$FG/stackcollapse-perf.pl" | \
  "$FG/flamegraph.pl" --title "RMDB TPC-C (scale=$SCALE, ${MEASURE}s)" \
  > "$OUT/flamegraph.svg"

echo "Flamegraph: $OUT/flamegraph.svg"
echo "perf data:  $OUT/perf.data"
echo "bench log:  $OUT/bench.log"
