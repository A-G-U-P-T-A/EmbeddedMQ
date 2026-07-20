# Version compatibility fixtures

Frozen durable log directories for `test_compat`.

## `log_v1/`

Contains a durable queue snapshot with 10 self-verifying payloads (seq 0–9, 64-byte bodies).

Generate or refresh:

```sh
cmake --build build --target gen_fixture
./build/tests/gen_fixture tests/fixtures/log_v1
```

On Windows:

```powershell
cmake --build build --config Release --target gen_fixture
.\build\tests\Release\gen_fixture.exe tests\fixtures\log_v1
```

Commit the generated `log.meta`, segment, and blob files under `log_v1/` after verifying `test_compat` passes.
