/*
 * Fanout scaling: 1 publisher -> N subscribers.
 * Reports latency, RSS, and runtime allocator stats (no payload duplication).
 */
#include "emq/emq.h"
#include "platform/emq_platform.h"
#include "bench_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_fanout(uint32_t n_subs, size_t ops, size_t payload) {
  emq_runtime *rt = NULL;
  emq_subscription **subs = NULL;
  emq_runtime_stats rs;
  emq_bench_process_metrics before, after, delta;
  uint8_t *buf;
  uint64_t *lat;
  size_t i, got = 0;
  uint64_t t0, t1;
  char name[64];
  int rc = 0;

  buf = (uint8_t *)malloc(payload ? payload : 1);
  lat = (uint64_t *)malloc(sizeof(uint64_t) * ops);
  subs = (emq_subscription **)calloc(n_subs, sizeof(*subs));
  if (!buf || !lat || !subs) {
    free(buf);
    free(lat);
    free(subs);
    return 1;
  }
  memset(buf, 0xA5, payload);

  if (emq_runtime_create(&rt) != EMQ_OK) {
    free(buf);
    free(lat);
    free(subs);
    return 1;
  }
  for (i = 0; i < n_subs; ++i) {
    snprintf(name, sizeof(name), "g%u", (unsigned)i);
    if (emq_subscribe(rt, "fanout/topic", name, &subs[i]) != EMQ_OK) {
      rc = 1;
      goto done;
    }
  }

  emq_bench_process_sample(&before);
  t0 = emq_now_ns();
  for (i = 0; i < ops; ++i) {
    uint64_t a = emq_now_ns();
    if (emq_publish(rt, "fanout/topic", buf, payload) != EMQ_OK) {
      rc = 1;
      goto done;
    }
    lat[i] = emq_now_ns() - a;
  }
  t1 = emq_now_ns();
  emq_bench_process_sample(&after);
  emq_bench_process_delta(&before, &after, &delta);

  /* Drain one subscriber to prove delivery. */
  for (i = 0; i < ops; ++i) {
    emq_message m;
    if (emq_sub_next(subs[0], &m, 100) == EMQ_OK) {
      got++;
      emq_message_release(&m);
    }
  }

  emq_bench_sort_u64(lat, ops);
  memset(&rs, 0, sizeof(rs));
  (void)emq_get_runtime_stats(rt, &rs);

  printf("subs=%-5u ops=%-8zu payload=%-6zu pub_ops/s=%-10.0f p50=%llu p99=%llu "
         "rss_MB=%.2f alloc_live=%llu drained=%zu\n",
         (unsigned)n_subs, ops, payload,
         (t1 > t0) ? ((double)ops * 1e9) / (double)(t1 - t0) : 0.0,
         (unsigned long long)emq_bench_percentile(lat, ops, 0.50),
         (unsigned long long)emq_bench_percentile(lat, ops, 0.99),
         delta.rss_bytes / (1024.0 * 1024.0),
         (unsigned long long)rs.allocator_live_bytes, got);

  if (got == 0) rc = 1;

done:
  for (i = 0; i < n_subs; ++i) {
    if (subs[i]) emq_unsubscribe(subs[i]);
  }
  emq_runtime_destroy(rt);
  free(buf);
  free(lat);
  free(subs);
  return rc;
}

int main(int argc, char **argv) {
  int quick = 0;
  int i;
  static const uint32_t subs[] = {1u, 10u, 100u, 1000u};
  int failed = 0;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--quick") == 0) quick = 1;
  }

  printf("EmbeddedMQ fanout bench (1 publisher -> N subscribers)\n");
  for (i = 0; i < (int)(sizeof(subs) / sizeof(subs[0])); ++i) {
    uint32_t n = subs[i];
    size_t ops = quick ? 200 : 2000;
    if (n >= 1000u && quick) ops = 50;
    if (n >= 1000u && !quick) ops = 500;
    if (run_fanout(n, ops, 64) != 0) failed = 1;
  }
  printf("%s\n", failed ? "FANOUT BENCH FAILED" : "FANOUT BENCH OK");
  return failed;
}
