#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
test -f src/main/resources/native/linux/x86_64/libemq.so
mvn -q -B -DskipTests package test-compile
mvn -q -B dependency:build-classpath -Dmdep.outputFile=/tmp/emq-cp.txt
java --enable-native-access=ALL-UNNAMED \
  -cp "target/classes:target/test-classes:$(cat /tmp/emq-cp.txt)" \
  io.embeddedmq.QueueLoadTest
