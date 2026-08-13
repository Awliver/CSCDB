import contextlib
import io
import unittest
from types import SimpleNamespace

from compare_tpcc_results import compare


def result(p50, p99):
    return {
        "tier": "quick",
        "scale": "full",
        "warehouses": 50,
        "threads": 32,
        "seed": 42,
        "warmup_sec": 3,
        "measure_sec": 15,
        "rounds": 1,
        "protocol": "PREPARE_SET+EXEC_BATCH",
        "mix": "finals",
        "use_batch": True,
        "routing": {"enabled": True, "seed": 42},
        "median_tpmc": 1000,
        "new_order_abort_rate": 0.3,
        "new_order_latency": {"p50_ms": p50, "p99_ms": p99},
        "overall": "PASS",
    }


def args(**overrides):
    defaults = {
        "allow_config_mismatch": False,
        "require_pass": False,
        "min_tpmc_gain_pct": None,
        "max_abort_rate_increase_pp": None,
        "max_p50_increase_pct": None,
        "max_p99_increase_pct": None,
        "max_p50_ms": 10,
        "max_p99_ms": 50,
    }
    defaults.update(overrides)
    return SimpleNamespace(**defaults)


class CompareTpccResultsTest(unittest.TestCase):
    def compare_quietly(self, baseline, candidate, options):
        with contextlib.redirect_stdout(io.StringIO()):
            return compare(baseline, candidate, options)

    def test_absolute_latency_targets_pass(self):
        self.assertEqual(
            self.compare_quietly(result(12, 60), result(9, 49), args()), 0
        )

    def test_p50_target_failure_is_not_hidden_by_good_p99(self):
        self.assertEqual(
            self.compare_quietly(result(9, 49), result(11, 45), args()), 1
        )

if __name__ == "__main__":
    unittest.main()
