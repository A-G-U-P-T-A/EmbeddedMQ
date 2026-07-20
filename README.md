# EmbeddedMQ

[![CI](https://github.com/A-G-U-P-T-A/EmbeddedMQ/actions/workflows/ci.yml/badge.svg)](https://github.com/A-G-U-P-T-A/EmbeddedMQ/actions/workflows/ci.yml)
[![Pages](https://github.com/A-G-U-P-T-A/EmbeddedMQ/actions/workflows/pages.yml/badge.svg)](https://github.com/A-G-U-P-T-A/EmbeddedMQ/actions/workflows/pages.yml)
[![Website](https://img.shields.io/badge/website-a--g--u--p--t--a.github.io%2FEmbeddedMQ-1f6f5b)](https://a-g-u-p-t-a.github.io/EmbeddedMQ/)

**Website:** [a-g-u-p-t-a.github.io/EmbeddedMQ](https://a-g-u-p-t-a.github.io/EmbeddedMQ/)

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

## Repository layout

```
core/           C11 engine (library, tests, benches, stress)
bindings/       Language clients (Rust, Python, Go, Java)
transport/      IPC + network media-driver
site/           Project website (GitHub Pages)
docs/           User docs
scripts/        Local orchestration
```

---

## What it is

EmbeddedMQ is a **C11 messaging runtime** you embed in your binary. It replaces “stand up Redis/Rabbit/Kafka for local IPC” when producers and consumers already share an address space.

| You get | You do not get |
| ------- | -------------- |
| Lock-free FAST queues (SPSC / MPMC) | Multi-host clustering (see `transport/`) |
| Durable / mmap / hybrid storage | Distributed transactions |
| Pub/sub with wildcards & groups | SQL or query language |
| Work queues (ack / nack / visibility) | A standalone broker process |
| Backpressure, delay, ring, stream policies | — |
| In-process event loop & stackless tasks | — |

**Who it’s for:** games, embedded hosts, simulators, local pipelines, and services that need many queues with low latency inside one process.

Deeper product explanation: [docs/overview.md](docs/overview.md) · landing page: [site/](site/).

---

## Requirements

- C11 compiler (MSVC, GCC, or Clang)
- CMake 3.16+
- Optional: Ninja (recommended)

Supported platforms: **Windows**, **Linux**, **macOS**.

---

## Build

Prefer building the engine directly (keeps binary paths flat under `build/`):

```bash
cmake -S core -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows (VS Developer PowerShell / `msvc-dev-cmd`):

```powershell
cmake -S core -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Or from the monorepo root wrapper: `cmake -S . -B build` (outputs land under `build/core/`).

### Useful CMake options

| Option | Default | Purpose |
| ------ | ------- | ------- |
| `EMQ_BUILD_TESTS` | ON | Unit / integration tests |
| `EMQ_BUILD_EXAMPLES` | ON | `core/examples/` demos |
| `EMQ_BUILD_BENCH` | ON | Benchmarks |
| `EMQ_BUILD_STRESS` | ON | Stress / fuzz / soak suites |
| `EMQ_BUILD_SHARED` | OFF | Also build `libemq` shared library |
| `EMQ_BUILD_TRANSPORT` | OFF | Shared-memory IPC + UDP media driver |
| `EMQ_FAULT_INJECT` | OFF | Fault-injection builds |
| `EMQ_SANITIZE` | — | `address`, `undefined`, `thread` (Clang/GCC) |

Artifacts of interest after `cmake -S core -B build`:

- `build/libemq.a` (or `.lib`) — static library
- `core/include/emq/` — public headers
- `build/examples/emq_producer_consumer`
- `build/examples/emq_pubsub_demo`

---

## Use in your project

### CMake

```cmake
add_subdirectory(path/to/EmbeddedMQ/core)
target_link_libraries(your_app PRIVATE emq)
```

### Manual

```bash
cc -Icore/include your_app.c -Lbuild -lemq -lpthread -o your_app   # Linux/macOS
```

Public API: [`core/include/emq/emq.h`](core/include/emq/emq.h) · types: [`core/include/emq/emq_types.h`](core/include/emq/emq_types.h).

**Ownership rule:** successful `emq_pop` / `emq_try_pop` / `emq_pop_batch` / `emq_sub_next` transfer `message.data` to you. Always call `emq_message_release(&m)`. Zero-copy FAST path: `emq_claim` / `emq_release_claim`.

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

Hints `producers=1` and `consumers=1` enable the SPSC FAST path.

---

## Examples

```bash
./build/examples/emq_producer_consumer
./build/examples/emq_pubsub_demo
```

More detail: [docs/getting-started.md](docs/getting-started.md).

---

## Bindings

Language clients live under [`bindings/`](bindings/). See each subdirectory for build instructions. The stable C ABI is the contract every binding uses.

---

## Website

The project site is a static page in [`site/`](site/), deployed by [`.github/workflows/pages.yml`](.github/workflows/pages.yml) to GitHub Pages:

**https://a-g-u-p-t-a.github.io/EmbeddedMQ/**

Preview locally by opening `site/index.html` or serving the folder:

```bash
python -m http.server -d site 8080
```

---

## Documentation

| Doc | Contents |
| --- | -------- |
| [Website](https://a-g-u-p-t-a.github.io/EmbeddedMQ/) | Landing page |
| [docs/overview.md](docs/overview.md) | What EmbeddedMQ is, architecture, fit |
| [docs/getting-started.md](docs/getting-started.md) | Install, first queue, pub/sub, work queues |
| [`core/include/emq/emq.h`](core/include/emq/emq.h) | Full C API |

---

## Status

Active C runtime with CI on Windows / Linux / macOS (MSVC, GCC, Clang), plus ASan/UBSan and TSan jobs. Monorepo layout hosts the engine in `core/` with bindings, transport, and the GitHub Pages site alongside.
