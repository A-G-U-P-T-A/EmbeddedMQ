#include "registry/emq_registry.h"
#include "platform/emq_platform.h"
#include "core/emq_atomic.h"
#include "core/emq_cpu.h"

#include <stdlib.h>
#include <string.h>

static uint32_t emq_pow2_ge(uint32_t v) {
  uint32_t p = 1;
  if (v == 0) return 1u << 20; /* 1 MiB default ring */
  while (p < v) {
    if (p > (1u << 30)) return 1u << 30;
    p <<= 1;
  }
  return p;
}

#define EMQ_NAME_TOMBSTONE ((emq_queue_desc *)(uintptr_t)1)

static uint64_t emq_name_hash(const char *name) {
  uint64_t hash = 1469598103934665603ULL;
  while (*name) {
    hash ^= (uint8_t)*name++;
    hash *= 1099511628211ULL;
  }
  return hash;
}

static emq_queue_desc *emq_registry_find_locked(emq_registry *r,
                                                 const char *name) {
  uint32_t mask;
  uint32_t slot;
  uint32_t probes;
  if (!r->name_index || r->name_capacity == 0) return NULL;
  mask = r->name_capacity - 1u;
  slot = (uint32_t)emq_name_hash(name) & mask;
  for (probes = 0; probes < r->name_capacity; ++probes) {
    emq_queue_desc *d = r->name_index[slot];
    if (!d) return NULL;
    if (d != EMQ_NAME_TOMBSTONE && strcmp(d->name, name) == 0) return d;
    slot = (slot + 1u) & mask;
  }
  return NULL;
}

static int emq_registry_index_insert_raw(emq_queue_desc **index,
                                         uint32_t capacity,
                                         emq_queue_desc *d) {
  uint32_t mask = capacity - 1u;
  uint32_t slot = (uint32_t)emq_name_hash(d->name) & mask;
  uint32_t probes;
  uint32_t tombstone = UINT32_MAX;
  for (probes = 0; probes < capacity; ++probes) {
    if (!index[slot]) {
      index[tombstone != UINT32_MAX ? tombstone : slot] = d;
      return 0;
    }
    if (index[slot] == EMQ_NAME_TOMBSTONE && tombstone == UINT32_MAX) {
      tombstone = slot;
    }
    slot = (slot + 1u) & mask;
  }
  if (tombstone != UINT32_MAX) {
    index[tombstone] = d;
    return 0;
  }
  return -4;
}

static int emq_registry_index_grow_locked(emq_registry *r) {
  uint32_t ncap = r->name_capacity ? r->name_capacity * 2u : 2048u;
  emq_queue_desc **index;
  uint32_t i;
  if (ncap < r->name_capacity) return -2;
  index = (emq_queue_desc **)calloc(ncap, sizeof(*index));
  if (!index) return -2;
  for (i = 1; i < r->capacity; ++i) {
    if (r->queues[i] &&
        emq_registry_index_insert_raw(index, ncap, r->queues[i]) != 0) {
      free(index);
      return -2;
    }
  }
  free(r->name_index);
  r->name_index = index;
  r->name_capacity = ncap;
  return 0;
}

static int emq_registry_index_insert_locked(emq_registry *r,
                                             emq_queue_desc *d) {
  if ((r->count + 1u) * 10u >= r->name_capacity * 7u) {
    int rc = emq_registry_index_grow_locked(r);
    if (rc != 0) return rc;
  }
  return emq_registry_index_insert_raw(r->name_index, r->name_capacity, d);
}

static void emq_registry_index_remove_locked(emq_registry *r,
                                              const char *name) {
  uint32_t mask = r->name_capacity - 1u;
  uint32_t slot = (uint32_t)emq_name_hash(name) & mask;
  uint32_t probes;
  for (probes = 0; probes < r->name_capacity; ++probes) {
    emq_queue_desc *d = r->name_index[slot];
    if (!d) return;
    if (d != EMQ_NAME_TOMBSTONE && strcmp(d->name, name) == 0) {
      r->name_index[slot] = EMQ_NAME_TOMBSTONE;
      return;
    }
    slot = (slot + 1u) & mask;
  }
}

void emq_registry_set_pool(emq_registry *r, emq_pool *pool) {
  if (r) r->pool = pool;
}

