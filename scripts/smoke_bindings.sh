#!/usr/bin/env bash
set -euo pipefail
python3 scripts/sync_native.py
rm -rf bindings/python/src/*.egg-info bindings/python/build
python3 -m venv /tmp/venv
/tmp/venv/bin/pip install -q -U pip
/tmp/venv/bin/pip install -q ./bindings/python
/tmp/venv/bin/python -c 'import embeddedmq; print("py ok")'
(cd bindings/go && CGO_ENABLED=1 go build ./...)
echo GO_OK
