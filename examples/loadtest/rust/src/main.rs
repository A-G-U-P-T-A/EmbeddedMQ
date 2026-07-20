use emq::{QueueOpts, Runtime};
use std::env;
use std::time::{Duration, Instant};

fn main() -> emq::Result<()> {
    let n: usize = env::var("EMQ_LOAD_N")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(100_000);
    let payload_len: usize = env::var("EMQ_LOAD_PAYLOAD")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(64);
    let capacity = (n + 16).max(1024) as u32;
    let payload: Vec<u8> = (0..payload_len).map(|i| (i % 256) as u8).collect();

    println!("client=rust n={n} payload={payload_len} capacity={capacity}");
    let rt = Runtime::new()?;
    let q = rt.create_queue(
        "loadtest-rs",
        Some(QueueOpts {
            capacity,
            producers: 1,
            consumers: 1,
        }),
    )?;

    let t0 = Instant::now();
    for _ in 0..n {
        q.push(&payload)?;
    }
    let t1 = Instant::now();

    for _ in 0..n {
        let msg = q.pop(Some(Duration::from_millis(1000)))?;
        let _ = msg.as_bytes();
    }
    let t2 = Instant::now();

    let push_s = (t1 - t0).as_secs_f64();
    let pop_s = (t2 - t1).as_secs_f64();
    let total_s = (t2 - t0).as_secs_f64();
    println!(
        "RESULT lang=rust push_ops={:.0}/s pop_ops={:.0}/s roundtrip_ops={:.0}/s push_ms={:.1} pop_ms={:.1} total_ms={:.1}",
        n as f64 / push_s,
        n as f64 / pop_s,
        n as f64 / total_s,
        push_s * 1000.0,
        pop_s * 1000.0,
        total_s * 1000.0
    );
    Ok(())
}
