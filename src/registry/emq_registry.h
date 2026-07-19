#ifndef EMQ_REGISTRY_H
#define EMQ_REGISTRY_H

#include "emq/emq_types.h"
#include "core/emq_log.h"
#include "core/emq_lfq.h"
#include "core/emq_hot.h"
#include "core/emq_pool.h"
#include "policy/emq_policy.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMQ_MAX_QUEUES 100000u
#define EMQ_QUEUE_TABLE_CAPACITY (EMQ_MAX_QUEUES + 1u)
#define EMQ_BITMAP_WORDS ((EMQ_QUEUE_TABLE_CAPACITY + 63u) / 64u)

typedef struct emq_inflight {
  uint64_t msg_id;
  uint64_t offset;
  uint64_t visible_at_ns;
  uint8_t *payload;
  uint32_t payload_len;
  uint32_t priority;
  int in_use;
} emq_inflight;

/* Cold metadata — allocated lazily / lives beside hot slot. */
typedef struct emq_queue_cold {
  char name[EMQ_NAME_MAX];
  emq_queue_opts opts;
  emq_policy policy;
  emq_stats stats;
} emq_queue_cold;

typedef struct emq_queue_desc {
  char name[EMQ_NAME_MAX];
  uint32_t id;
  uint32_t generation;
  emq_queue_opts opts;
  emq_policy policy;
  emq_log *log;
  emq_lfq *lfq;             /* FAST FIFO lock-free path */
  emq_hot_slot hot;         /* 64B hot control */
  emq_pool *pool;           /* borrowed from runtime; not owned */
  uint64_t read_offset;
  uint64_t consumer_offset;
  emq_stats stats;
  emq_inflight *inflight;
  uint32_t inflight_cap;
  uint32_t inflight_hint;   /* rotating free-slot search start */
  void *op_mu_opaque;       /* control-plane / legacy path */
  void *ready_cond_opaque;
  uint32_t handle_count;
  int active;
  int closed;
  volatile uint64_t seq_wait; /* futex wait word for blocking pop */
} emq_queue_desc;

typedef struct emq_registry {
  emq_queue_desc **queues;
  emq_queue_desc **name_index;
  uint32_t capacity;
  uint32_t name_capacity;
  uint32_t count;
  uint32_t next_id;
  uint64_t active_bitmap[EMQ_BITMAP_WORDS];
  void *mu_opaque;
  emq_pool *pool; /* shared payload pool; not owned */
} emq_registry;

int emq_registry_init(emq_registry *r, uint32_t capacity);
void emq_registry_destroy(emq_registry *r);
void emq_registry_set_pool(emq_registry *r, emq_pool *pool);

int emq_registry_create(emq_registry *r, const char *name,
                        const emq_queue_opts *opts, emq_queue_desc **out);
emq_queue_desc *emq_registry_find(emq_registry *r, const char *name);
emq_queue_desc *emq_registry_get(emq_registry *r, uint32_t id);
int emq_registry_remove(emq_registry *r, const char *name);

void emq_registry_set_active(emq_registry *r, uint32_t id, int active);
int emq_registry_is_active(const emq_registry *r, uint32_t id);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_REGISTRY_H */
