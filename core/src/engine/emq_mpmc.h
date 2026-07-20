#ifndef EMQ_MPMC_H
#define EMQ_MPMC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_mpmc emq_mpmc;

int emq_mpmc_create(emq_mpmc **out, uint32_t capacity_pow2);
void emq_mpmc_destroy(emq_mpmc *q);
int emq_mpmc_push(emq_mpmc *q, void *item);
int emq_mpmc_pop(emq_mpmc *q, void **out);
uint32_t emq_mpmc_size(const emq_mpmc *q);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_MPMC_H */
