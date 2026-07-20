/*
 * SPSC marathon: one FAST FIFO queue, dedicated producer + consumer threads.
 * Verifies self-checking payloads, strict FIFO order, and RSS plateau.
 */

#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

typedef struct spsc_ctx {
  emq_queue *q;
  uint64_t ops_target;
  volatile uint64_t produced;
  volatile uint64_t consumed;
  volatile uint64_t next_expected;
  size_t payload_len;
  emq_watchdog *wd;
  uint64_t rss_after_warmup;
  uint64_t rss_peak_after;
  uint64_t warmup_ops;
} spsc_ctx;

static void producer_main(void *arg) {
  spsc_ctx *ctx = (spsc_ctx *)arg;
  uint8_t *buf;
  uint64_t seq;

  buf = (uint8_t *)malloc(ctx->payload_len);
  EMQ_REQUIRE(buf != NULL);

  for (seq = 0; seq < ctx->ops_target; ++seq) {
    EMQ_REQUIRE(emq_payload_fill(buf, ctx->payload_len, seq, 0) == 0);
    while (emq_push(ctx->q, buf, ctx->payload_len, NULL) != EMQ_OK) {
      emq_sleep_ms(0);
    }
    ctx->produced = seq + 1;
    if (((seq + 1) % 10000u) == 0) {
      emq_watchdog_heartbeat(ctx->wd);
    }
  }
  free(buf);
}

static void consumer_main(void *arg) {
  spsc_ctx *ctx = (spsc_ctx *)arg;
  emq_message m;
  uint64_t seq;
  uint32_t producer;
  emq_proc_snap snap;

  memset(&m, 0, sizeof(m));
  while (ctx->consumed < ctx->ops_target) {
    emq_status st = emq_pop(ctx->q, &m, 100);
    if (st == EMQ_ERR_TIMEOUT || st == EMQ_ERR_EMPTY) {
      emq_sleep_ms(0);
      continue;
    }
    EMQ_REQUIRE(st == EMQ_OK);
    EMQ_REQUIRE(emq_payload_check((const uint8_t *)m.data, m.size, &seq,
                                  &producer) == 0);
    EMQ_REQUIRE(producer == 0);
    EMQ_REQUIRE(seq == ctx->next_expected);
    ctx->next_expected = seq + 1;
    ctx->consumed = seq + 1;
    emq_message_release(&m);

    if (((seq + 1) % 10000u) == 0) {
      emq_watchdog_heartbeat(ctx->wd);
      if ((seq + 1) >= ctx->warmup_ops &&
          emq_proc_sample(&snap) == 0 && snap.rss_bytes > 0) {
        if (ctx->rss_after_warmup == 0) {
          ctx->rss_after_warmup = snap.rss_bytes;
        }
        if (snap.rss_bytes > ctx->rss_peak_after) {
          ctx->rss_peak_after = snap.rss_bytes;
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  emq_cli cfg;
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  spsc_ctx ctx;
  emq_thread *prod = NULL;
  emq_thread *cons = NULL;
  emq_watchdog *wd = NULL;
  uint64_t seed;
  emq_rng rng;
  emq_proc_snap snap0, snap1;
  uint64_t rss_growth;

  emq_cli_defaults(&cfg);
  if (emq_cli_parse(&cfg, argc, argv) == 1) return 0;

  if (cfg.quick) {
    cfg.ops = 200000;
  } else if (cfg.ops == 100000) {
    cfg.ops = 5000000;
  }
  if (cfg.payload < EMQ_PAYLOAD_HDR) {
    cfg.payload = 64;
  }

  seed = emq_cli_seed_or_time(&cfg);
  emq_rng_seed(&rng, seed);
  (void)rng;
  printf("stress_spsc seed=%llu ops=%llu payload=%u quick=%d\n",
         (unsigned long long)seed, (unsigned long long)cfg.ops,
         cfg.payload, cfg.quick);

  memset(&ctx, 0, sizeof(ctx));
  ctx.ops_target = cfg.ops;
  ctx.payload_len = cfg.payload;
  ctx.warmup_ops = cfg.ops / 5u;
  if (ctx.warmup_ops < 10000) {
    ctx.warmup_ops = cfg.ops > 10000 ? 10000 : cfg.ops;
  }

  wd = emq_watchdog_start(30, "stress_spsc");
  EMQ_REQUIRE(wd != NULL);
  ctx.wd = wd;

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.producers = 1;
  opts.consumers = 1;
  opts.backpressure = EMQ_BP_MODE_BLOCK;
  opts.capacity = 65536;
  EMQ_REQUIRE(emq_queue_create(rt, "spsc", &opts, &q) == EMQ_OK);
  ctx.q = q;

  emq_proc_sample(&snap0);

  EMQ_REQUIRE(emq_thread_create(&prod, producer_main, &ctx) == 0);
  EMQ_REQUIRE(emq_thread_create(&cons, consumer_main, &ctx) == 0);
  emq_thread_join(prod);
  emq_thread_destroy(prod);
  emq_thread_join(cons);
  emq_thread_destroy(cons);

  EMQ_REQUIRE(ctx.produced == cfg.ops);
  EMQ_REQUIRE(ctx.consumed == cfg.ops);
  EMQ_REQUIRE(ctx.next_expected == cfg.ops);

  emq_proc_sample(&snap1);
#if !defined(EMQ_SANITIZER_BUILD)
  if (ctx.rss_after_warmup > 0) {
    rss_growth = ctx.rss_peak_after - ctx.rss_after_warmup;
    EMQ_REQUIRE(rss_growth <= ctx.rss_after_warmup / 2u);
  } else if (snap0.rss_bytes > 0 && snap1.rss_bytes > 0) {
    rss_growth = snap1.rss_bytes > snap0.rss_bytes ?
                 snap1.rss_bytes - snap0.rss_bytes : 0;
    EMQ_REQUIRE(rss_growth <= snap0.rss_bytes / 2u);
  }
#else
  (void)rss_growth;
  (void)snap1;
#endif

  emq_watchdog_stop(wd);
  emq_queue_close(q);
  emq_runtime_destroy(rt);

  printf("stress_spsc ok produced=%llu consumed=%llu rss_peak=%llu\n",
         (unsigned long long)ctx.produced, (unsigned long long)ctx.consumed,
         (unsigned long long)ctx.rss_peak_after);
  return 0;
}
