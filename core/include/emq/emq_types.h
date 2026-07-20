#ifndef EMQ_TYPES_H
#define EMQ_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMQ_INLINE_PAYLOAD_MAX 256u
#define EMQ_NAME_MAX 128u
#define EMQ_TOPIC_MAX 256u

/* Public message.flags bits (library-owned range 0x80000000..). */
#define EMQ_MSG_FLAG_BORROWED 0x80000000u /* peek: do not free data */
#define EMQ_MSG_FLAG_CLAIMED 0x40000000u  /* zero-copy claim into ring */

typedef enum emq_status {
  EMQ_OK = 0,
  EMQ_ERR_INVALID = -1,
  EMQ_ERR_NOMEM = -2,
  EMQ_ERR_NOT_FOUND = -3,
  EMQ_ERR_FULL = -4,
  EMQ_ERR_EMPTY = -5,
  EMQ_ERR_IO = -6,
  EMQ_ERR_TIMEOUT = -7,
  EMQ_ERR_EXISTS = -8,
  EMQ_ERR_CLOSED = -9,
  EMQ_ERR_BUSY = -10,
  EMQ_ERR_UNSUPPORTED = -11
} emq_status;

typedef enum emq_storage_mode {
  EMQ_STORAGE_FAST = 0,
  EMQ_STORAGE_DURABLE = 1,
  EMQ_STORAGE_MMAP = 2,
  EMQ_STORAGE_HYBRID = 3,
  EMQ_STORAGE_RING = 4,
  EMQ_STORAGE_STREAM = 5
} emq_storage_mode;

typedef enum emq_queue_policy {
  EMQ_POLICY_FIFO = 0,
  EMQ_POLICY_PRIORITY = 1,
  EMQ_POLICY_RING = 2,
  EMQ_POLICY_BROADCAST = 3,
  EMQ_POLICY_STREAM = 4,
  EMQ_POLICY_WORK = 5,
  EMQ_POLICY_DELAY = 6,
  EMQ_POLICY_RANDOM = 7,
  EMQ_POLICY_LIFO = 8,
  EMQ_POLICY_PUBSUB = 9
} emq_queue_policy;

typedef enum emq_delivery {
  EMQ_AT_MOST_ONCE = 0,
  EMQ_AT_LEAST_ONCE = 1
} emq_delivery;

typedef enum emq_fsync_policy {
  EMQ_FSYNC_NONE = 0,
  EMQ_FSYNC_EVERY_WRITE = 1,
  EMQ_FSYNC_INTERVAL = 2
} emq_fsync_policy;

/* Public backpressure modes (mirror policy engine). */
typedef enum emq_bp_mode {
  EMQ_BP_MODE_DROP_NEW = 0,
  EMQ_BP_MODE_DROP_OLD = 1,
  EMQ_BP_MODE_BLOCK = 2,
  EMQ_BP_MODE_SPILL = 3,
  EMQ_BP_MODE_EXPAND = 4,
  EMQ_BP_MODE_OVERWRITE = 5
} emq_bp_mode;

typedef struct emq_queue_opts {
  emq_storage_mode storage;
  emq_queue_policy policy;
  emq_delivery delivery;
  emq_fsync_policy fsync;
  uint32_t capacity;          /* 0 = unbounded / default */
  uint32_t visibility_ms;     /* work-queue visibility timeout */
  uint32_t inline_threshold;  /* bytes; 0 = default 256 */
  uint32_t ring_size;         /* for ring policy */
  const char *path;           /* durable/mmap directory; NULL for RAM */
  uint32_t producers;         /* concurrency hint: 0/1 = SPSC-capable */
  uint32_t consumers;         /* concurrency hint: 0/1 = SPSC-capable */
  emq_bp_mode backpressure;   /* default DROP_NEW when capacity set */
  uint32_t high_watermark;    /* 0 = 80% of capacity */
  uint32_t low_watermark;     /* 0 = 50% of capacity */
} emq_queue_opts;

typedef struct emq_message {
  uint64_t id;
  uint64_t offset;
  uint32_t priority;
  uint64_t deliver_at_ns;
  uint64_t ttl_ns;
  const void *data;
  size_t size;
  uint32_t flags;
} emq_message;

typedef struct emq_batch_item {
  const void *data;
  size_t size;
  emq_message meta;
} emq_batch_item;

typedef struct emq_stats {
  uint64_t enqueued;
  uint64_t dequeued;
  uint64_t acked;
  uint64_t nacked;
  uint64_t expired;
  uint64_t depth;
  uint64_t bytes;
  uint64_t redelivered;
} emq_stats;

typedef struct emq_runtime_stats {
  uint64_t queues;
  uint64_t scheduler_activations;
  uint64_t wakeups;
  uint64_t allocator_hits;
  uint64_t allocator_misses;
  uint64_t allocator_live_bytes;
  uint64_t malloc_fallbacks;
  uint64_t worker_jobs;
} emq_runtime_stats;

typedef struct emq_runtime emq_runtime;
typedef struct emq_queue emq_queue;
typedef struct emq_subscription emq_subscription;

typedef void (*emq_completion_cb)(emq_status status, void *user);
typedef void (*emq_message_cb)(emq_status status, emq_message *message,
                               void *user);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_TYPES_H */
