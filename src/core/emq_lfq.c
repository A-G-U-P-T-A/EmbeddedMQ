#include "core/emq_lfq.h"
#include "core/emq_atomic.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

#define EMQ_LFQ_FRAME_HDR 32u

typedef struct emq_lfq_frame {
  int32_t length;
  int32_t status;
  uint64_t msg_id;
  uint32_t flags;
  uint32_t priority;
  uint32_t reserved[2];
} emq_lfq_frame;

struct emq_lfq {
  uint8_t *buf;
  uint32_t capacity;
  uint32_t mask;
  int spsc;
  EMQ_ALIGN_CACHE emq_atomic_u64 head;
  EMQ_ALIGN_CACHE emq_atomic_u64 tail;
};

static uint32_t emq_lfq_round_up_pow2(uint32_t v) {
  uint32_t c = 1;
  if (v < 4096u) v = 4096u;
  while (c < v) c <<= 1;
  return c;
}

static uint32_t emq_lfq_align8(uint32_t v) {
  return (v + 7u) & ~7u;
}

static uint32_t emq_lfq_frame_total(uint32_t data_len) {
  return emq_lfq_align8(EMQ_LFQ_FRAME_HDR + data_len);
}

static emq_lfq_frame *emq_lfq_frame_at(emq_lfq *q, uint64_t offset) {
  return (emq_lfq_frame *)(q->buf + (offset & q->mask));
}

static void emq_lfq_write_padding(emq_lfq *q, uint64_t offset, uint32_t pad_len) {
  emq_lfq_frame *f = emq_lfq_frame_at(q, offset);
  f->length = -(int32_t)pad_len;
  f->msg_id = 0;
  f->flags = 0;
  f->priority = 0;
  f->reserved[0] = 0;
  f->reserved[1] = 0;
  emq_atomic_store_i32((emq_atomic_i32 *)&f->status, EMQ_LFQ_FRAME_COMMITTED);
}

static int emq_lfq_reserve(emq_lfq *q, uint32_t need, uint64_t *out_offset) {
  for (;;) {
    uint64_t head = emq_atomic_load_acquire_u64(&q->head);
    uint64_t tail = emq_atomic_load_u64(&q->tail);
    uint32_t tail_idx = (uint32_t)(tail & q->mask);
    uint32_t pad = 0;
    uint64_t claim_bytes = need;
    uint64_t frame_off;

    if (tail_idx + need > q->capacity) {
      pad = q->capacity - tail_idx;
      claim_bytes = (uint64_t)pad + need;
    }
    if (tail - head + claim_bytes > q->capacity) return -4;

    if (q->spsc) {
      uint64_t reserved = emq_atomic_fetch_add_u64(&q->tail, claim_bytes);
      if (reserved != tail) continue;
      frame_off = reserved + pad;
      if (pad > 0) emq_lfq_write_padding(q, reserved, pad);
      *out_offset = frame_off;
      return 0;
    }

    {
      uint64_t expected = tail;
      if (!emq_atomic_cas_u64(&q->tail, &expected, tail + claim_bytes)) continue;
      frame_off = tail + pad;
      if (pad > 0) emq_lfq_write_padding(q, tail, pad);
      *out_offset = frame_off;
      return 0;
    }
  }
}

int emq_lfq_create(emq_lfq **out, uint32_t capacity_bytes, int spsc) {
  emq_lfq *q;
  if (!out) return -1;
  q = (emq_lfq *)calloc(1, sizeof(*q));
  if (!q) return -2;
  q->capacity = emq_lfq_round_up_pow2(capacity_bytes);
  q->mask = q->capacity - 1u;
  q->spsc = spsc ? 1 : 0;
  q->buf = (uint8_t *)emq_aligned_alloc(EMQ_CACHE_LINE, q->capacity);
  if (!q->buf) {
    free(q);
    return -2;
  }
  memset(q->buf, 0, q->capacity);
  emq_atomic_init_u64(&q->head, 0);
  emq_atomic_init_u64(&q->tail, 0);
  *out = q;
  return 0;
}

