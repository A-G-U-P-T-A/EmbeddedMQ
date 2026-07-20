#include "emq_test.h"
#include "emq/emq.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

static void test_lifo(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message message;

  EMQ_CHECK_EQ(emq_runtime_create(&rt), EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.policy = EMQ_POLICY_LIFO;
  EMQ_CHECK_EQ(emq_queue_create(rt, "lifo", &opts, &q), EMQ_OK);
  EMQ_CHECK_EQ(emq_push(q, "a", 1, NULL), EMQ_OK);
  EMQ_CHECK_EQ(emq_push(q, "b", 1, NULL), EMQ_OK);
  EMQ_CHECK_EQ(emq_push(q, "c", 1, NULL), EMQ_OK);
  EMQ_CHECK_EQ(emq_pop(q, &message, 0), EMQ_OK);
  EMQ_CHECK(((const char *)message.data)[0] == 'c');
  emq_message_release(&message);
  EMQ_CHECK_EQ(emq_pop(q, &message, 0), EMQ_OK);
  EMQ_CHECK(((const char *)message.data)[0] == 'b');
  emq_message_release(&message);
  EMQ_CHECK_EQ(emq_pop(q, &message, 0), EMQ_OK);
  EMQ_CHECK(((const char *)message.data)[0] == 'a');
  emq_message_release(&message);
  EMQ_CHECK_EQ(emq_pop(q, &message, 0), EMQ_ERR_EMPTY);
  emq_queue_close(q);
  emq_runtime_destroy(rt);
}

static void test_ring(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message message;

  EMQ_CHECK_EQ(emq_runtime_create(&rt), EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.policy = EMQ_POLICY_RING;
  opts.ring_size = 2;
  EMQ_CHECK_EQ(emq_queue_create(rt, "ring", &opts, &q), EMQ_OK);
  EMQ_CHECK_EQ(emq_push(q, "a", 1, NULL), EMQ_OK);
  EMQ_CHECK_EQ(emq_push(q, "b", 1, NULL), EMQ_OK);
  EMQ_CHECK_EQ(emq_push(q, "c", 1, NULL), EMQ_OK);
  EMQ_CHECK_EQ(emq_pop(q, &message, 0), EMQ_OK);
  EMQ_CHECK(((const char *)message.data)[0] == 'b');
  emq_message_release(&message);
  EMQ_CHECK_EQ(emq_pop(q, &message, 0), EMQ_OK);
  EMQ_CHECK(((const char *)message.data)[0] == 'c');
  emq_message_release(&message);
  emq_queue_close(q);
  emq_runtime_destroy(rt);
}

static void test_ttl_and_delay(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message meta;
  emq_message message;
  emq_stats stats;

  EMQ_CHECK_EQ(emq_runtime_create(&rt), EMQ_OK);
  emq_queue_opts_default(&opts);
  memset(&meta, 0, sizeof(meta));
  meta.ttl_ns = 1000000ULL;
  EMQ_CHECK_EQ(emq_queue_create(rt, "ttl", &opts, &q), EMQ_OK);
  EMQ_CHECK_EQ(emq_push(q, "x", 1, &meta), EMQ_OK);
  emq_sleep_ms(3);
  EMQ_CHECK_EQ(emq_pop(q, &message, 0), EMQ_ERR_EMPTY);
  EMQ_CHECK_EQ(emq_queue_stats(q, &stats), EMQ_OK);
  EMQ_CHECK_EQ(stats.expired, 1u);
  emq_queue_close(q);
  emq_runtime_destroy(rt);

  rt = NULL;
  q = NULL;
  EMQ_CHECK_EQ(emq_runtime_create(&rt), EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.policy = EMQ_POLICY_DELAY;
  memset(&meta, 0, sizeof(meta));
  meta.deliver_at_ns = emq_now_ns() + 3000000ULL;
  EMQ_CHECK_EQ(emq_queue_create(rt, "delay", &opts, &q), EMQ_OK);
  EMQ_CHECK_EQ(emq_push(q, "d", 1, &meta), EMQ_OK);
  EMQ_CHECK_EQ(emq_try_pop(q, &message), EMQ_ERR_EMPTY);
  emq_sleep_ms(5);
  EMQ_CHECK_EQ(emq_try_pop(q, &message), EMQ_OK);
  emq_message_release(&message);
  emq_queue_close(q);
  emq_runtime_destroy(rt);
}

int main(void) {
  test_lifo();
  test_ring();
  test_ttl_and_delay();
  return emq_test_report();
}
