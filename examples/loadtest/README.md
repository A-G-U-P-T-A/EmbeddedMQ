# Cross-client queue load test

Same workload for Java / Python / Go / Rust against the published packages:

1. create runtime + queue (capacity ≥ message count)
2. push `N` fixed-size payloads
3. pop `N` messages and release them
4. report ops/sec for push, pop, and round-trip

Defaults: `N=100000`, payload `64` bytes.

```powershell
# from repo root (Windows + Docker)
.\examples\loadtest\run-all.ps1
```

```bash
# Linux/macOS + Docker
bash examples/loadtest/run-all.sh
```

Env overrides: `EMQ_LOAD_N`, `EMQ_LOAD_PAYLOAD`.

**Note:** Go is load-tested via monorepo `replace` because the published
`bindings/go` module still expects sibling `bindings/native` headers (not
inside the module zip). Python / Rust / Java use the public registries.
