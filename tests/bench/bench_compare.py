#!/usr/bin/env python3
"""Compare two benchmark JSON outputs (baseline vs optimized)"""
import json, sys

if len(sys.argv) != 3:
    print("Usage: bench_compare.py <baseline.json> <optimized.json>")
    sys.exit(1)

with open(sys.argv[1]) as f: baseline = json.load(f)
with open(sys.argv[2]) as f: optimized = json.load(f)

print(f"\n{'Benchmark':<32} {'Baseline':>12} {'Optimized':>12} {'Speedup':>10}  {'Verdict':>10}")
print("=" * 84)

total_b, total_o, n = 0, 0, 0
for name in baseline["benchmarks"]:
    b = baseline["benchmarks"][name]
    o = optimized["benchmarks"].get(name)
    if o is None: continue
    speedup = b / o if o > 0 else float('inf')
    verdict = "  faster" if speedup > 1.05 else "  slower" if speedup < 0.95 else "    same"
    print(f"{name:<32} {b:>10.2f} ms {o:>10.2f} ms {speedup:>9.2f}x  {verdict:>10}")
    total_b += b; total_o += o; n += 1

print("=" * 84)
if n > 0:
    avg = total_b / total_o if total_o > 0 else 0
    print(f"{'OVERALL (sum)':<32} {total_b:>10.2f} ms {total_o:>10.2f} ms {avg:>9.2f}x")
