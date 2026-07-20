#!/usr/bin/env python3
"""Per-op latency: timed push + pop_copy pair."""
from __future__ import annotations

import os
import time

from embeddedmq import Runtime


def pct(sorted_vals: list[int], p: float) -> int:
    if not sorted_vals:
        return 0
    idx = int(p * (len(sorted_vals) - 1) + 0.5)
    if idx >= len(sorted_vals):
        idx = len(sorted_vals) - 1
    return sorted_vals[idx]


def main() -> None:
    n = int(os.environ.get("EMQ_LOAD_N", "1000000"))
    payload_len = int(os.environ.get("EMQ_LOAD_PAYLOAD", "64"))
    warmup = int(os.environ.get("EMQ_LOAD_WARMUP", "50000"))
    payload = bytes([0xAB]) * payload_len
    dst = bytearray(payload_len)
    lat = [0] * n

    rt = Runtime()
    q = rt.create_queue("lat-py", 4096)

    for _ in range(warmup):
        q.push(payload)
        q.pop_copy(dst, 0)

    for i in range(n):
        t0 = time.perf_counter_ns()
        q.push(payload)
        q.pop_copy(dst, 0)
        lat[i] = time.perf_counter_ns() - t0

    lat.sort()
    print(
        f"LATENCY lang=python payload={payload_len} n={n} "
        f"p50_ns={pct(lat, 0.50)} p99_ns={pct(lat, 0.99)} "
        f"p999_ns={pct(lat, 0.999)} p9999_ns={pct(lat, 0.9999)}"
    )
    q.close()


if __name__ == "__main__":
    main()
