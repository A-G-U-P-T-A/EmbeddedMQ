# EmbeddedMQ Testing Guide

Full per-test catalog (unit, stress, fuzz, fault, recovery, soak, difftest, model):
**[TESTS.md](TESTS.md)**.

## What we prove

| Class | Status | How |
| ----- | ------ | --- |
| Unit / integration | Excellent | `tests/` via `ctest` |
| ABI / compat | Present | `test_abi`, `test_compat` + `fixtures/log_v1` |
| Microbenchmarks | Good | `bench/emq_bench_*` |
| Load | Good | `emq_bench_load` matrix |
| Stress | Good | `stress/` (quick in ctest, long via scripts) |
| Differential | Present | `difftest/diff_runner` vs golden model |
| Model checking | Present | `model/model_workqueue` BFS |
| Soak | Present | `soak/soak_runner` + `scripts/soak.ps1` |
| Crash recovery | Present | `recovery_supervisor` + `--crashpoint` matrix |
| Fault injection | Present | `fault/*` (needs `-DEMQ_FAULT_INJECT=ON`) |
| Fuzz | Present | `fuzz/fuzz_ops`, `fuzz/fuzz_log` (+ libFuzzer hook) |
| Sanitizers | Present | `scripts/sanitize.ps1` (WSL Clang) + GHA |
| Perf regression | Present | `scripts/perf_check.ps1` (hard gate local) |
| CI | Present | `.github/workflows/ci.yml` + `nightly.yml` |

## Quick pass (minutes)

```powershell
.\scripts\run_all.ps1 -Configure
# with fault injection:
.\scripts\run_all.ps1 -Configure -FaultInject
```

Or manually:

```powershell
cmake -B build -DEMQ_BUILD_STRESS=ON -DEMQ_FAULT_INJECT=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CTest labels: `stress`, `fuzz`, `fault`, `recovery`, `soak`, `difftest`, `model`.

## Long runs

```powershell
.\scripts\stress_long.ps1 -Suite spsc -Ops 100000000
.\scripts\soak.ps1 -Hours 24 -Csv soak24.csv
.\scripts\perf_check.ps1                 # fail on >10% thr↓ or p99↑
.\scripts\perf_check.ps1 -Update         # refresh baseline
.\scripts\sanitize.ps1                   # ASan+UBSan under WSL
.\scripts\sanitize.ps1 -Mode thread      # TSan
.\scripts\sanitize.ps1 -Mode valgrind
.\build\difftest\diff_runner.exe --ops 5000000
.\build\recovery\recovery_supervisor.exe --crashpoint
```

## Suite map

```
tests/          unit + integration + abi + compat
bench/          latency, load, compare, fanout, sched
stress/         spsc, mpmc, churn, compact, replay
fuzz/           op-sequence + log parser
fault/          alloc, disk, corrupt
recovery/       writer child + kill / crash-point supervisor
difftest/       golden policy model vs real queue
model/          explicit-state work-queue checker
soak/           steady mixed workload + RSS samples
testsupport/    RNG, payloads, watchdog, CLI, proc metrics
scripts/        orchestration
.github/        ci.yml (push/PR) + nightly.yml (cron)
```

## CI / Nightly

Workflows live under `.github/workflows/` and activate once the repo is on GitHub:

- **`ci.yml`** — matrix: Windows MSVC, Ubuntu gcc/clang, macOS AppleClang; full quick CTest with stress + fault inject. Extra Ubuntu clang jobs: ASan+UBSan (full ctest) and TSan (`-L "stress|fuzz|difftest|model"`). Bench is report-only (runners too noisy for the hard perf gate).
- **`nightly.yml`** — long `stress_spsc` / `stress_mpmc` / `fuzz_ops` / `diff_runner` / recovery cycles + crash-point matrix / 1h soak with CSV artifact. Weekly Sunday cron scales ops ~10×.

Until a remote exists, validate Linux legs locally via WSL (`scripts/sanitize.ps1` or a plain gcc/clang build). macOS RSS sampling uses `task_info(MACH_TASK_BASIC_INFO)`; RSS-based assertions already skip when a platform reports 0.

## Fault injection

Build with `-DEMQ_FAULT_INJECT=ON`. Hooks:

- Memory: fault point `malloc` (via `emq_aligned_alloc` / `emq_mem.h`)
- Disk: `file_pwrite`, `file_short_write`, `file_pread`, `file_sync`, `file_resize`, `mmap_sync`
- Crash points in log: `log_append_pre`, `log_sync_pre/post`, `log_meta_write`, `log_blob_write`, `log_segment_rotate`, `log_compact`, `log_snapshot`, `log_trim_front`

Env:

```
EMQ_FAULT=malloc:after:5
EMQ_FAULT=file_pwrite:every:3
EMQ_CRASH_AT=log_sync_pre:1
```

## Invariants stress suites check

- Self-verifying payloads (seq + producer + FNV checksum)
- FIFO order (SPSC) and policy order (difftest)
- Conservation: Σpushed == Σpopped + depth (MPMC)
- Watchdog aborts on stalled heartbeats (deadlock/livelock)
- RSS plateau (no unbounded growth in quick modes; skip if RSS==0)
- Recovery: contiguous verified prefix after kill / crash point
- Work-queue lifecycle (model checker): no loss, single inflight, DLQ at retry limit

## Perf baseline

Committed at `bench/baselines/windows-x64.csv`. Gate thresholds: throughput −10%, p99 +10%.
The hard gate stays local in `scripts/perf_check.ps1`; CI benches are informational only.
