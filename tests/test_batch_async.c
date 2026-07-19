#include "emq_test.h"
#include "emq/emq.h"
#include "platform/emq_platform.h"

#include <string.h>

typedef struct async_state {
  emq_mutex *mu;
  emq_cond *cond;
  int completed;
  int ok;
} async_state;

static void push_done(emq_status status, void *user) {
  async_state *state = (async_state *)user;
  emq_mutex_lock(state->mu);
  state->ok = state->ok && status == EMQ_OK;
  state->completed++;
  emq_cond_broadcast(state->cond);
  emq_mutex_unlock(state->mu);
}

static void pop_done(emq_status status, emq_message *message, void *user) {
  async_state *state = (async_state *)user;
  int valid = status == EMQ_OK && message && message->size == 5 &&
              memcmp(message->data, "async", 5) == 0;
  if (message) emq_message_release(message);
  emq_mutex_lock(state->mu);
  state->ok = state->ok && valid;
  state->completed++;
  emq_cond_broadcast(state->cond);
  emq_mutex_unlock(state->mu);
}

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_batch_item items[3];
  emq_message messages[3];
  size_t count = 0;
  async_state state;

  memset(&state, 0, sizeof(state));
  state.mu = emq_mutex_create();
  state.cond = emq_cond_create();
  state.ok = 1;
  EMQ_CHECK(state.mu != NULL && state.cond != NULL);

  EMQ_CHECK_EQ(emq_runtime_create(&rt), EMQ_OK);
  emq_queue_opts_default(&opts);
  EMQ_CHECK_EQ(emq_queue_create(rt, "batch", &opts, &q), EMQ_OK);

  memset(items, 0, sizeof(items));
  items[0].data = "a"; items[0].size = 1;
  items[1].data = "b"; items[1].size = 1;
  items[2].data = "c"; items[2].size = 1;
  EMQ_CHECK_EQ(emq_push_batch(q, items, 3, &count), EMQ_OK);
  EMQ_CHECK_EQ(count, 3u);
  EMQ_CHECK_EQ(emq_pop_batch(q, messages, 3, &count), EMQ_OK);
  EMQ_CHECK_EQ(count, 3u);
  EMQ_CHECK(((const char *)messages[0].data)[0] == 'a');
  EMQ_CHECK(((const char *)messages[1].data)[0] == 'b');
  EMQ_CHECK(((const char *)messages[2].data)[0] == 'c');
  emq_message_release(&messages[0]);
  emq_message_release(&messages[1]);
  emq_message_release(&messages[2]);

  EMQ_CHECK_EQ(emq_push_async(q, "async", 5, NULL, push_done, &state), EMQ_OK);
  EMQ_CHECK_EQ(emq_pop_async(q, 500, pop_done, &state), EMQ_OK);

  emq_mutex_lock(state.mu);
  while (state.completed < 2) {
    if (emq_cond_timedwait(state.cond, state.mu, 1000) != 0) break;
  }
  emq_mutex_unlock(state.mu);
  EMQ_CHECK_EQ(state.completed, 2);
  EMQ_CHECK(state.ok);

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  emq_cond_destroy(state.cond);
  emq_mutex_destroy(state.mu);
  return emq_test_report();
}
