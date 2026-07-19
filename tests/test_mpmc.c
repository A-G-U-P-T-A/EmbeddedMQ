#include "emq_test.h"
#include "engine/emq_mpmc.h"
#include "platform/emq_platform.h"

#include <stdint.h>
#include <stdlib.h>

#define PRODUCERS 4
#define CONSUMERS 4
#define ITEMS_PER_PRODUCER 5000

typedef struct mpmc_state {
  emq_mpmc *queue;
  emq_mutex *mu;
  uint32_t producers_done;
  uint64_t consumed;
  uint64_t sum;
} mpmc_state;

typedef struct producer_arg {
  mpmc_state *state;
  uint32_t producer;
} producer_arg;

static void producer_main(void *arg) {
  producer_arg *pa = (producer_arg *)arg;
  uint32_t i;
  for (i = 0; i < ITEMS_PER_PRODUCER; ++i) {
    uintptr_t value = (uintptr_t)pa->producer * ITEMS_PER_PRODUCER + i + 1u;
    while (emq_mpmc_push(pa->state->queue, (void *)value) == -4) {
      emq_sleep_ms(0);
    }
  }
  emq_mutex_lock(pa->state->mu);
  pa->state->producers_done++;
  emq_mutex_unlock(pa->state->mu);
}

static void consumer_main(void *arg) {
  mpmc_state *state = (mpmc_state *)arg;
  for (;;) {
    void *item = NULL;
    if (emq_mpmc_pop(state->queue, &item) == 0) {
      emq_mutex_lock(state->mu);
      state->consumed++;
      state->sum += (uint64_t)(uintptr_t)item;
      emq_mutex_unlock(state->mu);
      continue;
    }
    emq_mutex_lock(state->mu);
    if (state->producers_done == PRODUCERS &&
        state->consumed == (uint64_t)PRODUCERS * ITEMS_PER_PRODUCER) {
      emq_mutex_unlock(state->mu);
      break;
    }
    emq_mutex_unlock(state->mu);
    emq_sleep_ms(0);
  }
}

int main(void) {
  mpmc_state state;
  producer_arg pargs[PRODUCERS];
  emq_thread *producers[PRODUCERS] = {0};
  emq_thread *consumers[CONSUMERS] = {0};
  uint64_t total = (uint64_t)PRODUCERS * ITEMS_PER_PRODUCER;
  uint64_t expected_sum = total * (total + 1u) / 2u;
  uint32_t i;

  state.queue = NULL;
  state.mu = emq_mutex_create();
  state.producers_done = 0;
  state.consumed = 0;
  state.sum = 0;
  EMQ_CHECK(state.mu != NULL);
  EMQ_CHECK_EQ(emq_mpmc_create(&state.queue, 1024), 0);

  for (i = 0; i < CONSUMERS; ++i) {
    EMQ_CHECK_EQ(emq_thread_create(&consumers[i], consumer_main, &state), 0);
  }
  for (i = 0; i < PRODUCERS; ++i) {
    pargs[i].state = &state;
    pargs[i].producer = i;
    EMQ_CHECK_EQ(emq_thread_create(&producers[i], producer_main, &pargs[i]), 0);
  }
  for (i = 0; i < PRODUCERS; ++i) {
    emq_thread_join(producers[i]);
    emq_thread_destroy(producers[i]);
  }
  for (i = 0; i < CONSUMERS; ++i) {
    emq_thread_join(consumers[i]);
    emq_thread_destroy(consumers[i]);
  }

  EMQ_CHECK_EQ(state.consumed, total);
  EMQ_CHECK_EQ(state.sum, expected_sum);
  EMQ_CHECK_EQ(emq_mpmc_size(state.queue), 0u);
  emq_mpmc_destroy(state.queue);
  emq_mutex_destroy(state.mu);
  return emq_test_report();
}