int emq_registry_init(emq_registry *r, uint32_t capacity) {
  if (!r) return -1;
  memset(r, 0, sizeof(*r));
  emq_cpu_init();
  r->capacity = capacity ? capacity : 1024u;
  if (r->capacity > EMQ_QUEUE_TABLE_CAPACITY) {
    r->capacity = EMQ_QUEUE_TABLE_CAPACITY;
  }
  r->queues = (emq_queue_desc **)calloc(r->capacity, sizeof(emq_queue_desc *));
  r->name_capacity = 2048u;
  r->name_index = (emq_queue_desc **)calloc(r->name_capacity,
                                             sizeof(emq_queue_desc *));
  r->mu_opaque = emq_mutex_create();
  if (!r->queues || !r->name_index || !r->mu_opaque) {
    emq_registry_destroy(r);
    return -2;
  }
  r->next_id = 1;
  return 0;
}

void emq_registry_destroy(emq_registry *r) {
  uint32_t i;
  if (!r) return;
  if (r->queues) {
    for (i = 0; i < r->capacity; ++i) {
      if (r->queues[i]) {
        emq_lfq_destroy(r->queues[i]->lfq);
        emq_log_close(r->queues[i]->log);
        emq_cond_destroy((emq_cond *)r->queues[i]->ready_cond_opaque);
        emq_mutex_destroy((emq_mutex *)r->queues[i]->op_mu_opaque);
        free(r->queues[i]->inflight);
        free(r->queues[i]);
      }
    }
    free(r->queues);
  }
  free(r->name_index);
  emq_mutex_destroy((emq_mutex *)r->mu_opaque);
  memset(r, 0, sizeof(*r));
}

static void emq_bitmap_set(uint64_t *bm, uint32_t id, int on) {
  uint32_t word = id / 64u;
  uint32_t bit = id % 64u;
  if (word >= EMQ_BITMAP_WORDS) return;
  if (on) bm[word] |= (1ULL << bit);
  else bm[word] &= ~(1ULL << bit);
}

static int emq_bitmap_get(const uint64_t *bm, uint32_t id) {
  uint32_t word = id / 64u;
  uint32_t bit = id % 64u;
  if (word >= EMQ_BITMAP_WORDS) return 0;
  return (bm[word] >> bit) & 1u;
}

void emq_registry_set_active(emq_registry *r, uint32_t id, int active) {
  if (!r || id == 0) return;
  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  emq_bitmap_set(r->active_bitmap, id, active);
  if (id < r->capacity && r->queues[id]) r->queues[id]->active = active;
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
}

int emq_registry_is_active(const emq_registry *r, uint32_t id) {
  int active;
  if (!r) return 0;
  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  active = emq_bitmap_get(r->active_bitmap, id);
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  return active;
}

emq_queue_desc *emq_registry_find(emq_registry *r, const char *name) {
  emq_queue_desc *d;
  if (!r || !name) return NULL;
  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  d = emq_registry_find_locked(r, name);
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  return d;
}

emq_queue_desc *emq_registry_get(emq_registry *r, uint32_t id) {
  if (!r || id == 0 || id >= r->capacity) return NULL;
  return r->queues[id];
}

