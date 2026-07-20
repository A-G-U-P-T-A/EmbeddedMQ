# EmbeddedMQ release checklist

SQLite-inspired gate before tagging a release. Every item should be checked or
explicitly waived with rationale in the release notes.

## Platforms

- [ ] Windows MSVC (x64) — `cmake -S core -B build && cmake --build build && ctest`
- [ ] Linux GCC — same as above
- [ ] Linux Clang — same as above
- [ ] macOS Apple Clang — same as above
- [ ] Optional: Linux ARM64 cross or native (`build-arm64` job stub in CI)
- [ ] Optional transport plane: `-DEMQ_BUILD_TRANSPORT=ON` builds `emq_ipc` + `emq_net`

## Sanitizers

- [ ] ASan + UBSan (Clang, `-DEMQ_SANITIZE=address,undefined`)
- [ ] TSan on threaded suites (`stress`, `fuzz`, `difftest`, `model`)
- [ ] MSVC `/fsanitize=address` when supported (`-DEMQ_ASAN=ON`)

## ABI / API

- [ ] `test_abi` passes — exported symbol set stable
- [ ] `test_compat` passes — log fixture v1 roundtrip
- [ ] Header/version bump recorded in release notes
- [ ] Shared library build smoke (`-DEMQ_BUILD_SHARED=ON`) if shipping `.so/.dll`

## Performance

- [ ] `emq_bench_load --quick` within regression budget (see `scripts/perf_check.ps1`)
- [ ] No unexpected hot-path regressions in LFQ/FIFO benchmarks
- [ ] Binding gates in [`examples/loadtest/RELEASE_GATES.md`](../examples/loadtest/RELEASE_GATES.md):
      median trials on Linux; scalar + batch % of C; `test_pop_into` green
- [ ] Docs/examples promote `pop_into` / batch — not owning `Message`+`data()`

## Coverage

- [ ] `-DEMQ_ENABLE_COVERAGE=ON` build completes
- [ ] `EMQ_TESTCASE` branches exercised in boundary/claim suites
- [ ] Coverage report archived for the tag

## Fuzz / stress

- [ ] Fuzz targets run (nightly or manual) with clean ASan
- [ ] Soak/stress suites green on Release build with `EMQ_BUILD_STRESS=ON`

## Documentation

- [ ] `docs/overview.md` and `docs/getting-started.md` match API
- [ ] `docs/MEMORY_ORDERING.md` reviewed if LFQ/atomics changed
- [ ] Transport threat models updated when IPC/net behavior changes

## Sign-off

- [ ] Changelog entry
- [ ] Tag signed / checksums published
- [ ] Known issues section updated
