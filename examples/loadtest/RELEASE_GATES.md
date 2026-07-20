# Binding performance release gates

Do not publish binding updates until these gates pass on a **pinned Linux**
host (native or CI), not only Docker Desktop.

## Benchmark matrix

| Mode | C API | Binding API |
|------|-------|-------------|
| scalar copy | `emq_push` + `emq_pop_into` | `pop_copy` / `PopCopy` / `pop_into` |
| scalar claim | `emq_push` + `emq_claim` | advanced / optional |
| batch | `emq_push_n` + `emq_pop_into_n` | `push_repeat` + `pop_copy_n` |

Defaults: `EMQ_LOAD_N=1000000`, `EMQ_LOAD_PAYLOAD=64`, `EMQ_LOAD_BATCH=32`,
`EMQ_LOAD_TRIALS=5`. Report **median** round-trip ops/s from `MEDIAN` lines.

Also spot-check payloads **1024** and **65536** (copy-bound; expect bindings
within a few percent of C).

## Targets (vs equivalent C median)

| Path | Rust | Java | Python | Go |
|------|------|------|--------|-----|
| Scalar `pop_into` | ≥85% of C | ≥60% of C | ≥60% of C | ≥50% of C |
| Batch 32 | ≥85% of C | ≥85% of C | ≥85% of C | ≥85% of C |

If safe Go scalar remains &lt;50% after `emq_pop_into`, keep scalar as
convenience and document **batch** as the throughput API. Do not ship
`fastcgo`/assembly trampolines for a microbenchmark win.

## Correctness (block publish)

- [ ] `test_pop_into` — empty, undersized buffer (message remains queued),
      meta fill, `push_n`, `pop_into_n` partial batch
- [ ] `test_claim` — claim lifetime / release
- [ ] `test_abi` — new symbols exported
- [ ] Binding smoke: undersized buffer error, timeout/empty, batch partial

## Record

Archive host, compiler/JDK/Python/Go versions, `MEDIAN` lines, and % of C in
`examples/loadtest/results.md` before tagging.