int emq_registry_create(emq_registry *r, const char *name,
                        const emq_queue_opts *opts, emq_queue_desc **out) {
  emq_queue_desc *d;
  uint32_t id;
  emq_storage_mode mode;
  uint32_t cap;
  int rc;

  if (!r || !name || !opts || !out) return -1;
  if (strlen(name) >= EMQ_NAME_MAX) return -1;

  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  if (emq_registry_find_locked(r, name)) {
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return -8;
  }
  if (r->next_id >= r->capacity) {
    /* grow table */
    uint32_t ncap = r->capacity * 2u;
    emq_queue_desc **nq;
    if (ncap > EMQ_QUEUE_TABLE_CAPACITY) ncap = EMQ_QUEUE_TABLE_CAPACITY;
    if (ncap <= r->capacity) {
      emq_mutex_unlock((emq_mutex *)r->mu_opaque);
      return -4;
    }
    nq = (emq_queue_desc **)realloc(r->queues, sizeof(emq_queue_desc *) * ncap);
    if (!nq) {
      emq_mutex_unlock((emq_mutex *)r->mu_opaque);
      return -2;
    }
    memset(nq + r->capacity, 0, sizeof(emq_queue_desc *) * (ncap - r->capacity));
    r->queues = nq;
    r->capacity = ncap;
  }

  id = r->next_id++;
  d = (emq_queue_desc *)calloc(1, sizeof(*d));
  if (!d) {
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return -2;
  }
  strncpy(d->name, name, EMQ_NAME_MAX - 1);
  d->id = id;
  d->generation = 1;
  d->opts = *opts;
  d->policy = emq_policy_from_queue_opts(opts);
  d->pool = r->pool;
  d->inflight_cap = 0;
  d->inflight = NULL;
  d->op_mu_opaque = emq_mutex_create();
  d->ready_cond_opaque = emq_cond_create();
  emq_atomic_init_u64(&d->hot.head, 0);
  emq_atomic_init_u64(&d->hot.tail, 0);
  emq_atomic_init_u64(&d->hot.depth, 0);
  d->hot.id = id;
  d->hot.policy = (uint16_t)opts->policy;
  d->hot.flags = 0;
  d->hot.band = 0;
  d->seq_wait = 0;
  if (!d->op_mu_opaque || !d->ready_cond_opaque) {
    emq_cond_destroy((emq_cond *)d->ready_cond_opaque);
    emq_mutex_destroy((emq_mutex *)d->op_mu_opaque);
    free(d->inflight);
    free(d);
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return -2;
  }

  mode = opts->storage;
  if (opts->policy == EMQ_POLICY_RING) mode = EMQ_STORAGE_RING;
  if (opts->policy == EMQ_POLICY_STREAM) mode = EMQ_STORAGE_STREAM;
  cap = opts->capacity;
  if (opts->policy == EMQ_POLICY_RING && opts->ring_size) cap = opts->ring_size;

  rc = emq_log_open(&d->log, mode, opts->path, cap, opts->fsync);
  if (rc != 0) {
    emq_cond_destroy((emq_cond *)d->ready_cond_opaque);
    emq_mutex_destroy((emq_mutex *)d->op_mu_opaque);
    free(d->inflight);
    free(d);
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return rc;
  }
  if (opts->inline_threshold != 0) {
    rc = emq_log_set_blob_threshold(d->log, opts->inline_threshold);
    if (rc != 0) {
      emq_log_close(d->log);
      emq_cond_destroy((emq_cond *)d->ready_cond_opaque);
      emq_mutex_destroy((emq_mutex *)d->op_mu_opaque);
      free(d->inflight);
      free(d);
      emq_mutex_unlock((emq_mutex *)r->mu_opaque);
      return rc;
    }
  } else {
    (void)emq_log_set_blob_threshold(d->log, EMQ_INLINE_PAYLOAD_MAX);
  }

  /* FAST FIFO: lock-free log-buffer ring (Engine v2 data plane). */
  if (opts->storage == EMQ_STORAGE_FAST && opts->policy == EMQ_POLICY_FIFO) {
    uint32_t ring_bytes = emq_pow2_ge(opts->capacity ? opts->capacity * 256u
                                                     : (1u << 20));
    int spsc = (opts->producers <= 1 && opts->consumers <= 1);
    rc = emq_lfq_create(&d->lfq, ring_bytes, spsc);
    if (rc != 0) {
      emq_log_close(d->log);
      emq_cond_destroy((emq_cond *)d->ready_cond_opaque);
      emq_mutex_destroy((emq_mutex *)d->op_mu_opaque);
      free(d);
      emq_mutex_unlock((emq_mutex *)r->mu_opaque);
      return rc;
    }
    d->hot.flags |= EMQ_HOT_FLAG_LFQ;
    if (spsc) d->hot.flags |= EMQ_HOT_FLAG_SPSC;
  }

  rc = emq_registry_index_insert_locked(r, d);
  if (rc != 0) {
    emq_lfq_destroy(d->lfq);
    emq_log_close(d->log);
    emq_cond_destroy((emq_cond *)d->ready_cond_opaque);
    emq_mutex_destroy((emq_mutex *)d->op_mu_opaque);
    free(d->inflight);
    free(d);
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return rc;
  }
  r->queues[id] = d;
  r->count++;
  emq_bitmap_set(r->active_bitmap, id, 0);
  d->active = 0;
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  *out = d;
  return 0;
}

int emq_registry_remove(emq_registry *r, const char *name) {
  emq_queue_desc *d;
  uint32_t id;
  if (!r || !name) return -1;
  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  d = emq_registry_find_locked(r, name);
  if (!d) {
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return -3;
  }
  id = d->id;
  emq_mutex_lock((emq_mutex *)d->op_mu_opaque);
  if (d->handle_count != 0) {
    emq_mutex_unlock((emq_mutex *)d->op_mu_opaque);
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return -10;
  }
  emq_mutex_unlock((emq_mutex *)d->op_mu_opaque);
  emq_bitmap_set(r->active_bitmap, d->id, 0);
  emq_registry_index_remove_locked(r, name);
  emq_lfq_destroy(d->lfq);
  emq_log_close(d->log);
  emq_cond_destroy((emq_cond *)d->ready_cond_opaque);
  emq_mutex_destroy((emq_mutex *)d->op_mu_opaque);
  free(d->inflight);
  free(d);
  r->queues[id] = NULL;
  r->count--;
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  return 0;
}
