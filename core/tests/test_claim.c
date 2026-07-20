#include "emq_test.h"
#include "emq/emq.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message m;
  const char payload[] = "claim-me";

  EMQ_CHECK(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.capacity = 8;

  EMQ_CHECK(emq_queue_create(rt, "claim_q", &opts, &q) == EMQ_OK);
  EMQ_CHECK(emq_push(q, payload, sizeof(payload), NULL) == EMQ_OK);

  EMQ_CHECK(emq_claim(q, &m, 0) == EMQ_OK);
  EMQ_CHECK((m.flags & EMQ_MSG_FLAG_CLAIMED) != 0);
  EMQ_CHECK(m.size == sizeof(payload));
  EMQ_CHECK(m.data != NULL);
  EMQ_CHECK(memcmp(m.data, payload, sizeof(payload)) == 0);

  EMQ_CHECK(emq_release_claim(q, &m) == EMQ_OK);
  EMQ_CHECK(m.data == NULL && m.size == 0);

  EMQ_CHECK(emq_claim(q, &m, 0) == EMQ_ERR_EMPTY);
  EMQ_CHECK(emq_try_pop(q, &m) == EMQ_ERR_EMPTY);

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  return emq_test_report();
}
