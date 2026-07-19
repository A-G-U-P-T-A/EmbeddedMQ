/*
 * Comparative microbench: EmbeddedMQ FAST SPSC vs mutex+deque baseline.
 * SQLite comparison is optional (--sqlite) when sqlite3 is linked later.
 */

#include "emq/emq.h"
#include "platform/emq_platform.h"
#include "bench_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

typedef struct baseline_node {
  void *data;
  size_t size;
  struct baseline_node *next;
} baseline_node;

typedef struct baseline_q {
  baseline_node *head;
  baseline_node *tail;
#if defined(_WIN32)
  CRITICAL_SECTION mu;
#else
  pthread_mutex_t mu;
#endif
  size_t depth;
} baseline_q;

static void baseline_init(baseline_q *q) {
  memset(q, 0, sizeof(*q));
#if defined(_WIN32)
  InitializeCriticalSection(&q->mu);
#else
  pthread_mutex_init(&q->mu, NULL);
#endif
}

static void baseline_destroy(baseline_q *q) {
  baseline_node *n = q->head;
  while (n) {
    baseline_node *nx = n->next;
    free(n->data);
    free(n);
    n = nx;
  }
#if defined(_WIN32)
  DeleteCriticalSection(&q->mu);
#else
  pthread_mutex_destroy(&q->mu);
#endif
}

static int baseline_push(baseline_q *q, const void *data, size_t size) {
  baseline_node *n = (baseline_node *)calloc(1, sizeof(*n));
  if (!n) return -1;
  n->data = malloc(size ? size : 1);
  if (!n->data) {
    free(n);
    return -1;
  }
  if (size) memcpy(n->data, data, size);
  n->size = size;
#if defined(_WIN32)
  EnterCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
#endif
  if (!q->tail) q->head = q->tail = n;
  else {
    q->tail->next = n;
    q->tail = n;
  }
  q->depth++;
#if defined(_WIN32)
  LeaveCriticalSection(&q->mu);
#else
  pthread_mutex_unlock(&q->mu);
#endif
  return 0;
}

static int baseline_pop(baseline_q *q, void **data, size_t *size) {
  baseline_node *n;
#if defined(_WIN32)
  EnterCriticalSection(&q->mu);
#else
  pthread_mutex_lock(&q->mu);
#endif
  n = q->head;
  if (!n) {
#if defined(_WIN32)
    LeaveCriticalSection(&q->mu);
#else
    pthread_mutex_unlock(&q->mu);
#endif
    return -1;
  }
  q->head = n->next;
  if (!q->head) q->tail = NULL;
  q->depth--;
#if defined(_WIN32)
  LeaveCriticalSection(&q->mu);
#else
  pthread_mutex_unlock(&q->mu);
#endif
  *data = n->data;
  *size = n->size;
  free(n);
  return 0;
}

static void run_emq(size_t ops, size_t payload, emq_bench_cell *out) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  uint8_t *buf;
  size_t i;
  uint64_t t0, t1;
  uint64_t *lat;
  emq_message m;

  memset(out, 0, sizeof(*out));
  out->queues = 1;
  out->payload = payload;
  out->ops_total = ops;
  buf = (uint8_t *)malloc(payload ? payload : 1);
  lat = (uint64_t *)malloc(sizeof(uint64_t) * ops);
  if (!buf || !lat) {
    out->error = "oom";
    free(buf);
    free(lat);
    return;
  }
  memset(buf, 0xAB, payload);

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.producers = 1;
  opts.consumers = 1;
  if (emq_runtime_create(&rt) != EMQ_OK ||
      emq_queue_create(rt, "cmp", &opts, &q) != EMQ_OK) {
    out->error = "emq_init";
    free(buf);
    free(lat);
    if (rt) emq_runtime_destroy(rt);
    return;
  }

  emq_bench_process_sample(&out->before);
  t0 = emq_now_ns();
  for (i = 0; i < ops; ++i) {
    uint64_t a = emq_now_ns();
    emq_push(q, buf, payload, NULL);
    emq_pop(q, &m, 0);
    emq_message_release(&m);
    lat[i] = emq_now_ns() - a;
  }
  t1 = emq_now_ns();
  emq_bench_process_sample(&out->after);

  out->elapsed_ns = t1 - t0;
  out->ops_per_sec =
      out->elapsed_ns ? ((double)ops * 1e9) / (double)out->elapsed_ns : 0;
  emq_bench_sort_u64(lat, ops);
  out->p50_ns = emq_bench_percentile(lat, ops, 0.50);
  out->p99_ns = emq_bench_percentile(lat, ops, 0.99);
  out->p999_ns = emq_bench_percentile(lat, ops, 0.999);
  out->p9999_ns = emq_bench_percentile(lat, ops, 0.9999);
  out->ok = 1;

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  free(buf);
  free(lat);
}

