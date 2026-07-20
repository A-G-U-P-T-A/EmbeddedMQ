#ifndef EMQ_POLICY_H
#define EMQ_POLICY_H

#include "emq/emq_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum emq_backpressure {
  EMQ_BP_DROP_NEW = 0,
  EMQ_BP_DROP_OLD = 1,
  EMQ_BP_BLOCK = 2,
  EMQ_BP_SPILL = 3,
  EMQ_BP_EXPAND = 4,
  EMQ_BP_OVERWRITE = 5
} emq_backpressure;

typedef enum emq_ordering {
  EMQ_ORDER_FIFO = 0,
  EMQ_ORDER_LIFO = 1,
  EMQ_ORDER_PRIORITY = 2,
  EMQ_ORDER_RANDOM = 3
} emq_ordering;

typedef enum emq_timing {
  EMQ_TIMING_IMMEDIATE = 0,
  EMQ_TIMING_DELAY = 1
} emq_timing;

typedef struct emq_policy {
  emq_ordering ordering;
  emq_delivery delivery;
  emq_backpressure backpressure;
  emq_timing timing;
  emq_storage_mode storage;
  uint32_t capacity;
  uint32_t high_watermark;
  uint32_t low_watermark;
} emq_policy;

typedef struct emq_policy_ops {
  /* return 0 allow retry, -4 reject, 1 dropped-old-ok */
  int (*on_full)(void *ctx, emq_backpressure bp);
  const char *name;
} emq_policy_ops;

emq_policy emq_policy_from_queue_opts(const emq_queue_opts *opts);
const emq_policy_ops *emq_policy_ops_get(emq_backpressure bp);
int emq_policy_decide_full(const emq_policy *p, uint32_t depth, uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_POLICY_H */
