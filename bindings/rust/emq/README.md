# emq

Safe Rust bindings for [EmbeddedMQ](https://github.com/A-G-U-P-T-A/EmbeddedMQ).
Bundles the C engine via [`emq-sys`](https://crates.io/crates/emq-sys) (no system `libemq` required).

```toml
[dependencies]
emq = "1.0.0-beta.2"
```

```rust
use emq::{QueueOpts, Runtime};

fn main() -> emq::Result<()> {
    let rt = Runtime::new()?;
    let q = rt.create_queue(
        "orders",
        Some(QueueOpts {
            capacity: 64,
            producers: 1,
            consumers: 1,
        }),
    )?;
    q.push(b"hello")?;
    let mut buf = [0u8; 64];
    let n = q.pop_into(&mut buf, None)?;
    println!("{}", String::from_utf8_lossy(&buf[..n]));
    Ok(())
}
```

Needs a C toolchain (`cc` crate compiles the vendored engine). Licensed under Apache-2.0.
