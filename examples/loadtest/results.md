# Load test results

Workload: **100,000** messages × **64-byte** payload, single producer/consumer,
push all then pop all. Host: Docker on Windows (Linux containers).

## Current (local `1.0.0-beta.4-SNAPSHOT` Java + published others)

| Client | Source | Round-trip | Notes |
|--------|--------|------------|-------|
| **Rust** | crates.io `emq 1.0.0-beta.1` | **13.2M ops/s** | reference |
| **Java (fast path)** | local SNAPSHOT | **5.0–7.2M ops/s** | `pushNative` + `popCopy` |
| **Python** | PyPI `1.0.0b1` | **3.3M ops/s** | |
| **Go** | monorepo cgo | **2.4M ops/s** | |
| **Java (legacy API)** | local SNAPSHOT | **~0.85M ops/s** | `Message` + `data()` — avoid in hot loops |

### Java bottleneck breakdown (`bindings/java` `QueueLoadTest`)

| Mode | Round-trip | What it measures |
|------|------------|------------------|
| `legacy_Message+data()` | ~0.85M/s | Old Central-style API (alloc + copy) |
| `push+popCopy` | ~3.0M/s | byte[] push with scratch reuse |
| `pushNative+popCopy` | **~7.2M/s** | stable native payload + popCopy |

Published Central **`1.0.0-beta.3`** matched the legacy path (~0.45M/s). That was
**client overhead**, not the C engine. Do not republish until the fast path is
what docs/examples use.

## Earlier published-client run (pre-fix Java)

```text
RESULT lang=rust   roundtrip_ops=13152855/s
RESULT lang=python roundtrip_ops=3262559/s
RESULT lang=go     roundtrip_ops=2361747/s
RESULT lang=java   roundtrip_ops=445153/s   # beta.3 broken hot path
```
