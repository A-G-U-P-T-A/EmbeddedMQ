#include "policy/emq_policy.h"

#include <stddef.h>

static int emq_bp_drop_new(void *ctx, emq_backpressure bp) {
  (void)ctx;
  (void)bp;
  return EMQ_ERR_FULL;
}

static int emq_bp_drop_old(void *ctx, emq_backpressure bp) {
  (void)ctx;
  (void)bp;
  return 1;
}

static int emq_bp_block(void *ctx, emq_backpressure bp) {
  (void)ctx;
  (void)bp;
  return EMQ_ERR_BUSY;
}

static int emq_bp_spill(void *ctx, emq_backpressure bp) {
  (void)ctx;
  (void)bp;
  return 0;
}

static int emq_bp_expand(void *ctx, emq_backpressure bp) {
  (void)ctx;
  (void)bp;
  return 0;
}

static int emq_bp_overwrite(void *ctx, emq_backpressure bp) {
  (void)ctx;
  (void)bp;
  return 1;
}

static const emq_policy_ops g_policy_ops[] = {
  {emq_bp_drop_new, "drop_new"},
  {emq_bp_drop_old, "drop_old"},
  {emq_bp_block, "block"},
  {emq_bp_spill, "spill"},
  {emq_bp_expand, "expand"},
  {emq_bp_overwrite, "overwrite"}
};

static emq_ordering emq_ordering_from_policy(emq_queue_policy policy) {
  switch (policy) {
    case EMQ_POLICY_LIFO:
      return EMQ_ORDER_LIFO;
    case EMQ_POLICY_PRIORITY:
      return EMQ_ORDER_PRIORITY;
    case EMQ_POLICY_RANDOM:
      return EMQ_ORDER_RANDOM;
    default:
      return EMQ_ORDER_FIFO;
  }
}

static emq_backpressure emq_backpressure_from_opts(const emq_queue_opts *opts) {
  /* Explicit public mode wins when set to a non-default meaningful value.
   * Default opts use BLOCK; policy presets still apply for RING/STREAM/WORK. */
  switch (opts->policy) {
    case EMQ_POLICY_RING:
      return EMQ_BP_DROP_OLD;
    case EMQ_POLICY_STREAM:
      return EMQ_BP_SPILL;
    case EMQ_POLICY_WORK:
      return EMQ_BP_BLOCK;
    default:
      break;
  }
  switch ((emq_backpressure)opts->backpressure) {
    case EMQ_BP_DROP_NEW:
    case EMQ_BP_DROP_OLD:
    case EMQ_BP_BLOCK:
    case EMQ_BP_SPILL:
    case EMQ_BP_EXPAND:
    case EMQ_BP_OVERWRITE:
      return (emq_backpressure)opts->backpressure;
    default:
      break;
  }
  if (opts->capacity != 0) {
    return EMQ_BP_DROP_NEW;
  }
  return EMQ_BP_EXPAND;
}

static emq_timing emq_timing_from_policy(emq_queue_policy policy) {
  return (policy == EMQ_POLICY_DELAY) ? EMQ_TIMING_DELAY : EMQ_TIMING_IMMEDIATE;
}

emq_policy emq_policy_from_queue_opts(const emq_queue_opts *opts) {
  emq_policy p;
  uint32_t cap;

  if (!opts) {
    emq_policy empty = {0};
    return empty;
  }

  cap = opts->capacity;
  p.ordering = emq_ordering_from_policy(opts->policy);
  p.delivery = opts->delivery;
  p.backpressure = emq_backpressure_from_opts(opts);
  p.timing = emq_timing_from_policy(opts->policy);
  p.storage = opts->storage;
  p.capacity = cap;
  if (cap == 0) {
    p.high_watermark = 0;
    p.low_watermark = 0;
  } else {
    p.high_watermark = (cap * 4u) / 5u;
    p.low_watermark = cap / 2u;
    if (p.high_watermark == 0) {
      p.high_watermark = cap;
    }
    if (p.low_watermark == 0 && cap > 1) {
      p.low_watermark = 1;
    }
  }
  return p;
}

const emq_policy_ops *emq_policy_ops_get(emq_backpressure bp) {
  if ((unsigned)bp >= (unsigned)(sizeof(g_policy_ops) / sizeof(g_policy_ops[0]))) {
    return &g_policy_ops[EMQ_BP_DROP_NEW];
  }
  return &g_policy_ops[bp];
}

int emq_policy_decide_full(const emq_policy *p, uint32_t depth, uint32_t capacity) {
  uint32_t cap;

  if (!p) {
    return EMQ_ERR_INVALID;
  }

  cap = capacity;
  if (cap == 0) {
    cap = p->capacity;
  }
  if (cap == 0 || depth < cap) {
    return 0;
  }

  switch (p->backpressure) {
    case EMQ_BP_DROP_NEW:
      return EMQ_ERR_FULL;
    case EMQ_BP_DROP_OLD:
    case EMQ_BP_OVERWRITE:
      return 1;
    case EMQ_BP_BLOCK:
      return EMQ_ERR_BUSY;
    case EMQ_BP_SPILL:
    case EMQ_BP_EXPAND:
      return 2;
    default:
      return EMQ_ERR_FULL;
  }
}
