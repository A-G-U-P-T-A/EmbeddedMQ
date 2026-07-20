# Changelog

## 1.0.7-beta — 2026-07-20

### Core

- `emq_pop_into` / `emq_push_n` / `emq_pop_into_n` — caller-buffer and batch hot paths (no engine malloc on pop)
- ABI tests for the new symbols; `test_pop_into` coverage

### Bindings

- Rust / Python / Go / Java map to the new APIs (`pop_into` / `PopCopy` / `pop_copy`, batch helpers)
- Prefer caller-buffer + batch over owning `Message` + per-pop alloc/copy
- Apache-2.0 license affirmed across the monorepo and published packages
- Versions: Python `1.0.0b2`, Rust `emq`/`emq-sys` `1.0.0-beta.2`, Java `1.0.0-beta.4`, Go `bindings/go/v1.0.0-beta.3`

### Benchmarks

- Cross-client throughput + latency matrices under `examples/loadtest/`
- Documented in root README and `examples/loadtest/results.md`

## 1.0.0-beta — 2026-07-20

First public beta of the EmbeddedMQ monorepo.

### Included

- **C core** (`core/`) — in-process queues, pub/sub routing, FAST/DURABLE paths,
  claim API, ABI export surface (`emq.h`)
- **Language clients** under `bindings/`:
  - Rust (`emq-sys` + `emq`)
  - Python (`embeddedmq` C-extension)
  - Go (cgo)
  - Java (Panama FFM)
- Conformance scenario corpus (`bindings/conformance/`)
- Transport prototypes (`transport/ipc`, `transport/net`)
- Project site (`site/`) and CI (Windows / Linux / macOS + ASan/TSan)
