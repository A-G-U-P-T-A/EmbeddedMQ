#include "emq_test.h"
#include "core/emq_hist.h"

int main(void) {
  emq_hist a, b, merged;

  emq_hist_init(&a);
  emq_hist_init(&b);
  emq_hist_init(&merged);

  emq_hist_record(&a, 0);
  emq_hist_record(&a, 1);
  emq_hist_record(&a, 2);
  emq_hist_record(&a, 4);
  emq_hist_record(&a, 100);

  emq_hist_record(&b, 8);
  emq_hist_merge(&merged, &a);
  emq_hist_merge(&merged, &b);

  EMQ_CHECK_EQ(merged.total_count, 6u);
  EMQ_CHECK(merged.total_ns > 0);
  EMQ_CHECK(emq_hist_percentile(&merged, 50.0) > 0);

  emq_hist_reset(&a);
  EMQ_CHECK_EQ(a.total_count, 0u);

  return emq_test_report();
}
