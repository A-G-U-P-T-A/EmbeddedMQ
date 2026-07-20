/*
 * Apples-to-apples C baseline for binding loadtests.
 *
 * Modes (all bulk push-all then pop-all unless noted):
 *   scalar_pop_into     — emq_push + emq_pop_into (hot path, no malloc)
 *   scalar_claim        — emq_push + emq_claim/release
 *   batch_pop_into_n    — emq_push_n + emq_pop_into_n
 *   legacy_pop          — emq_push + emq_pop + release (malloc path)
 *
 * Env: EMQ_LOAD_N, EMQ_LOAD_PAYLOAD, EMQ_LOAD_WARMUP, EMQ_LOAD_BATCH,
 *      EMQ_LOAD_TRIALS (default 5; reports median round-trip).
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

static double now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_double(const void *a, const void *b) {
  double da = *(const double *)a;
  double db = *(const double *)b;
  return (da > db) - (da < db);
}

static void report(const char *mode, int n, double t0, double t1, double t2) {
  double push_s = t1 - t0;
  double pop_s = t2 - t1;
  double total_s = t2 - t0;
  printf(
      "RESULT lang=c mode=%s push_ops=%.0f/s pop_ops=%.0f/s "
      "roundtrip_ops=%.0f/s push_ms=%.1f pop_ms=%.1f total_ms=%.1f\n",
      mode, (double)n / push_s, (double)n / pop_s, (double)n / total_s,
      push_s * 1000.0, pop_s * 1000.0, total_s * 1000.0);
}

typedef struct {
  emq_queue *q;
  uint8_t *payload;
  uint8_t *dst;
  uint8_t *batch_dst;
  int payload_len;
  int batch;
  int n;
} ctx_t;

static double run_scalar_pop_into(ctx_t *c) {
  size_t got = 0;
  int i;
  double t0, t1, t2;
  t0 = now_s();
  for (i = 0; i < c->n; ++i) {
    if (emq_push(c->q, c->payload, (size_t)c->payload_len, NULL) != EMQ_OK)
      return -1;
  }
  t1 = now_s();
  for (i = 0; i < c->n; ++i) {
    if (emq_pop_into(c->q, c->dst, (size_t)c->payload_len, &got, NULL, 1000) !=
            EMQ_OK ||
        (int)got != c->payload_len)
      return -1;
  }
  t2 = now_s();
  report("scalar_pop_into", c->n, t0, t1, t2);
  return (double)c->n / (t2 - t0);
}

static double run_scalar_claim(ctx_t *c) {
  emq_message m;
  int i;
  double t0, t1, t2;
  t0 = now_s();
  for (i = 0; i < c->n; ++i) {
    if (emq_push(c->q, c->payload, (size_t)c->payload_len, NULL) != EMQ_OK)
      return -1;
  }
  t1 = now_s();
  for (i = 0; i < c->n; ++i) {
    if (emq_claim(c->q, &m, 1000) != EMQ_OK) return -1;
    if ((int)m.size != c->payload_len) return -1;
    if (emq_release_claim(c->q, &m) != EMQ_OK) return -1;
  }
  t2 = now_s();
  report("scalar_claim", c->n, t0, t1, t2);
  return (double)c->n / (t2 - t0);
}

static double run_batch_pop_into_n(ctx_t *c) {
  int left, chunk;
  size_t count = 0;
  double t0, t1, t2;
  t0 = now_s();
  left = c->n;
  while (left > 0) {
    size_t pushed = 0;
    chunk = c->batch < left ? c->batch : left;
    if (emq_push_n(c->q, c->payload, (size_t)c->payload_len, (size_t)chunk,
                   &pushed) != EMQ_OK ||
        (int)pushed != chunk)
      return -1;
    left -= chunk;
  }
  t1 = now_s();
  left = c->n;
  while (left > 0) {
    chunk = c->batch < left ? c->batch : left;
    if (emq_pop_into_n(c->q, c->batch_dst, (size_t)c->payload_len,
                       (size_t)chunk, &count, NULL, 1000) != EMQ_OK ||
        (int)count != chunk)
      return -1;
    left -= (int)count;
  }
  t2 = now_s();
  report("batch_pop_into_n", c->n, t0, t1, t2);
  return (double)c->n / (t2 - t0);
}

static double run_legacy_pop(ctx_t *c) {
  emq_message m;
  int i;
  double t0, t1, t2;
  t0 = now_s();
  for (i = 0; i < c->n; ++i) {
    if (emq_push(c->q, c->payload, (size_t)c->payload_len, NULL) != EMQ_OK)
      return -1;
  }
  t1 = now_s();
  for (i = 0; i < c->n; ++i) {
    if (emq_pop(c->q, &m, 1000) != EMQ_OK) return -1;
    emq_message_release(&m);
  }
  t2 = now_s();
  report("legacy_pop", c->n, t0, t1, t2);
  return (double)c->n / (t2 - t0);
}

static void median_report(const char *mode, double *samples, int trials) {
  qsort(samples, (size_t)trials, sizeof(double), cmp_double);
  printf("MEDIAN lang=c mode=%s roundtrip_ops=%.0f/s trials=%d "
         "min=%.0f/s max=%.0f/s\n",
         mode, samples[trials / 2], trials, samples[0], samples[trials - 1]);
}

int main(void) {
  int n = env_int("EMQ_LOAD_N", 100000);
  int payload_len = env_int("EMQ_LOAD_PAYLOAD", 64);
  int warmup = env_int("EMQ_LOAD_WARMUP", 20000);
  int batch = env_int("EMQ_LOAD_BATCH", 32);
  int trials = env_int("EMQ_LOAD_TRIALS", 5);
  uint32_t capacity = (uint32_t)(n + 16);
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  ctx_t ctx;
  double *samples;
  int t, i;
  size_t got;

  if (capacity < 1024u) capacity = 1024u;
  if (trials < 1) trials = 1;

  ctx.payload = (uint8_t *)malloc((size_t)payload_len);
  ctx.dst = (uint8_t *)malloc((size_t)payload_len);
  ctx.batch_dst = (uint8_t *)malloc((size_t)payload_len * (size_t)batch);
  samples = (double *)malloc(sizeof(double) * (size_t)trials);
  if (!ctx.payload || !ctx.dst || !ctx.batch_dst || !samples) {
    fprintf(stderr, "oom\n");
    return 1;
  }
  for (i = 0; i < payload_len; ++i) ctx.payload[i] = (uint8_t)(i % 256);
  ctx.payload_len = payload_len;
  ctx.batch = batch;
  ctx.n = n;

  printf("client=c n=%d payload=%d capacity=%u batch=%d trials=%d\n", n,
         payload_len, capacity, batch, trials);

  if (emq_runtime_create(&rt) != EMQ_OK) return 1;
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.capacity = capacity;
  opts.producers = 1;
  opts.consumers = 1;
  if (emq_queue_create(rt, "loadtest-c", &opts, &q) != EMQ_OK) return 1;
  ctx.q = q;

  for (i = 0; i < warmup; ++i) {
    if (emq_push(q, ctx.payload, (size_t)payload_len, NULL) != EMQ_OK) return 1;
  }
  for (i = 0; i < warmup; ++i) {
    if (emq_pop_into(q, ctx.dst, (size_t)payload_len, &got, NULL, 1000) !=
        EMQ_OK)
      return 1;
  }

  for (t = 0; t < trials; ++t) {
    samples[t] = run_scalar_pop_into(&ctx);
    if (samples[t] < 0) return 1;
  }
  median_report("scalar_pop_into", samples, trials);

  for (t = 0; t < trials; ++t) {
    samples[t] = run_scalar_claim(&ctx);
    if (samples[t] < 0) return 1;
  }
  median_report("scalar_claim", samples, trials);

  for (t = 0; t < trials; ++t) {
    samples[t] = run_batch_pop_into_n(&ctx);
    if (samples[t] < 0) return 1;
  }
  median_report("batch_pop_into_n", samples, trials);

  for (t = 0; t < trials; ++t) {
    samples[t] = run_legacy_pop(&ctx);
    if (samples[t] < 0) return 1;
  }
  median_report("legacy_pop", samples, trials);

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  free(ctx.payload);
  free(ctx.dst);
  free(ctx.batch_dst);
  free(samples);
  return 0;
}
