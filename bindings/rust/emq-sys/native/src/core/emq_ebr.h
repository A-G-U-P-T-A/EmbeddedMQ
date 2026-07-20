#ifndef EMQ_EBR_H
#define EMQ_EBR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_ebr_domain emq_ebr_domain;

typedef void (*emq_ebr_free_fn)(void *ptr, void *arg);

int emq_ebr_domain_create(emq_ebr_domain **out);
void emq_ebr_domain_destroy(emq_ebr_domain *domain);

int emq_ebr_register(emq_ebr_domain *domain, uint32_t *slot_out);
void emq_ebr_unregister(emq_ebr_domain *domain, uint32_t slot);

void emq_ebr_pin(emq_ebr_domain *domain, uint32_t slot);
void emq_ebr_unpin(emq_ebr_domain *domain, uint32_t slot);

void emq_ebr_retire(emq_ebr_domain *domain, void *ptr, emq_ebr_free_fn free_fn,
                    void *arg);
uint32_t emq_ebr_try_reclaim(emq_ebr_domain *domain);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_EBR_H */
