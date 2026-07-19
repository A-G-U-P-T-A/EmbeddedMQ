# EmbeddedMQ — Embedded Messaging Runtime

[![CI](https://github.com/A-G-U-P-T-A/EmbeddedMQ/actions/workflows/ci.yml/badge.svg)](https://github.com/A-G-U-P-T-A/EmbeddedMQ/actions/workflows/ci.yml)

One engine. Many APIs.

An embeddable, ultra-low-latency messaging runtime that links into your process.
No broker. No daemon. No Docker.

```
Storage          Routing           Concurrency
✓ Durable Queue  ✓ Pub/Sub         ✓ Lock-free SPSC/MPMC rings
✓ Streams        ✓ Fanout (refs)   ✓ Active-queue scheduler
✓ Delay / Work   ✓ Replay / DLQ    ✓ Timing wheel
✓ Memory / Hybrid✓ Consumer groups ✓ Epoch reclamation
✓ Snapshots      ✓ Wildcards       ✓ Futex / WaitOnAddress
```

## Engine v2 highlights

- **Lock-free FAST FIFO** — Aeron-style log-buffer rings; SPSC skips per-queue mutexes
- **Active-queue scheduler** — hierarchical bitmaps + ready rings; workers never scan queues
- **Size-class page pools** — 64B…64KB magazines; malloc is a fallback, not the hot path
- **Policy engine** — ordering × delivery × backpressure × timing × storage
- **Backpressure modes** — drop-new/old, block, spill, expand, overwrite
- **32B record headers (v3)** + 256B inline / pool / blob payload tiers
- **CPU dispatch** — runtime CRC32C / ISA feature table (simdjson-style)
- **NUMA-ready domains** — single domain today; seams for per-node pools/schedulers
- **Event loop** — `emq_poll` / `emq_wait` / `emq_run_once` / `emq_run` for games & embedded
- **Stackless tasks** — protothread-style `EMQ_TASK_*` substrate
- **Observability** — latency histograms, allocator + scheduler stats, CSV baselines

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows (VS Developer shell):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Options: `EMQ_BUILD_TESTS`, `EMQ_BUILD_BENCH`, `EMQ_BUILD_EXAMPLES`,
`EMQ_BUILD_STRESS`, `EMQ_FAULT_INJECT`, `EMQ_SANITIZE`, `EMQ_ASAN`,
`EMQ_ENABLE_SANITIZERS`, `EMQ_WITH_LZ4`, `EMQ_WITH_ZSTD`.

Robustness suites (stress / fuzz / fault / recovery / soak / difftest / model)
and sanitizer / perf-regression scripts: see [docs/TESTING.md](docs/TESTING.md).
Full catalog of every test: [docs/TESTS.md](docs/TESTS.md).

GitHub Actions: `.github/workflows/ci.yml` (push/PR matrix) and `nightly.yml`
(long stress/fuzz/difftest/recovery/soak). Workflows are inert until the repo
is pushed; replace `OWNER` in the badge above with your GitHub org/user.

```powershell
.\scripts\run_all.ps1 -Configure -FaultInject
.\scripts\perf_check.ps1
.\scripts\sanitize.ps1          # WSL Clang ASan+UBSan
```

## Quick start

```c
emq_runtime *rt;
emq_queue *q;
emq_queue_opts opts;
emq_message m;

emq_runtime_create(&rt);
emq_queue_opts_default(&opts);
opts.storage = EMQ_STORAGE_FAST;
opts.producers = 1;
opts.consumers = 1;
emq_queue_create(rt, "jobs", &opts, &q);

emq_push(q, "hello", 5, NULL);
emq_pop(q, &m, 1000);
emq_message_release(&m);
emq_runtime_destroy(rt);
```

## Benchmarks

```bash
./build/bench/emq_bench_load --quick
./build/bench/emq_bench_load --quick --csv out.csv --baseline baseline.csv
./build/bench/emq_bench_compare --ops 100000 --payload 64
```

Reports ops/s, p50/p99/p99.9/p99.99, RSS, CPU, context switches.

### Where we win / don't

| Win | Don't (yet) |
|-----|-------------|
| In-process queues & work steeling | Multi-host networking |
| 1…100k queues in one address space | Exactly-once distributed txns |
| Pub/sub fanout without payload copies | SQL query engine |
| Games / embedded event loops | io_uring / full NUMA binding |

Honest comparisons live in `bench/compare/` (mutex+list baseline today;
SQLite/LMDB harnesses can plug into the same metrics layer).

## Public API

See [`include/emq/emq.h`](include/emq/emq.h).

`emq_pop` / `emq_try_pop` / `emq_pop_batch` / `emq_sub_next` transfer
`message.data` ownership — release with `emq_message_release()`.

## Status

Engine v2 rebuild of the concurrency, memory, scheduler, and measurement planes.
Durable storage, routing, and policies remain fully supported; the FAST path is
the performance spearhead.
