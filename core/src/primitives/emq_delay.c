#include "primitives/emq_primitives.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

int emq_delay_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta,
                   emq_wheel *wheel) {
  emq_message m;
  uint64_t deliver;
  int rc;
  if (!meta || meta->deliver_at_ns == 0) {
    return emq_fifo_push(q, data, size, meta);
  }
  m = *meta;
  deliver = m.deliver_at_ns;
  rc = emq_fifo_push(q, data, size, &m);
  if (rc == 0 && wheel) {
    emq_wheel_schedule(wheel, deliver, (void *)(uintptr_t)q->id);
  }
  return rc;
}

int emq_delay_pop(emq_queue_desc *q, emq_message *out) {
  emq_log_entry e;
  uint64_t off;
  uint64_t now = emq_now_ns();
  if (!q || !out) return -9;

  for (off = q->read_offset; off < emq_log_next_offset(q->log); ++off) {
    if (emq_prim_offset_taken(q, off)) continue;
    if (emq_log_read_copy(q->log, off, &e) != 0) continue;
    if (emq_prim_entry_expired(q, &e, now)) {
      free(e.payload);
      if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) return -2;
      continue;
    }
    if (e.deliver_at_ns != 0 && e.deliver_at_ns > now) {
      free(e.payload);
      continue;
    }
    if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) {
      free(e.payload);
      return -2;
    }
    emq_prim_fill_message(q, &e, out, now);
    emq_prim_compact_consumed(q);
    q->stats.dequeued++;
    return 0;
  }
  emq_prim_compact_consumed(q);
  return -5;
}
