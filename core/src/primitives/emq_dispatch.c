#include "primitives/emq_primitives.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

void emq_prim_state_clear(emq_inflight *state) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
}

emq_inflight *emq_prim_state_slot(emq_queue_desc *q, int state) {
  uint32_t i;
  uint32_t idx;
  uint32_t start;
  emq_inflight *grown;
  uint32_t old_cap;
  uint32_t new_cap;
  if (!q) return NULL;
  if (!q->inflight) {
    q->inflight_cap = 8u;
    q->inflight = (emq_inflight *)calloc(q->inflight_cap,
                                          sizeof(*q->inflight));
    if (!q->inflight) {
      q->inflight_cap = 0;
      return NULL;
    }
  }
  /* Rotating hint keeps the free-slot search amortized O(1) instead of
   * rescanning a mostly-occupied array from index 0 on every request. */
  start = q->inflight_hint < q->inflight_cap ? q->inflight_hint : 0;
  for (i = 0; i < q->inflight_cap; ++i) {
    idx = start + i;
    if (idx >= q->inflight_cap) idx -= q->inflight_cap;
    if (q->inflight[idx].in_use == 0) {
      q->inflight[idx].in_use = state;
      q->inflight_hint = idx + 1u;
      return &q->inflight[idx];
    }
  }
  old_cap = q->inflight_cap;
  new_cap = old_cap ? old_cap * 2u : 8u;
  if (new_cap <= old_cap) return NULL;
  grown = (emq_inflight *)realloc(q->inflight, sizeof(*grown) * new_cap);
  if (!grown) return NULL;
  memset(grown + old_cap, 0, sizeof(*grown) * (new_cap - old_cap));
  q->inflight = grown;
  q->inflight_cap = new_cap;
  q->inflight[old_cap].in_use = state;
  q->inflight_hint = old_cap + 1u;
  return &q->inflight[old_cap];
}

emq_inflight *emq_prim_state_find(emq_queue_desc *q, int state,
                                  uint64_t msg_id, uint64_t offset) {
  uint32_t i;
  if (!q || !q->inflight) return NULL;
  for (i = 0; i < q->inflight_cap; ++i) {
    emq_inflight *entry = &q->inflight[i];
    if (entry->in_use != state) continue;
    if (msg_id != 0 && entry->msg_id != msg_id) continue;
    if (offset != UINT64_MAX && entry->offset != offset) continue;
    return entry;
  }
  return NULL;
}

static void emq_prim_forget_ttl(emq_queue_desc *q, uint64_t msg_id,
                                uint64_t offset) {
  emq_inflight *ttl = emq_prim_state_find(q, EMQ_INFLIGHT_TTL, msg_id, offset);
  if (ttl) emq_prim_state_clear(ttl);
}

int emq_prim_track_ttl(emq_queue_desc *q, uint64_t msg_id, uint64_t offset,
                       uint64_t ttl_ns) {
  emq_inflight *ttl;
  uint64_t now;
  if (ttl_ns == 0) return 0;
  ttl = emq_prim_state_slot(q, EMQ_INFLIGHT_TTL);
  if (!ttl) return -2;
  now = emq_now_ns();
  ttl->msg_id = msg_id;
  ttl->offset = offset;
  ttl->visible_at_ns = UINT64_MAX - now < ttl_ns ? UINT64_MAX : now + ttl_ns;
  return 0;
}

int emq_prim_entry_expired(emq_queue_desc *q, const emq_log_entry *entry,
                           uint64_t now_ns) {
  emq_inflight *ttl;
  uint64_t expiry;
  if (!q || !entry) return 0;
  ttl = emq_prim_state_find(q, EMQ_INFLIGHT_TTL, entry->msg_id, entry->offset);
  if (ttl) {
    if (ttl->visible_at_ns > now_ns) return 0;
    emq_prim_state_clear(ttl);
  } else {
    if (entry->ttl_ns == 0) return 0;
    expiry = UINT64_MAX - entry->timestamp_ns < entry->ttl_ns ?
             UINT64_MAX : entry->timestamp_ns + entry->ttl_ns;
    if (expiry > now_ns) return 0;
  }
  q->stats.expired++;
  return 1;
}

void emq_prim_fill_message(emq_queue_desc *q, const emq_log_entry *entry,
                           emq_message *out, uint64_t now_ns) {
  emq_inflight *ttl;
  (void)now_ns;
  memset(out, 0, sizeof(*out));
  out->id = entry->msg_id;
  out->offset = entry->offset;
  out->priority = entry->priority;
  out->deliver_at_ns = entry->deliver_at_ns;
  out->data = entry->payload;
  out->size = entry->payload_len;
  out->flags = entry->flags;
  out->ttl_ns = entry->ttl_ns;
  ttl = emq_prim_state_find(q, EMQ_INFLIGHT_TTL, entry->msg_id, entry->offset);
  if (ttl && ttl->visible_at_ns > entry->timestamp_ns) {
    out->ttl_ns = ttl->visible_at_ns - entry->timestamp_ns;
  }
}

