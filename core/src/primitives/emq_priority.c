#include "primitives/emq_primitives.h"
#include "platform/emq_platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

int emq_priority_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta) {
  return emq_fifo_push(q, data, size, meta);
}

int emq_priority_pop(emq_queue_desc *q, emq_message *out) {
  uint64_t best_off = UINT64_MAX;
  uint32_t best_prio = 0;
  int found = 0;
  uint64_t off;
  uint64_t now;
  uint64_t next;
  emq_log_entry e;

  if (!q || !out || q->closed) return -9;
  now = emq_now_ns();
  next = emq_log_next_offset(q->log);

  for (off = q->read_offset; off < next; ++off) {
    if (emq_prim_offset_taken(q, off)) continue;
    if (emq_log_read(q->log, off, &e) != 0) continue;
    if (emq_prim_entry_expired(q, &e, now)) {
      if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) return -2;
      continue;
    }
    if (e.deliver_at_ns != 0 && e.deliver_at_ns > now) continue;
    if (!found || e.priority > best_prio ||
        (e.priority == best_prio && e.offset < best_off)) {
      best_off = e.offset;
      best_prio = e.priority;
      found = 1;
    }
  }
  emq_prim_compact_consumed(q);
  if (!found) return -5;

  if (emq_log_read_copy(q->log, best_off, &e) != 0) return -5;
  if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) {
    free(e.payload);
    return -2;
  }
  emq_prim_fill_message(q, &e, out, now);
  emq_prim_compact_consumed(q);
  q->stats.dequeued++;
  return 0;
}
