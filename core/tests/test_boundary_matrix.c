#include "emq_test.h"
#include "emq/emq.h"
#include "emq/emq_testcase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(EMQ_COVERAGE_BUILD)
void emq_testcase_branch(int truth, const char *expr, const char *file, int line) {
  static volatile int emq_testcase_sink = 0;
  if (truth) {
    emq_testcase_sink ^=
        (int)(line ^ (int)(size_t)file ^ (int)(size_t)expr);
  }
  (void)expr;
  (void)file;
}
#endif

static const size_t k_payload_sizes[] = {0, 1, 255, 256, 257, 1024};
static const uint32_t k_capacities[] = {1, 2, 8};

static void fill_pattern(uint8_t *buf, size_t n, unsigned seed) {
  size_t i;
  for (i = 0; i < n; ++i) {
    buf[i] = (uint8_t)((seed + (unsigned)i) & 0xFFu);
  }
}

int main(void) {
  emq_runtime *rt = NULL;
  size_t pi, ci;

  EMQ_CHECK(emq_runtime_create(&rt) == EMQ_OK);

  for (ci = 0; ci < sizeof(k_capacities) / sizeof(k_capacities[0]); ++ci) {
    for (pi = 0; pi < sizeof(k_payload_sizes) / sizeof(k_payload_sizes[0]);
         ++pi) {
      emq_queue *q = NULL;
      emq_queue_opts opts;
      emq_message m;
      size_t sz = k_payload_sizes[pi];
      uint32_t cap = k_capacities[ci];
      uint8_t *payload = NULL;
      char name[32];

      emq_queue_opts_default(&opts);
      opts.storage = EMQ_STORAGE_FAST;
      opts.policy = EMQ_POLICY_FIFO;
      opts.capacity = cap;

      (void)snprintf(name, sizeof(name), "bm_%u_%u", cap, (unsigned)sz);
      EMQ_CHECK(emq_queue_create(rt, name, &opts, &q) == EMQ_OK);

      if (sz > 0) {
        payload = (uint8_t *)malloc(sz);
        EMQ_CHECK(payload != NULL);
        fill_pattern(payload, sz, (unsigned)(ci * 17u + pi));
      }

      EMQ_TESTCASE(sz <= EMQ_INLINE_PAYLOAD_MAX || sz > EMQ_INLINE_PAYLOAD_MAX);
      EMQ_CHECK(emq_push(q, payload, sz, NULL) == EMQ_OK);
      EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_OK);
      EMQ_CHECK(m.size == sz);
      if (sz > 0) {
        EMQ_CHECK(memcmp(m.data, payload, sz) == 0);
      }
      emq_message_release(&m);
      EMQ_CHECK(emq_pop(q, &m, 0) == EMQ_ERR_EMPTY);

      free(payload);
      emq_queue_close(q);
      EMQ_CHECK(emq_queue_destroy(rt, name) == EMQ_OK);
    }
  }

  emq_runtime_destroy(rt);
  return emq_test_report();
}
