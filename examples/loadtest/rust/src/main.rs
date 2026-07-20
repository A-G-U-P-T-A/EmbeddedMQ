use emq::{QueueOpts, Runtime};
use std::env;
use std::time::{Duration, Instant};

fn env_usize(key: &str, def: usize) -> usize {
    env::var(key)
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(def)
}

fn report(mode: &str, n: usize, t0: Instant, t1: Instant, t2: Instant) {
    let push_s = (t1 - t0).as_secs_f64();
    let pop_s = (t2 - t1).as_secs_f64();
    let total_s = (t2 - t0).as_secs_f64();
    println!(
        "RESULT lang=rust mode={mode} push_ops={:.0}/s pop_ops={:.0}/s roundtrip_ops={:.0}/s push_ms={:.1} pop_ms={:.1} total_ms={:.1}",
        n as f64 / push_s,
        n as f64 / pop_s,
        n as f64 / total_s,
        push_s * 1000.0,
        pop_s * 1000.0,
        total_s * 1000.0
    );
}

fn median(samples: &mut [f64]) -> f64 {
    samples.sort_by(|a, b| a.partial_cmp(b).unwrap());
    samples[samples.len() / 2]
}

fn main() -> emq::Result<()> {
    let n = env_usize("EMQ_LOAD_N", 100_000);
    let payload_len = env_usize("EMQ_LOAD_PAYLOAD", 64);
    let warmup = env_usize("EMQ_LOAD_WARMUP", 20_000);
    let batch = env_usize("EMQ_LOAD_BATCH", 32);
    let trials = env_usize("EMQ_LOAD_TRIALS", 5).max(1);
    let capacity = (n + 16).max(1024) as u32;
    let payload: Vec<u8> = (0..payload_len).map(|i| (i % 256) as u8).collect();
    let mut dst = vec![0u8; payload_len];
    let mut batch_dst = vec![0u8; payload_len * batch];

    println!("client=rust n={n} payload={payload_len} capacity={capacity} batch={batch} trials={trials}");
    let rt = Runtime::new()?;
    let q = rt.create_queue(
        "loadtest-rs",
        Some(QueueOpts {
            capacity,
            producers: 1,
            consumers: 1,
        }),
    )?;

    for _ in 0..warmup {
        q.push(&payload)?;
    }
    for _ in 0..warmup {
        let _ = q.pop_into(&mut dst, Some(Duration::from_millis(1000)))?;
    }

    let mut samples = vec![0.0f64; trials];

    // scalar_pop_into
    for s in samples.iter_mut() {
        let t0 = Instant::now();
        for _ in 0..n {
            q.push(&payload)?;
        }
        let t1 = Instant::now();
        for _ in 0..n {
            let got = q.pop_into(&mut dst, Some(Duration::from_millis(1000)))?;
            if got != payload_len {
                panic!("bad len {got}");
            }
        }
        let t2 = Instant::now();
        report("scalar_pop_into", n, t0, t1, t2);
        *s = n as f64 / (t2 - t0).as_secs_f64();
    }
    println!(
        "MEDIAN lang=rust mode=scalar_pop_into roundtrip_ops={:.0}/s trials={trials}",
        median(&mut samples)
    );

    // batch
    for s in samples.iter_mut() {
        let t0 = Instant::now();
        let mut left = n;
        while left > 0 {
            let chunk = batch.min(left);
            q.push_n(&payload, chunk)?;
            left -= chunk;
        }
        let t1 = Instant::now();
        left = n;
        while left > 0 {
            let chunk = batch.min(left);
            let got = q.pop_into_n(&mut batch_dst, payload_len, chunk, Some(Duration::from_millis(1000)))?;
            if got != chunk {
                panic!("bad batch {got}");
            }
            left -= got;
        }
        let t2 = Instant::now();
        report("batch_pop_into_n", n, t0, t1, t2);
        *s = n as f64 / (t2 - t0).as_secs_f64();
    }
    println!(
        "MEDIAN lang=rust mode=batch_pop_into_n roundtrip_ops={:.0}/s trials={trials}",
        median(&mut samples)
    );

    Ok(())
}
