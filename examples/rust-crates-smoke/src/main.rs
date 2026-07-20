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
    q.push(b"hello-crates")?;
    let msg = q.pop(None)?;
    let got = msg.as_bytes();
    assert_eq!(got, b"hello-crates");
    println!("rust ok: {}", String::from_utf8_lossy(got));
    Ok(())
}
