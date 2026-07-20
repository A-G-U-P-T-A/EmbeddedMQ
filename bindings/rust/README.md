# Rust bindings

Cargo workspace with two crates:

| Crate | Role |
| ----- | ---- |
| [`emq-sys`](emq-sys/) | Hand-written `extern "C"` + `build.rs` link directives |
| [`emq`](emq/) | Safe `Runtime`, `Queue`, `Message`, `SpscQueue` |

## Prerequisites

Build the C library first (static `libemq`):

```bash
cmake -S core -B build -DEMQ_BUILD_TESTS=OFF
cmake --build build
```

## Build

From this directory:

```bash
# defaults: include=../../core/include, lib=../../build
cargo build

# override paths
EMQ_INCLUDE_DIR=/path/to/core/include EMQ_LIB_DIR=/path/to/build cargo build
```

Linking requires `libemq.a` (or `emq.lib` on Windows) in `EMQ_LIB_DIR`. Compilation succeeds without the library; linking fails until the core is built.

## Usage sketch

```rust
use emq::{Runtime, SpscQueue};
use std::time::Duration;

let rt = Runtime::new()?;
let q = SpscQueue::new(&rt, "demo", 64)?;
q.push(b"hello")?;
let msg = q.pop(Some(Duration::from_millis(100)))?;
assert_eq!(msg.as_bytes(), b"hello");
// Message dropped here calls emq_message_release
```

## SPSC

`SpscQueue` is intentionally `!Sync`. Only one thread may push and one may pop, matching the C SPSC contract documented in `emq.h`.

## Conformance

Replay scenarios from [`../conformance/`](../conformance/) against this wrapper (runner TBD).
