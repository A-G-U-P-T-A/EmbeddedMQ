#include "emq_test.h"
#include "emq/emq.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message m;
  uint64_t id;

  EMQ_CHECK(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.policy = EMQ_POLICY_WORK;
  opts.delivery = EMQ_AT_LEAST_ONCE;
  opts.visibility_ms = 10;
  EMQ_CHECK(emq_queue_create(rt, "work", &opts, &q) == EMQ_OK);
  EMQ_CHECK(emq_push(q, "job", 3, NULL) == EMQ_OK);
  EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_OK);
  id = m.id;
  EMQ_CHECK(m.size == 3);
  /* Do not free yet — inflight holds same pointer for work queue */
  EMQ_CHECK(emq_ack(q, id) == EMQ_OK);
  free((void *)m.data);
  EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_ERR_EMPTY);
  emq_queue_close(q);
  emq_runtime_destroy(rt);
  return emq_test_report();
}
