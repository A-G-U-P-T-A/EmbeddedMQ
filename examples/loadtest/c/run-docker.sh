#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
N="${EMQ_LOAD_N:-100000}"
PAYLOAD="${EMQ_LOAD_PAYLOAD:-64}"
BATCH="${EMQ_LOAD_BATCH:-32}"
TRIALS="${EMQ_LOAD_TRIALS:-5}"

docker run --rm \
  -e "EMQ_LOAD_N=$N" \
  -e "EMQ_LOAD_PAYLOAD=$PAYLOAD" \
  -e "EMQ_LOAD_BATCH=$BATCH" \
  -e "EMQ_LOAD_TRIALS=$TRIALS" \
  -v "$ROOT:/src" -w /src gcc:14-bookworm bash -c '
set -e
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq cmake ninja-build >/tmp/a.log
cmake -S /src/core -B /tmp/emq-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DEMQ_BUILD_TESTS=ON -DEMQ_BUILD_BENCH=OFF -DEMQ_BUILD_EXAMPLES=OFF -DEMQ_BUILD_STRESS=OFF
cmake --build /tmp/emq-build -j
ctest --test-dir /tmp/emq-build --output-on-failure -R "test_pop_into|test_claim|test_abi"
gcc -O2 -std=c11 -D_GNU_SOURCE -DEMQ_PLATFORM_POSIX \
  -I/src/core/include -I/src/core/src \
  /src/examples/loadtest/c/loadtest.c /tmp/emq-build/libemq.a -lpthread -o /tmp/loadtest-c
/tmp/loadtest-c
'