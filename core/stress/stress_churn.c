/*
 * Queue churn: random create / destroy / push / pop under time budget.
 * Watchdog catches stalls; end-state registry is empty.
 */

#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct churn_slot {
  emq_queue *q;
  char name[32];
  int live;
} churn_slot;

typedef struct churn_ctx {
  emq_runtime *rt;
  churn_slot *slots;
  uint32_t max_slots;
  uint32_t live_count;
  size_t payload_len;
  emq_rng rng;
  emq_watchdog *wd;
  uint64_t end_ns;
  emq_mutex *mu;
} churn_ctx;

static void churn_create(churn_ctx *ctx) {
  uint32_t i;
  emq_queue_opts opts;

  emq_mutex_lock(ctx->mu);
  if (ctx->live_count >= ctx->max_slots) {
    emq_mutex_unlock(ctx->mu);
    return;
  }
  for (i = 0; i < ctx->max_slots; ++i) {
    if (!ctx->slots[i].live) break;
  }
  EMQ_REQUIRE(i < ctx->max_slots);

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.producers = 2;
  opts.consumers = 2;
  opts.backpressure = EMQ_BP_MODE_BLOCK;
  opts.capacity = 4096;

  snprintf(ctx->slots[i].name, sizeof(ctx->slots[i].name), "c%u", i);
  EMQ_REQUIRE(emq_queue_create(ctx->rt, ctx->slots[i].name, &opts,
                               &ctx->slots[i].q) == EMQ_OK);
  ctx->slots[i].live = 1;
  ctx->live_count++;
  emq_mutex_unlock(ctx->mu);
}

static void churn_destroy(churn_ctx *ctx) {
  uint32_t idx;
  uint32_t pick;
  uint32_t seen = 0;
  churn_slot *slot;

  emq_mutex_lock(ctx->mu);
  if (ctx->live_count == 0) {
    emq_mutex_unlock(ctx->mu);
    return;
  }
  pick = (uint32_t)emq_rng_bounded(&ctx->rng, ctx->live_count);
  for (idx = 0; idx < ctx->max_slots; ++idx) {
    if (!ctx->slots[idx].live) continue;
    if (seen == pick) break;
    seen++;
  }
  EMQ_REQUIRE(idx < ctx->max_slots);
  slot = &ctx->slots[idx];
  EMQ_REQUIRE(emq_queue_close(slot->q) == EMQ_OK);
  EMQ_REQUIRE(emq_queue_destroy(ctx->rt, slot->name) == EMQ_OK);
  slot->q = NULL;
  slot->live = 0;
  ctx->live_count--;
  emq_mutex_unlock(ctx->mu);
}

static void churn_push(churn_ctx *ctx) {
  uint32_t idx;
  uint32_t pick;
  uint32_t seen = 0;
  uint8_t buf[256];
  uint64_t seq;
  churn_slot *slot;

  emq_mutex_lock(ctx->mu);
  if (ctx->live_count == 0) {
    emq_mutex_unlock(ctx->mu);
    return;
  }
  pick = (uint32_t)emq_rng_bounded(&ctx->rng, ctx->live_count);
  for (idx = 0; idx < ctx->max_slots; ++idx) {
    if (!ctx->slots[idx].live) continue;
    if (seen == pick) break;
    seen++;
  }
  slot = &ctx->slots[idx];
  emq_mutex_unlock(ctx->mu);

  seq = emq_rng_u64(&ctx->rng);
  EMQ_REQUIRE(emq_payload_fill(buf, ctx->payload_len, seq, 0) == 0);
  if (emq_push(slot->q, buf, ctx->payload_len, NULL) != EMQ_OK) {
    /* DROP_NEW / full is acceptable under churn. */
  }
}

