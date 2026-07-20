#ifndef EMQ_HOT_H
#define EMQ_HOT_H

#include "core/emq_atomic.h"

#include <stdint.h>
#if !defined(_MSC_VER)
#  include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Hot control plane fields (kept compact; padded toward one cache line). */
typedef struct emq_hot_slot {
  emq_atomic_u64 head;
  emq_atomic_u64 tail;
  uint32_t id;
  uint16_t policy;
  uint16_t flags;
  uint32_t band;
  uint32_t storage_ref;
  emq_atomic_u64 depth;
  uint64_t pad[2];
} emq_hot_slot;

enum {
  EMQ_HOT_FLAG_ACTIVE = 1u,
  EMQ_HOT_FLAG_LFQ = 2u,
  EMQ_HOT_FLAG_SPSC = 4u,
  EMQ_HOT_FLAG_CLOSED = 8u
};

/* Concurrent readers must use this — producers OR ACTIVE via atomics. */
static inline uint16_t emq_hot_flags_load(const emq_hot_slot *hot) {
  if (!hot) return 0;
#if defined(_MSC_VER)
  return *(volatile const uint16_t *)&hot->flags;
#else
  return atomic_load_explicit((_Atomic uint16_t *)(void *)&hot->flags,
                              memory_order_relaxed);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* EMQ_HOT_H */
