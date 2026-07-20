# Python bindings

CPython extension wrapping core queue operations (`Runtime`, `Queue`, `Message`).

## Prerequisites

Build static `libemq`:

```bash
cmake -S core -B build -DEMQ_BUILD_TESTS=OFF
cmake --build build
```

## Install (editable)

From this directory:

```bash
# EMQ_ROOT defaults to repo root (../.. from bindings/python)
pip install -e .

# override library location
EMQ_ROOT=/path/to/EmbeddedMQ EMQ_LIB_DIR=/path/to/build pip install -e .
```

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

`Message` releases native memory when garbage-collected (`emq_message_release`).

## Layout

```
python/
  pyproject.toml
  setup.py          # Extension build (EMQ_ROOT, EMQ_LIB_DIR)
  src/embeddedmq/
    __init__.py
    _emq.c          # C extension
```

Compilation requires headers under `core/include`. Linking requires `libemq` in the build directory.
