#ifndef EMQ_MEM_H
#define EMQ_MEM_H

#include <stdlib.h>

/*
 * Allocation wrappers. When EMQ_FAULT_INJECT is defined, malloc/calloc/
 * realloc can be forced to fail via the "malloc" fault point.
 */

#if defined(EMQ_FAULT_INJECT)
#  include "core/emq_fault.h"

static inline void *emq_malloc(size_t n) {
  if (EMQ_FAULT_CHECK("malloc")) return NULL;
  return malloc(n);
}
static inline void *emq_calloc(size_t nm, size_t sz) {
  if (EMQ_FAULT_CHECK("malloc")) return NULL;
  return calloc(nm, sz);
}
static inline void *emq_realloc(void *p, size_t n) {
  if (EMQ_FAULT_CHECK("malloc")) return NULL;
  return realloc(p, n);
}
static inline void emq_free(void *p) { free(p); }

#else

#  define emq_malloc(n) malloc(n)
#  define emq_calloc(n, s) calloc(n, s)
#  define emq_realloc(p, n) realloc(p, n)
#  define emq_free(p) free(p)

#endif

#endif /* EMQ_MEM_H */
