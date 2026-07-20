#include "core/emq_hist.h"

#if defined(_MSC_VER)
#  include <intrin.h>
#endif

static unsigned emq_hist_bucket(uint64_t value_ns) {
  unsigned bucket;

  if (value_ns == 0) {
    return 0;
  }
#if defined(_MSC_VER)
  {
    unsigned long idx;
    _BitScanReverse64(&idx, value_ns);
    bucket = (unsigned)idx;
  }
#else
  bucket = 63u - (unsigned)__builtin_clzll(value_ns);
#endif
  if (bucket >= EMQ_HIST_BUCKETS) {
    bucket = EMQ_HIST_BUCKETS - 1u;
  }
  return bucket;
}

void emq_hist_init(emq_hist *h) {
  size_t i;
  if (!h) {
    return;
  }
  for (i = 0; i < EMQ_HIST_BUCKETS; ++i) {
    emq_atomic_init_u64(&h->buckets[i], 0);
  }
  emq_atomic_init_u64(&h->total_count, 0);
  emq_atomic_init_u64(&h->total_ns, 0);
}

void emq_hist_reset(emq_hist *h) {
  size_t i;
  if (!h) {
    return;
  }
  for (i = 0; i < EMQ_HIST_BUCKETS; ++i) {
    emq_atomic_store_u64(&h->buckets[i], 0);
  }
  emq_atomic_store_u64(&h->total_count, 0);
  emq_atomic_store_u64(&h->total_ns, 0);
}

void emq_hist_record(emq_hist *h, uint64_t value_ns) {
  unsigned bucket;
  if (!h) {
    return;
  }
  bucket = emq_hist_bucket(value_ns);
  (void)emq_atomic_fetch_add_u64(&h->buckets[bucket], 1);
  (void)emq_atomic_fetch_add_u64(&h->total_count, 1);
  (void)emq_atomic_fetch_add_u64(&h->total_ns, value_ns);
}

void emq_hist_merge(emq_hist *dst, const emq_hist *src) {
  size_t i;
  if (!dst || !src) {
    return;
  }
  for (i = 0; i < EMQ_HIST_BUCKETS; ++i) {
    uint64_t n = emq_atomic_load_u64(&src->buckets[i]);
    if (n != 0) {
      (void)emq_atomic_fetch_add_u64(&dst->buckets[i], n);
    }
  }
  (void)emq_atomic_fetch_add_u64(&dst->total_count,
                                 emq_atomic_load_u64(&src->total_count));
  (void)emq_atomic_fetch_add_u64(&dst->total_ns,
                                 emq_atomic_load_u64(&src->total_ns));
}

uint64_t emq_hist_percentile(const emq_hist *h, double percentile) {
  uint64_t target;
  uint64_t seen = 0;
  uint64_t total;
  size_t i;

  if (!h) {
    return 0;
  }
  total = emq_atomic_load_u64(&h->total_count);
  if (total == 0) {
    return 0;
  }
  if (percentile <= 0.0) {
    return 1;
  }
  if (percentile >= 100.0) {
    return (uint64_t)1ULL << (EMQ_HIST_BUCKETS - 1u);
  }

  target = (uint64_t)((double)total * percentile / 100.0);
  if (target == 0) {
    target = 1;
  }

  for (i = 0; i < EMQ_HIST_BUCKETS; ++i) {
    uint64_t n = emq_atomic_load_u64(&h->buckets[i]);
    seen += n;
    if (seen >= target) {
      if (i == 0) {
        return 0;
      }
      return (uint64_t)1ULL << (i - 1u);
    }
  }
  return (uint64_t)1ULL << (EMQ_HIST_BUCKETS - 2u);
}
