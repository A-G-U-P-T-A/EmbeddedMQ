#include "bench_metrics.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <psapi.h>
#else
#  include <stdio.h>
#  include <unistd.h>
#  include <sys/resource.h>
#  include <sys/time.h>
#endif

static int cmp_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a;
  uint64_t y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

void emq_bench_sort_u64(uint64_t *values, size_t n) {
  if (values && n) qsort(values, n, sizeof(uint64_t), cmp_u64);
}

uint64_t emq_bench_percentile(uint64_t *sorted, size_t n, double p) {
  size_t idx;
  if (!sorted || n == 0) return 0;
  idx = (size_t)(p * (double)(n - 1));
  if (idx >= n) idx = n - 1;
  return sorted[idx];
}

int emq_bench_process_sample(emq_bench_process_metrics *out) {
  if (!out) return -1;
  memset(out, 0, sizeof(*out));
#if defined(_WIN32)
  {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    FILETIME create, exit_t, kernel, user;
    ULARGE_INTEGER ku, uu;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             (PROCESS_MEMORY_COUNTERS *)&pmc,
                             sizeof(pmc))) {
      out->rss_bytes = (uint64_t)pmc.WorkingSetSize;
      out->peak_rss_bytes = (uint64_t)pmc.PeakWorkingSetSize;
    }
    if (GetProcessTimes(GetCurrentProcess(), &create, &exit_t, &kernel,
                        &user)) {
      ku.LowPart = kernel.dwLowDateTime;
      ku.HighPart = kernel.dwHighDateTime;
      uu.LowPart = user.dwLowDateTime;
      uu.HighPart = user.dwHighDateTime;
      /* FILETIME is 100ns units */
      out->kernel_time_ns = ku.QuadPart * 100ULL;
      out->user_time_ns = uu.QuadPart * 100ULL;
    }
    /* Windows does not expose cheap process context-switch counters here. */
    out->context_switches = 0;
  }
#else
  {
    struct rusage ru;
    long rss_pages = 0;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
      out->user_time_ns =
          (uint64_t)ru.ru_utime.tv_sec * 1000000000ULL +
          (uint64_t)ru.ru_utime.tv_usec * 1000ULL;
      out->kernel_time_ns =
          (uint64_t)ru.ru_stime.tv_sec * 1000000000ULL +
          (uint64_t)ru.ru_stime.tv_usec * 1000ULL;
      out->context_switches =
          (uint64_t)ru.ru_nvcsw + (uint64_t)ru.ru_nivcsw;
#if defined(__APPLE__) || defined(__FreeBSD__)
      out->rss_bytes = (uint64_t)ru.ru_maxrss;
      out->peak_rss_bytes = out->rss_bytes;
#else
      out->peak_rss_bytes = (uint64_t)ru.ru_maxrss * 1024ULL;
#endif
    }
#if !defined(__APPLE__) && !defined(__FreeBSD__)
    {
      FILE *f = fopen("/proc/self/statm", "r");
      long size = 0, resident = 0;
      if (f) {
        if (fscanf(f, "%ld %ld", &size, &resident) == 2) {
          rss_pages = resident;
          out->rss_bytes = (uint64_t)rss_pages * (uint64_t)sysconf(_SC_PAGESIZE);
        }
        fclose(f);
      }
    }
#endif
  }
#endif
  return 0;
}

void emq_bench_process_delta(const emq_bench_process_metrics *before,
                             const emq_bench_process_metrics *after,
                             emq_bench_process_metrics *delta) {
  if (!before || !after || !delta) return;
  memset(delta, 0, sizeof(*delta));
  delta->rss_bytes = after->rss_bytes;
  delta->peak_rss_bytes = after->peak_rss_bytes;
  delta->user_time_ns =
      after->user_time_ns > before->user_time_ns
          ? after->user_time_ns - before->user_time_ns
          : 0;
  delta->kernel_time_ns =
      after->kernel_time_ns > before->kernel_time_ns
          ? after->kernel_time_ns - before->kernel_time_ns
          : 0;
  delta->context_switches =
      after->context_switches > before->context_switches
          ? after->context_switches - before->context_switches
          : 0;
  delta->wakeups =
      after->wakeups > before->wakeups ? after->wakeups - before->wakeups : 0;
}

void emq_bench_print_header(FILE *out) {
  fprintf(out,
          "%-8s %-8s %-10s %-12s %-10s %-10s %-10s %-10s %-10s %-10s %-10s "
          "%-10s %-10s\n",
          "queues", "payload", "ops", "ops/s", "ms", "p50", "p99", "p99.9",
          "p99.99", "rss_MB", "cpu_ms", "csw", "wake");
  fprintf(out,
          "%-8s %-8s %-10s %-12s %-10s %-10s %-10s %-10s %-10s %-10s %-10s "
          "%-10s %-10s\n",
          "--------", "--------", "----------", "------------", "----------",
          "----------", "----------", "----------", "----------", "----------",
          "----------", "----------", "----------");
}

