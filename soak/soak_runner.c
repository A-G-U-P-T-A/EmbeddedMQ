#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { k_queues = 4 };

static void mixed_work(emq_runtime *rt, emq_queue **qs, emq_rng *rng) {
  int qi = (int)emq_rng_bounded(rng, k_queues);
  if (emq_rng_u32(rng) & 1u) {
    uint8_t buf[64];
    EMQ_REQUIRE(emq_payload_fill(buf, sizeof(buf), emq_rng_u64(rng), 0) == 0);
    (void)emq_push(qs[qi], buf, sizeof(buf), NULL);
  } else {
    emq_message msg;
    emq_status st = emq_try_pop(qs[qi], &msg);
    if (st == EMQ_OK) emq_message_release(&msg);
  }
}

int main(int argc, char **argv) {
  emq_cli cli;
  emq_rng rng;
  emq_runtime *rt = NULL;
  emq_queue *qs[k_queues];
  emq_queue_opts opts;
  emq_proc_snap snap0, snap1, snap;
  uint64_t seed;
  uint64_t end_ns;
  uint64_t next_sample_ns;
  uint64_t baseline_rss = 0;
  int warmed = 0;
  FILE *csv = NULL;
  int i;

  emq_cli_defaults(&cli);
  if (emq_cli_parse(&cli, argc, argv) != 0) {
    fprintf(stderr,
            "usage: soak_runner [--quick] [--duration SEC] [--csv PATH] [--seed S]\n");
    return 2;
  }
  if (cli.quick) cli.duration_sec = 3;
  else if (cli.duration_sec == 0) cli.duration_sec = 30;

  seed = emq_cli_seed_or_time(&cli);
  emq_rng_seed(&rng, seed);
  if (cli.csv_path) {
    csv = fopen(cli.csv_path, "w");
    EMQ_REQUIRE(csv != NULL);
    fprintf(csv, "elapsed_ms,rss_bytes,peak_rss,handles,user_ns,kernel_ns\n");
  }

  EMQ_REQUIRE(emq_runtime_create_ex(&rt, 2) == EMQ_OK);
  EMQ_REQUIRE(emq_runtime_start(rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.delivery = EMQ_AT_MOST_ONCE;
  opts.capacity = 128;

  for (i = 0; i < k_queues; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "soak_q%d", i);
    EMQ_REQUIRE(emq_queue_create(rt, name, &opts, &qs[i]) == EMQ_OK);
  }

  EMQ_REQUIRE(emq_proc_sample(&snap0) == 0);
  baseline_rss = snap0.rss_bytes ? snap0.rss_bytes : 1;
  end_ns = emq_now_ns() + cli.duration_sec * 1000000000ULL;
  next_sample_ns = emq_now_ns();

  printf("soak_runner seed=%llu duration=%llu s quick=%d\n",
         (unsigned long long)seed, (unsigned long long)cli.duration_sec,
         cli.quick);

  while (emq_now_ns() < end_ns) {
    mixed_work(rt, qs, &rng);
    if (emq_now_ns() >= next_sample_ns) {
      EMQ_REQUIRE(emq_proc_sample(&snap) == 0);
      if (csv) {
        uint64_t elapsed_ms = (emq_now_ns() - (end_ns - cli.duration_sec * 1000000000ULL)) / 1000000ULL;
        fprintf(csv, "%llu,%llu,%llu,%llu,%llu,%llu\n",
                (unsigned long long)elapsed_ms,
                (unsigned long long)snap.rss_bytes,
                (unsigned long long)snap.peak_rss_bytes,
                (unsigned long long)snap.handle_count,
                (unsigned long long)snap.user_time_ns,
                (unsigned long long)snap.kernel_time_ns);
      }
      if (!warmed) {
        baseline_rss = snap.rss_bytes ? snap.rss_bytes : baseline_rss;
        warmed = 1;
      } else if (cli.quick && snap.rss_bytes > baseline_rss * 2) {
        fprintf(stderr, "RSS growth exceeded 2x baseline: %llu > 2 * %llu\n",
                (unsigned long long)snap.rss_bytes,
                (unsigned long long)baseline_rss);
        EMQ_REQUIRE(0);
      }
      next_sample_ns = emq_now_ns() + 500000000ULL;
    }
  }

  for (i = 0; i < k_queues; ++i) emq_queue_close(qs[i]);
  EMQ_REQUIRE(emq_runtime_stop(rt) == EMQ_OK);
  emq_runtime_destroy(rt);
  EMQ_REQUIRE(emq_proc_sample(&snap1) == 0);

  if (csv) fclose(csv);
  printf("PASS soak_runner rss=%llu peak=%llu\n",
         (unsigned long long)snap1.rss_bytes,
         (unsigned long long)snap1.peak_rss_bytes);
  return 0;
}
