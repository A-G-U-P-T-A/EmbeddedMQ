#include "emq/emq.h"
#include "emq_testsupport.h"

#include <stdio.h>
#include <string.h>

#ifdef EMQ_FAULT_INJECT
#  include "core/emq_fault.h"
#endif

static int run_workload(emq_runtime *rt, int *nomem_seen) {
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message msg;
  emq_status st;
  int i;

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.delivery = EMQ_AT_MOST_ONCE;
  opts.capacity = 64;

  st = emq_queue_create(rt, "fault_alloc_q", &opts, &q);
  if (st == EMQ_ERR_NOMEM) {
    if (nomem_seen) *nomem_seen = 1;
    return 0;
  }
  EMQ_REQUIRE(st == EMQ_OK);

  for (i = 0; i < 32; ++i) {
    uint8_t buf[32];
    EMQ_REQUIRE(emq_payload_fill(buf, sizeof(buf), (uint64_t)i, 0) == 0);
    st = emq_push(q, buf, sizeof(buf), NULL);
    if (st == EMQ_ERR_NOMEM) {
      if (nomem_seen) *nomem_seen = 1;
      break;
    }
    EMQ_REQUIRE(st == EMQ_OK);
  }

  for (i = 0; i < 16; ++i) {
    st = emq_try_pop(q, &msg);
    if (st == EMQ_ERR_EMPTY) break;
    if (st == EMQ_ERR_NOMEM) {
      if (nomem_seen) *nomem_seen = 1;
      break;
    }
    EMQ_REQUIRE(st == EMQ_OK);
    {
      uint64_t seq;
      EMQ_REQUIRE(emq_payload_check(msg.data, msg.size, &seq, NULL) == 0);
    }
    emq_message_release(&msg);
  }

  emq_queue_close(q);
  return 0;
}

#ifndef EMQ_FAULT_INJECT
int main(void) {
  printf("SKIP fault_alloc: EMQ_FAULT_INJECT not enabled\n");
  return 0;
}
#else
int main(void) {
  emq_runtime *rt = NULL;
  int nomem_seen = 0;

  emq_fault_init();
  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);

  /* Fail every aligned_alloc/malloc probe after create so queue setup or
   * pop-path payload alloc must surface EMQ_ERR_NOMEM. */
  emq_fault_configure("malloc", EMQ_FAULT_EVERY_N, 1, 0);
  run_workload(rt, &nomem_seen);
  EMQ_REQUIRE(nomem_seen);

  emq_fault_reset();
  run_workload(rt, NULL);

  {
    emq_queue *q = NULL;
    emq_queue_opts opts;
    uint8_t buf[16];
    emq_message msg;

    emq_queue_opts_default(&opts);
    EMQ_REQUIRE(emq_queue_create(rt, "verify_q", &opts, &q) == EMQ_OK);
    EMQ_REQUIRE(emq_payload_fill(buf, sizeof(buf), 42, 0) == 0);
    EMQ_REQUIRE(emq_push(q, buf, sizeof(buf), NULL) == EMQ_OK);
    EMQ_REQUIRE(emq_pop(q, &msg, 0) == EMQ_OK);
    emq_message_release(&msg);
    emq_queue_close(q);
  }

  emq_runtime_destroy(rt);
  printf("PASS fault_alloc\n");
  return 0;
}
#endif
