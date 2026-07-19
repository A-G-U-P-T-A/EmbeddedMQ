#include "emq_test.h"
#include "core/emq_ebr.h"

#include <stdlib.h>

static void ebr_free_cb(void *ptr, void *arg) {
  int *count = (int *)arg;
  free(ptr);
  if (count) (*count)++;
}

int main(void) {
  emq_ebr_domain *domain;
  uint32_t slot;
  int freed = 0;
  void *obj;

  EMQ_CHECK(emq_ebr_domain_create(&domain) == 0);
  EMQ_CHECK(emq_ebr_register(domain, &slot) == 0);

  emq_ebr_pin(domain, slot);
  obj = malloc(16);
  EMQ_CHECK(obj != NULL);
  emq_ebr_retire(domain, obj, ebr_free_cb, &freed);
  EMQ_CHECK_EQ(emq_ebr_try_reclaim(domain), 0u);

  emq_ebr_unpin(domain, slot);
  EMQ_CHECK(emq_ebr_try_reclaim(domain) >= 1u);
  EMQ_CHECK_EQ(freed, 1);

  emq_ebr_unregister(domain, slot);
  emq_ebr_domain_destroy(domain);
  return emq_test_report();
}
