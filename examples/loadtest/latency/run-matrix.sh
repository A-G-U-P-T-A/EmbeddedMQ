#!/usr/bin/env bash
# Latency percentile matrix: C → Go → Rust → Python
# Payload×N: 64B/10M, 256B/10M, 1KB/10M, 4KB/5M, 16KB/1M
set -euo pipefail
# Prevent Git Bash from rewriting /src -> C:/Program Files/Git/src
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
# Docker Desktop on Windows needs a Windows-style host path
if command -v cygpath >/dev/null 2>&1; then
  ROOT_DOCKER="$(cygpath -w "$ROOT")"
else
  ROOT_DOCKER="$ROOT"
fi
OUT="${ROOT}/examples/loadtest/latency/results.txt"
WARMUP="${EMQ_LOAD_WARMUP:-50000}"
if [[ "${EMQ_LAT_APPEND:-0}" != "1" ]]; then
  : >"$OUT"
fi

SIZES=(64 256 1024 4096 16384)
COUNTS=(10000000 10000000 10000000 5000000 1000000)

echo "EmbeddedMQ latency matrix (push+pop_into per op)"
echo "warmup=$WARMUP"
echo "================================================"

run_c() {
  docker run --rm \
    -e "EMQ_LOAD_WARMUP=$WARMUP" \
    -v "${ROOT_DOCKER}:/src" -w /src gcc:14-bookworm bash -c '
set -e
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq cmake ninja-build >/tmp/a.log
cmake -S /src/core -B /tmp/emq-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DEMQ_BUILD_TESTS=OFF -DEMQ_BUILD_BENCH=OFF -DEMQ_BUILD_EXAMPLES=OFF -DEMQ_BUILD_STRESS=OFF
cmake --build /tmp/emq-build -j
gcc -O2 -std=c11 -D_GNU_SOURCE -DEMQ_PLATFORM_POSIX \
  -I/src/core/include -I/src/core/src \
  /src/examples/loadtest/latency/c/latency.c /tmp/emq-build/libemq.a -lpthread -o /tmp/latency-c
SIZES=(64 256 1024 4096 16384)
COUNTS=(10000000 10000000 10000000 5000000 1000000)
for i in 0 1 2 3 4; do
  export EMQ_LOAD_PAYLOAD=${SIZES[$i]} EMQ_LOAD_N=${COUNTS[$i]}
  echo ">>> c payload=$EMQ_LOAD_PAYLOAD n=$EMQ_LOAD_N" >&2
  /tmp/latency-c
done
'
}

run_go() {
  docker run --rm \
    -e "EMQ_LOAD_WARMUP=$WARMUP" \
    -v "${ROOT_DOCKER}:/src" -w /src/examples/loadtest/latency \
    golang:1.22-bookworm bash -c '
set -e
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq gcc >/tmp/a.log
export CGO_ENABLED=1
go mod tidy
go build -o /tmp/latency-go .
SIZES=(64 256 1024 4096 16384)
COUNTS=(10000000 10000000 10000000 5000000 1000000)
for i in 0 1 2 3 4; do
  export EMQ_LOAD_PAYLOAD=${SIZES[$i]} EMQ_LOAD_N=${COUNTS[$i]}
  echo ">>> go payload=$EMQ_LOAD_PAYLOAD n=$EMQ_LOAD_N" >&2
  /tmp/latency-go
done
'
}

run_rust() {
  docker run --rm \
    -e "EMQ_LOAD_WARMUP=$WARMUP" \
    -v "${ROOT_DOCKER}:/src" -w /src/examples/loadtest/latency \
    rust:1.85-bookworm bash -c '
set -e
export PATH=/usr/local/cargo/bin:$PATH CARGO_TARGET_DIR=/tmp/ct
cargo build --release -q
SIZES=(64 256 1024 4096 16384)
COUNTS=(10000000 10000000 10000000 5000000 1000000)
for i in 0 1 2 3 4; do
  export EMQ_LOAD_PAYLOAD=${SIZES[$i]} EMQ_LOAD_N=${COUNTS[$i]}
  echo ">>> rust payload=$EMQ_LOAD_PAYLOAD n=$EMQ_LOAD_N" >&2
  /tmp/ct/release/emq-latency
done
'
}

run_python() {
  docker run --rm \
    -e "EMQ_LOAD_WARMUP=$WARMUP" \
    -v "${ROOT_DOCKER}:/src" -w /src/examples/loadtest/latency \
    python:3.12-bookworm bash -c '
set -e
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential >/tmp/a.log
pip -q install -U pip
pip -q install /src/bindings/python
SIZES=(64 256 1024 4096 16384)
COUNTS=(10000000 10000000 10000000 5000000 1000000)
for i in 0 1 2 3 4; do
  export EMQ_LOAD_PAYLOAD=${SIZES[$i]} EMQ_LOAD_N=${COUNTS[$i]}
  echo ">>> python payload=$EMQ_LOAD_PAYLOAD n=$EMQ_LOAD_N" >&2
  python latency.py
done
'
}

LANGS="${EMQ_LAT_LANGS:-c go rust python}"
for lang in $LANGS; do
  echo
  echo "=== $lang ==="
  set +e
  "run_$lang" 2>&1 | tee -a "$OUT"
  rc=${PIPESTATUS[0]}
  set -e
  if [[ $rc -ne 0 ]] || ! grep -q "^LATENCY lang=$lang " "$OUT"; then
    echo "FAILED: $lang (exit=$rc)" | tee -a "$OUT"
    exit 1
  fi
done

echo
echo "================================================"
echo "LATENCY lines:"
grep '^LATENCY ' "$OUT" || true
echo "Full log: $OUT"
