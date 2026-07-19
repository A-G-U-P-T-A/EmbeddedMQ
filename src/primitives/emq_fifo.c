#include "primitives/emq_primitives.h"
#include "platform/emq_platform.h"
#include "core/emq_atomic.h"
#include "core/emq_mem.h"
#include "core/emq_pool.h"
#include "policy/emq_policy.h"

#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
#  include <intrin.h>
#else
#  include <stdatomic.h>
#endif

#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

static int emq_fifo_push_lfq(emq_queue_desc *q, const void *data, size_t size,
                             const emq_message *meta) {
  uint64_t msg_id;
  uint32_t prio = meta ? meta->priority : 0;
  uint32_t flags = meta ? meta->flags : 0;
  uint32_t depth;
  uint32_t cap;
  int rc;
  int decide;

  if (size > UINT32_MAX) return -1;
  depth = (uint32_t)emq_atomic_load_u64(&q->hot.depth);
  cap = q->opts.capacity ? q->opts.capacity : UINT32_MAX;
  decide = emq_policy_decide_full(&q->policy, depth, cap);
  if (decide == -4 || decide == -10) return decide;
  if (decide == 1) {
    uint8_t trash[256];
    uint32_t len = 0;
    uint64_t mid = 0;
    uint32_t f = 0, pr = 0;
    uint8_t *big = NULL;
    int drop_rc;
    drop_rc = emq_lfq_try_pop(q->lfq, trash, (uint32_t)sizeof(trash), &len,
                              &mid, &f, &pr);
    if (drop_rc == -1 && len > sizeof(trash)) {
      big = (uint8_t *)malloc(len);
      if (big) {
        drop_rc = emq_lfq_try_pop(q->lfq, big, len, &len, &mid, &f, &pr);
        free(big);
      }
    }
    if (drop_rc == 0) emq_atomic_fetch_add_u64(&q->hot.depth, (uint64_t)-1);
  }

  /* Counter-based id: unique per queue, no clock call on the hot path.
   * The ring is RAM-only, so ids need not survive a restart. */
  msg_id = ((uint64_t)q->id << 40) | (q->stats.enqueued + 1u);
  rc = emq_lfq_try_push(q->lfq, data, (uint32_t)size, msg_id, flags, prio);
  if (rc == 0) {
    q->stats.enqueued++;
    q->stats.bytes += size;
    q->active = 1;
    q->stats.depth = emq_atomic_fetch_add_u64(&q->hot.depth, 1) + 1;
    /* Sticky ACTIVE bit — atomic OR so concurrent producers don't race. */
#if defined(_MSC_VER)
    {
      short old, neu;
      do {
        old = (short)q->hot.flags;
        if (((uint16_t)old & EMQ_HOT_FLAG_ACTIVE) != 0) break;
        neu = (short)((uint16_t)old | EMQ_HOT_FLAG_ACTIVE);
      } while (_InterlockedCompareExchange16((volatile short *)&q->hot.flags,
                                            neu, old) != old);
    }
#else
    {
      _Atomic uint16_t *fp = (_Atomic uint16_t *)&q->hot.flags;
      uint16_t old = atomic_load_explicit(fp, memory_order_relaxed);
      while ((old & EMQ_HOT_FLAG_ACTIVE) == 0) {
        if (atomic_compare_exchange_weak_explicit(
                fp, &old, (uint16_t)(old | EMQ_HOT_FLAG_ACTIVE),
                memory_order_relaxed, memory_order_relaxed))
          break;
      }
    }
#endif
    (void)emq_atomic_fetch_add_u64((emq_atomic_u64 *)&q->seq_wait, 1);
    emq_wake_u64(&q->seq_wait, 1);
  }
  return rc;
}

