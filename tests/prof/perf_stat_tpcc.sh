#!/usr/bin/env bash
# Collect macro performance counters for one local TPC-C run.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build"
PROF_DIR="$(dirname "$0")"

WARMUP=5
MEASURE=60
THREADS=16
SCALE=full

while [[ $# -gt 0 ]]; do
  case "$1" in
    --quick) WARMUP=3; MEASURE=15 ;;
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

if ! command -v perf >/dev/null 2>&1; then
  echo "perf is not installed in this WSL distribution. Install linux-tools for the running kernel."
  exit 2
fi

pkill -9 -f bin/rmdb 2>/dev/null || true
sleep 0.5

cd "$ROOT"
perf stat -d -d \
  python3 -B tests/local/bench_tpcc.py \
    --scale "$SCALE" \
    --warmup "$WARMUP" \
    --measure "$MEASURE" \
    --rounds 1 \
    --threads "$THREADS" \
    --skip-crash \
    --skip-consistency \
    --skip-p2 \
    --skip-load-content \
    --skip-stress \
    --diagnostics \
    --no-save-history \
  2>&1
