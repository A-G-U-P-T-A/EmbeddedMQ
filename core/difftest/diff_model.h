#ifndef EMQ_DIFF_MODEL_H
#define EMQ_DIFF_MODEL_H

#include "emq/emq_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct diff_msg {
  uint64_t seq;
  uint64_t id; /* for WORK ack matching; mirrors emq message id when known */
  uint32_t priority;
  uint32_t checksum;
} diff_msg;

typedef struct diff_model {
  emq_queue_policy policy;
  emq_bp_mode bp;
  uint32_t capacity; /* 0 = unbounded */
  diff_msg *items;
  size_t count;
  size_t cap;
  /* WORK: reserved (inflight) set */
  diff_msg *inflight;
  size_t inflight_count;
  size_t inflight_cap;
  uint64_t next_id;
} diff_model;

int diff_model_init(diff_model *m, emq_queue_policy policy, emq_bp_mode bp,
                    uint32_t capacity);
void diff_model_destroy(diff_model *m);

/* Returns EMQ_OK / EMQ_ERR_FULL / EMQ_ERR_BUSY matching public API semantics. */
emq_status diff_model_push(diff_model *m, uint64_t seq, uint32_t priority,
                           uint32_t checksum);
/* Returns EMQ_OK / EMQ_ERR_EMPTY. On OK fills *out. */
emq_status diff_model_pop(diff_model *m, diff_msg *out);
emq_status diff_model_ack(diff_model *m, uint64_t id);
emq_status diff_model_nack(diff_model *m, uint64_t id);

size_t diff_model_depth(const diff_model *m);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_DIFF_MODEL_H */
