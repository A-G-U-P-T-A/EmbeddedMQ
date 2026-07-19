#ifndef EMQ_BENCH_METRICS_H
#define EMQ_BENCH_METRICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_bench_process_metrics {
  uint64_t rss_bytes;
  uint64_t peak_rss_bytes;
  uint64_t user_time_ns;
  uint64_t kernel_time_ns;
  uint64_t context_switches; /* voluntary + involuntary where available */
  uint64_t wakeups;          /* optional; filled by caller if known */
} emq_bench_process_metrics;

typedef struct emq_bench_cell {
  uint32_t queues;
  size_t payload;
  size_t ops_total;
  uint64_t elapsed_ns;
  double ops_per_sec;
  uint64_t p50_ns;
  uint64_t p99_ns;
  uint64_t p999_ns;
  uint64_t p9999_ns;
  emq_bench_process_metrics before;
  emq_bench_process_metrics after;
  int ok;
  const char *error;
} emq_bench_cell;

int emq_bench_process_sample(emq_bench_process_metrics *out);
void emq_bench_process_delta(const emq_bench_process_metrics *before,
                             const emq_bench_process_metrics *after,
                             emq_bench_process_metrics *delta);

uint64_t emq_bench_percentile(uint64_t *sorted, size_t n, double p);
void emq_bench_sort_u64(uint64_t *values, size_t n);

void emq_bench_print_header(FILE *out);
void emq_bench_print_cell(FILE *out, const emq_bench_cell *cell);
int emq_bench_write_csv_header(FILE *out);
int emq_bench_write_csv_row(FILE *out, const emq_bench_cell *cell);

/* Write / append a baseline snapshot for later phase deltas. */
int emq_bench_baseline_write(const char *path, const emq_bench_cell *cells,
                             size_t count, const char *label);
int emq_bench_baseline_compare(const char *path, const emq_bench_cell *cells,
                               size_t count, FILE *report);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_BENCH_METRICS_H */
