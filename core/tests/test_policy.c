#include "emq_test.h"
#include "policy/emq_policy.h"

#include <string.h>

static void test_policy_from_opts(void) {
  emq_queue_opts opts;
  emq_policy p;

  memset(&opts, 0, sizeof(opts));
  opts.policy = EMQ_POLICY_LIFO;
  opts.delivery = EMQ_AT_LEAST_ONCE;
  opts.storage = EMQ_STORAGE_FAST;
  opts.capacity = 100;

  p = emq_policy_from_queue_opts(&opts);
  EMQ_CHECK_EQ((int)p.ordering, (int)EMQ_ORDER_LIFO);
  EMQ_CHECK_EQ((int)p.delivery, (int)EMQ_AT_LEAST_ONCE);
  EMQ_CHECK_EQ(p.capacity, 100u);
  EMQ_CHECK(p.high_watermark > 0);
}

static void test_policy_decide_full(void) {
  emq_policy p;

  memset(&p, 0, sizeof(p));
  p.backpressure = EMQ_BP_DROP_NEW;
  EMQ_CHECK_EQ(emq_policy_decide_full(&p, 5, 10), 0);
  EMQ_CHECK_EQ(emq_policy_decide_full(&p, 10, 10), EMQ_ERR_FULL);

  p.backpressure = EMQ_BP_DROP_OLD;
  EMQ_CHECK_EQ(emq_policy_decide_full(&p, 10, 10), 1);

  p.backpressure = EMQ_BP_BLOCK;
  EMQ_CHECK_EQ(emq_policy_decide_full(&p, 10, 10), EMQ_ERR_BUSY);

  p.backpressure = EMQ_BP_SPILL;
  EMQ_CHECK_EQ(emq_policy_decide_full(&p, 10, 10), 2);

  p.backpressure = EMQ_BP_EXPAND;
  EMQ_CHECK_EQ(emq_policy_decide_full(&p, 10, 10), 2);
}

static void test_policy_ops(void) {
  const emq_policy_ops *ops;

  ops = emq_policy_ops_get(EMQ_BP_DROP_NEW);
  EMQ_CHECK(ops != NULL);
  EMQ_CHECK_STREQ(ops->name, "drop_new");
  EMQ_CHECK_EQ(ops->on_full(NULL, EMQ_BP_DROP_NEW), EMQ_ERR_FULL);

  ops = emq_policy_ops_get(EMQ_BP_DROP_OLD);
  EMQ_CHECK_EQ(ops->on_full(NULL, EMQ_BP_DROP_OLD), 1);
}

int main(void) {
  test_policy_from_opts();
  test_policy_decide_full();
  test_policy_ops();
  return emq_test_report();
}
