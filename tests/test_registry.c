#include "emq_test.h"
#include "registry/emq_registry.h"

#include <string.h>

int main(void) {
  emq_registry r;
  emq_queue_opts opts;
  emq_queue_desc *d = NULL;

  memset(&opts, 0, sizeof(opts));
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;

  EMQ_CHECK(emq_registry_init(&r, 16) == 0);
  EMQ_CHECK(emq_registry_create(&r, "a", &opts, &d) == 0);
  EMQ_CHECK(d != NULL);
  EMQ_CHECK(emq_registry_find(&r, "a") == d);
  EMQ_CHECK(emq_registry_create(&r, "a", &opts, &d) == -8);
  emq_registry_set_active(&r, d->id, 1);
  EMQ_CHECK(emq_registry_is_active(&r, d->id) == 1);
  EMQ_CHECK(emq_registry_remove(&r, "a") == 0);
  EMQ_CHECK(emq_registry_find(&r, "a") == NULL);
  emq_registry_destroy(&r);
  return emq_test_report();
}
