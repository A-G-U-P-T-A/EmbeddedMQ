#include "emq_test.h"
#include "emq/emq.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message m;
  emq_stats st;

  EMQ_CHECK(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.policy = EMQ_POLICY_FIFO;
  EMQ_CHECK(emq_queue_create(rt, "q1", &opts, &q) == EMQ_OK);
  EMQ_CHECK(emq_push(q, "a", 1, NULL) == EMQ_OK);
  EMQ_CHECK(emq_push(q, "b", 1, NULL) == EMQ_OK);
  EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_OK);
  EMQ_CHECK(m.size == 1 && ((const char *)m.data)[0] == 'a');
  free((void *)m.data);
  EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_OK);
  EMQ_CHECK(((const char *)m.data)[0] == 'b');
  free((void *)m.data);
  EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_ERR_EMPTY);
  EMQ_CHECK(emq_queue_stats(q, &st) == EMQ_OK);
  EMQ_CHECK_EQ(st.enqueued, 2u);
  EMQ_CHECK_EQ(st.dequeued, 2u);
  emq_queue_close(q);
  emq_runtime_destroy(rt);
  return emq_test_report();
}
