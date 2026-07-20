#ifndef EMQ_DOMAIN_H
#define EMQ_DOMAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_domain {
  uint32_t id;
  void *pool;
  void *scheduler;
  uint32_t worker_count;
} emq_domain;

int emq_domain_init(emq_domain *d, uint32_t id);
void emq_domain_destroy(emq_domain *d);
emq_domain *emq_domain_local(void);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_DOMAIN_H */
