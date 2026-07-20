#!/usr/bin/env bash
# Run comparable queue load tests for all published clients (via Docker).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
N="${EMQ_LOAD_N:-100000}"
PAYLOAD="${EMQ_LOAD_PAYLOAD:-64}"
OUT="${ROOT}/examples/loadtest/results.txt"
: >"$OUT"

echo "EmbeddedMQ cross-client load test  N=$N payload=$PAYLOAD"
echo "========================================================"

run_one() {
  local lang="$1"
  shift
  echo
  echo ">>> $lang"
  if "$@" 2>&1 | tee /tmp/emq-load-"$lang".log | tee -a "$OUT" | grep -E '^(client=|RESULT )'; then
    :
  else
    echo "FAILED: $lang" | tee -a "$OUT"
    tail -40 /tmp/emq-load-"$lang".log || true
    return 1
  fi
}

run_one python docker run --rm \
  -e EMQ_LOAD_N="$N" -e EMQ_LOAD_PAYLOAD="$PAYLOAD" \
  -v "$ROOT/examples/loadtest/python:/app" -w /app \
  python:3.12-bookworm bash -c '
    set -e
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential >/tmp/a.log
    pip -q install -U pip
    pip -q install "embeddedmq==1.0.0b1"
    python loadtest.py
  '

run_one rust docker run --rm \
  -e EMQ_LOAD_N="$N" -e EMQ_LOAD_PAYLOAD="$PAYLOAD" \
  -v "$ROOT/examples/loadtest/rust:/app" -w /app \
  rust:1.85-bookworm bash -c '
    export PATH=/usr/local/cargo/bin:$PATH CARGO_TARGET_DIR=/tmp/ct
    set -e
    cargo build --release -q
    cargo run --release -q
  '

run_one go docker run --rm \
  -e EMQ_LOAD_N="$N" -e EMQ_LOAD_PAYLOAD="$PAYLOAD" \
  -v "$ROOT/examples/loadtest/go:/app" -w /app \
  golang:1.22-bookworm bash -c '
    set -e
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq gcc >/tmp/a.log
    export CGO_ENABLED=1
    go mod tidy
    go run .
  '

run_one java docker run --rm \
  -e EMQ_LOAD_N="$N" -e EMQ_LOAD_PAYLOAD="$PAYLOAD" \
  -v "$ROOT/examples/loadtest/java:/app" -w /app \
  maven:3.9-eclipse-temurin-22 bash -c '
    set -e
    mvn -q -B package
    java --enable-native-access=ALL-UNNAMED -jar target/java-loadtest-1.0.0-SNAPSHOT.jar
  '

echo
echo "========================================================"
echo "Summary (RESULT lines):"
grep '^RESULT ' "$OUT" || true
echo "Full log: $OUT"
