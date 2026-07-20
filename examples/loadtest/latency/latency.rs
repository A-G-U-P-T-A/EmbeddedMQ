use emq::{QueueOpts, Runtime};
use std::env;
use std::time::Instant;

fn env_usize(key: &str, def: usize) -> usize {
    env::var(key)
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(def)
}

fn pct(sorted: &[u64], p: f64) -> u64 {
    if sorted.is_empty() {
        return 0;
    }
    let idx = (p * (sorted.len() - 1) as f64 + 0.5) as usize;
    sorted[idx.min(sorted.len() - 1)]
}

fn main() -> emq::Result<()> {
    let n = env_usize("EMQ_LOAD_N", 1_000_000);
    let payload_len = env_usize("EMQ_LOAD_PAYLOAD", 64);
    let warmup = env_usize("EMQ_LOAD_WARMUP", 50_000);
    let payload = vec![0xABu8; payload_len];
    let mut dst = vec![0u8; payload_len];
    let mut lat = vec![0u64; n];

    let rt = Runtime::new()?;
    let q = rt.create_queue(
        "lat-rs",
        Some(QueueOpts {
            capacity: 4096,
            producers: 1,
            consumers: 1,
        }),
    )?;

    for _ in 0..warmup {
        q.push(&payload)?;
        let _ = q.pop_into(&mut dst, None)?;
    }

    for i in 0..n {
        let t0 = Instant::now();
        q.push(&payload)?;
        let _ = q.pop_into(&mut dst, None)?;
        lat[i] = t0.elapsed().as_nanos() as u64;
    }

    lat.sort_unstable();
    println!(
        "LATENCY lang=rust payload={payload_len} n={n} p50_ns={} p99_ns={} p999_ns={} p9999_ns={}",
        pct(&lat, 0.50),
        pct(&lat, 0.99),
        pct(&lat, 0.999),
        pct(&lat, 0.9999)
    );
    Ok(())
}