static void churn_pop(churn_ctx *ctx) {
  uint32_t idx;
  uint32_t pick;
  uint32_t seen = 0;
  emq_message m;
  churn_slot *slot;

  emq_mutex_lock(ctx->mu);
  if (ctx->live_count == 0) {
    emq_mutex_unlock(ctx->mu);
    return;
  }
  pick = (uint32_t)emq_rng_bounded(&ctx->rng, ctx->live_count);
  for (idx = 0; idx < ctx->max_slots; ++idx) {
    if (!ctx->slots[idx].live) continue;
    if (seen == pick) break;
    seen++;
  }
  slot = &ctx->slots[idx];
  emq_mutex_unlock(ctx->mu);

  memset(&m, 0, sizeof(m));
  if (emq_try_pop(slot->q, &m) == EMQ_OK) {
    EMQ_REQUIRE(emq_payload_check((const uint8_t *)m.data, m.size, NULL,
                                  NULL) == 0);
    emq_message_release(&m);
  }
}

static void churn_teardown(churn_ctx *ctx) {
  uint32_t i;
  for (i = 0; i < ctx->max_slots; ++i) {
    if (!ctx->slots[i].live) continue;
    emq_queue_close(ctx->slots[i].q);
    emq_queue_destroy(ctx->rt, ctx->slots[i].name);
    ctx->slots[i].live = 0;
  }
  ctx->live_count = 0;
}

int main(int argc, char **argv) {
  emq_cli cfg;
  emq_runtime *rt = NULL;
  churn_ctx ctx;
  emq_watchdog *wd = NULL;
  uint64_t seed;
  uint64_t ops = 0;

  emq_cli_defaults(&cfg);
  if (emq_cli_parse(&cfg, argc, argv) == 1) return 0;

  if (cfg.quick) {
    cfg.duration_sec = 2;
    cfg.queues = 200;
  } else {
    if (cfg.duration_sec == 0) cfg.duration_sec = 30;
    if (cfg.queues == 1) cfg.queues = 1000;
  }
  if (cfg.payload < EMQ_PAYLOAD_HDR) {
    cfg.payload = 64;
  }
  if (cfg.payload > 256) {
    cfg.payload = 256;
  }

  seed = emq_cli_seed_or_time(&cfg);
  emq_rng_seed(&ctx.rng, seed);
  printf("stress_churn seed=%llu duration=%llu s max_queues=%u payload=%u "
         "quick=%d\n",
         (unsigned long long)seed, (unsigned long long)cfg.duration_sec,
         cfg.queues, cfg.payload, cfg.quick);

  memset(&ctx, 0, sizeof(ctx));
  ctx.max_slots = cfg.queues;
  ctx.payload_len = cfg.payload;
  ctx.end_ns = emq_now_ns() + cfg.duration_sec * 1000000000ULL;
  ctx.mu = emq_mutex_create();
  EMQ_REQUIRE(ctx.mu != NULL);

  ctx.slots = (churn_slot *)calloc(cfg.queues, sizeof(churn_slot));
  EMQ_REQUIRE(ctx.slots != NULL);

  wd = emq_watchdog_start(15, "stress_churn");
  EMQ_REQUIRE(wd != NULL);
  ctx.wd = wd;

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  ctx.rt = rt;

  while (emq_now_ns() < ctx.end_ns) {
    uint32_t op = emq_rng_u32(&ctx.rng) % 4u;
    switch (op) {
      case 0: churn_create(&ctx); break;
      case 1: churn_destroy(&ctx); break;
      case 2: churn_push(&ctx); break;
      default: churn_pop(&ctx); break;
    }
    ops++;
    if ((ops % 1000u) == 0) {
      emq_watchdog_heartbeat(wd);
    }
  }

  churn_teardown(&ctx);
  emq_watchdog_stop(wd);
  emq_mutex_destroy(ctx.mu);
  emq_runtime_destroy(rt);
  free(ctx.slots);

  printf("stress_churn ok ops=%llu\n", (unsigned long long)ops);
  return 0;
}
