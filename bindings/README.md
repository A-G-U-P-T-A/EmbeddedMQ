# Language bindings

**Version:** `1.0.7-beta` · License: **Apache-2.0** · Contract: C ABI in `core/include/emq/emq.h`

Bindings follow the usual packaging patterns for each ecosystem
([go-sqlite3](https://github.com/mattn/go-sqlite3), [rusqlite](https://github.com/rusqlite/rusqlite),
[sqlite-jdbc](https://github.com/xerial/sqlite-jdbc), Python wheels):

| Directory | Registry target | How the engine is obtained |
| --------- | --------------- | -------------------------- |
| [`native/`](native/) | (shared) | Vendored C sources from `core/` via `scripts/sync_native.py` |
| [`rust/`](rust/) | crates.io | `emq-sys` compiles bundled sources (`cc`, like rusqlite `bundled`) |
| [`python/`](python/) | PyPI | Extension compiles vendored `native/` into the wheel/module |
| [`go/`](go/) | Go module proxy | cgo compiles per-file wrappers (like go-sqlite3 shipping C) |
| [`java/`](java/) | Maven Central | Fat JAR + `NativeLoader` extracts OS/arch `.dll`/`.so`/`.dylib` |
| [`conformance/`](conformance/) | — | Shared JSON scenario corpus |

After changing `core/`, refresh the vendored tree:

```bash
python scripts/sync_native.py
```

## Quick start (no separate cmake)

```bash
# Python
pip install ./bindings/python

# Rust
cd bindings/rust && cargo build

# Go (needs a C toolchain + cgo)
cd bindings/go && go build ./...

# Java — needs prebuilt natives in src/main/resources/native/<os>/<arch>/
# (produced by .github/workflows/release-bindings.yml) or -Demq.lib.path=
cd bindings/java && mvn -q package
```

Escape hatch for a system `libemq`: set `EMQ_LIB_DIR` / `EMQ_SYSTEM_LIB=1` /
`-tags emq_system` / `-Demq.lib.path=` (see each binding README).

## Layout conventions

```text
bindings/
  native/                 # single source of truth for packaged C
  rust/                   # Cargo workspace (emq-sys + emq)
  python/                 # src/embeddedmq + vendored native/
  go/                     # module root; zz_*.c wrappers + emq.go
  java/                   # Maven src/main/java + resources/native/
  conformance/            # cross-language scenario corpus
```
