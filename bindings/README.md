# Language bindings

Stable contract: the C ABI exported by `libemq` (`core/include/emq/emq.h`).

| Directory | Status | Technology | Notes |
| --------- | ------ | ---------- | ----- |
| [`conformance/`](conformance/) | corpus | JSON + Python validator | Shared scenario replay contract for all bindings |
| [`rust/`](rust/) | scaffold | `emq-sys` + safe `emq` crate | Hand-written FFI, `EMQ_LIB_DIR` / `EMQ_INCLUDE_DIR` |
| [`python/`](python/) | scaffold | CPython C-extension | `embeddedmq` package, `EMQ_ROOT` / `EMQ_LIB_DIR` |
| [`go/`](go/) | scaffold | cgo | `EMQ_INCLUDE` / `EMQ_LIB` env vars |
| [`java/`](java/) | scaffold | Panama FFM (JDK 21+) | Not JNI; `SymbolLookup.libraryLookup` |

## Quick start

1. Build the C core:

   ```bash
   cmake -S core -B build -DEMQ_BUILD_TESTS=OFF
   cmake --build build
   ```

2. Pick a binding and follow its README (link paths differ per toolchain).

3. Validate shared conformance scenarios:

   ```bash
   python bindings/conformance/validate_corpus.py bindings/conformance/scenarios/*.json
   ```

Each binding wraps the same surface: runtime lifecycle, queue create/push/pop, and `emq_message_release` ownership. Conformance runners that replay `conformance/scenarios/*.json` are future work; the corpus defines the shared contract today.
