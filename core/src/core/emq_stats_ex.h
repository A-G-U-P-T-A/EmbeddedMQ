#ifndef EMQ_STATS_EX_H
#define EMQ_STATS_EX_H

#include "emq/emq_types.h"

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_stats_ex {
  emq_stats base;
  uint64_t producer_lag;
  uint64_t consumer_lag;
  uint64_t avg_payload;
  uint64_t compression_ratio_x1000;
  uint64_t allocator_hits;
  uint64_t allocator_misses;
  uint64_t scheduler_activations;
  uint64_t wakeups;
  uint64_t hot_score;
} emq_stats_ex;

int emq_stats_dump_json(FILE *out, const emq_stats_ex *s, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_STATS_EX_H */
