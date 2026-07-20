#!/usr/bin/env python3
"""Python load test — scalar pop_into vs batch, with median over trials."""
from __future__ import annotations

import os
import statistics
import time

from embeddedmq import Runtime


def report(mode: str, n: int, t0: float, t1: float, t2: float) -> float:
    push_s = t1 - t0
    pop_s = t2 - t1
    total_s = t2 - t0
    print(
        f"RESULT lang=python mode={mode} "
        f"push_ops={n / push_s:.0f}/s pop_ops={n / pop_s:.0f}/s "
        f"roundtrip_ops={n / total_s:.0f}/s "
        f"push_ms={push_s * 1000:.1f} pop_ms={pop_s * 1000:.1f} total_ms={total_s * 1000:.1f}"
    )
    return n / total_s


def main() -> None:
    n = int(os.environ.get("EMQ_LOAD_N", "100000"))
    payload_len = int(os.environ.get("EMQ_LOAD_PAYLOAD", "64"))
    warmup = int(os.environ.get("EMQ_LOAD_WARMUP", "20000"))
    batch = int(os.environ.get("EMQ_LOAD_BATCH", "32"))
    trials = max(1, int(os.environ.get("EMQ_LOAD_TRIALS", "5")))
    payload = bytes(i % 256 for i in range(payload_len))
    dst = bytearray(payload_len)
    batch_dst = bytearray(payload_len * batch)
    capacity = max(n + 16, 1024)

    print(
        f"client=python n={n} payload={payload_len} capacity={capacity} "
        f"batch={batch} trials={trials}"
    )
    rt = Runtime()
    q = rt.create_queue("loadtest-py", capacity)

    for _ in range(warmup):
        q.push(payload)
    for _ in range(warmup):
        q.pop_copy(dst, 1000)

    samples: list[float] = []
    for _ in range(trials):
        t0 = time.perf_counter()
        for _ in range(n):
            q.push(payload)
        t1 = time.perf_counter()
        for _ in range(n):
            got = q.pop_copy(dst, 1000)
            if got != payload_len:
                raise RuntimeError(f"bad len {got}")
        t2 = time.perf_counter()
        samples.append(report("scalar_pop_into", n, t0, t1, t2))
    print(
        f"MEDIAN lang=python mode=scalar_pop_into "
        f"roundtrip_ops={statistics.median(samples):.0f}/s trials={trials}"
    )

    samples = []
    for _ in range(trials):
        t0 = time.perf_counter()
        left = n
        while left > 0:
            chunk = batch if batch <= left else left
            q.push_repeat(payload, chunk)
            left -= chunk
        t1 = time.perf_counter()
        left = n
        while left > 0:
            chunk = batch if batch <= left else left
            got = q.pop_copy_n(batch_dst, payload_len, chunk)
            if got != chunk:
                raise RuntimeError(f"bad batch pop {got} want {chunk}")
            left -= got
        t2 = time.perf_counter()
        samples.append(report(f"batch_pop_into_n_b{batch}", n, t0, t1, t2))
    print(
        f"MEDIAN lang=python mode=batch_pop_into_n "
        f"roundtrip_ops={statistics.median(samples):.0f}/s trials={trials}"
    )

    q.close()


if __name__ == "__main__":
    main()
