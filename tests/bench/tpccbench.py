#!/usr/bin/env python3
"""tpccbench entry point. Usage: python3 tests/bench/tpccbench.py --help"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tpccbench.cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
