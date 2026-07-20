#include "engine/emq_mpmc.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#  include <intrin.h>
typedef volatile __int64 emq_atomic_u64;

static void emq_atomic_init_u64(emq_atomic_u64 *p, uint64_t value) {
  (void)_InterlockedExchange64(p, (__int64)value);
}

static uint64_t emq_atomic_load_relaxed(const emq_atomic_u64 *p) {
  return (uint64_t)_InterlockedCompareExchange64(
      (volatile __int64 *)p, (__int64)0, (__int64)0);
}

static uint64_t emq_atomic_load_acquire(const emq_atomic_u64 *p) {
  return emq_atomic_load_relaxed(p);
}

static void emq_atomic_store_relaxed(emq_atomic_u64 *p, uint64_t value) {
  (void)_InterlockedExchange64(p, (__int64)value);
}

static void emq_atomic_store_release(emq_atomic_u64 *p, uint64_t value) {
  emq_atomic_store_relaxed(p, value);
}

static int emq_atomic_cas_relaxed(emq_atomic_u64 *p,
                                  uint64_t *expected,
                                  uint64_t desired) {
  __int64 old = _InterlockedCompareExchange64(
      p, (__int64)desired, (__int64)*expected);
  if ((uint64_t)old == *expected) return 1;
  *expected = (uint64_t)old;
  return 0;
}
#else
#  include <stdatomic.h>
typedef _Atomic uint64_t emq_atomic_u64;

static void emq_atomic_init_u64(emq_atomic_u64 *p, uint64_t value) {
  atomic_init(p, value);
}

static uint64_t emq_atomic_load_relaxed(const emq_atomic_u64 *p) {
  return atomic_load_explicit(p, memory_order_relaxed);
}

static uint64_t emq_atomic_load_acquire(const emq_atomic_u64 *p) {
  return atomic_load_explicit(p, memory_order_acquire);
}

static void emq_atomic_store_relaxed(emq_atomic_u64 *p, uint64_t value) {
  atomic_store_explicit(p, value, memory_order_relaxed);
}

static void emq_atomic_store_release(emq_atomic_u64 *p, uint64_t value) {
  atomic_store_explicit(p, value, memory_order_release);
}

static int emq_atomic_cas_relaxed(emq_atomic_u64 *p,
                                  uint64_t *expected,
                                  uint64_t desired) {
  return atomic_compare_exchange_weak_explicit(
      p, expected, desired, memory_order_relaxed, memory_order_relaxed);
}
#endif

#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4324) /* intentional cache-line padding */
#endif

typedef struct emq_mpmc_slot {
  EMQ_ALIGN_CACHE emq_atomic_u64 seq;
  void *item;
} emq_mpmc_slot;

struct emq_mpmc {
  uint32_t capacity;
  uint32_t mask;
  emq_mpmc_slot *slots;
  EMQ_ALIGN_CACHE emq_atomic_u64 enqueue_pos;
  EMQ_ALIGN_CACHE emq_atomic_u64 dequeue_pos;
};

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

int emq_mpmc_create(emq_mpmc **out, uint32_t capacity_pow2) {
  emq_mpmc *q;
  uint32_t i;
  uint32_t cap = capacity_pow2;
  if (!out) return -1;
  if (cap < 2) cap = 2;
  if (cap > 0x80000000u) return -1;
  /* round up to power of two */
  {
    uint32_t c = 1;
    while (c < cap) c <<= 1;
    cap = c;
  }
  q = (emq_mpmc *)calloc(1, sizeof(*q));
  if (!q) return -2;
  q->capacity = cap;
  q->mask = cap - 1u;
  if ((size_t)cap > SIZE_MAX / sizeof(emq_mpmc_slot)) {
    free(q);
    return -2;
  }
  q->slots = (emq_mpmc_slot *)emq_aligned_alloc(EMQ_CACHE_LINE, sizeof(emq_mpmc_slot) * cap);
  if (!q->slots) {
    emq_mpmc_destroy(q);
    return -2;
  }
  memset(q->slots, 0, sizeof(emq_mpmc_slot) * cap);
  for (i = 0; i < cap; ++i) {
    emq_atomic_init_u64(&q->slots[i].seq, (uint64_t)i);
  }
  emq_atomic_init_u64(&q->enqueue_pos, 0);
  emq_atomic_init_u64(&q->dequeue_pos, 0);
  *out = q;
  return 0;
}

void emq_mpmc_destroy(emq_mpmc *q) {
  if (!q) return;
  emq_aligned_free(q->slots);
  free(q);
}

int emq_mpmc_push(emq_mpmc *q, void *item) {
  emq_mpmc_slot *slot;
  uint64_t pos;
  if (!q) return -1;

  pos = emq_atomic_load_relaxed(&q->enqueue_pos);
  for (;;) {
    uint64_t seq;
    int64_t diff;
    slot = &q->slots[(uint32_t)pos & q->mask];
    seq = emq_atomic_load_acquire(&slot->seq);
    diff = (int64_t)(seq - pos);
    if (diff == 0) {
      uint64_t expected = pos;
      if (emq_atomic_cas_relaxed(&q->enqueue_pos, &expected, pos + 1u)) {
        break;
      }
      pos = expected;
    } else if (diff < 0) {
      return -4;
    } else {
      pos = emq_atomic_load_relaxed(&q->enqueue_pos);
    }
  }

  slot->item = item;
  emq_atomic_store_release(&slot->seq, pos + 1u);
  return 0;
}

int emq_mpmc_pop(emq_mpmc *q, void **out) {
  emq_mpmc_slot *slot;
  uint64_t pos;
  if (!q || !out) return -1;

  pos = emq_atomic_load_relaxed(&q->dequeue_pos);
  for (;;) {
    uint64_t seq;
    int64_t diff;
    slot = &q->slots[(uint32_t)pos & q->mask];
    seq = emq_atomic_load_acquire(&slot->seq);
    diff = (int64_t)(seq - (pos + 1u));
    if (diff == 0) {
      uint64_t expected = pos;
      if (emq_atomic_cas_relaxed(&q->dequeue_pos, &expected, pos + 1u)) {
        break;
      }
      pos = expected;
    } else if (diff < 0) {
      return -5;
    } else {
      pos = emq_atomic_load_relaxed(&q->dequeue_pos);
    }
  }

  *out = slot->item;
  slot->item = NULL;
  emq_atomic_store_release(&slot->seq, pos + (uint64_t)q->capacity);
  return 0;
}

uint32_t emq_mpmc_size(const emq_mpmc *q) {
  uint64_t head;
  uint64_t tail;
  uint64_t size;
  if (!q) return 0;
  tail = emq_atomic_load_acquire(&q->enqueue_pos);
  head = emq_atomic_load_acquire(&q->dequeue_pos);
  if (tail <= head) return 0;
  size = tail - head;
  if (size > q->capacity) size = q->capacity;
  return (uint32_t)size;
}
