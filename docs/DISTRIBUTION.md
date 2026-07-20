# Client distribution

Goal: users install **one package** per language and never clone this repo or
run cmake for the common case.

## Patterns we follow

| Language | Reference project | Strategy in EmbeddedMQ |
| -------- | ----------------- | ---------------------- |
| Go | [mattn/go-sqlite3](https://github.com/mattn/go-sqlite3) | cgo compiles vendored C (`bindings/go/zz_*.c` → `bindings/native`) |
| Rust | [rusqlite](https://github.com/rusqlite/rusqlite) `bundled` | `emq-sys/build.rs` + `cc` compiles `bindings/native` |
| Python | wheels / src extensions | `setup.py` compiles vendored `bindings/python/native` |
| Java | [xerial/sqlite-jdbc](https://github.com/xerial/sqlite-jdbc) | Fat JAR + runtime extract/load of OS/arch natives |

The `.db` / queue-storage file that appears when an app opens a path is **API
behavior**, not packaging. Same for `emq` durable paths.

## Monorepo layout

```text
core/                     # canonical engine
bindings/native/          # synced copy for packaging (scripts/sync_native.py)
bindings/{rust,python,go,java}/
.github/workflows/release-bindings.yml
```

Keep `bindings/native` in sync after engine changes:

```bash
python scripts/sync_native.py
python scripts/sync_native.py --check   # CI
```

## Registries (publish path)

| Package | Registry | Workflow |
| ------- | -------- | -------- |
| C tarball + natives | GitHub Releases | `release-bindings.yml` on tag `v*` |
| `io.embeddedmq:embeddedmq` | Maven Central | JAR from release job; `-Pcentral` needs Sonatype + GPG |
| `embeddedmq` | PyPI | sdist now; cibuildwheel wheels next |
| `emq` / `emq-sys` | crates.io | `cargo publish` after vendoring native into the crate |
| Go module | proxy.golang.org | git tags on this repo (`…/bindings/go`) |

Maven Central one-time setup (account + namespace verification + GPG) is
documented at [central.sonatype.com](https://central.sonatype.com).

## Escape hatches

Advanced users can still link a system/build-tree `libemq` via
`EMQ_LIB_DIR` / `EMQ_SYSTEM_LIB` / `-tags emq_system` / `-Demq.lib.path`.