void emq_lfq_destroy(emq_lfq *q) {
  if (!q) return;
  emq_aligned_free(q->buf);
  free(q);
}

int emq_lfq_try_push(emq_lfq *q, const void *data, uint32_t len,
                     uint64_t msg_id, uint32_t flags, uint32_t priority) {
  emq_lfq_frame *f;
  uint64_t offset;
  uint32_t total;
  if (!q || (len > 0 && !data)) return -1;
  total = emq_lfq_frame_total(len);
  if (total > q->capacity) return -4;
  if (emq_lfq_reserve(q, total, &offset) != 0) return -4;

  f = emq_lfq_frame_at(q, offset);
  f->length = (int32_t)len;
  f->msg_id = msg_id;
  f->flags = flags;
  f->priority = priority;
  f->reserved[0] = 0;
  f->reserved[1] = 0;
  if (len > 0) {
    memcpy((uint8_t *)f + EMQ_LFQ_FRAME_HDR, data, len);
  }
  emq_atomic_store_i32((emq_atomic_i32 *)&f->status, EMQ_LFQ_FRAME_COMMITTED);
  return 0;
}

int emq_lfq_peek_len(emq_lfq *q, uint32_t *out_len) {
  emq_lfq_frame *f;
  uint64_t head;
  uint64_t tail;
  int32_t len;

  if (!q || !out_len) return -1;

  for (;;) {
    head = emq_atomic_load_u64(&q->head);
    tail = emq_atomic_load_acquire_u64(&q->tail);
    if (head >= tail) return -5;

    f = emq_lfq_frame_at(q, head);
    if (emq_atomic_load_i32((const emq_atomic_i32 *)&f->status) !=
        EMQ_LFQ_FRAME_COMMITTED) {
      return -5;
    }

    len = f->length;
    if (len < 0) {
      /* Skip padding; caller side is the (single) consumer. */
      emq_atomic_store_release_u64(&q->head, head + (uint32_t)(-len));
      continue;
    }
    *out_len = (uint32_t)len;
    return 0;
  }
}

int emq_lfq_try_pop(emq_lfq *q, void *out_buf, uint32_t out_cap,
                    uint32_t *out_len, uint64_t *out_msg_id, uint32_t *out_flags,
                    uint32_t *out_priority) {
  emq_lfq_frame *f;
  uint64_t head;
  uint64_t tail;
  int32_t len;
  uint32_t total;

  if (!q) return -1;

  for (;;) {
    head = emq_atomic_load_u64(&q->head);
    tail = emq_atomic_load_acquire_u64(&q->tail);
    if (head >= tail) return -5;

    f = emq_lfq_frame_at(q, head);
    if (emq_atomic_load_i32((const emq_atomic_i32 *)&f->status) !=
        EMQ_LFQ_FRAME_COMMITTED) {
      return -5;
    }

    len = f->length;
    if (len < 0) {
      total = (uint32_t)(-len);
      emq_atomic_store_release_u64(&q->head, head + total);
      continue;
    }

    total = emq_lfq_frame_total((uint32_t)len);
    if (out_len) *out_len = (uint32_t)len;
    if (out_buf && out_cap < (uint32_t)len) return -1;
    if (out_msg_id) *out_msg_id = f->msg_id;
    if (out_flags) *out_flags = f->flags;
    if (out_priority) *out_priority = f->priority;
    if (len > 0 && out_buf) {
      memcpy(out_buf, (uint8_t *)f + EMQ_LFQ_FRAME_HDR, (size_t)len);
    }

    emq_atomic_store_i32((emq_atomic_i32 *)&f->status, EMQ_LFQ_FRAME_EMPTY);
    emq_atomic_store_release_u64(&q->head, head + total);
    return 0;
  }
}

uint32_t emq_lfq_depth_approx(const emq_lfq *q) {
  uint64_t head;
  uint64_t tail;
  if (!q) return 0;
  head = emq_atomic_load_u64(&q->head);
  tail = emq_atomic_load_u64(&q->tail);
  if (tail <= head) return 0;
  return (uint32_t)(tail - head);
}
