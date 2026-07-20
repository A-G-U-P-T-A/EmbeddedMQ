# Load test results

Workload: **100,000** messages × **64-byte** payload, single producer/consumer,
push all then pop all. Host: Docker on Windows (Linux containers).

| Client | Source | Push ops/s | Pop ops/s | Round-trip ops/s | Total |
|--------|--------|------------|-----------|------------------|-------|
| **Rust** | crates.io `emq 1.0.0-beta.1` | 53.5M | 17.4M | **13.2M** | 7.6 ms |
| **Python** | PyPI `embeddedmq 1.0.0b1` | 8.9M | 5.1M | **3.3M** | 30.7 ms |
| **Go** | monorepo (cgo) | 10.9M | 3.0M | **2.4M** | 42.3 ms |
| **Java** | Maven `1.0.0-beta.3` | 0.83M | 0.96M | **0.45M** | 224.6 ms |

Raw lines:

```text
RESULT lang=rust   push_ops=53474363/s pop_ops=17443310/s roundtrip_ops=13152855/s
RESULT lang=python push_ops=8923770/s  pop_ops=5142774/s  roundtrip_ops=3262559/s
RESULT lang=go     push_ops=10856189/s pop_ops=3018394/s  roundtrip_ops=2361747/s
RESULT lang=java   push_ops=829466/s   pop_ops=960779/s   roundtrip_ops=445153/s
```

Notes:

- Numbers are single-run, same container class, not a lab benchmark.
- Java pays Panama FFM + per-call `byte[]` copy overhead.
- Go remote `go get` still needs `bindings/native` vendored into the module;
  this run used a monorepo `replace`.
