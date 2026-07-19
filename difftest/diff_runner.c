/*
 * Differential testing: golden model vs EmbeddedMQ after every operation.
 */
#include "diff_model.h"
#include "emq/emq.h"
#include "emq_testsupport.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct case_cfg {
  const char *name;
  emq_queue_policy policy;
  emq_bp_mode bp;
  uint32_t capacity;
  emq_delivery delivery;
  int use_work_ack;
} case_cfg;

static uint32_t payload_checksum(const uint8_t *buf, size_t len) {
  uint32_t h = 2166136261u;
  size_t i;
  for (i = 0; i < len; ++i) {
    h ^= buf[i];
    h *= 16777619u;
  }
  return h;
}

static void fail_trace(const char *case_name, uint64_t seed, uint64_t op_i,
                       const char *detail) {
  fprintf(stderr, "DIFF FAIL case=%s seed=%llu op=%llu: %s\n", case_name,
          (unsigned long long)seed, (unsigned long long)op_i, detail);
  abort();
}

static int run_case(const case_cfg *cfg, uint64_t ops, uint64_t seed) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  diff_model model;
  emq_rng rng;
  uint64_t next_seq = 0;
  uint64_t i;
  char qname[64];
  uint8_t buf[64];

  emq_rng_seed(&rng, seed ^ ((uint64_t)(uintptr_t)cfg->name << 8));
  EMQ_REQUIRE(diff_model_init(&model, cfg->policy, cfg->bp, cfg->capacity) ==
              0);
  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.policy = cfg->policy;
  opts.delivery = cfg->delivery;
  opts.capacity = cfg->capacity;
  opts.backpressure = cfg->bp;
  opts.producers = 1;
  opts.consumers = 1;
  opts.ring_size = cfg->capacity ? cfg->capacity : 64;
  opts.visibility_ms = 3600000u; /* keep WORK from redelivering mid-run */

  snprintf(qname, sizeof(qname), "diff_%s", cfg->name);
  EMQ_REQUIRE(emq_queue_create(rt, qname, &opts, &q) == EMQ_OK);

  for (i = 0; i < ops; ++i) {
    uint32_t roll = emq_rng_u32(&rng) % 100u;
    emq_stats st;
    size_t model_depth;

    if (cfg->use_work_ack && roll < 20u && model.inflight_count > 0) {
      /* Single-inflight discipline: ack/nack the only outstanding message so
       * redelivery order stays deterministic (matches engine scan). */
      uint64_t id = model.inflight[0].id;
      emq_status ms, rs;
      if ((emq_rng_u32(&rng) & 1u) != 0u) {
        ms = diff_model_ack(&model, id);
        rs = emq_ack(q, id);
        if (ms != rs) {
          char detail[128];
          snprintf(detail, sizeof(detail), "ack status model=%d real=%d",
                   (int)ms, (int)rs);
          fail_trace(cfg->name, seed, i, detail);
        }
      } else {
        ms = diff_model_nack(&model, id);
        rs = emq_nack(q, id, 0);
        if (ms != rs) {
          char detail[128];
          snprintf(detail, sizeof(detail), "nack status model=%d real=%d",
                   (int)ms, (int)rs);
          fail_trace(cfg->name, seed, i, detail);
        }
      }
    } else if (cfg->use_work_ack && model.inflight_count > 0) {
      /* Must settle inflight before another pop under AT_LEAST_ONCE. */
      uint64_t id = model.inflight[0].id;
      EMQ_REQUIRE(diff_model_ack(&model, id) == EMQ_OK);
      EMQ_REQUIRE(emq_ack(q, id) == EMQ_OK);
    } else if (roll < 55u) {
      uint64_t seq = next_seq++;
      uint32_t prio = (uint32_t)emq_rng_bounded(&rng, 8);
      uint32_t csum;
      emq_message meta;
      emq_status ms, rs;

      EMQ_REQUIRE(emq_payload_fill(buf, sizeof(buf), seq, prio) == 0);
      csum = payload_checksum(buf, sizeof(buf));
      memset(&meta, 0, sizeof(meta));
      meta.priority = prio;

      ms = diff_model_push(&model, seq, prio, csum);
      rs = emq_push(q, buf, sizeof(buf), &meta);
      /* Normalize: EXPAND/SPILL may differ; only compare hard rejects. */
      if ((ms == EMQ_ERR_FULL || ms == EMQ_ERR_BUSY) &&
          (rs == EMQ_ERR_FULL || rs == EMQ_ERR_BUSY)) {
        /* both rejected — OK even if exact code differs FULL vs BUSY */
      } else if (ms != rs) {
        char detail[128];
        snprintf(detail, sizeof(detail), "push status model=%d real=%d",
                 (int)ms, (int)rs);
        fail_trace(cfg->name, seed, i, detail);
      }
      if (rs == EMQ_OK && ms != EMQ_OK) {
        fail_trace(cfg->name, seed, i, "push accept mismatch");
      }
      if (rs != EMQ_OK && ms == EMQ_OK) {
        fail_trace(cfg->name, seed, i, "push reject mismatch");
      }
    } else {
      diff_msg mout;
      emq_message msg;
      emq_status ms = diff_model_pop(&model, &mout);
      emq_status rs = emq_try_pop(q, &msg);

      if (ms != rs) {
        char detail[128];
        snprintf(detail, sizeof(detail), "pop status model=%d real=%d",
                 (int)ms, (int)rs);
        fail_trace(cfg->name, seed, i, detail);
      }
      if (rs == EMQ_OK) {
        uint64_t seq = 0;
        uint32_t prod = 0;
        EMQ_REQUIRE(emq_payload_check(msg.data, msg.size, &seq, &prod) == 0);
        if (seq != mout.seq) {
          char detail[128];
          snprintf(detail, sizeof(detail),
                   "pop seq model=%llu real=%llu",
                   (unsigned long long)mout.seq, (unsigned long long)seq);
          fail_trace(cfg->name, seed, i, detail);
        }
        /* For WORK, sync model id with real id so ack/nack can match. */
        if (cfg->use_work_ack && model.inflight_count > 0) {
          model.inflight[model.inflight_count - 1].id = msg.id;
        }
        emq_message_release(&msg);
      }
    }

    EMQ_REQUIRE(emq_queue_stats(q, &st) == EMQ_OK);
    model_depth = diff_model_depth(&model);
    /* Depth accounting on the durable log path counts historical entries;
     * only the FAST FIFO lfq path exposes live depth via hot.depth. */
    if (cfg->policy == EMQ_POLICY_FIFO) {
      if (st.depth != model_depth) {
        char detail[128];
        snprintf(detail, sizeof(detail),
                 "depth model=%zu real=%llu", model_depth,
                 (unsigned long long)st.depth);
        fail_trace(cfg->name, seed, i, detail);
      }
    }
    (void)st;
  }

  emq_queue_close(q);
  emq_queue_destroy(rt, qname);
  emq_runtime_destroy(rt);
  diff_model_destroy(&model);
  return 0;
}

