#!/usr/bin/env python3

import os
import tempfile
import unittest

from perf_reporting import (
    batch_latency_summaries,
    classify_failure,
    latency_summary_seconds,
    merge_counts,
    parse_server_diagnostics,
)


class PerfReportingTest(unittest.TestCase):
    def test_abort_stage_and_reason(self):
        info = classify_failure(
            "neworder b2 abort op=13 executed=13: Transaction aborted because of deadlock prevention"
        )
        self.assertEqual("abort", info["kind"])
        self.assertEqual("neworder_b2", info["stage"])
        self.assertEqual(13, info["failed_op"])
        self.assertEqual("deadlock_or_si_conflict", info["reason"])

    def test_merge_and_latency(self):
        rounds = [
            {
                "abort_reasons": {"a": 2},
                "new_order_latencies": [0.010, 0.030],
                "all_success_latencies": [0.005, 0.010, 0.030],
            },
            {
                "abort_reasons": {"a": 1, "b": 4},
                "new_order_latencies": [0.020],
                "all_success_latencies": [0.020],
            },
        ]
        self.assertEqual({"a": 3, "b": 4}, merge_counts(rounds, "abort_reasons"))
        summary = latency_summary_seconds(rounds)
        self.assertEqual(3, summary["count"])
        self.assertAlmostEqual(20.0, summary["p50_ms"])
        self.assertAlmostEqual(30.0, summary["p99_ms"])
        all_success = latency_summary_seconds(rounds, "all_success_latencies")
        self.assertEqual(4, all_success["count"])
        self.assertAlmostEqual(20.0, all_success["p50_ms"])
        self.assertAlmostEqual(30.0, all_success["p99_ms"])

    def test_server_log_parser(self):
        content = "\n".join(
            [
                "[WAL_STATS timer] fsync=8 avg_us=120 max_us=700 bytes=4096 batches=4",
                "[bpm-stats] pool=100 free=3 evictable=60 pinned=37",
                "[sweep-wm] wm=12 published=15 rts_n=2 rts_min=12",
                "[batch-op-stats] id=24 count=200 avg_us=321.50 max_us=800.25",
                "[abort-stats periodic] lock_active_writer=17 mvcc_stale_snapshot=9",
                "[pressure-abort] Buffer pool pressure | sql: update t set a=1",
            ]
        )
        fd, path = tempfile.mkstemp(prefix="rmdb-perf-report-")
        try:
            with os.fdopen(fd, "w") as stream:
                stream.write(content)
            report = parse_server_diagnostics(path)
            self.assertTrue(report["exists"])
            self.assertEqual(8, report["wal_last"]["fsync"])
            self.assertEqual(37, report["bpm"]["max_pinned"])
            self.assertEqual(1, report["line_counts"]["pressure_abort"])
            self.assertEqual(200, report["batch_ops"]["last"]["24"]["count"])
            self.assertAlmostEqual(321.5, report["batch_ops"]["last"]["24"]["avg_us"])
            self.assertEqual(17, report["abort_stats"]["last"]["lock_active_writer"])
            self.assertEqual(9, report["abort_stats"]["last"]["mvcc_stale_snapshot"])
        finally:
            os.unlink(path)

    def test_batch_latency_summaries(self):
        rounds = [
            {"batch_latencies": {"new_order.b1": [0.003], "new_order.b2": [0.007]}},
            {"batch_latencies": {"new_order.b1": [0.005], "new_order.b2": [0.009]}},
        ]
        summaries = batch_latency_summaries(rounds)
        self.assertEqual(2, summaries["new_order.b1"]["count"])
        self.assertAlmostEqual(5.0, summaries["new_order.b1"]["p99_ms"])
        self.assertAlmostEqual(9.0, summaries["new_order.b2"]["p99_ms"])


if __name__ == "__main__":
    unittest.main()