int emq_prim_offset_taken(emq_queue_desc *q, uint64_t offset) {
  if (!q) return 0;
  if (emq_prim_state_find(q, EMQ_INFLIGHT_CONSUMED, 0, offset)) return 1;
  if (emq_prim_state_find(q, EMQ_INFLIGHT_ACTIVE, 0, offset)) return 1;
  return 0;
}

int emq_prim_mark_consumed(emq_queue_desc *q, uint64_t msg_id, uint64_t offset) {
  emq_inflight *taken;
  if (!q) return -1;
  if (emq_prim_state_find(q, EMQ_INFLIGHT_CONSUMED, 0, offset)) return 0;
  taken = emq_prim_state_slot(q, EMQ_INFLIGHT_CONSUMED);
  if (!taken) return -2;
  taken->msg_id = msg_id;
  taken->offset = offset;
  return 0;
}

void emq_prim_compact_consumed(emq_queue_desc *q) {
  emq_inflight *taken;
  uint64_t trim_to;
  uint32_t i;
  if (!q) return;
  for (;;) {
    taken = emq_prim_state_find(q, EMQ_INFLIGHT_CONSUMED, 0, q->read_offset);
    if (!taken) break;
    emq_prim_state_clear(taken);
    q->read_offset++;
  }
  if (emq_prim_state_find(q, EMQ_INFLIGHT_RESERVED, 0, UINT64_MAX)) return;
  trim_to = q->read_offset;
  for (i = 0; i < q->inflight_cap; ++i) {
    emq_inflight *entry = &q->inflight[i];
    if (entry->in_use == EMQ_INFLIGHT_ACTIVE && entry->offset < trim_to) {
      trim_to = entry->offset;
    }
  }
  if (trim_to != 0) (void)emq_log_trim_front(q->log, trim_to);
}

void emq_prim_discard_before(emq_queue_desc *q, uint64_t offset) {
  uint32_t i;
  if (!q) return;
  for (i = 0; i < q->inflight_cap; ++i) {
    if (q->inflight[i].in_use != 0 && q->inflight[i].offset < offset) {
      emq_prim_state_clear(&q->inflight[i]);
    }
  }
  if (q->read_offset < offset) q->read_offset = offset;
  if (q->consumer_offset < offset) q->consumer_offset = offset;
}

int emq_prim_push(emq_queue_desc *q, const void *data, size_t size,
                  const emq_message *meta) {
  if (!q) return -1;
  switch (q->opts.policy) {
    case EMQ_POLICY_PRIORITY: return emq_priority_push(q, data, size, meta);
    case EMQ_POLICY_RING: return emq_ring_push(q, data, size, meta);
    case EMQ_POLICY_DELAY: return emq_delay_push(q, data, size, meta, NULL);
    case EMQ_POLICY_WORK: return emq_work_push(q, data, size, meta);
    case EMQ_POLICY_STREAM: return emq_stream_push(q, data, size, meta);
    case EMQ_POLICY_PUBSUB:
    case EMQ_POLICY_BROADCAST: return emq_pubsub_push(q, data, size, meta);
    case EMQ_POLICY_LIFO:
    case EMQ_POLICY_RANDOM:
    case EMQ_POLICY_FIFO:
    default: return emq_fifo_push(q, data, size, meta);
  }
}

static int emq_lifo_pop(emq_queue_desc *q, emq_message *out) {
  uint64_t next = emq_log_next_offset(q->log);
  emq_log_entry e;
  uint64_t off;
  uint64_t now = emq_now_ns();
  if (next == 0 || q->read_offset >= next) return -5;
  off = next - 1;
  while (off >= q->read_offset) {
    if (!emq_prim_offset_taken(q, off) &&
        emq_log_read_copy(q->log, off, &e) == 0) {
      if (emq_prim_entry_expired(q, &e, now)) {
        free(e.payload);
        if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) return -2;
        emq_prim_compact_consumed(q);
      } else if (e.deliver_at_ns == 0 || e.deliver_at_ns <= now) {
        if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) {
          free(e.payload);
          return -2;
        }
        emq_prim_fill_message(q, &e, out, now);
        emq_prim_compact_consumed(q);
        q->stats.dequeued++;
        return 0;
      } else {
        free(e.payload);
      }
    }
    if (off == 0) break;
    off--;
  }
  return -5;
}

static uint64_t emq_random_step(uint64_t *state) {
  uint64_t x = *state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  *state = x;
  return x * 2685821657736338717ULL;
}

