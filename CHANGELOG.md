# Changelog

## 1.0.0-beta — 2026-07-20

First public beta of the EmbeddedMQ monorepo.

### Included

- **C core** (`core/`) — in-process queues, pub/sub routing, FAST/DURABLE paths,
  claim API, ABI export surface (`emq.h`)
- **Language client scaffolds** under `bindings/`:
  - Rust (`emq-sys` + `emq`)
  - Python (`embeddedmq` C-extension)
  - Go (cgo)
  - Java (Panama FFM)
- Conformance scenario corpus (`bindings/conformance/`)
- Transport prototypes (`transport/ipc`, `transport/net`)
- Project site (`site/`) and CI (Windows / Linux / macOS + ASan/TSan)

### Client install reality (important)

Bindings in this beta still **link a separately built `libemq`**. They do
**not** yet vendor/compile the engine the way SQLite amalgamation +
`rusqlite` `bundled` does. See [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md).

```bash
cmake -S core -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# then follow bindings/*/README.md (EMQ_LIB_DIR / EMQ_INCLUDE_DIR)
```

### Planned next (SQLite-style distribution)

- Amalgamation (`emq.c`) + compile-from-source in each binding
- Python wheels (cibuildwheel) with native code inside the wheel
- crates.io / Go module that build without a prior cmake step
