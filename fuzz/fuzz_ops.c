#include "emq/emq.h"
#include "emq_testsupport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { k_queues = 2, k_ref_cap = 4096 };

typedef struct ref_queue {
  uint64_t seq[k_ref_cap];
  size_t head;
  size_t tail;
  size_t depth;
  int closed;
} ref_queue;

typedef struct fuzz_ctx {
  emq_runtime *rt;
  emq_queue *qs[k_queues];
  ref_queue ref[k_queues];
  emq_rng rng;
  uint64_t next_seq[k_queues];
  uint32_t payload;
} fuzz_ctx;

static void ref_push(ref_queue *rq, uint64_t seq) {
  size_t idx;
  EMQ_REQUIRE(rq->depth < k_ref_cap);
  idx = (rq->head + rq->depth) % k_ref_cap;
  rq->seq[idx] = seq;
  ++rq->depth;
  (void)rq->tail;
}

static int ref_pop(ref_queue *rq, uint64_t *seq_out) {
  if (rq->depth == 0) return 0;
  if (seq_out) *seq_out = rq->seq[rq->head];
  rq->head = (rq->head + 1) % k_ref_cap;
  --rq->depth;
  return 1;
}

static void check_stats(const fuzz_ctx *ctx, emq_queue *q, const ref_queue *rq) {
  emq_stats st;
  EMQ_REQUIRE(emq_queue_stats(q, &st) == EMQ_OK);
  EMQ_REQUIRE(st.depth == rq->depth);
}

static void do_push(fuzz_ctx *ctx, int qi) {
  uint8_t buf[128];
  uint64_t seq = ctx->next_seq[qi]++;
  emq_status st;

  if (!ctx->qs[qi] || ctx->ref[qi].closed) return;
  EMQ_REQUIRE(emq_payload_fill(buf, ctx->payload, seq, (uint32_t)qi) == 0);
  st = emq_push(ctx->qs[qi], buf, ctx->payload, NULL);
  if (st == EMQ_OK) ref_push(&ctx->ref[qi], seq);
}

static void do_pop(fuzz_ctx *ctx, int qi) {
  emq_message msg;
  emq_status st;

  if (!ctx->qs[qi] || ctx->ref[qi].closed) return;
  st = emq_try_pop(ctx->qs[qi], &msg);
  if (st == EMQ_ERR_EMPTY) return;
  EMQ_REQUIRE(st == EMQ_OK);
  {
    uint64_t seq;
    EMQ_REQUIRE(emq_payload_check(msg.data, msg.size, &seq, NULL) == 0);
    if (ctx->ref[qi].depth > 0) {
      uint64_t expect;
      EMQ_REQUIRE(ref_pop(&ctx->ref[qi], &expect));
      EMQ_REQUIRE(seq == expect);
    }
  }
  emq_message_release(&msg);
}

static void do_ack(fuzz_ctx *ctx, int qi) {
  (void)ctx;
  (void)qi;
  /* FAST + AT_MOST_ONCE: ack is a no-op path; included for op coverage. */
}

static void do_stats(fuzz_ctx *ctx, int qi) {
  if (!ctx->qs[qi] || ctx->ref[qi].closed) return;
  check_stats(ctx, ctx->qs[qi], &ctx->ref[qi]);
}

static void do_close(fuzz_ctx *ctx, int qi) {
  if (ctx->ref[qi].closed) return;
  emq_queue_close(ctx->qs[qi]);
  ctx->qs[qi] = NULL;
  ctx->ref[qi].closed = 1;
}

static void do_reopen(fuzz_ctx *ctx, int qi) {
  if (!ctx->ref[qi].closed) return;
  EMQ_REQUIRE(emq_queue_open(ctx->rt, qi == 0 ? "fuzz_q0" : "fuzz_q1",
                             &ctx->qs[qi]) == EMQ_OK);
  ctx->ref[qi].closed = 0;
}

static void setup_ctx(fuzz_ctx *ctx, uint64_t seed) {
  emq_queue_opts opts;
  int i;

  memset(ctx, 0, sizeof(*ctx));
  ctx->payload = 48;
  emq_rng_seed(&ctx->rng, seed);
  EMQ_REQUIRE(emq_runtime_create(&ctx->rt) == EMQ_OK);

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = EMQ_POLICY_FIFO;
  opts.delivery = EMQ_AT_MOST_ONCE;
  opts.capacity = 512;
  opts.backpressure = EMQ_BP_MODE_DROP_NEW;
  opts.producers = 1;
  opts.consumers = 1;

  for (i = 0; i < k_queues; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "fuzz_q%d", i);
    EMQ_REQUIRE(emq_queue_create(ctx->rt, name, &opts, &ctx->qs[i]) == EMQ_OK);
  }
}

static void teardown_ctx(fuzz_ctx *ctx) {
  int i;
  for (i = 0; i < k_queues; ++i) {
    if (ctx->qs[i] && !ctx->ref[i].closed) emq_queue_close(ctx->qs[i]);
  }
  emq_runtime_destroy(ctx->rt);
}

static void run_fuzz_bytes(fuzz_ctx *ctx, const uint8_t *data, size_t size) {
  size_t i;
  for (i = 0; data && i < size; ++i) {
    uint8_t b = data[i];
    int op = b % 6;
    int qi = (b >> 3) % k_queues;

    switch (op) {
      case 0: do_push(ctx, qi); break;
      case 1: do_pop(ctx, qi); break;
      case 2: do_ack(ctx, qi); break;
      case 3: do_stats(ctx, qi); break;
      case 4: do_close(ctx, qi); break;
      case 5: do_reopen(ctx, qi); break;
    }
    if (ctx->qs[qi]) check_stats(ctx, ctx->qs[qi], &ctx->ref[qi]);
  }
}

static void run_fuzz_rng(fuzz_ctx *ctx, uint64_t iters) {
  uint64_t i;
  for (i = 0; i < iters; ++i) {
    uint8_t b = (uint8_t)emq_rng_u32(&ctx->rng);
    run_fuzz_bytes(ctx, &b, 1);
  }
}

#ifdef EMQ_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  fuzz_ctx ctx;
  setup_ctx(&ctx, 0xF005BA11u);
  run_fuzz_bytes(&ctx, data, size);
  teardown_ctx(&ctx);
  return 0;
}
#endif

#ifndef EMQ_LIBFUZZER
int main(int argc, char **argv) {
  emq_cli cli;
  fuzz_ctx ctx;
  uint64_t seed;

  emq_cli_defaults(&cli);
  if (emq_cli_parse(&cli, argc, argv) != 0) {
    fprintf(stderr, "usage: fuzz_ops [--quick] [--iters N] [--seed S]\n");
    return 2;
  }
  if (cli.quick) cli.ops = 20000;

  seed = emq_cli_seed_or_time(&cli);
  setup_ctx(&ctx, seed);
  printf("fuzz_ops seed=%llu iters=%llu\n", (unsigned long long)seed,
         (unsigned long long)cli.ops);
  run_fuzz_rng(&ctx, cli.ops);
  teardown_ctx(&ctx);
  printf("PASS fuzz_ops\n");
  return 0;
}
#endif
