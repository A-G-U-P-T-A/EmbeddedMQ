#ifndef EMQ_ATOMIC_H
#define EMQ_ATOMIC_H

#include <stdint.h>

#if defined(_MSC_VER)
#  include <intrin.h>
typedef volatile long emq_atomic_i32;
typedef volatile __int64 emq_atomic_u64;

static void emq_atomic_init_i32(emq_atomic_i32 *p, int32_t v) {
  (void)_InterlockedExchange(p, (long)v);
}
static int32_t emq_atomic_load_i32(const emq_atomic_i32 *p) {
  /* Aligned 32-bit volatile reads are atomic on x64/ARM64; avoid the
   * LOCK cmpxchg an interlocked "load" would cost on the hot path. */
  return (int32_t)*p;
}
static void emq_atomic_store_i32(emq_atomic_i32 *p, int32_t v) {
  (void)_InterlockedExchange(p, (long)v);
}
static int32_t emq_atomic_fetch_add_i32(emq_atomic_i32 *p, int32_t v) {
  return (int32_t)_InterlockedExchangeAdd(p, (long)v);
}
static int emq_atomic_cas_i32(emq_atomic_i32 *p, int32_t *expected,
                              int32_t desired) {
  long old = _InterlockedCompareExchange(p, (long)desired, (long)*expected);
  if ((int32_t)old == *expected) return 1;
  *expected = (int32_t)old;
  return 0;
}

static void emq_atomic_init_u64(emq_atomic_u64 *p, uint64_t v) {
  (void)_InterlockedExchange64(p, (__int64)v);
}
static uint64_t emq_atomic_load_u64(const emq_atomic_u64 *p) {
  /* Aligned 64-bit volatile reads are atomic on x64/ARM64; avoid the
   * LOCK cmpxchg an interlocked "load" would cost on the hot path. */
  return (uint64_t)*p;
}
static uint64_t emq_atomic_load_acquire_u64(const emq_atomic_u64 *p) {
  uint64_t v = (uint64_t)*p;
  _ReadWriteBarrier(); /* compiler fence; x86 loads carry acquire order */
  return v;
}
static void emq_atomic_store_u64(emq_atomic_u64 *p, uint64_t v) {
  (void)_InterlockedExchange64(p, (__int64)v);
}
static void emq_atomic_store_release_u64(emq_atomic_u64 *p, uint64_t v) {
  emq_atomic_store_u64(p, v);
}
static uint64_t emq_atomic_fetch_add_u64(emq_atomic_u64 *p, uint64_t v) {
  return (uint64_t)_InterlockedExchangeAdd64(p, (__int64)v);
}
static int emq_atomic_cas_u64(emq_atomic_u64 *p, uint64_t *expected,
                              uint64_t desired) {
  __int64 old =
      _InterlockedCompareExchange64(p, (__int64)desired, (__int64)*expected);
  if ((uint64_t)old == *expected) return 1;
  *expected = (uint64_t)old;
  return 0;
}

static unsigned emq_ctz64(uint64_t v) {
  unsigned long idx;
  if (v == 0) return 64;
  _BitScanForward64(&idx, v);
  return (unsigned)idx;
}
#else
#  include <stdatomic.h>
typedef _Atomic int32_t emq_atomic_i32;
typedef _Atomic uint64_t emq_atomic_u64;

static inline void emq_atomic_init_i32(emq_atomic_i32 *p, int32_t v) {
  atomic_init(p, v);
}
static inline int32_t emq_atomic_load_i32(const emq_atomic_i32 *p) {
  return atomic_load_explicit(p, memory_order_relaxed);
}
static inline void emq_atomic_store_i32(emq_atomic_i32 *p, int32_t v) {
  atomic_store_explicit(p, v, memory_order_relaxed);
}
static inline int32_t emq_atomic_fetch_add_i32(emq_atomic_i32 *p, int32_t v) {
  return atomic_fetch_add_explicit(p, v, memory_order_relaxed);
}
static inline int emq_atomic_cas_i32(emq_atomic_i32 *p, int32_t *expected,
                                     int32_t desired) {
  return atomic_compare_exchange_weak_explicit(
      p, expected, desired, memory_order_acq_rel, memory_order_relaxed);
}

static inline void emq_atomic_init_u64(emq_atomic_u64 *p, uint64_t v) {
  atomic_init(p, v);
}
static inline uint64_t emq_atomic_load_u64(const emq_atomic_u64 *p) {
  return atomic_load_explicit(p, memory_order_relaxed);
}
static inline uint64_t emq_atomic_load_acquire_u64(const emq_atomic_u64 *p) {
  return atomic_load_explicit(p, memory_order_acquire);
}
static inline void emq_atomic_store_u64(emq_atomic_u64 *p, uint64_t v) {
  atomic_store_explicit(p, v, memory_order_relaxed);
}
static inline void emq_atomic_store_release_u64(emq_atomic_u64 *p, uint64_t v) {
  atomic_store_explicit(p, v, memory_order_release);
}
static inline uint64_t emq_atomic_fetch_add_u64(emq_atomic_u64 *p, uint64_t v) {
  return atomic_fetch_add_explicit(p, v, memory_order_relaxed);
}
static inline int emq_atomic_cas_u64(emq_atomic_u64 *p, uint64_t *expected,
                                     uint64_t desired) {
  return atomic_compare_exchange_weak_explicit(
      p, expected, desired, memory_order_acq_rel, memory_order_relaxed);
}

static inline unsigned emq_ctz64(uint64_t v) {
  if (v == 0) return 64;
  return (unsigned)__builtin_ctzll(v);
}
#endif

#endif /* EMQ_ATOMIC_H */
