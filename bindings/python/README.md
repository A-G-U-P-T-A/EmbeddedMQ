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
q = rt.create_queue("demo", capacity=64)
q.push(b"hello")
msg = q.pop()
assert msg.data() == b"hello"
q.close()
```

`Message` calls `emq_message_release` when garbage-collected.

## Optional: link a prebuilt libemq

```bash
EMQ_SYSTEM_LIB=1 EMQ_LIB_DIR=/path/to/build pip install -e ./bindings/python
```
