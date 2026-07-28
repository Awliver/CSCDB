#!/usr/bin/env python3
"""决赛 Wire Protocol v3 就绪探针：握手 + EXEC_STREAM(show tables;)。

对齐测评说明测活要求：
  端口由 rmdb 监听，且 SHOW TABLES 返回完整 COMMAND_OK 或 META…RESULT_END。
空库 0 行是有效结果。

用法：
  python3 tests/local/wire_readiness_probe.py [--host 127.0.0.1] [--port 8765]
"""

from __future__ import annotations

import argparse
import sys
import time

from wire_client import WireClient, result_to_text


def probe_show_tables(host: str, port: int, timeout: float) -> int:
    t0 = time.monotonic()
    try:
        cli = WireClient(host=host, port=port, timeout=timeout)
        res = cli.exec_stream("show tables;")
        cli.close()
    except Exception as e:
        print(f"FAIL: {type(e).__name__}: {e}", file=sys.stderr)
        return 1

    elapsed = time.monotonic() - t0
    print(f"OK handshake+show tables in {elapsed:.3f}s")
    if res.columns:
        print(f"  META cols={len(res.columns)} names={[c[0] for c in res.columns]}")
    print(f"  rows={len(res.rows)} aborted={res.aborted} error={res.error}")
    if res.diagnostic:
        print(f"  diag={res.diagnostic[:200]!r}")

    if res.error or res.aborted:
        print("FAIL: " + result_to_text(res), file=sys.stderr)
        return 2
    if res.columns:
        print("PASS (META…RESULT_END)")
        return 0
    print("PASS (COMMAND_OK)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Wire v3 readiness probe (show tables)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--timeout", type=float, default=10.0)
    args = ap.parse_args()
    return probe_show_tables(args.host, args.port, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
