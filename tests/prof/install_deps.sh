#!/usr/bin/env bash
# Install perf + bpftrace + FlameGraph for TPC-C profiling (Ubuntu/Debian).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FG_DIR="$ROOT/tools/FlameGraph"

echo "==> Installing system packages (perf, bpftrace)..."
if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update -qq
  sudo apt-get install -y linux-tools-common linux-tools-generic linux-tools-$(uname -r) \
    bpftrace git 2>/dev/null || \
  sudo apt-get install -y linux-perf bpftrace git
else
  echo "apt-get not found; install perf and bpftrace manually."
fi

if [[ ! -d "$FG_DIR" ]]; then
  echo "==> Cloning FlameGraph..."
  mkdir -p "$ROOT/tools"
  git clone --depth 1 https://github.com/brendangregg/FlameGraph.git "$FG_DIR"
fi

echo "Done. FlameGraph at $FG_DIR"
