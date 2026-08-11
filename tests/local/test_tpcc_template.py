#!/usr/bin/env python3

import os
import tempfile
import unittest

from tpcc_common import _clone_database_template


class TpccTemplateTest(unittest.TestCase):
    def test_clone_is_independent_from_template(self):
        with tempfile.TemporaryDirectory() as root:
            source = os.path.join(root, "base")
            destination = os.path.join(root, "work")
            os.makedirs(source)
            with open(os.path.join(source, "db.meta"), "wb") as stream:
                stream.write(b"meta")
            with open(os.path.join(source, "table.fd"), "wb") as stream:
                stream.write(b"base-data")

            _clone_database_template(source, destination)

            with open(os.path.join(destination, "table.fd"), "wb") as stream:
                stream.write(b"changed")
            with open(os.path.join(source, "table.fd"), "rb") as stream:
                self.assertEqual(b"base-data", stream.read())

    def test_clone_requires_database_metadata(self):
        with tempfile.TemporaryDirectory() as root:
            source = os.path.join(root, "not-a-db")
            os.makedirs(source)
            with self.assertRaises(RuntimeError):
                _clone_database_template(source, os.path.join(root, "work"))


if __name__ == "__main__":
    unittest.main()
