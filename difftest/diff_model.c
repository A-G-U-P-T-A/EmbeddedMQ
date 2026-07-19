#include "diff_model.h"

#include <stdlib.h>
#include <string.h>

static int grow(diff_msg **arr, size_t *cap, size_t need) {
  size_t ncap;
  diff_msg *n;
  if (*cap >= need) return 0;
  ncap = *cap ? *cap * 2u : 16u;
  while (ncap < need) ncap *= 2u;
  n = (diff_msg *)realloc(*arr, ncap * sizeof(diff_msg));
  if (!n) return -1;
  *arr = n;
  *cap = ncap;
  return 0;
}

int diff_model_init(diff_model *m, emq_queue_policy policy, emq_bp_mode bp,
                    uint32_t capacity) {
  if (!m) return -1;
  memset(m, 0, sizeof(*m));
  m->policy = policy;
  m->bp = bp;
  m->capacity = capacity;
  m->next_id = 1;
  return 0;
}

void diff_model_destroy(diff_model *m) {
  if (!m) return;
  free(m->items);
  free(m->inflight);
  memset(m, 0, sizeof(*m));
}

size_t diff_model_depth(const diff_model *m) {
  if (!m) return 0;
  if (m->policy == EMQ_POLICY_WORK) return m->count + m->inflight_count;
  return m->count;
}

static void drop_oldest(diff_model *m) {
  size_t i;
  if (m->count == 0) return;
  for (i = 1; i < m->count; ++i) m->items[i - 1] = m->items[i];
  m->count--;
}

static int find_highest_prio_idx(const diff_model *m) {
  size_t i;
  size_t best = 0;
  if (m->count == 0) return -1;
  for (i = 1; i < m->count; ++i) {
    if (m->items[i].priority > m->items[best].priority) best = i;
  }
  return (int)best;
}

emq_status diff_model_push(diff_model *m, uint64_t seq, uint32_t priority,
                           uint32_t checksum) {
  diff_msg msg;
  if (!m) return EMQ_ERR_INVALID;
  msg.seq = seq;
  msg.id = m->next_id++;
  msg.priority = priority;
  msg.checksum = checksum;

  /* RING always overwrites oldest when full (engine forces DROP_OLD). */
  if (m->policy == EMQ_POLICY_RING && m->capacity != 0 &&
      m->count >= m->capacity) {
    drop_oldest(m);
  } else if (m->capacity != 0 && m->count >= m->capacity) {
    switch (m->bp) {
      case EMQ_BP_MODE_DROP_NEW:
        return EMQ_ERR_FULL;
      case EMQ_BP_MODE_BLOCK:
        return EMQ_ERR_BUSY;
      case EMQ_BP_MODE_DROP_OLD:
      case EMQ_BP_MODE_OVERWRITE:
        drop_oldest(m);
        break;
      case EMQ_BP_MODE_SPILL:
      case EMQ_BP_MODE_EXPAND:
        break;
      default:
        return EMQ_ERR_FULL;
    }
  }

  if (grow(&m->items, &m->cap, m->count + 1) != 0) return EMQ_ERR_NOMEM;
  /* All policies append; LIFO differs only at pop (take newest). */
  m->items[m->count++] = msg;
  return EMQ_OK;
}

emq_status diff_model_pop(diff_model *m, diff_msg *out) {
  size_t idx;
  size_t i;
  diff_msg msg;
  if (!m || !out) return EMQ_ERR_INVALID;
  if (m->count == 0) return EMQ_ERR_EMPTY;

  if (m->policy == EMQ_POLICY_PRIORITY) {
    int bi = find_highest_prio_idx(m);
    if (bi < 0) return EMQ_ERR_EMPTY;
    idx = (size_t)bi;
  } else if (m->policy == EMQ_POLICY_LIFO) {
    idx = m->count - 1u; /* newest */
  } else {
    idx = 0; /* FIFO / RING / WORK ready queue */
  }

  msg = m->items[idx];
  for (i = idx + 1; i < m->count; ++i) m->items[i - 1] = m->items[i];
  m->count--;

  if (m->policy == EMQ_POLICY_WORK) {
    if (grow(&m->inflight, &m->inflight_cap, m->inflight_count + 1) != 0)
      return EMQ_ERR_NOMEM;
    m->inflight[m->inflight_count++] = msg;
  }

  *out = msg;
  return EMQ_OK;
}

emq_status diff_model_ack(diff_model *m, uint64_t id) {
  size_t i, j;
  if (!m || m->policy != EMQ_POLICY_WORK) return EMQ_ERR_UNSUPPORTED;
  for (i = 0; i < m->inflight_count; ++i) {
    if (m->inflight[i].id == id) {
      for (j = i + 1; j < m->inflight_count; ++j)
        m->inflight[j - 1] = m->inflight[j];
      m->inflight_count--;
      return EMQ_OK;
    }
  }
  return EMQ_ERR_NOT_FOUND;
}

emq_status diff_model_nack(diff_model *m, uint64_t id) {
  size_t i, j;
  diff_msg msg;
  if (!m || m->policy != EMQ_POLICY_WORK) return EMQ_ERR_UNSUPPORTED;
  for (i = 0; i < m->inflight_count; ++i) {
    if (m->inflight[i].id == id) {
      msg = m->inflight[i];
      for (j = i + 1; j < m->inflight_count; ++j)
        m->inflight[j - 1] = m->inflight[j];
      m->inflight_count--;
      /* Real nack(delay=0) becomes immediately redeliverable and is preferred
       * over new ready messages on the next pop — model as ready front. */
      if (grow(&m->items, &m->cap, m->count + 1) != 0) return EMQ_ERR_NOMEM;
      for (j = m->count; j > 0; --j) m->items[j] = m->items[j - 1];
      m->items[0] = msg;
      m->count++;
      return EMQ_OK;
    }
  }
  return EMQ_ERR_NOT_FOUND;
}
