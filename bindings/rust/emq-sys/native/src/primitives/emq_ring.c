#include "primitives/emq_primitives.h"

int emq_ring_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta) {
  int rc = emq_fifo_push(q, data, size, meta);
  if (rc == 0) {
    uint64_t next = emq_log_next_offset(q->log);
    uint64_t count = emq_log_count(q->log);
    uint64_t first = next >= count ? next - count : 0;
    emq_prim_discard_before(q, first);
    q->stats.depth = count;
  }
  return rc;
}

int emq_ring_pop(emq_queue_desc *q, emq_message *out) {
  return emq_fifo_pop(q, out);
}