int emq_fifo_push(emq_queue_desc *q, const void *data, size_t size,
                  const emq_message *meta) {
  uint64_t msg_id;
  uint64_t off;
  uint32_t prio = meta ? meta->priority : 0;
  uint64_t deliver = meta ? meta->deliver_at_ns : 0;
  uint64_t ttl_ns = meta ? meta->ttl_ns : 0;
  emq_inflight *ttl = NULL;
  int rc;
  if (!q || q->closed) return -9;
  if (size > UINT32_MAX) return -1;

  /* TTL / delayed delivery still use the durable log path. */
  if (q->lfq && !(meta && (meta->ttl_ns != 0 || meta->deliver_at_ns != 0))) {
    return emq_fifo_push_lfq(q, data, size, meta);
  }

  if (ttl_ns != 0) {
    ttl = emq_prim_state_slot(q, EMQ_INFLIGHT_TTL);
    if (!ttl) return -2;
  }
  msg_id = emq_now_ns() ^ ((uint64_t)q->id << 32) ^ q->stats.enqueued;
  rc = emq_log_append_ex(q->log, q->id, msg_id, prio, deliver, ttl_ns, data,
                         (uint32_t)size, &off);
  if (rc == 0) {
    if (ttl) {
      uint64_t now = emq_now_ns();
      ttl->msg_id = msg_id;
      ttl->offset = off;
      ttl->visible_at_ns =
          UINT64_MAX - now < ttl_ns ? UINT64_MAX : now + ttl_ns;
    }
    q->stats.enqueued++;
    q->stats.depth = emq_log_count(q->log);
    q->stats.bytes += size;
    q->active = 1;
  } else if (ttl) {
    emq_prim_state_clear(ttl);
  }
  return rc;
}

static int emq_fifo_pop_lfq(emq_queue_desc *q, emq_message *out) {
  uint32_t len = 0;
  uint64_t msg_id = 0;
  uint32_t flags = 0;
  uint32_t prio = 0;
  int rc;
  uint8_t *payload;

  /* Peek the frame length so the ring copies straight into the caller's
   * buffer: one allocation, one memcpy per pop. */
  rc = emq_lfq_peek_len(q->lfq, &len);
  if (rc != 0) return rc;
  payload = (uint8_t *)emq_malloc(len ? len : 1);
  if (!payload) return -2;
  rc = emq_lfq_try_pop(q->lfq, payload, len, &len, &msg_id, &flags, &prio);
  if (rc != 0) {
    emq_free(payload);
    return rc;
  }

  memset(out, 0, sizeof(*out));
  out->id = msg_id;
  out->offset = q->stats.dequeued;
  out->priority = prio;
  out->flags = flags;
  out->data = payload;
  out->size = len;
  q->stats.dequeued++;
  q->stats.depth = emq_atomic_fetch_add_u64(&q->hot.depth, (uint64_t)-1) - 1;
  return 0;
}

int emq_fifo_pop(emq_queue_desc *q, emq_message *out) {
  emq_log_entry e;
  uint64_t off;
  if (!q || !out || q->closed) return -9;
  if (q->lfq) {
    int rc = emq_fifo_pop_lfq(q, out);
    if (rc != -5) return rc;
    /* Fall through to log for TTL / spill path messages. */
  }

  off = q->read_offset;
  while (off < emq_log_next_offset(q->log)) {
    if (emq_log_read_copy(q->log, off, &e) == 0) {
      uint64_t now = emq_now_ns();
      if (emq_prim_entry_expired(q, &e, now)) {
        free(e.payload);
        q->read_offset = off + 1;
        off++;
        continue;
      }
      if (e.deliver_at_ns != 0 && e.deliver_at_ns > now) {
        free(e.payload);
        return -5;
      }
      emq_prim_fill_message(q, &e, out, now);
      q->read_offset = off + 1;
      q->stats.dequeued++;
      emq_prim_compact_consumed(q);
      return 0;
    }
    off++;
    q->read_offset = off;
  }
  emq_prim_compact_consumed(q);
  return -5;
}
