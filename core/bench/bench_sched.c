/*
 * Timing-wheel / delay-queue jitter bench.
 * Schedules N delayed messages with random delays, measures fire-time error.
 */
#include "emq/emq.h"
#include "platform/emq_platform.h"
#include "bench_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  uint64_t n = 10000;
  int quick = 0;
  size_t i;
  uint64_t *errors = NULL;
  uint64_t fired = 0;
  uint64_t deadline;
  uint8_t payload[32];
  int argi;

  for (argi = 1; argi < argc; ++argi) {
    if (strcmp(argv[argi], "--quick") == 0) {
      quick = 1;
      n = 1000;
    } else if (strcmp(argv[argi], "--ops") == 0 && argi + 1 < argc) {
      n = (uint64_t)strtoull(argv[++argi], NULL, 10);
    }
  }

  errors = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)n);
  if (!errors) return 1;

  if (emq_runtime_create(&rt) != EMQ_OK) {
    free(errors);
    return 1;
  }
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_DELAY;
  if (emq_queue_create(rt, "delay", &opts, &q) != EMQ_OK) {
    emq_runtime_destroy(rt);
    free(errors);
    return 1;
  }

  printf("Scheduling %llu delayed messages...\n", (unsigned long long)n);
  for (i = 0; i < (size_t)n; ++i) {
    emq_message meta;
    uint32_t delay_ms = 1u + (uint32_t)((i * 37u) % (quick ? 20u : 200u));
    memset(&meta, 0, sizeof(meta));
    meta.deliver_at_ns = emq_now_ns() + (uint64_t)delay_ms * 1000000ULL;
    snprintf((char *)payload, sizeof(payload), "%llu", (unsigned long long)i);
    memcpy(payload + 16, &meta.deliver_at_ns, 8);
    if (emq_push(q, payload, sizeof(payload), &meta) != EMQ_OK) {
      fprintf(stderr, "push failed at %zu\n", i);
      emq_runtime_destroy(rt);
      free(errors);
      return 1;
    }
  }

  deadline = emq_now_ns() + (quick ? 5ull : 30ull) * 1000000000ULL;
  while (fired < n && emq_now_ns() < deadline) {
    emq_message m;
    emq_status st = emq_pop(q, &m, 10);
    if (st == EMQ_OK) {
      uint64_t expected = 0;
      uint64_t now = emq_now_ns();
      int64_t err;
      if (m.size >= 24) memcpy(&expected, (const uint8_t *)m.data + 16, 8);
      err = (int64_t)now - (int64_t)expected;
      if (err < 0) err = -err;
      errors[fired++] = (uint64_t)err;
      emq_message_release(&m);
    }
    (void)emq_run_once(rt, 64);
  }

  if (fired == 0) {
    fprintf(stderr, "no messages fired\n");
    emq_runtime_destroy(rt);
    free(errors);
    return 1;
  }

  emq_bench_sort_u64(errors, (size_t)fired);
  printf("fired=%llu/%llu p50_err_ns=%llu p99_err_ns=%llu\n",
         (unsigned long long)fired, (unsigned long long)n,
         (unsigned long long)emq_bench_percentile(errors, (size_t)fired, 0.50),
         (unsigned long long)emq_bench_percentile(errors, (size_t)fired, 0.99));

  /* Soft bound: p99 jitter under 50ms for this synthetic load. */
  if (emq_bench_percentile(errors, (size_t)fired, 0.99) > 50000000ULL) {
    fprintf(stderr, "p99 jitter too high\n");
    emq_runtime_destroy(rt);
    free(errors);
    return 1;
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  free(errors);
  printf("SCHED BENCH OK\n");
  return 0;
}
