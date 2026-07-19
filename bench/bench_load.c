/*
 * Contained in-process load suite (Engine v2 metrics).
 *
 * Reports throughput, p50/p99/p99.9/p99.99, RSS, CPU, context switches.
 *
 * Usage:
 *   emq_bench_load
 *   emq_bench_load --quick
 *   emq_bench_load --queues 100 --payload 1024 --ops 1000
 *   emq_bench_load --quick --csv out.csv --baseline baseline.csv
 */

#include "emq/emq.h"
#include "platform/emq_platform.h"
#include "bench_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct load_config {
  const uint32_t *queue_counts;
  size_t n_queue_counts;
  const size_t *payload_sizes;
  size_t n_payload_sizes;
  size_t ops_per_queue;
  int quick;
  const char *csv_path;
  const char *baseline_path;
  const char *baseline_label;
  int compare_baseline;
} load_config;

static void fill_payload(uint8_t *buf, size_t len, uint32_t seed) {
  size_t i;
  for (i = 0; i < len; ++i) {
    buf[i] = (uint8_t)((seed + (uint32_t)i * 17u) & 0xffu);
  }
}

static emq_bench_cell run_cell(uint32_t n_queues, size_t payload_len,
                               size_t ops_per_queue) {
  emq_bench_cell r;
  emq_runtime *rt = NULL;
  emq_queue **qs = NULL;
  emq_queue_opts opts;
  uint8_t *payload = NULL;
  uint64_t *lat = NULL;
  size_t ops_total;
  size_t lat_count = 0;
  size_t sample_stride;
  size_t i;
  uint64_t t0, t1, wall0, wall1;
  char name[64];
  emq_message m;
  emq_status st;

  memset(&r, 0, sizeof(r));
  r.queues = n_queues;
  r.payload = payload_len;
  ops_total = (size_t)n_queues * ops_per_queue;
  r.ops_total = ops_total;

  {
    size_t max_samples = 200000;
    if (ops_total <= max_samples) {
      sample_stride = 1;
      lat = (uint64_t *)malloc(sizeof(uint64_t) * ops_total);
    } else {
      sample_stride = ops_total / max_samples;
      if (sample_stride == 0) sample_stride = 1;
      lat = (uint64_t *)malloc(sizeof(uint64_t) * (ops_total / sample_stride + 1));
    }
  }

  qs = (emq_queue **)calloc(n_queues, sizeof(emq_queue *));
  payload = (uint8_t *)malloc(payload_len ? payload_len : 1);
  if (!qs || !payload || !lat) {
    r.error = "oom";
    free(qs);
    free(payload);
    free(lat);
    return r;
  }
  fill_payload(payload, payload_len, 0xC0FFEEu);

  st = emq_runtime_create(&rt);
  if (st != EMQ_OK) {
    r.error = "runtime_create";
    free(qs);
    free(payload);
    free(lat);
    return r;
  }

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.producers = 1;
  opts.consumers = 1;

  for (i = 0; i < n_queues; ++i) {
    snprintf(name, sizeof(name), "q-%u", (unsigned)i);
    st = emq_queue_create(rt, name, &opts, &qs[i]);
    if (st != EMQ_OK) {
      r.error = "queue_create";
      goto cleanup;
    }
  }

  emq_bench_process_sample(&r.before);
  wall0 = emq_now_ns();
  for (i = 0; i < ops_total; ++i) {
    emq_queue *q = qs[i % n_queues];
    t0 = emq_now_ns();
    st = emq_push(q, payload, payload_len, NULL);
    if (st != EMQ_OK) {
      r.error = "push";
      goto cleanup;
    }
    st = emq_pop(q, &m, 0);
    t1 = emq_now_ns();
    if (st != EMQ_OK) {
      r.error = "pop";
      goto cleanup;
    }
    if (m.size != payload_len) {
      emq_message_release(&m);
      r.error = "size_mismatch";
      goto cleanup;
    }
    emq_message_release(&m);

    if ((i % sample_stride) == 0) {
      lat[lat_count++] = t1 - t0;
    }
  }
  wall1 = emq_now_ns();
  emq_bench_process_sample(&r.after);

  r.elapsed_ns = wall1 - wall0;
  r.ops_per_sec = (r.elapsed_ns > 0)
                      ? ((double)ops_total * 1e9) / (double)r.elapsed_ns
                      : 0.0;

  emq_bench_sort_u64(lat, lat_count);
  r.p50_ns = emq_bench_percentile(lat, lat_count, 0.50);
  r.p99_ns = emq_bench_percentile(lat, lat_count, 0.99);
  r.p999_ns = emq_bench_percentile(lat, lat_count, 0.999);
  r.p9999_ns = emq_bench_percentile(lat, lat_count, 0.9999);
  r.ok = 1;

cleanup:
  if (qs) {
    for (i = 0; i < n_queues; ++i) {
      if (qs[i]) emq_queue_close(qs[i]);
    }
  }
  if (rt) emq_runtime_destroy(rt);
  free(qs);
  free(payload);
  free(lat);
  return r;
}