static void run_baseline(size_t ops, size_t payload, emq_bench_cell *out) {
  baseline_q q;
  uint8_t *buf;
  size_t i;
  uint64_t t0, t1;
  uint64_t *lat;

  memset(out, 0, sizeof(*out));
  out->queues = 1;
  out->payload = payload;
  out->ops_total = ops;
  buf = (uint8_t *)malloc(payload ? payload : 1);
  lat = (uint64_t *)malloc(sizeof(uint64_t) * ops);
  if (!buf || !lat) {
    out->error = "oom";
    free(buf);
    free(lat);
    return;
  }
  memset(buf, 0xCD, payload);
  baseline_init(&q);

  emq_bench_process_sample(&out->before);
  t0 = emq_now_ns();
  for (i = 0; i < ops; ++i) {
    void *data = NULL;
    size_t sz = 0;
    uint64_t a = emq_now_ns();
    baseline_push(&q, buf, payload);
    baseline_pop(&q, &data, &sz);
    free(data);
    lat[i] = emq_now_ns() - a;
  }
  t1 = emq_now_ns();
  emq_bench_process_sample(&out->after);

  out->elapsed_ns = t1 - t0;
  out->ops_per_sec =
      out->elapsed_ns ? ((double)ops * 1e9) / (double)out->elapsed_ns : 0;
  emq_bench_sort_u64(lat, ops);
  out->p50_ns = emq_bench_percentile(lat, ops, 0.50);
  out->p99_ns = emq_bench_percentile(lat, ops, 0.99);
  out->p999_ns = emq_bench_percentile(lat, ops, 0.999);
  out->p9999_ns = emq_bench_percentile(lat, ops, 0.9999);
  out->ok = 1;

  baseline_destroy(&q);
  free(buf);
  free(lat);
}

int main(int argc, char **argv) {
  size_t ops = 100000;
  size_t payload = 64;
  emq_bench_cell emq_r, base_r;
  int i;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--ops") == 0 && i + 1 < argc)
      ops = (size_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc)
      payload = (size_t)atoi(argv[++i]);
  }

  printf("EmbeddedMQ compare: FAST SPSC vs mutex+linked-list\n");
  printf("ops=%zu payload=%zu\n\n", ops, payload);
  emq_bench_print_header(stdout);

  run_emq(ops, payload, &emq_r);
  printf("emq     ");
  emq_bench_print_cell(stdout, &emq_r);

  run_baseline(ops, payload, &base_r);
  printf("mutex   ");
  emq_bench_print_cell(stdout, &base_r);

  if (emq_r.ok && base_r.ok && base_r.ops_per_sec > 0) {
    printf("\nSpeedup vs mutex baseline: %.2fx\n",
           emq_r.ops_per_sec / base_r.ops_per_sec);
  }
  printf("\nWhere we win: in-process SPSC/MPSC messaging, many queues, "
         "pub/sub fanout without a broker.\n");
  printf("Where we don't: networked multi-host brokers, SQL query engines, "
         "exactly-once distributed transactions.\n");
  return (emq_r.ok && base_r.ok) ? 0 : 1;
}