static int emq_random_pop(emq_queue_desc *q, emq_message *out) {
  uint64_t off;
  uint64_t chosen = UINT64_MAX;
  uint64_t eligible = 0;
  uint64_t now = emq_now_ns();
  uint64_t seed = now ^ ((uint64_t)q->id << 32) ^
                  emq_log_next_offset(q->log) ^ q->stats.dequeued;
  uint64_t next = emq_log_next_offset(q->log);
  emq_log_entry e;
  if (seed == 0) seed = 0x9E3779B97F4A7C15ULL;
  for (off = q->read_offset; off < next; ++off) {
    if (emq_prim_offset_taken(q, off)) continue;
    if (emq_log_read(q->log, off, &e) != 0) continue;
    if (emq_prim_entry_expired(q, &e, now)) {
      if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) return -2;
      continue;
    }
    if (e.deliver_at_ns != 0 && e.deliver_at_ns > now) continue;
    eligible++;
    if ((emq_random_step(&seed) % eligible) == 0) chosen = off;
  }
  emq_prim_compact_consumed(q);
  if (chosen == UINT64_MAX) return -5;
  if (emq_log_read_copy(q->log, chosen, &e) != 0) return -5;
  if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) {
    free(e.payload);
    return -2;
  }
  emq_prim_fill_message(q, &e, out, now);
  emq_prim_compact_consumed(q);
  q->stats.dequeued++;
  return 0;
}

static int emq_prim_redeliver(emq_queue_desc *q, emq_message *out,
                              uint64_t now) {
  uint32_t i;
  uint32_t visibility_ms = q->opts.visibility_ms ?
                           q->opts.visibility_ms : 30000u;
  for (i = 0; i < q->inflight_cap; ++i) {
    emq_inflight *active = &q->inflight[i];
    emq_log_entry e;
    if (active->in_use != EMQ_INFLIGHT_ACTIVE ||
        active->visible_at_ns > now) {
      continue;
    }
    if (emq_log_read_copy(q->log, active->offset, &e) != 0) {
      emq_prim_state_clear(active);
      emq_prim_compact_consumed(q);
      continue;
    }
    if (emq_prim_entry_expired(q, &e, now)) {
      free(e.payload);
      emq_prim_state_clear(active);
      emq_prim_compact_consumed(q);
      continue;
    }
    emq_prim_fill_message(q, &e, out, now);
    active->visible_at_ns = now + (uint64_t)visibility_ms * 1000000ULL;
    q->stats.redelivered++;
    return 0;
  }
  return -5;
}

int emq_prim_pop(emq_queue_desc *q, emq_message *out, uint32_t timeout_ms,
                 emq_wheel *wheel) {
  uint64_t deadline;
  int rc;
  if (!q || !out) return -1;

  /*
   * Engine v2 ring path: FAST FIFO pops are terminal deliveries.  The ring
   * transfers ownership on pop, so no inflight reservation, redelivery scan,
   * or wheel tick runs here.  TTL/delayed messages still fall back to the
   * log path inside emq_fifo_pop, which does its own expiry checks.
   */
  if (q->lfq && q->opts.policy == EMQ_POLICY_FIFO) {
    deadline = timeout_ms == 0
                   ? 0
                   : emq_now_ns() + (uint64_t)timeout_ms * 1000000ULL;
    for (;;) {
      rc = emq_fifo_pop(q, out);
      if (rc != -5) return rc;
      if (timeout_ms == 0) return -5;
      if (emq_now_ns() >= deadline) return -7;
      emq_sleep_ms(1);
    }
  }

  if (wheel) emq_wheel_tick(wheel, emq_now_ns(), NULL, NULL);

  deadline = timeout_ms == 0 ? 0 : emq_now_ns() + (uint64_t)timeout_ms * 1000000ULL;
  for (;;) {
    emq_inflight *reserved;
    int has_reservation = 0;
    uint64_t now = emq_now_ns();
    if (q->opts.delivery == EMQ_AT_LEAST_ONCE) {
      rc = emq_prim_redeliver(q, out, now);
      if (rc == 0) return 0;
      reserved = emq_prim_state_slot(q, EMQ_INFLIGHT_RESERVED);
      if (!reserved) return -2;
      has_reservation = 1;
    }
    switch (q->opts.policy) {
      case EMQ_POLICY_PRIORITY: rc = emq_priority_pop(q, out); break;
      case EMQ_POLICY_RING: rc = emq_ring_pop(q, out); break;
      case EMQ_POLICY_DELAY: rc = emq_delay_pop(q, out); break;
      case EMQ_POLICY_WORK: rc = emq_work_pop(q, out); break;
      case EMQ_POLICY_STREAM: rc = emq_stream_pop(q, out); break;
      case EMQ_POLICY_PUBSUB:
      case EMQ_POLICY_BROADCAST: rc = emq_pubsub_pop(q, out); break;
      case EMQ_POLICY_LIFO: rc = emq_lifo_pop(q, out); break;
      case EMQ_POLICY_RANDOM: rc = emq_random_pop(q, out); break;
      case EMQ_POLICY_FIFO:
      default: rc = emq_fifo_pop(q, out); break;
    }
    reserved = has_reservation ?
        emq_prim_state_find(q, EMQ_INFLIGHT_RESERVED, 0, UINT64_MAX) : NULL;
    if (rc == 0 && reserved) {
      reserved->in_use = EMQ_INFLIGHT_ACTIVE;
      reserved->msg_id = out->id;
      reserved->offset = out->offset;
      reserved->priority = out->priority;
      reserved->visible_at_ns =
          now + (uint64_t)(q->opts.visibility_ms ?
                           q->opts.visibility_ms : 30000u) * 1000000ULL;
      return 0;
    }
    if (reserved) {
      emq_prim_state_clear(reserved);
      emq_prim_compact_consumed(q);
    }
    if (rc == 0 && has_reservation) return -2;
    if (rc == 0) {
      emq_prim_forget_ttl(q, out->id, out->offset);
      emq_prim_compact_consumed(q);
      return 0;
    }
    if (rc != -5) return rc;
    if (timeout_ms == 0) return -5;
    if (emq_now_ns() >= deadline) return -7;
    if (wheel) emq_wheel_tick(wheel, emq_now_ns(), NULL, NULL);
    emq_sleep_ms(1);
  }
}

