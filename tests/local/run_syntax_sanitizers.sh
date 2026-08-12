#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sanitizer_build=${RMDB_SANITIZER_BUILD_DIR:-"${repo_root}/build-asan-ubsan"}
build_jobs=${RMDB_BUILD_JOBS:-2}
gate_rows=${RMDB_SANITIZER_GATE_ROWS:-10000}
diff_rows=${RMDB_SANITIZER_DIFF_ROWS:-500}
diff_cases=${RMDB_SANITIZER_DIFF_CASES:-25}
union_diff_cases=${RMDB_SANITIZER_UNION_DIFF_CASES:-8}
extended_rows=${RMDB_SANITIZER_EXTENDED_ROWS:-1000000}
extended_diff_rounds=${RMDB_SANITIZER_EXTENDED_ROUNDS:-1}

cmake -S "${repo_root}" -B "${sanitizer_build}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS_DEBUG="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_C_FLAGS_DEBUG="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build "${sanitizer_build}" --target rmdb test_parser -j"${build_jobs}"

export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1:halt_on_error=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}

RMDB_BUILD_DIR="${sanitizer_build}" \
    python3 -B "${repo_root}/tests/syntax_functional_test.py" \
    --predicate-rows "${gate_rows}" --union-rows "${gate_rows}" \
    --diff-rows "${diff_rows}" --cases-per-seed "${diff_cases}" --seeds 42 \
    --union-cases-per-seed "${union_diff_cases}" --union-seeds 42 \
    --extended-rows "${extended_rows}" --extended-rounds "${extended_diff_rounds}" \
    --extended-seeds 42
