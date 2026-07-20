#include "emq_test.h"
#include "emq/emq.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_subscription *sub = NULL;
  emq_queue_opts opts;
  emq_message m;

  EMQ_CHECK(emq_runtime_create_ex(&rt, 1) == EMQ_OK);
  EMQ_CHECK(emq_runtime_start(rt) == EMQ_OK);

  emq_queue_opts_default(&opts);
  opts.policy = EMQ_POLICY_PRIORITY;
  EMQ_CHECK(emq_queue_create(rt, "prio", &opts, &q) == EMQ_OK);
  {
    emq_message meta;
    memset(&meta, 0, sizeof(meta));
    meta.priority = 1;
    EMQ_CHECK(emq_push(q, "lo", 2, &meta) == EMQ_OK);
    meta.priority = 10;
    EMQ_CHECK(emq_push(q, "hi", 2, &meta) == EMQ_OK);
  }
  EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_OK);
  EMQ_CHECK(m.size == 2 && memcmp(m.data, "hi", 2) == 0);
  free((void *)m.data);

  EMQ_CHECK(emq_subscribe(rt, "orders.#", "g1", &sub) == EMQ_OK);
  EMQ_CHECK(emq_publish(rt, "orders.eu.new", "z", 1) == EMQ_OK);
  EMQ_CHECK(emq_sub_next(sub, &m, 100) == EMQ_OK);
  EMQ_CHECK(m.size == 1);
  emq_message_release(&m);
  EMQ_CHECK(emq_unsubscribe(sub) == EMQ_OK);

  emq_queue_close(q);
  EMQ_CHECK(emq_runtime_stop(rt) == EMQ_OK);
  emq_runtime_destroy(rt);
  return emq_test_report();
}
