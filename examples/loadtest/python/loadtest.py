#!/usr/bin/env python3
"""PyPI embeddedmq queue load test."""
from __future__ import annotations

import os
import time

from embeddedmq import Runtime


def main() -> None:
    n = int(os.environ.get("EMQ_LOAD_N", "100000"))
    payload_len = int(os.environ.get("EMQ_LOAD_PAYLOAD", "64"))
    payload = bytes([i % 256 for i in range(payload_len)])
    capacity = max(n + 16, 1024)

    print(f"client=python n={n} payload={payload_len} capacity={capacity}")
    rt = Runtime()
    q = rt.create_queue("loadtest-py", capacity)

    t0 = time.perf_counter()
    for _ in range(n):
        q.push(payload)
    t1 = time.perf_counter()

    for _ in range(n):
        msg = q.pop(1000)
        _ = msg.data()
        del msg
    t2 = time.perf_counter()

    q.close()
    push_s = t1 - t0
    pop_s = t2 - t1
    total_s = t2 - t0
    print(
        f"RESULT lang=python push_ops={n / push_s:.0f}/s "
        f"pop_ops={n / pop_s:.0f}/s roundtrip_ops={n / total_s:.0f}/s "
        f"push_ms={push_s * 1000:.1f} pop_ms={pop_s * 1000:.1f} total_ms={total_s * 1000:.1f}"
    )


if __name__ == "__main__":
    main()
