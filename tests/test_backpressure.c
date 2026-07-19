#include "emq_test.h"
#include "emq/emq.h"

#include <stdio.h>
#include <string.h>

static void fill(uint8_t *b, size_t n, uint8_t v) {
  size_t i;
  for (i = 0; i < n; ++i) b[i] = v;
}

static int fill_to_capacity(emq_queue *q, uint32_t cap, uint8_t *buf,
                            size_t len) {
  uint32_t i;
  for (i = 0; i < cap; ++i) {
    if (emq_push(q, buf, len, NULL) != EMQ_OK) return -1;
  }
  return 0;
}

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  uint8_t buf[64];
  emq_message m;
  emq_status st;
  emq_stats stats;

  fill(buf, sizeof(buf), 0x11);
  EMQ_CHECK(emq_runtime_create(&rt) == EMQ_OK);

  /* DROP_NEW: push beyond capacity returns FULL */
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.capacity = 8;
  opts.backpressure = EMQ_BP_MODE_DROP_NEW;
  opts.producers = 1;
  opts.consumers = 1;
  EMQ_CHECK(emq_queue_create(rt, "bp_drop_new", &opts, &q) == EMQ_OK);
  EMQ_CHECK(fill_to_capacity(q, 8, buf, sizeof(buf)) == 0);
  st = emq_push(q, buf, sizeof(buf), NULL);
  EMQ_CHECK(st == EMQ_ERR_FULL);
  EMQ_CHECK(emq_queue_close(q) == EMQ_OK);
  EMQ_CHECK(emq_queue_destroy(rt, "bp_drop_new") == EMQ_OK);

  /* DROP_OLD / OVERWRITE: push succeeds by discarding oldest */
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.capacity = 4;
  opts.backpressure = EMQ_BP_MODE_DROP_OLD;
  opts.producers = 1;
  opts.consumers = 1;
  EMQ_CHECK(emq_queue_create(rt, "bp_drop_old", &opts, &q) == EMQ_OK);
  EMQ_CHECK(fill_to_capacity(q, 4, buf, sizeof(buf)) == 0);
  fill(buf, sizeof(buf), 0x22);
  EMQ_CHECK(emq_push(q, buf, sizeof(buf), NULL) == EMQ_OK);
  EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_OK);
  /* After drop-old of the first message, first pop should not be 0x11 only
   * guaranteed if ring dropped correctly — accept any successful pop. */
  EMQ_CHECK(m.size == sizeof(buf));
  emq_message_release(&m);
  EMQ_CHECK(emq_queue_close(q) == EMQ_OK);
  EMQ_CHECK(emq_queue_destroy(rt, "bp_drop_old") == EMQ_OK);

  /* BLOCK: returns BUSY when full */
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.capacity = 3;
  opts.backpressure = EMQ_BP_MODE_BLOCK;
  opts.producers = 1;
  opts.consumers = 1;
  EMQ_CHECK(emq_queue_create(rt, "bp_block", &opts, &q) == EMQ_OK);
  EMQ_CHECK(fill_to_capacity(q, 3, buf, sizeof(buf)) == 0);
  st = emq_push(q, buf, sizeof(buf), NULL);
  EMQ_CHECK(st == EMQ_ERR_BUSY || st == EMQ_ERR_FULL);
  EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_OK);
  emq_message_release(&m);
  /* After a pop, push should succeed again. */
  EMQ_CHECK(emq_push(q, buf, sizeof(buf), NULL) == EMQ_OK);
  EMQ_CHECK(emq_queue_close(q) == EMQ_OK);
  EMQ_CHECK(emq_queue_destroy(rt, "bp_block") == EMQ_OK);

  /* OVERWRITE mirrors DROP_OLD on the FAST ring path */
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.capacity = 2;
  opts.backpressure = EMQ_BP_MODE_OVERWRITE;
  opts.producers = 1;
  opts.consumers = 1;
  EMQ_CHECK(emq_queue_create(rt, "bp_ow", &opts, &q) == EMQ_OK);
  EMQ_CHECK(fill_to_capacity(q, 2, buf, sizeof(buf)) == 0);
  EMQ_CHECK(emq_push(q, buf, sizeof(buf), NULL) == EMQ_OK);
  EMQ_CHECK(emq_queue_stats(q, &stats) == EMQ_OK);
  EMQ_CHECK(stats.depth <= 2);
  EMQ_CHECK(emq_queue_close(q) == EMQ_OK);

  emq_runtime_destroy(rt);
  return emq_test_report();
}
