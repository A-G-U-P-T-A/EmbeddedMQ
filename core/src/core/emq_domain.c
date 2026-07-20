#include "core/emq_domain.h"

#include <stddef.h>
#include <string.h>

static emq_domain g_local_domain;
static int g_local_ready = 0;

int emq_domain_init(emq_domain *d, uint32_t id) {
  if (!d) {
    return -1;
  }
  memset(d, 0, sizeof(*d));
  d->id = id;
  d->worker_count = 1;
  return 0;
}

void emq_domain_destroy(emq_domain *d) {
  if (!d) {
    return;
  }
  d->pool = NULL;
  d->scheduler = NULL;
  d->worker_count = 0;
}

emq_domain *emq_domain_local(void) {
  if (!g_local_ready) {
    (void)emq_domain_init(&g_local_domain, 0);
    g_local_ready = 1;
  }
  return &g_local_domain;
}
