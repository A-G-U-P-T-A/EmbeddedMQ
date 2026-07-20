# Load test results

## Methodology

- Host: Docker Linux on Windows (gcc/python/go/rust/maven images)
- Throughput: bulk push-all → pop-all, SPSC FAST FIFO, **100k × 64B**, batch=32
- Medians over **3 trials** (`EMQ_LOAD_TRIALS=3`)
- Latency: see [latency/results.md](latency/results.md)
- Gates: [RELEASE_GATES.md](RELEASE_GATES.md)

## C baselines (throughput)

| Mode | Median ops/s |
|------|-------------:|
| `scalar_pop_into` | **18.79M** |
| `scalar_claim` | 17.47M |
| `batch_pop_into_n` | **23.95M** |
| `legacy_pop` (malloc) | 14.24M |

## Scoreboard vs C (throughput)

| Client | Mode | Median | % of C | Degradation |
|--------|------|-------:|-------:|------------:|
| **C** | scalar_pop_into | 18.79M | 100% | 0% |
| **Rust** | scalar_pop_into | 15.65M | **83%** | 17% |
| **Java** | scalar_pop_into | 7.58M | **40%** | 60% |
| **Python** | scalar_pop_into | 5.66M | **30%** | 70% |
| **Go** | scalar_pop_into | 4.06M | **22%** | 78% |
| **C** | batch_pop_into_n | 23.95M | 100% | 0% |
| **Rust** | batch_pop_into_n | 22.41M | **94%** | 6% |
| **Python** | batch_pop_into_n | 18.10M | **76%** | 24% |
| **Go** | batch_pop_into_n | 15.62M | **65%** | 35% |
| **Java** | batch_pop_into_n | 12.34M | **52%** | 48% |

## Latency (p50, ns) — push + pop_into

| Payload | Messages | C | Rust | Python | Go |
| ------- | -------- | -: | ---: | -----: | -: |
| 64 B | 10M | 55 | 61 | 176 | 240 |
| 256 B | 10M | 57 | 69 | 175 | 237 |
| 1 KB | 10M | 71 | 84 | 199 | 256 |
| 4 KB | 5M | 143 | 156 | 279 | 328 |
| 16 KB | 1M | 506 | 511 | 716 | 726 |

Full percentiles: [latency/results.md](latency/results.md).

## Correctness

- `test_abi`, `test_claim`, `test_pop_into` — **passed**

## Publish decision (v1.0.7-beta)

Maintainer-accepted for beta publish with documented binding tax:

- Prefer **batch** APIs for Go/Python/Java throughput; scalar `pop_into` for latency-sensitive paths.
- Rust is near-C on both scalar and batch.
- Docker Desktop numbers are noisy; re-baseline on native Linux CI when tightening gates.