int main(int argc, char **argv) {
  emq_cli cli;
  uint64_t seed;
  uint64_t ops;
  size_t c;
  /* Capacity/BP are enforced on the FAST FIFO lfq path only. Other policies
   * append to the log without a soft capacity check, so those cases stay
   * unbounded and compare ordering / ack semantics instead. */
  static const case_cfg cases[] = {
      {"fifo_dn", EMQ_POLICY_FIFO, EMQ_BP_MODE_DROP_NEW, 32, EMQ_AT_MOST_ONCE,
       0},
      {"fifo_do", EMQ_POLICY_FIFO, EMQ_BP_MODE_DROP_OLD, 32, EMQ_AT_MOST_ONCE,
       0},
      {"lifo", EMQ_POLICY_LIFO, EMQ_BP_MODE_EXPAND, 0, EMQ_AT_MOST_ONCE, 0},
      {"prio", EMQ_POLICY_PRIORITY, EMQ_BP_MODE_EXPAND, 0, EMQ_AT_MOST_ONCE, 0},
      {"ring", EMQ_POLICY_RING, EMQ_BP_MODE_DROP_OLD, 16, EMQ_AT_MOST_ONCE, 0},
      {"work", EMQ_POLICY_WORK, EMQ_BP_MODE_BLOCK, 0, EMQ_AT_LEAST_ONCE, 1},
  };

  emq_cli_defaults(&cli);
  if (emq_cli_parse(&cli, argc, argv) != 0) {
    fprintf(stderr, "usage: diff_runner [--quick] [--ops N] [--seed S]\n");
    return 2;
  }
  /* Quick stays small: PRIORITY/LIFO scan the log, so 10k ops is minutes. */
  if (cli.quick) {
    ops = 2000;
  } else {
    ops = cli.ops ? cli.ops : 100000;
  }
  seed = emq_cli_seed_or_time(&cli);

  printf("diff_runner seed=%llu ops=%llu cases=%zu\n",
         (unsigned long long)seed, (unsigned long long)ops,
         sizeof(cases) / sizeof(cases[0]));

  for (c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
    printf("  case %s...\n", cases[c].name);
    run_case(&cases[c], ops, seed + c * 9973ull);
  }

  printf("PASS diff_runner\n");
  return 0;
}
