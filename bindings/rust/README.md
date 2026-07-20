# emq (Rust)

Cargo workspace modeled on [rusqlite](https://github.com/rusqlite/rusqlite) /
`libsqlite3-sys`:

| Crate | Role |
| ----- | ---- |
| [`emq-sys`](emq-sys/) | `extern "C"` + `build.rs` that compiles bundled C via [`cc`](https://crates.io/crates/cc) |
| [`emq`](emq/) | Safe `Runtime`, `Queue`, `Message`, `SpscQueue` |

By default **no system `libemq` is required** — `emq-sys` builds
`bindings/native` sources.

```text
rust/
  Cargo.toml          # workspace
  emq-sys/
    build.rs
    src/lib.rs
  emq/
    src/lib.rs
```

## Build

```bash
# from a clone (run sync if native/ is missing)
python scripts/sync_native.py
cd bindings/rust
cargo build
cargo test
```

```toml
# in your Cargo.toml (path dep until crates.io publish)
emq = { path = "path/to/EmbeddedMQ/bindings/rust/emq" }
```

## Usage

```rust
use emq::{Runtime, SpscQueue};
use std::time::Duration;

let rt = Runtime::new()?;
let q = SpscQueue::new(&rt, "demo", 64)?;
q.push(b"hello")?;
let msg = q.pop(Some(Duration::from_millis(100)))?;
assert_eq!(msg.as_bytes(), b"hello");
```

`Message` drop calls `emq_message_release`. `SpscQueue` is `!Sync` (one pusher, one popper).

## Optional: link a prebuilt libemq

```bash
EMQ_SYSTEM_LIB=1 EMQ_LIB_DIR=../../build cargo build
```
