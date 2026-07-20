#ifndef EMQ_HOT_H
#define EMQ_HOT_H

#include "core/emq_atomic.h"
#include "platform/emq_platform.h"

#include <stdint.h>
#if !defined(_MSC_VER)
#  include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
  EMQ_HOT_FLAG_ACTIVE = 1u,
  EMQ_HOT_FLAG_LFQ = 2u,
  EMQ_HOT_FLAG_SPSC = 4u,
  EMQ_HOT_FLAG_CLOSED = 8u
};

/*
 * Hot control plane — two cache lines to separate producer vs consumer writes.
 * Contiguous arrays of these slots form the SoA-style hot plane keyed by id.
 */
typedef struct emq_hot_slot {
  /* Line 0: producer-dominated */
  EMQ_ALIGN_CACHE emq_atomic_u64 tail;
  uint32_t id;
  uint16_t policy;
  uint16_t flags;
  uint32_t band;
  uint32_t storage_ref;
  emq_atomic_u64 depth;
  uint64_t _pad0[3];
  /* Line 1: consumer-dominated */
  EMQ_ALIGN_CACHE emq_atomic_u64 head;
  uint64_t _pad1[7];
} emq_hot_slot;

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
