# How clients get the native engine (SQLite model vs EmbeddedMQ)

## What SQLite actually does

SQLite does **not** magically inject a prebuilt `.dll` / `.so` into every language
ecosystem. The usual path is **source bundling**:

1. **Amalgamation** — the whole engine is concatenated into `sqlite3.c` +
   `sqlite3.h` ([SQLite amalgamation](https://www.sqlite.org/amalgamation.html)).
2. Language packages **vendor that C source** (or download it at build time) and
   **compile it as part of installing the package**:
   - Rust `rusqlite` / `libsqlite3-sys`: optional `bundled` feature compiles
     embedded `sqlite3.c` with the `cc` crate.
   - Python, Go, Node, etc.: same idea — compile amalgamation into the
     extension / cgo archive when you `pip install` / `go build`.
3. Separately, some ecosystems also ship **prebuilt binaries** (NuGet native
   assets, Android AARs, Python *wheels* with a `.so` already inside). That is
   a packaging choice on top of the amalgamation story, not the default SQLite
   distribution itself.

So “SQLite just shows up in the client” usually means: **the C source traveled
with the package and was compiled on your machine (or in CI into a wheel).**

## EmbeddedMQ today (1.0.0-beta)

| Piece | State |
| ----- | ----- |
| C core | Multi-file library under `core/` (no amalgamation yet) |
| Bindings | Scaffolds that **link a prebuilt `libemq`** via env vars |
| Wheels / crates.io / Maven Central | Not publishing yet |
| GitHub Release | Source + binding trees; build core yourself |

```text
You today:
  cmake -S core -B build && cmake --build build
  export EMQ_LIB_DIR=$PWD/build   # then pip install / cargo build / go test
```

## Can we do the SQLite thing here?

**Yes.** Two complementary tracks:

### A. Bundled source (SQLite-like — preferred default)

1. Add an **amalgamation** (or CMake `OBJECT` / unity build) → `emq.c` + public
   headers, regenerated each release.
2. Teach each binding to compile that source when no system lib is set:
   - Python: list amalgamation in `Extension(sources=[...])`
   - Rust: `cc::Build` in `emq-sys/build.rs` (`bundled` feature)
   - Go: `#cgo` compile `emq.c` from the module tree
   - Java: still needs a **shared** library for FFM; build amalgamation to
     `.dll`/`.so` in the package or load from a classifier JAR

Result: `pip install` / `cargo add` works with **no prior cmake**, same UX as
`rusqlite` with `bundled`.

### B. Prebuilt natives in the package (wheel / JAR)

CI builds `libemq` (or the extension) per platform and uploads:

- Python **wheels** via cibuildwheel (binary already inside the wheel)
- Java **native-classifier JARs**
- Optional: release assets `libemq-linux-x64.so`, `emq.dll`, …

This is how users get a binary without a local C compiler. SQLite ecosystems
use this *in addition* to amalgamation, not instead of it.

## Recommendation for EmbeddedMQ

1. **Ship amalgamation + bundled-source builds** in the bindings (SQLite path).
2. **Add wheel CI** so Python users get a ready binary.
3. Keep GitHub Release assets as the C tarball + checksums for embedders who
   compile into their own apps (again: SQLite’s download-page model).

Until A/B land, a GitHub tag can still mark the API/clients snapshot; installing
a binding still requires building `libemq` first.