void emq_bench_print_cell(FILE *out, const emq_bench_cell *cell) {
  emq_bench_process_metrics d;
  if (!out || !cell) return;
  if (!cell->ok) {
    fprintf(out, "%-8u %-8zu FAILED (%s)\n", (unsigned)cell->queues,
            cell->payload, cell->error ? cell->error : "?");
    return;
  }
  emq_bench_process_delta(&cell->before, &cell->after, &d);
  fprintf(out,
          "%-8u %-8zu %-10zu %-12.0f %-10.2f %-10llu %-10llu %-10llu %-10llu "
          "%-10.2f %-10.2f %-10llu %-10llu\n",
          (unsigned)cell->queues, cell->payload, cell->ops_total,
          cell->ops_per_sec, (double)cell->elapsed_ns / 1e6,
          (unsigned long long)cell->p50_ns, (unsigned long long)cell->p99_ns,
          (unsigned long long)cell->p999_ns,
          (unsigned long long)cell->p9999_ns,
          (double)d.rss_bytes / (1024.0 * 1024.0),
          (double)(d.user_time_ns + d.kernel_time_ns) / 1e6,
          (unsigned long long)d.context_switches,
          (unsigned long long)d.wakeups);
}

int emq_bench_write_csv_header(FILE *out) {
  if (!out) return -1;
  fprintf(out,
          "queues,payload,ops,ops_per_sec,elapsed_ns,p50_ns,p99_ns,p999_ns,"
          "p9999_ns,rss_bytes,cpu_ns,context_switches,wakeups,ok\n");
  return 0;
}

int emq_bench_write_csv_row(FILE *out, const emq_bench_cell *cell) {
  emq_bench_process_metrics d;
  if (!out || !cell) return -1;
  emq_bench_process_delta(&cell->before, &cell->after, &d);
  fprintf(out,
          "%u,%zu,%zu,%.0f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%d\n",
          (unsigned)cell->queues, cell->payload, cell->ops_total,
          cell->ops_per_sec, (unsigned long long)cell->elapsed_ns,
          (unsigned long long)cell->p50_ns, (unsigned long long)cell->p99_ns,
          (unsigned long long)cell->p999_ns,
          (unsigned long long)cell->p9999_ns,
          (unsigned long long)d.rss_bytes,
          (unsigned long long)(d.user_time_ns + d.kernel_time_ns),
          (unsigned long long)d.context_switches,
          (unsigned long long)d.wakeups, cell->ok);
  return 0;
}

int emq_bench_baseline_write(const char *path, const emq_bench_cell *cells,
                             size_t count, const char *label) {
  FILE *f;
  size_t i;
  if (!path || !cells) return -1;
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "# EmbeddedMQ baseline label=%s cells=%zu\n",
          label ? label : "unnamed", count);
  emq_bench_write_csv_header(f);
  for (i = 0; i < count; ++i) emq_bench_write_csv_row(f, &cells[i]);
  fclose(f);
  return 0;
}

int emq_bench_baseline_compare(const char *path, const emq_bench_cell *cells,
                               size_t count, FILE *report) {
  FILE *f;
  char line[512];
  size_t matched = 0;
  if (!path || !cells || !report) return -1;
  f = fopen(path, "r");
  if (!f) {
    fprintf(report, "baseline missing: %s\n", path);
    return -1;
  }
  fprintf(report, "Baseline compare vs %s\n", path);
  while (fgets(line, sizeof(line), f)) {
    unsigned q = 0;
    size_t payload = 0;
    double ops = 0;
    size_t i;
    if (line[0] == '#' || strncmp(line, "queues", 6) == 0) continue;
    if (sscanf(line, "%u,%zu,%*zu,%lf", &q, &payload, &ops) != 3) continue;
    for (i = 0; i < count; ++i) {
      if (cells[i].queues == q && cells[i].payload == payload && cells[i].ok) {
        double delta = cells[i].ops_per_sec - ops;
        double pct = ops > 0 ? (delta / ops) * 100.0 : 0.0;
        fprintf(report, "  q=%u p=%zu baseline=%.0f now=%.0f delta=%+.1f%%\n",
                q, payload, ops, cells[i].ops_per_sec, pct);
        matched++;
        break;
      }
    }
  }
  fclose(f);
  fprintf(report, "matched %zu cells\n", matched);
  return 0;
}
