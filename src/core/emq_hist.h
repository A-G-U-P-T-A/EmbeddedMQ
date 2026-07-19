#ifndef EMQ_HIST_H
#define EMQ_HIST_H

#include "core/emq_atomic.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMQ_HIST_BUCKETS 64u

typedef struct emq_hist {
  emq_atomic_u64 buckets[EMQ_HIST_BUCKETS];
  uint64_t total_count;
  uint64_t total_ns;
} emq_hist;

void emq_hist_init(emq_hist *h);
void emq_hist_reset(emq_hist *h);
void emq_hist_record(emq_hist *h, uint64_t value_ns);
void emq_hist_merge(emq_hist *dst, const emq_hist *src);
uint64_t emq_hist_percentile(const emq_hist *h, double percentile);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_HIST_H */
