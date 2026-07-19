# Where EmbeddedMQ Wins (and Doesn't)

## Win

- **In-process messaging** — no sockets, no broker hop
- **Many queues** — 1…100k queue descriptors in one address space
- **SPSC FAST path** — lock-free log-buffer rings; no per-queue mutex on the hot path
- **Pub/Sub fanout** — payload stored once; subscribers get offsets/refs
- **Games / embedded hosts** — `emq_poll` / `emq_run_once` / stackless tasks
- **Honest metrics** — ops/s + p50/p99/p99.9/p99.99 + RSS/CPU/csw via `emq_bench_load`

## Don't (yet)

- Multi-host networking / clustering
- Exactly-once distributed transactions
- SQL query engine (use SQLite beside us, not instead)
- io_uring, full NUMA binding, language bindings

## Compare harness

```bash
./build/bench/emq_bench_compare --ops 1000000 --payload 64
```

Compares EmbeddedMQ FAST SPSC against a mutex + linked-list baseline in the same process.
Both sides do identical work per op: push (copy payload in), pop (allocate + copy payload
out), release. Sample result (Windows x64, 1M ops, 64 B):

| impl  | ops/s | p50 | p99 | p99.9 |
| ----- | ----: | --: | --: | ----: |
| emq FAST SPSC | ~9.0M | 100 ns | 200 ns | 200 ns |
| mutex+list    | ~9.2M | 100 ns | 100 ns | 200 ns |

An uncontended `CRITICAL_SECTION` is nearly free, so parity here is expected; the ring's
advantage appears under contention, with many queues, and in tail latency — which is what
`emq_bench_load` measures. SQLite/LMDB/Redis comparisons can plug into the same
`bench_metrics` CSV layer.
