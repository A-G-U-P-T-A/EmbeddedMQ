#ifndef EMQ_PRIMITIVES_H
#define EMQ_PRIMITIVES_H

#include "registry/emq_registry.h"
#include "sched/emq_wheel.h"
#include "emq/emq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int emq_prim_push(emq_queue_desc *q, const void *data, size_t size,
                  const emq_message *meta);
int emq_prim_pop(emq_queue_desc *q, emq_message *out, uint32_t timeout_ms,
                 emq_wheel *wheel);
int emq_prim_peek(emq_queue_desc *q, emq_message *out);
int emq_prim_ack(emq_queue_desc *q, uint64_t message_id);
int emq_prim_nack(emq_queue_desc *q, uint64_t message_id, uint32_t delay_ms,
                  emq_wheel *wheel);
int emq_prim_ack_batch(emq_queue_desc *q, const uint64_t *ids, size_t count);
int emq_prim_seek(emq_queue_desc *q, uint64_t offset);

/* Policy-specific helpers (also used directly by tests) */
int emq_fifo_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta);
int emq_fifo_pop(emq_queue_desc *q, emq_message *out);
/* Copy into caller buffer; no malloc. -1 if dst too small (not consumed). */
int emq_fifo_pop_into(emq_queue_desc *q, void *dst, uint32_t dst_cap,
                      uint32_t *out_len, uint64_t *out_id, uint32_t *out_flags,
                      uint32_t *out_prio);
int emq_priority_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta);
int emq_priority_pop(emq_queue_desc *q, emq_message *out);
int emq_ring_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta);
int emq_ring_pop(emq_queue_desc *q, emq_message *out);
int emq_delay_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta,
                   emq_wheel *wheel);
int emq_delay_pop(emq_queue_desc *q, emq_message *out);
int emq_work_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta);
int emq_work_pop(emq_queue_desc *q, emq_message *out);
int emq_stream_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta);
int emq_stream_pop(emq_queue_desc *q, emq_message *out);
int emq_pubsub_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta);
int emq_pubsub_pop(emq_queue_desc *q, emq_message *out);

/* Private delivery-state helpers. These use emq_inflight.in_use as a state tag. */
#define EMQ_INFLIGHT_ACTIVE 1
#define EMQ_INFLIGHT_CONSUMED 2
#define EMQ_INFLIGHT_TTL 3
#define EMQ_INFLIGHT_RESERVED 4

emq_inflight *emq_prim_state_slot(emq_queue_desc *q, int state);
emq_inflight *emq_prim_state_find(emq_queue_desc *q, int state,
                                  uint64_t msg_id, uint64_t offset);
void emq_prim_state_clear(emq_inflight *state);
int emq_prim_track_ttl(emq_queue_desc *q, uint64_t msg_id, uint64_t offset,
                       uint64_t ttl_ns);
int emq_prim_entry_expired(emq_queue_desc *q, const emq_log_entry *entry,
                           uint64_t now_ns);
void emq_prim_fill_message(emq_queue_desc *q, const emq_log_entry *entry,
                           emq_message *out, uint64_t now_ns);
int emq_prim_offset_taken(emq_queue_desc *q, uint64_t offset);
int emq_prim_mark_consumed(emq_queue_desc *q, uint64_t msg_id, uint64_t offset);
void emq_prim_compact_consumed(emq_queue_desc *q);
void emq_prim_discard_before(emq_queue_desc *q, uint64_t offset);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_PRIMITIVES_H */
