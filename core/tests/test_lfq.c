#include "emq_test.h"
#include "core/emq_lfq.h"

#include <string.h>

int main(void) {
  emq_lfq *q;
  char buf[64];
  uint32_t len = 0;
  uint64_t msg_id = 0;
  uint32_t flags = 0;
  uint32_t priority = 0;

  EMQ_CHECK(emq_lfq_create(&q, 4096, 1) == 0);
  EMQ_CHECK(emq_lfq_try_pop(q, buf, sizeof(buf), &len, &msg_id, &flags,
                            &priority) == -5);

  EMQ_CHECK(emq_lfq_try_push(q, "hello", 5, 42, 7, 3) == 0);
  EMQ_CHECK(emq_lfq_try_pop(q, buf, sizeof(buf), &len, &msg_id, &flags,
                            &priority) == 0);
  EMQ_CHECK_EQ(len, 5u);
  EMQ_CHECK_EQ(msg_id, 42ull);
  EMQ_CHECK_EQ(flags, 7u);
  EMQ_CHECK_EQ(priority, 3u);
  EMQ_CHECK(memcmp(buf, "hello", 5) == 0);

  EMQ_CHECK(emq_lfq_depth_approx(q) == 0u);
  emq_lfq_destroy(q);
  return emq_test_report();
}
