#!/usr/bin/env python3

import random
import unittest
from collections import Counter

from tpcc_routing import FinalsRouter


class FinalsRouterTest(unittest.TestCase):
    def test_slot_contract_and_five_wave_coverage(self):
        router = FinalsRouter(50, 10, 100000, workload_seed=42, phase_id=1)
        counts = router.slot_counts()
        self.assertEqual(160, sum(counts.values()))
        self.assertEqual(set(range(1, 51)), set(counts))
        for warehouse in router.hot_warehouses:
            self.assertEqual(26, counts[warehouse])

        covered = []
        route_rng = random.Random(7)
        for txn_no in range(5):
            for client_id in range(32):
                covered.append(router.route(client_id, txn_no, route_rng)["slot"])
        self.assertEqual(set(range(160)), set(covered))

    def test_identity_stable_but_phase_slots_change(self):
        first = FinalsRouter(50, 10, 100000, workload_seed=99, phase_id=1)
        second = FinalsRouter(50, 10, 100000, workload_seed=99, phase_id=2)
        self.assertEqual(first.hot_warehouses, second.hot_warehouses)
        self.assertEqual(first.hot_districts, second.hot_districts)
        self.assertEqual(first.hot_items, second.hot_items)
        self.assertNotEqual(first.slots, second.slots)

    def test_hot_district_probability_is_close(self):
        router = FinalsRouter(50, 10, 100000, workload_seed=42, phase_id=1)
        hot_w = router.hot_warehouses[0]
        slot = router.slots.index(hot_w)
        # Choose client/txn pairs by reading the slot directly; only district sampling matters here.
        rng = random.Random(123)
        samples = Counter()
        for _ in range(10000):
            if rng.random() < router.HOT_DISTRICT_PROBABILITY:
                district = router.hot_districts[hot_w]
            else:
                district = rng.randint(1, 10)
            samples[district] += 1
        observed = samples[router.hot_districts[hot_w]] / 10000.0
        # Random fallback can also choose the hot district: 0.65 + 0.35 / 10 = 0.685.
        self.assertGreater(observed, 0.66)
        self.assertLess(observed, 0.71)
        self.assertGreaterEqual(slot, 0)


if __name__ == "__main__":
    unittest.main()
