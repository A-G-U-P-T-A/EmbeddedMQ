#include "emq/emq.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a;
  uint64_t y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

static uint64_t percentile(uint64_t *sorted, size_t n, double p) {
  size_t idx;
  if (n == 0) return 0;
  idx = (size_t)(p * (double)(n - 1));
  return sorted[idx];
}

int main(int argc, char **argv) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  size_t n = 50000;
  size_t i;
  uint64_t *lat;
  char payload[64];
  uint64_t t0, t1;
  emq_message m;

  if (argc > 1) n = (size_t)atoi(argv[1]);
  lat = (uint64_t *)malloc(sizeof(uint64_t) * n);
  if (!lat) return 1;

  memset(payload, 'x', sizeof(payload));
  emq_runtime_create(&rt);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  emq_queue_create(rt, "bench", &opts, &q);

  for (i = 0; i < n; ++i) {
    t0 = emq_now_ns();
    emq_push(q, payload, sizeof(payload), NULL);
    emq_pop(q, &m, 0);
    t1 = emq_now_ns();
    lat[i] = t1 - t0;
    free((void *)m.data);
  }

  qsort(lat, n, sizeof(uint64_t), cmp_u64);
  printf("SPSC push+pop n=%zu\n", n);
  printf("p50   %llu ns\n", (unsigned long long)percentile(lat, n, 0.50));
  printf("p99   %llu ns\n", (unsigned long long)percentile(lat, n, 0.99));
  printf("p99.9 %llu ns\n", (unsigned long long)percentile(lat, n, 0.999));
  printf("p99.99 %llu ns\n", (unsigned long long)percentile(lat, n, 0.9999));

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  free(lat);
  return 0;
}
