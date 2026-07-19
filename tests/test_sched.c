#include "emq_test.h"
#include "sched/emq_sched.h"

int main(void) {
  emq_sched *sched;
  emq_sched_stats stats;
  uint32_t qid = 0;
  uint32_t credit = 0;

  EMQ_CHECK(emq_sched_init(&sched, 1024) == 0);
  EMQ_CHECK(emq_sched_pop_ready(sched, &qid, &credit) == -5);

  EMQ_CHECK(emq_sched_activate(sched, 10, 0) == 0);
  EMQ_CHECK(emq_sched_activate(sched, 10, 0) == 0);

  EMQ_CHECK(emq_sched_pop_ready(sched, &qid, &credit) == 0);
  EMQ_CHECK_EQ(qid, 10u);
  EMQ_CHECK_EQ(credit, 64u);

  EMQ_CHECK(emq_sched_activate(sched, 20, 3) == 0);
  EMQ_CHECK(emq_sched_activate(sched, 5, 0) == 0);
  EMQ_CHECK(emq_sched_pop_ready(sched, &qid, &credit) == 0);
  EMQ_CHECK_EQ(qid, 5u);

  emq_sched_get_stats(sched, &stats);
  EMQ_CHECK(stats.activations >= 3);
  EMQ_CHECK(stats.drained >= 2);

  emq_sched_destroy(sched);
  return emq_test_report();
}
