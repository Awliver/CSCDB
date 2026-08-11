#!/usr/bin/env python3

import csv
import os
import tempfile
import unittest

from generate_tpcc_data import _sample_csv_rows


class GenerateTpccDataTest(unittest.TestCase):
    def test_sampling_is_bounded_and_keeps_edges(self):
        with tempfile.TemporaryDirectory() as root:
            path = os.path.join(root, "rows.csv")
            with open(path, "w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["id", "value"])
                for index in range(1000):
                    writer.writerow([index, "v%d" % index])

            rows = _sample_csv_rows(path, n=3)
            ids = {int(row["id"]) for row in rows}
            self.assertLessEqual(len(rows), 9)
            self.assertTrue({0, 1, 2}.issubset(ids))
            self.assertTrue({997, 998, 999}.issubset(ids))


if __name__ == "__main__":
    unittest.main()
