# EmbeddedMQ

[![CI](https://github.com/A-G-U-P-T-A/EmbeddedMQ/actions/workflows/ci.yml/badge.svg)](https://github.com/A-G-U-P-T-A/EmbeddedMQ/actions/workflows/ci.yml)

**In-process messaging for C applications** — queues, pub/sub, and work delivery inside your process. No broker, no daemon, no network hop.

Link the library. Create a runtime. Push and pop messages.

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
emq_pop(q, &m, 1000);          /* owns m.data — must release */
emq_message_release(&m);

emq_queue_close(q);
emq_runtime_destroy(rt);
```

---

## What it is

EmbeddedMQ is a **C11 messaging runtime** you embed in your binary. It replaces “stand up Redis/Rabbit/Kafka for local IPC” when producers and consumers already share an address space.

| You get | You do not get |
| ------- | -------------- |
| Lock-free FAST queues (SPSC / MPMC) | Multi-host clustering |
| Durable / mmap / hybrid storage | Distributed transactions |
| Pub/sub with wildcards & groups | A network protocol |
| Work queues (ack / nack / visibility) | SQL or query language |
| Backpressure, delay, ring, stream policies | Language bindings (C API today) |
| In-process event loop & stackless tasks | A standalone server process |

**Who it’s for:** games, embedded hosts, simulators, local pipelines, and services that need many queues with low latency inside one process.

Deeper product explanation: [docs/overview.md](docs/overview.md).

---

## Requirements

- C11 compiler (MSVC, GCC, or Clang)
- CMake 3.16+
- Optional: Ninja (recommended)

Supported platforms: **Windows**, **Linux**, **macOS**.

---

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows (VS Developer PowerShell / `msvc-dev-cmd`):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Useful CMake options

| Option | Default | Purpose |
| ------ | ------- | ------- |
| `EMQ_BUILD_TESTS` | ON | Unit / integration tests |
| `EMQ_BUILD_EXAMPLES` | ON | `examples/` demos |
| `EMQ_BUILD_BENCH` | ON | Benchmarks |
| `EMQ_BUILD_STRESS` | OFF | Stress / fuzz / soak suites |
| `EMQ_FAULT_INJECT` | OFF | Fault-injection builds |
| `EMQ_SANITIZE` | — | `address`, `undefined`, `thread` (Clang/GCC) |

Artifacts of interest after build:

- `build/libemq.a` (or `.lib`) — static library
- `include/emq/` — public headers
- `build/examples/emq_producer_consumer`
- `build/examples/emq_pubsub_demo`

---

## Use in your project

### CMake

```cmake
add_subdirectory(path/to/EmbeddedMQ)
target_link_libraries(your_app PRIVATE emq)
```

### Manual

```bash
cc -Iinclude your_app.c -Lbuild -lemq -lpthread -o your_app   # Linux/macOS
```

Public API: [`include/emq/emq.h`](include/emq/emq.h) · types: [`include/emq/emq_types.h`](include/emq/emq_types.h).

**Ownership rule:** successful `emq_pop` / `emq_try_pop` / `emq_pop_batch` / `emq_sub_next` transfer `message.data` to you. Always call `emq_message_release(&m)`.

---

## Concepts (short)

1. **Runtime** — process-wide engine (`emq_runtime_create`). Optionally starts worker threads.
2. **Queue** — named endpoint with storage mode, policy, capacity, and backpressure.
3. **Message** — id, payload (`data`/`size`), optional priority / delay / TTL.
4. **Subscription** — pub/sub consumer over a topic pattern (optional consumer group).

| Storage | Meaning |
| ------- | ------- |
| `EMQ_STORAGE_FAST` | RAM, lock-free rings when SPSC-capable |
| `EMQ_STORAGE_DURABLE` | Append log on disk |
| `EMQ_STORAGE_MMAP` | Memory-mapped segments |
| `EMQ_STORAGE_HYBRID` | Hot RAM + durable spill |
| `EMQ_STORAGE_RING` | Fixed-size overwrite ring |
| `EMQ_STORAGE_STREAM` | Offset / replay oriented |

| Policy | Meaning |
| ------ | ------- |
| `FIFO` / `LIFO` | Ordering |
| `PRIORITY` | Higher priority first |
| `WORK` | Visibility timeout + ack/nack |
| `RING` | Bounded overwrite |
| `PUBSUB` / `BROADCAST` | Fanout |
| `DELAY` | Deliver-at scheduling |

Hints `producers=1` and `consumers=1` enable the SPSC FAST path.

---

## Examples

```bash
./build/examples/emq_producer_consumer
./build/examples/emq_pubsub_demo
```

More detail and API walkthrough: [docs/getting-started.md](docs/getting-started.md).

---

## Benchmarks

```bash
./build/bench/emq_bench_load --quick
./build/bench/emq_bench_compare --ops 100000 --payload 64
```

Reports throughput, latency percentiles, and process metrics.

---

## Documentation

| Doc | Contents |
| --- | -------- |
| [docs/overview.md](docs/overview.md) | What EmbeddedMQ is, architecture, fit |
| [docs/getting-started.md](docs/getting-started.md) | Install, first queue, pub/sub, work queues |
| [`include/emq/emq.h`](include/emq/emq.h) | Full C API |

---

## Status

Active C runtime with CI on Windows / Linux / macOS (MSVC, GCC, Clang), plus ASan/UBSan and TSan jobs. The FAST path is the performance focus; durable storage and routing are supported.

Not a network message broker. If you need cross-machine delivery, put a transport beside EmbeddedMQ — don’t expect one inside it.