static int parse_args(int argc, char **argv, load_config *cfg,
                      uint32_t *single_queues, size_t *single_payload,
                      size_t *single_ops, int *use_single) {
  int i;
  *use_single = 0;
  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--quick") == 0) {
      cfg->quick = 1;
    } else if (strcmp(argv[i], "--queues") == 0 && i + 1 < argc) {
      *single_queues = (uint32_t)atoi(argv[++i]);
      *use_single = 1;
    } else if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
      *single_payload = (size_t)atoi(argv[++i]);
      *use_single = 1;
    } else if (strcmp(argv[i], "--ops") == 0 && i + 1 < argc) {
      *single_ops = (size_t)atoi(argv[++i]);
      cfg->ops_per_queue = *single_ops;
    } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
      cfg->csv_path = argv[++i];
    } else if (strcmp(argv[i], "--baseline") == 0 && i + 1 < argc) {
      cfg->baseline_path = argv[++i];
    } else if (strcmp(argv[i], "--baseline-label") == 0 && i + 1 < argc) {
      cfg->baseline_label = argv[++i];
    } else if (strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
      cfg->baseline_path = argv[++i];
      cfg->compare_baseline = 1;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Usage: emq_bench_load [--quick] [--queues N] [--payload BYTES]\n"
             "                     [--ops PER_QUEUE] [--csv PATH]\n"
             "                     [--baseline PATH] [--compare PATH]\n");
      return 1;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  static const uint32_t queues_full[] = {
      1u, 10u, 100u, 1000u, 10000u, 25000u, 50000u, 100000u};
  static const uint32_t queues_quick[] = {1u, 10u, 100u};
  static const size_t payloads_full[] = {
      16u, 64u, 256u, 1024u, 4096u, 8192u, 65536u, 1048576u};
  static const size_t payloads_quick[] = {64u, 256u, 1024u};

  load_config cfg;
  uint32_t single_queues = 0;
  size_t single_payload = 0;
  size_t single_ops = 0;
  int use_single = 0;
  size_t qi, pi;
  int failed = 0;
  emq_bench_cell *results = NULL;
  size_t result_count = 0;
  size_t result_cap = 0;
  FILE *csv = NULL;

  memset(&cfg, 0, sizeof(cfg));
  cfg.ops_per_queue = 200;
  cfg.baseline_label = "phase0";

  if (parse_args(argc, argv, &cfg, &single_queues, &single_payload, &single_ops,
                 &use_single)) {
    return 0;
  }

  if (cfg.quick) {
    cfg.queue_counts = queues_quick;
    cfg.n_queue_counts = sizeof(queues_quick) / sizeof(queues_quick[0]);
    cfg.payload_sizes = payloads_quick;
    cfg.n_payload_sizes = sizeof(payloads_quick) / sizeof(payloads_quick[0]);
    if (single_ops == 0) cfg.ops_per_queue = 50;
  } else {
    cfg.queue_counts = queues_full;
    cfg.n_queue_counts = sizeof(queues_full) / sizeof(queues_full[0]);
    cfg.payload_sizes = payloads_full;
    cfg.n_payload_sizes = sizeof(payloads_full) / sizeof(payloads_full[0]);
  }

  if (cfg.csv_path) {
    csv = fopen(cfg.csv_path, "w");
    if (csv) emq_bench_write_csv_header(csv);
  }

  printf("EmbeddedMQ Engine v2 load suite\n");
  printf("storage=FAST policy=FIFO ops_per_queue=%zu\n\n", cfg.ops_per_queue);
  emq_bench_print_header(stdout);

  if (use_single && single_queues > 0 && single_payload > 0) {
    emq_bench_cell r = run_cell(single_queues, single_payload, cfg.ops_per_queue);
    emq_bench_print_cell(stdout, &r);
    if (csv) emq_bench_write_csv_row(csv, &r);
    if (cfg.baseline_path && !cfg.compare_baseline)
      emq_bench_baseline_write(cfg.baseline_path, &r, 1, cfg.baseline_label);
    if (csv) fclose(csv);
    return r.ok ? 0 : 1;
  }

  for (qi = 0; qi < cfg.n_queue_counts; ++qi) {
    for (pi = 0; pi < cfg.n_payload_sizes; ++pi) {
      uint32_t nq = cfg.queue_counts[qi];
      size_t psz = cfg.payload_sizes[pi];
      size_t ops = cfg.ops_per_queue;
      emq_bench_cell r;

      if (nq >= 100000u && ops > 2) ops = 2;
      else if (nq >= 10000u && ops > 20) ops = 20;
      else if (nq >= 1000u && ops > 50) ops = 50;

      r = run_cell(nq, psz, ops);
      emq_bench_print_cell(stdout, &r);
      if (csv) emq_bench_write_csv_row(csv, &r);
      if (!r.ok) failed = 1;

      if (result_count + 1 > result_cap) {
        size_t ncap = result_cap ? result_cap * 2 : 32;
        emq_bench_cell *n =
            (emq_bench_cell *)realloc(results, ncap * sizeof(*results));
        if (n) {
          results = n;
          result_cap = ncap;
        }
      }
      if (results && result_count < result_cap) results[result_count++] = r;
    }
  }

  if (cfg.baseline_path && !cfg.compare_baseline && results)
    emq_bench_baseline_write(cfg.baseline_path, results, result_count,
                             cfg.baseline_label);
  if (cfg.compare_baseline && cfg.baseline_path && results)
    emq_bench_baseline_compare(cfg.baseline_path, results, result_count, stdout);

  if (csv) fclose(csv);
  free(results);
  printf("\n%s\n", failed ? "LOAD SUITE FAILED" : "LOAD SUITE OK");
  return failed ? 1 : 0;
}
