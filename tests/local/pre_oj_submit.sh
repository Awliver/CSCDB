#!/usr/bin/env bash
# OJ pre-submit gate: Release build + P2 + strict full benchmark.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
THREADS="${1:-16}"

echo "=== OJ pre-submit (${THREADS} threads) ==="
echo "  data: self-generated full CSV (OJ does not ship official data)"

cd "${ROOT}/build"
echo ">>> cmake Release"
cmake .. -DCMAKE_BUILD_TYPE=Release
make rmdb -j"$(nproc)"

cd "${ROOT}"
echo ">>> P2 functional tests"
python3 tests/run_tests.py

JSON="${ROOT}/build/oj_submit_$(date +%Y%m%d_%H%M%S).json"
echo ">>> OJ strict benchmark (also auto-saves to build/bench_history/)"
python3 tests/local/run_oj_perf_test.py --strict --threads "${THREADS}" --json "${JSON}"

echo "=== DONE: check median tpmC in ${JSON} ==="
echo "    history: python3 tests/local/list_bench_history.py"
