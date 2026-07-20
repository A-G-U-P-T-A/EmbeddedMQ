#!/usr/bin/env bash
set -euo pipefail
export PATH=/usr/local/cargo/bin:$PATH
export CARGO_TARGET_DIR=/tmp/ct
for i in 1 2 3 4 5 6 7 8; do
  if cargo build; then
    cargo run --quiet
    exit 0
  fi
  echo "retry $i after cargo build failure"
  sleep 20
done
exit 1
