# Cross-client queue load test

Apples-to-apples bulk push-all / pop-all against the **local** engine + bindings
(after `scripts/sync_native.py`).

## Modes

| Mode | Meaning |
|------|---------|
| `scalar_pop_into` | Per-message `push` + caller-buffer pop (hot path) |
| `batch_pop_into_n` | `push_n` / `push_repeat` + batch pop (throughput) |
| `scalar_claim` | C only — zero-copy claim baseline |
| `legacy_pop` | C only — owning malloc pop (avoid in bindings) |

## Env

| Variable | Default | Purpose |
|----------|---------|---------|
| `EMQ_LOAD_N` | 100000 | messages |
| `EMQ_LOAD_PAYLOAD` | 64 | bytes |
| `EMQ_LOAD_BATCH` | 32 | batch size |
| `EMQ_LOAD_TRIALS` | 5 | medians |
| `EMQ_LOAD_WARMUP` | 20000 | warmup ops |

## Run

```bash
# C baseline (Docker)
bash examples/loadtest/c/run-docker.sh

# Full matrix — see run-all.sh / run-all.ps1 (use local bindings, not PyPI)
```

Record medians in [results.md](results.md). Latency percentiles:
[latency/results.md](latency/results.md). Gates: [RELEASE_GATES.md](RELEASE_GATES.md).

```bash
# Latency matrix (C → Go → Rust → Python)
bash examples/loadtest/latency/run-matrix.sh
```
