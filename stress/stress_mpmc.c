/*
 * MPMC storm: multiple FAST FIFO queues, mixed producer/consumer threads.
 * Verifies message-count conservation at shutdown.
 */

#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mpmc_shared {
  emq_runtime *rt;
  emq_queue **queues;
  uint32_t n_queues;
  size_t payload_len;
  uint64_t ops_per_thread;
  uint64_t pushed;
  uint64_t popped;
  emq_mutex *mu;
  emq_watchdog *wd;
} mpmc_shared;

typedef struct mpmc_worker {
  mpmc_shared *shared;
  uint32_t tid;
  emq_rng rng;
} mpmc_worker;

static void worker_main(void *arg) {
  mpmc_worker *w = (mpmc_worker *)arg;
  mpmc_shared *s = w->shared;
  uint8_t *buf;
  uint64_t i;
  uint64_t local_seq = 0;

  buf = (uint8_t *)malloc(s->payload_len);
  EMQ_REQUIRE(buf != NULL);

  for (i = 0; i < s->ops_per_thread; ++i) {
    uint32_t qi = (uint32_t)emq_rng_bounded(&w->rng, s->n_queues);
    emq_queue *q = s->queues[qi];

    if (emq_rng_u32(&w->rng) & 1u) {
      EMQ_REQUIRE(emq_payload_fill(buf, s->payload_len, local_seq++,
                                   w->tid) == 0);
      while (emq_push(q, buf, s->payload_len, NULL) != EMQ_OK) {
        emq_sleep_ms(0);
      }
      emq_mutex_lock(s->mu);
      s->pushed++;
      emq_mutex_unlock(s->mu);
    } else {
      emq_message m;
      memset(&m, 0, sizeof(m));
      if (emq_try_pop(q, &m) == EMQ_OK) {
        uint64_t seq;
        uint32_t producer;
        EMQ_REQUIRE(emq_payload_check((const uint8_t *)m.data, m.size, &seq,
                                      &producer) == 0);
        (void)seq;
        (void)producer;
        emq_message_release(&m);
        emq_mutex_lock(s->mu);
        s->popped++;
        emq_mutex_unlock(s->mu);
      }
    }

    if (((i + 1) % 5000u) == 0) {
      emq_watchdog_heartbeat(s->wd);
    }
  }
  free(buf);
}

int main(int argc, char **argv) {
  emq_cli cfg;
  emq_runtime *rt = NULL;
  emq_queue_opts opts;
  mpmc_shared shared;
  mpmc_worker *workers = NULL;
  emq_thread **threads = NULL;
  emq_watchdog *wd = NULL;
  uint64_t seed;
  uint32_t i;
  uint64_t total_depth = 0;
  uint64_t drained = 0;

  emq_cli_defaults(&cfg);
  if (emq_cli_parse(&cfg, argc, argv) == 1) return 0;

  if (cfg.quick) {
    cfg.queues = 8;
    cfg.threads = 4;
    cfg.ops = 50000;
  } else {
    if (cfg.queues <= 1) cfg.queues = 32;
    if (cfg.threads <= 1) cfg.threads = 16;
    if (cfg.ops == 100000) cfg.ops = 2000000;
  }
  if (cfg.payload < EMQ_PAYLOAD_HDR) {
    cfg.payload = 64;
  }
  if (cfg.threads == 0) cfg.threads = 1;

  seed = emq_cli_seed_or_time(&cfg);
  printf("stress_mpmc seed=%llu ops=%llu queues=%u threads=%u payload=%u "
         "quick=%d\n",
         (unsigned long long)seed, (unsigned long long)cfg.ops, cfg.queues,
         cfg.threads, cfg.payload, cfg.quick);

  memset(&shared, 0, sizeof(shared));
  shared.n_queues = cfg.queues;
  shared.payload_len = cfg.payload;
  shared.ops_per_thread = cfg.ops / cfg.threads;
  if (shared.ops_per_thread == 0) {
    shared.ops_per_thread = 1;
  }

  wd = emq_watchdog_start(30, "stress_mpmc");
  EMQ_REQUIRE(wd != NULL);
  shared.wd = wd;
  shared.mu = emq_mutex_create();
  EMQ_REQUIRE(shared.mu != NULL);

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  shared.rt = rt;

  shared.queues = (emq_queue **)calloc(cfg.queues, sizeof(emq_queue *));
  workers = (mpmc_worker *)calloc(cfg.threads, sizeof(mpmc_worker));
  threads = (emq_thread **)calloc(cfg.threads, sizeof(emq_thread *));
  EMQ_REQUIRE(shared.queues != NULL && workers != NULL && threads != NULL);

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.producers = 4;
  opts.consumers = 4;
  opts.backpressure = EMQ_BP_MODE_BLOCK;
  opts.capacity = 131072;

  for (i = 0; i < cfg.queues; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "q%u", i);
    EMQ_REQUIRE(emq_queue_create(rt, name, &opts, &shared.queues[i]) == EMQ_OK);
  }

  for (i = 0; i < cfg.threads; ++i) {
    workers[i].shared = &shared;
    workers[i].tid = i;
    emq_rng_seed(&workers[i].rng, seed + (uint64_t)i * 0x9e3779b97f4a7c15ULL);
    EMQ_REQUIRE(emq_thread_create(&threads[i], worker_main, &workers[i]) == 0);
  }

  for (i = 0; i < cfg.threads; ++i) {
    emq_thread_join(threads[i]);
    emq_thread_destroy(threads[i]);
  }

  for (;;) {
    emq_message m;
    int got = 0;
    memset(&m, 0, sizeof(m));
    for (i = 0; i < cfg.queues; ++i) {
      if (emq_try_pop(shared.queues[i], &m) == EMQ_OK) {
        EMQ_REQUIRE(emq_payload_check((const uint8_t *)m.data, m.size, NULL,
                                      NULL) == 0);
        emq_message_release(&m);
        drained++;
        got = 1;
        break;
      }
    }
    if (!got) break;
  }

  for (i = 0; i < cfg.queues; ++i) {
    emq_stats st;
    EMQ_REQUIRE(emq_queue_stats(shared.queues[i], &st) == EMQ_OK);
    total_depth += st.depth;
  }

  EMQ_REQUIRE(shared.pushed == shared.popped + drained + total_depth);

  emq_watchdog_stop(wd);
  emq_mutex_destroy(shared.mu);
  for (i = 0; i < cfg.queues; ++i) {
    emq_queue_close(shared.queues[i]);
  }
  emq_runtime_destroy(rt);
  free(shared.queues);
  free(workers);
  free(threads);

  printf("stress_mpmc ok pushed=%llu popped=%llu drained=%llu depth=%llu\n",
         (unsigned long long)shared.pushed, (unsigned long long)shared.popped,
         (unsigned long long)drained, (unsigned long long)total_depth);
  return 0;
}
