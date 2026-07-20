# embeddedmq (Python)

CPython extension for EmbeddedMQ. Uses a standard **src layout** and vendors the
C engine under `native/` (refreshed by `scripts/sync_native.py`), so
`pip install` compiles the library into the extension — no prior cmake step.

```text
python/
  pyproject.toml
  setup.py
  MANIFEST.in
  native/                 # vendored engine (do not edit; sync_native.py)
  src/embeddedmq/
    __init__.py
    _emq.c
```

## Install

```bash
# from a clone
pip install ./bindings/python

# editable
pip install -e ./bindings/python
```

Needs a C compiler on the machine for source installs. Prebuilt wheels are
produced by the release workflow when published to PyPI.

## Usage

```python
from embeddedmq import Runtime

rt = Runtime()
q = rt.create_queue("demo", 64)
q.push(b"hello")

# Hot path: emq_pop_into into a reused buffer (no engine malloc)
buf = bytearray(64)
n = q.pop_copy(buf, 1000)

# Throughput: emq_push_n + emq_pop_into_n
# q.push_repeat(payload, 32)
# got = q.pop_copy_n(batch_buf, msg_cap, 32)

# Convenience (allocates):
# msg = q.pop(); data = msg.data()

q.close()
```

Prefer `pop_copy` or batch APIs. `Message.data()` allocates every time.

## Optional: link a prebuilt libemq

```bash
EMQ_SYSTEM_LIB=1 EMQ_LIB_DIR=/path/to/build pip install -e ./bindings/python
```

Licensed under Apache-2.0 (see repo root `LICENSE`).