int emq_prim_peek(emq_queue_desc *q, emq_message *out) {
  emq_log_entry e;
  uint64_t off;
  uint64_t now;
  if (!q || !out) return -1;
  now = emq_now_ns();
  for (off = q->read_offset; off < emq_log_next_offset(q->log); ++off) {
    if (emq_prim_offset_taken(q, off)) continue;
    if (emq_log_read(q->log, off, &e) != 0) continue;
    if (emq_prim_entry_expired(q, &e, now)) {
      if (emq_prim_mark_consumed(q, e.msg_id, e.offset) != 0) return -2;
      continue;
    }
    if (e.deliver_at_ns != 0 && e.deliver_at_ns > now) continue;
    emq_prim_fill_message(q, &e, out, now);
    emq_prim_compact_consumed(q);
    return 0;
  }
  emq_prim_compact_consumed(q);
  return -5;
}

int emq_prim_ack(emq_queue_desc *q, uint64_t message_id) {
  emq_inflight *active;
  if (!q) return -1;
  active = emq_prim_state_find(q, EMQ_INFLIGHT_ACTIVE, message_id, UINT64_MAX);
  if (!active) return -3;
  emq_prim_forget_ttl(q, active->msg_id, active->offset);
  emq_prim_state_clear(active);
  q->stats.acked++;
  emq_prim_compact_consumed(q);
  return 0;
}

int emq_prim_nack(emq_queue_desc *q, uint64_t message_id, uint32_t delay_ms,
                  emq_wheel *wheel) {
  uint32_t i;
  if (!q) return -1;
  for (i = 0; i < q->inflight_cap; ++i) {
    if (q->inflight[i].in_use == EMQ_INFLIGHT_ACTIVE &&
        q->inflight[i].msg_id == message_id) {
      uint64_t when = emq_now_ns() + (uint64_t)delay_ms * 1000000ULL;
      q->inflight[i].visible_at_ns = when;
      if (wheel) emq_wheel_schedule(wheel, when, (void *)(uintptr_t)message_id);
      q->stats.nacked++;
      return 0;
    }
  }
  return -3;
}

int emq_prim_ack_batch(emq_queue_desc *q, const uint64_t *ids, size_t count) {
  size_t i, j;
  if (!q || (!ids && count != 0)) return -1;
  for (i = 0; i < count; ++i) {
    if (!emq_prim_state_find(q, EMQ_INFLIGHT_ACTIVE, ids[i], UINT64_MAX)) {
      return -3;
    }
    for (j = 0; j < i; ++j) {
      if (ids[j] == ids[i]) return -1;
    }
  }
  for (i = 0; i < count; ++i) {
    emq_inflight *active =
        emq_prim_state_find(q, EMQ_INFLIGHT_ACTIVE, ids[i], UINT64_MAX);
    emq_prim_forget_ttl(q, active->msg_id, active->offset);
    emq_prim_state_clear(active);
  }
  q->stats.acked += count;
  emq_prim_compact_consumed(q);
  return 0;
}

int emq_prim_seek(emq_queue_desc *q, uint64_t offset) {
  if (!q) return -1;
  q->consumer_offset = offset;
  q->read_offset = offset;
  return 0;
}
