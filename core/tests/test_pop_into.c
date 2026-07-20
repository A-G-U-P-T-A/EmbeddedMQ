#include "emq_test.h"
#include "emq/emq.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message meta;
  uint8_t dst[64];
  uint8_t batch[64 * 4];
  size_t n = 0, count = 0, sizes[4];
  size_t pushed = 0;
  const char payload[] = "pop-into-payload-ok";
  uint8_t tiny[4];

  EMQ_CHECK(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.capacity = 64;
  opts.producers = 1;
  opts.consumers = 1;

  EMQ_CHECK(emq_queue_create(rt, "pop_into_q", &opts, &q) == EMQ_OK);

  /* Empty */
  EMQ_CHECK(emq_pop_into(q, dst, sizeof(dst), &n, NULL, 0) == EMQ_ERR_EMPTY);

  /* Scalar pop_into + meta */
  EMQ_CHECK(emq_push(q, payload, sizeof(payload), NULL) == EMQ_OK);
  memset(&meta, 0xff, sizeof(meta));
  EMQ_CHECK(emq_pop_into(q, dst, sizeof(dst), &n, &meta, 0) == EMQ_OK);
  EMQ_CHECK(n == sizeof(payload));
  EMQ_CHECK(memcmp(dst, payload, sizeof(payload)) == 0);
  EMQ_CHECK(meta.size == sizeof(payload));
  EMQ_CHECK(meta.data == NULL);
  EMQ_CHECK(meta.id != 0);

  /* Undersized buffer leaves message queued */
  EMQ_CHECK(emq_push(q, payload, sizeof(payload), NULL) == EMQ_OK);
  EMQ_CHECK(emq_pop_into(q, tiny, sizeof(tiny), &n, NULL, 0) ==
            EMQ_ERR_INVALID);
  EMQ_CHECK(emq_pop_into(q, dst, sizeof(dst), &n, NULL, 0) == EMQ_OK);
  EMQ_CHECK(n == sizeof(payload));

  /* push_n + pop_into_n */
  EMQ_CHECK(emq_push_n(q, payload, sizeof(payload), 4, &pushed) == EMQ_OK);
  EMQ_CHECK(pushed == 4);
  EMQ_CHECK(emq_pop_into_n(q, batch, 64, 4, &count, sizes, 0) == EMQ_OK);
  EMQ_CHECK(count == 4);
  EMQ_CHECK(sizes[0] == sizeof(payload) && sizes[3] == sizeof(payload));
  EMQ_CHECK(memcmp(batch, payload, sizeof(payload)) == 0);

  /* Partial batch */
  EMQ_CHECK(emq_push_n(q, payload, sizeof(payload), 2, &pushed) == EMQ_OK);
  EMQ_CHECK(emq_pop_into_n(q, batch, 64, 4, &count, NULL, 0) == EMQ_OK);
  EMQ_CHECK(count == 2);
  EMQ_CHECK(emq_pop_into_n(q, batch, 64, 4, &count, NULL, 0) == EMQ_ERR_EMPTY);

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  return emq_test_report();
}
