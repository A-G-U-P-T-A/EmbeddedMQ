/*
 * Per-op latency: timed push + pop_into pair.
 * Env: EMQ_LOAD_N, EMQ_LOAD_PAYLOAD, EMQ_LOAD_WARMUP
 * Prints: LATENCY lang=c payload=N n=N p50_ns=... p99_ns=... p999_ns=... p9999_ns=...
 */
#include "emq/emq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int env_int(const char *key, int def) {
  const char *v = getenv(key);
  if (!v || !*v) return def;
  return atoi(v);
}

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b) {
  uint64_t da = *(const uint64_t *)a;
  uint64_t db = *(const uint64_t *)b;
  return (da > db) - (da < db);
}

static uint64_t pct(uint64_t *sorted, size_t n, double p) {
  if (n == 0) return 0;
  size_t idx = (size_t)(p * (double)(n - 1) + 0.5);
  if (idx >= n) idx = n - 1;
  return sorted[idx];
}

int main(void) {
  int n = env_int("EMQ_LOAD_N", 1000000);
  int payload_len = env_int("EMQ_LOAD_PAYLOAD", 64);
  int warmup = env_int("EMQ_LOAD_WARMUP", 50000);
  uint32_t capacity = 4096;
  uint8_t *payload, *dst;
  uint64_t *lat;
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  size_t got;
  int i;

  if (n < 1) n = 1;
  payload = (uint8_t *)malloc((size_t)payload_len);
  dst = (uint8_t *)malloc((size_t)payload_len);
  lat = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)n);
  if (!payload || !dst || !lat) {
    fprintf(stderr, "oom\n");
    return 1;
  }
  memset(payload, 0xAB, (size_t)payload_len);

  if (emq_runtime_create(&rt) != EMQ_OK) return 1;
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.capacity = capacity;
  opts.producers = 1;
  opts.consumers = 1;
  if (emq_queue_create(rt, "lat-c", &opts, &q) != EMQ_OK) return 1;

  for (i = 0; i < warmup; ++i) {
    if (emq_push(q, payload, (size_t)payload_len, NULL) != EMQ_OK) return 1;
    if (emq_pop_into(q, dst, (size_t)payload_len, &got, NULL, 0) != EMQ_OK)
      return 1;
  }

  for (i = 0; i < n; ++i) {
    uint64_t t0 = now_ns();
    if (emq_push(q, payload, (size_t)payload_len, NULL) != EMQ_OK) return 1;
    if (emq_pop_into(q, dst, (size_t)payload_len, &got, NULL, 0) != EMQ_OK)
      return 1;
    lat[i] = now_ns() - t0;
  }

  qsort(lat, (size_t)n, sizeof(uint64_t), cmp_u64);
  printf("LATENCY lang=c payload=%d n=%d p50_ns=%llu p99_ns=%llu p999_ns=%llu "
         "p9999_ns=%llu\n",
         payload_len, n, (unsigned long long)pct(lat, (size_t)n, 0.50),
         (unsigned long long)pct(lat, (size_t)n, 0.99),
         (unsigned long long)pct(lat, (size_t)n, 0.999),
         (unsigned long long)pct(lat, (size_t)n, 0.9999));

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  free(payload);
  free(dst);
  free(lat);
  return 0;
}
