#include "primitives/emq_primitives.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

int emq_stream_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta) {
  return emq_fifo_push(q, data, size, meta);
}

int emq_stream_pop(emq_queue_desc *q, emq_message *out) {
  emq_log_entry e;
  uint64_t off;
  uint64_t now;
  if (!q || !out) return -9;
  off = q->consumer_offset;
  now = emq_now_ns();
  while (off < emq_log_next_offset(q->log)) {
    if (emq_log_read_copy(q->log, off, &e) != 0) {
      off++;
      q->consumer_offset = off;
      continue;
    }
    if (emq_prim_entry_expired(q, &e, now)) {
      free(e.payload);
      off++;
      q->consumer_offset = off;
      continue;
    }
    if (e.deliver_at_ns != 0 && e.deliver_at_ns > now) {
      free(e.payload);
      return -5;
    }
    emq_prim_fill_message(q, &e, out, now);
    q->consumer_offset = off + 1;
    q->stats.dequeued++;
    return 0;
  }
  return -5;
}
