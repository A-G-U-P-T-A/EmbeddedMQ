#include "core/emq_stats_ex.h"

int emq_stats_dump_json(FILE *out, const emq_stats_ex *s, const char *name) {
  if (!out || !s) {
    return -1;
  }
  if (!name) {
    name = "queue";
  }

  return fprintf(
             out,
             "{"
             "\"name\":\"%s\","
             "\"enqueued\":%llu,"
             "\"dequeued\":%llu,"
             "\"acked\":%llu,"
             "\"nacked\":%llu,"
             "\"expired\":%llu,"
             "\"depth\":%llu,"
             "\"bytes\":%llu,"
             "\"redelivered\":%llu,"
             "\"producer_lag\":%llu,"
             "\"consumer_lag\":%llu,"
             "\"avg_payload\":%llu,"
             "\"compression_ratio_x1000\":%llu,"
             "\"allocator_hits\":%llu,"
             "\"allocator_misses\":%llu,"
             "\"scheduler_activations\":%llu,"
             "\"wakeups\":%llu,"
             "\"hot_score\":%llu"
             "}\n",
             name,
             (unsigned long long)s->base.enqueued,
             (unsigned long long)s->base.dequeued,
             (unsigned long long)s->base.acked,
             (unsigned long long)s->base.nacked,
             (unsigned long long)s->base.expired,
             (unsigned long long)s->base.depth,
             (unsigned long long)s->base.bytes,
             (unsigned long long)s->base.redelivered,
             (unsigned long long)s->producer_lag,
             (unsigned long long)s->consumer_lag,
             (unsigned long long)s->avg_payload,
             (unsigned long long)s->compression_ratio_x1000,
             (unsigned long long)s->allocator_hits,
             (unsigned long long)s->allocator_misses,
             (unsigned long long)s->scheduler_activations,
             (unsigned long long)s->wakeups,
             (unsigned long long)s->hot_score) < 0
             ? -1
             : 0;
}
