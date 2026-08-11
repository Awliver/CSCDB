#!/usr/bin/env python3
"""Deterministic finals-style 160-slot TPC-C hotspot routing."""

import random
from collections import Counter


class FinalsRouter:
    SLOT_COUNT = 160
    CLIENT_COUNT = 32
    HOT_WAREHOUSE_COUNT = 4
    HOT_SLOTS_PER_WAREHOUSE = 26
    HOT_DISTRICT_PROBABILITY = 0.65
    HOT_ITEM_COUNT = 24

    def __init__(self, warehouses, districts, items, workload_seed, phase_id):
        if warehouses < self.HOT_WAREHOUSE_COUNT:
            raise ValueError("finals hotspot routing requires at least four warehouses")
        self.warehouses = warehouses
        self.districts = districts
        self.items = items
        self.workload_seed = workload_seed
        self.phase_id = phase_id

        identity_rng = random.Random(workload_seed ^ 0x5A17C0DE)
        self.hot_warehouses = sorted(
            identity_rng.sample(range(1, warehouses + 1), self.HOT_WAREHOUSE_COUNT)
        )
        self.hot_districts = {
            warehouse: identity_rng.randint(1, districts)
            for warehouse in self.hot_warehouses
        }
        hot_item_count = min(self.HOT_ITEM_COUNT, items)
        self.hot_items = sorted(identity_rng.sample(range(1, items + 1), hot_item_count))

        slots = []
        for warehouse in self.hot_warehouses:
            slots.extend([warehouse] * self.HOT_SLOTS_PER_WAREHOUSE)
        cold = [
            warehouse for warehouse in range(1, warehouses + 1)
            if warehouse not in self.hot_warehouses
        ]
        slots.extend(cold)
        extra_count = self.SLOT_COUNT - len(slots)
        if extra_count > len(cold):
            for index in range(extra_count):
                slots.append(cold[index % len(cold)])
        else:
            slots.extend(identity_rng.sample(cold, extra_count))
        if len(slots) != self.SLOT_COUNT:
            raise AssertionError("invalid finals slot count: %d" % len(slots))

        phase_rng = random.Random(workload_seed ^ (phase_id * 0x9E3779B1))
        phase_rng.shuffle(slots)
        self.slots = slots

    def route(self, client_id, txn_no, route_rng):
        slot_index = (
            client_id
            + self.CLIENT_COUNT * (txn_no % 5)
            + 13 * (txn_no // 5)
        ) % self.SLOT_COUNT
        warehouse = self.slots[slot_index]
        if (
            warehouse in self.hot_districts
            and route_rng.random() < self.HOT_DISTRICT_PROBABILITY
        ):
            district = self.hot_districts[warehouse]
        else:
            district = route_rng.randint(1, self.districts)
        return {
            "w_id": warehouse,
            "d_id": district,
            "slot": slot_index,
            "hot_warehouse": warehouse in self.hot_districts,
        }

    def slot_counts(self):
        return dict(Counter(self.slots))


def describe_router(router):
    counts = router.slot_counts()
    return {
        "enabled": True,
        "slot_count": router.SLOT_COUNT,
        "hot_warehouses": list(router.hot_warehouses),
        "hot_districts": dict(router.hot_districts),
        "hot_items": list(router.hot_items),
        "warehouse_slots": dict(sorted(counts.items())),
        "phase_id": router.phase_id,
        "workload_seed": router.workload_seed,
    }

