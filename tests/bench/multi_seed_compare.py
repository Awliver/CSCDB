#!/usr/bin/env python3
"""Aggregate baseline/optimized across multiple seeds, compute robust speedup."""
import json, sys, statistics, glob

def load_all(pattern):
    """Load all JSONs matching pattern. Return dict: benchmark_name -> [time1, time2, ...]"""
    agg = {}
    for path in glob.glob(pattern):
        with open(path) as f:
            data = json.load(f)
        for name, t in data["benchmarks"].items():
            agg.setdefault(name, []).append(t)
    return agg

if len(sys.argv) != 3:
    print("Usage: multi_seed_compare.py 'baseline_*.json' 'optimized_*.json'")
    print("Example: python3 multi_seed_compare.py '/tmp/baseline_seed*.json' '/tmp/opt_seed*.json'")
    sys.exit(1)

baseline = load_all(sys.argv[1])
optimized = load_all(sys.argv[2])

if not baseline or not optimized:
    print("No JSON files matched. Check patterns.")
    sys.exit(1)

n_b = len(next(iter(baseline.values())))
n_o = len(next(iter(optimized.values())))
print(f"\n{'Benchmark':<28} {'Baseline (med±IQR)':>22} {'Optimized (med±IQR)':>22} {'Speedup':>10}  {'Verdict':>9}")
print("=" * 100)

for name in sorted(set(baseline) & set(optimized)):
    bs, os_ = baseline[name], optimized[name]
    bs_med = statistics.median(bs)
    os_med = statistics.median(os_)
    bs_iqr = (max(bs) - min(bs)) if len(bs) > 1 else 0
    os_iqr = (max(os_) - min(os_)) if len(os_) > 1 else 0
    speedup = bs_med / os_med if os_med > 0 else float('inf')
    # Confidence: if the IQR ranges overlap, low confidence
    overlap = (min(bs) <= max(os_)) and (min(os_) <= max(bs))
    if speedup > 1.3 and not overlap:
        verdict = "  WIN"
    elif speedup > 1.05:
        verdict = " maybe"
    elif speedup < 0.95 and not overlap:
        verdict = "  LOSE"
    else:
        verdict = " noise"
    print(f"{name:<28} {bs_med:8.2f}±{bs_iqr:5.1f} ms      {os_med:8.2f}±{os_iqr:5.1f} ms      {speedup:6.2f}x  {verdict:>9}")

print(f"\n[baseline runs: {n_b}, optimized runs: {n_o}]")
