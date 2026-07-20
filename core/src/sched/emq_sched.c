#include "sched/emq_sched.h"
#include "core/emq_bitmap.h"
#include "core/emq_atomic.h"
#include "engine/emq_mpmc.h"
#include "platform/emq_platform.h"
#include "registry/emq_registry.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define EMQ_SCHED_READY_CAP 4096u
#define EMQ_SCHED_CREDIT_DEFAULT 64u
#define EMQ_SCHED_DRR_QUANTUM 1024

static const int32_t emq_sched_band_weights[EMQ_SCHED_NUM_BANDS] = {8, 4, 2, 1};

struct emq_sched {
  emq_mpmc *ready[EMQ_SCHED_NUM_BANDS];
  emq_bitmap active[EMQ_SCHED_NUM_BANDS];
  int32_t deficit[EMQ_SCHED_NUM_BANDS];
  uint32_t max_queues;
  volatile uint64_t wake_seq;
  emq_mutex *wait_mu;
  emq_atomic_u64 activations;
  emq_atomic_u64 wakeups;
  emq_atomic_u64 drained;
};

int emq_sched_init(emq_sched **out, uint32_t max_queues) {
  emq_sched *sched;
  uint32_t i;
  if (!out) return -1;
  if (max_queues == 0) max_queues = EMQ_MAX_QUEUES;
  sched = (emq_sched *)calloc(1, sizeof(*sched));
  if (!sched) return -2;
  sched->max_queues = max_queues;
  sched->wait_mu = emq_mutex_create();
  if (!sched->wait_mu) {
    free(sched);
    return -2;
  }
  for (i = 0; i < EMQ_SCHED_NUM_BANDS; ++i) {
    if (emq_mpmc_create(&sched->ready[i], EMQ_SCHED_READY_CAP) != 0) {
      emq_sched_destroy(sched);
      return -2;
    }
    if (emq_bitmap_init(&sched->active[i], max_queues) != 0) {
      emq_sched_destroy(sched);
      return -2;
    }
    sched->deficit[i] = 0;
  }
  sched->wake_seq = 0;
  emq_atomic_init_u64(&sched->activations, 0);
  emq_atomic_init_u64(&sched->wakeups, 0);
  emq_atomic_init_u64(&sched->drained, 0);
  *out = sched;
  return 0;
}

void emq_sched_destroy(emq_sched *sched) {
  uint32_t i;
  if (!sched) return;
  for (i = 0; i < EMQ_SCHED_NUM_BANDS; ++i) {
    emq_mpmc_destroy(sched->ready[i]);
    emq_bitmap_destroy(&sched->active[i]);
  }
  emq_mutex_destroy(sched->wait_mu);
  free(sched);
}

int emq_sched_activate(emq_sched *sched, uint32_t queue_id, uint32_t band) {
  if (!sched || band >= EMQ_SCHED_NUM_BANDS || queue_id >= sched->max_queues) {
    return -1;
  }
  if (emq_bitmap_set(&sched->active[band], queue_id)) {
    if (emq_mpmc_push(sched->ready[band],
                      (void *)(uintptr_t)(queue_id + 1u)) != 0) {
      emq_bitmap_clear(&sched->active[band], queue_id);
      return -4;
    }
    (void)emq_atomic_fetch_add_u64(&sched->activations, 1);
    emq_sched_wake(sched);
  }
  return 0;
}

int emq_sched_deactivate(emq_sched *sched, uint32_t queue_id, uint32_t band) {
  if (!sched || band >= EMQ_SCHED_NUM_BANDS || queue_id >= sched->max_queues) {
    return -1;
  }
  (void)emq_bitmap_clear(&sched->active[band], queue_id);
  return 0;
}

static int emq_sched_pop_band(emq_sched *sched, uint32_t band,
                              uint32_t *queue_id_out) {
  void *item = NULL;
  uint32_t qid;
  if (emq_mpmc_pop(sched->ready[band], &item) != 0) return -5;
  qid = (uint32_t)((uintptr_t)item - 1u);
  if (!emq_bitmap_test(&sched->active[band], qid)) return -5;
  *queue_id_out = qid;
  return 0;
}

int emq_sched_pop_ready(emq_sched *sched, uint32_t *queue_id_out,
                        uint32_t *credit_out) {
  uint32_t band;
  if (!sched || !queue_id_out || !credit_out) return -1;

  /* Strict priority across bands (0 = highest). Deficit-weighted credit
   * scales how much work a band may claim when selected. */
  for (band = 0; band < EMQ_SCHED_NUM_BANDS; ++band) {
    if (emq_mpmc_size(sched->ready[band]) == 0) continue;
    if (emq_sched_pop_band(sched, band, queue_id_out) != 0) continue;
    sched->deficit[band] += emq_sched_band_weights[band];
    *credit_out = EMQ_SCHED_CREDIT_DEFAULT;
    (void)emq_atomic_fetch_add_u64(&sched->drained, 1);
    (void)emq_bitmap_clear(&sched->active[band], *queue_id_out);
    return 0;
  }
  return -5;
}

int emq_sched_wait(emq_sched *sched, uint32_t timeout_ms) {
  uint64_t expect;
  if (!sched) return -1;
  emq_mutex_lock(sched->wait_mu);
  expect = sched->wake_seq;
  emq_mutex_unlock(sched->wait_mu);
  return emq_wait_u64(&sched->wake_seq, expect, timeout_ms);
}

void emq_sched_wake(emq_sched *sched) {
  if (!sched) return;
  emq_mutex_lock(sched->wait_mu);
  sched->wake_seq++;
  emq_mutex_unlock(sched->wait_mu);
  (void)emq_atomic_fetch_add_u64(&sched->wakeups, 1);
  emq_wake_u64(&sched->wake_seq, 1);
}

void emq_sched_get_stats(const emq_sched *sched, emq_sched_stats *out) {
  if (!sched || !out) return;
  memset(out, 0, sizeof(*out));
  out->activations = emq_atomic_load_u64(&sched->activations);
  out->wakeups = emq_atomic_load_u64(&sched->wakeups);
  out->drained = emq_atomic_load_u64(&sched->drained);
}
