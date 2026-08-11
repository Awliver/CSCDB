# TPC-C A/B and abort diagnostics

This workflow keeps performance changes measurable before touching another kernel hot path.

## 1. One instrumented run

```bash
python3 tests/local/run_oj_perf_test.py --mid --threads 32 \
  --diagnostics --json build/candidate.json
```

The JSON includes:

- median and per-round tpmC;
- NewOrder p50/p95/p99/max latency;
- success/failure counts by transaction family;
- abort reasons, failing transaction stage, and bounded diagnostic samples;
- the failing `EXEC_BATCH` operation index;
- WAL, buffer-pool, MVCC, `pressure-abort`, and `error-abort` log summaries;
- cumulative prepared-statement timing (`batch_ops`) keyed by statement id;
- tested binary/build path, git revision, build mode, and host identity.

`deadlock_or_si_conflict` is intentionally broad. The current kernel reports several SI/MVCC
conflicts through `AbortReason::DEADLOCK_PREVENTION`, so the client must not invent a narrower
cause. Use `failure_stages`, `abort_by_type`, and `failed_op` to locate the hot statement.

Full-scale runs use the finals 160-slot hotspot router by default. This is required to reproduce
the official conflict rate; `--uniform-routing` is an explicit diagnostic opt-out and its result
must not be compared with a finals-routing result. The official latency metric pools successful
NewOrder samples across measurement rounds. `all_success_latency` is diagnostic only.

## 2. Compare existing JSON files

```bash
python3 tests/local/compare_tpcc_results.py \
  build/baseline.json build/candidate.json
```

Optional regression gates:

```bash
python3 tests/local/compare_tpcc_results.py \
  build/baseline.json build/candidate.json \
  --require-pass \
  --min-tpmc-gain-pct 2 \
  --max-abort-rate-increase-pp 1 \
  --max-p50-increase-pct 10 \
  --max-p99-increase-pct 10 \
  --max-p50-ms 10 \
  --max-p99-ms 50
```

The comparator rejects mismatched tier, scale, W, client count, seed, window, protocol, mix, or
routing metadata.
This prevents a faster but non-equivalent run from being mistaken for an optimization.

## 3. Build and run two git refs

Run this in WSL/Linux after generating `build/tpcc_data/full`. Prepare one pristine database
once so every candidate starts from exactly the same committed state:

```bash
python3 -B tests/local/prepare_tpcc_base.py
```

Then run the two refs:

```bash
python3 tests/local/run_ab_perf_test.py \
  --baseline-ref <known-good-commit> \
  --candidate-ref HEAD \
  --base-db build/tpcc_base_w50 \
  --tier mid --threads 32
```

The runner creates detached temporary worktrees, builds both refs in Release mode, links the
same full data set into both builds, runs them sequentially with identical parameters, and writes
artifacts under `build/ab_results/<timestamp>/`.

For source edits that have not been committed yet, replace `--candidate-ref HEAD` with
`--candidate-working-tree`. The candidate is then labelled `WORKTREE` and records
`git_dirty=true`, so its result cannot be mistaken for the baseline commit.

Use `--candidate-first` for the reverse order. If a result is close to the noise floor, repeat in
both orders and trust only a change that survives both runs. Use `--tier finals` before submission;
`--tier quick` is only a wiring smoke test.

## 4. Decide what to profile next

1. High `deadlock_or_si_conflict` concentrated in one stage: inspect that statement's lock/MVCC
   lifetime and index access path.
2. High WAL `avg_us`, `max_us`, or low `waits/fsync`: inspect group commit and log batching.
3. Rising BPM `max_pinned` or `pressure_abort`: find missing unpins and long page pin lifetimes.
4. Low aborts but high p99: collect `tests/prof/profile_tpcc.sh` and optimize the largest CPU stack.
5. A tpmC gain with worse abort rate or p99 is not automatically a win; compare all three gates.

Run the reporting unit tests with:

```bash
cd tests/local
python3 -B -m unittest \
  test_compare_tpcc_results test_perf_reporting test_tpcc_routing test_tpcc_template
```
