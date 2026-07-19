#include "core/emq_pool.h"
#include "core/emq_atomic.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

#define EMQ_POOL_NUM_CLASSES 11u
#define EMQ_POOL_SLAB_BYTES (2u * 1024u * 1024u)
#define EMQ_POOL_MAX_CLASS 65536u
#define EMQ_POOL_HDR_SIZE 8u
#define EMQ_POOL_MAG_DEPTH 32u

static const uint32_t emq_pool_class_sizes[EMQ_POOL_NUM_CLASSES] = {
    64u,   128u,  256u,   512u,   1024u, 2048u,
    4096u, 8192u, 16384u, 32768u, 65536u};

typedef struct emq_pool_hdr {
  uint32_t class_idx;
  uint32_t block_bytes;
} emq_pool_hdr;

typedef struct emq_pool_block {
  struct emq_pool_block *next;
} emq_pool_block;

typedef struct emq_pool_depot {
  emq_pool_block *freelist;
  emq_mutex *mu;
} emq_pool_depot;

typedef struct emq_pool_slab {
  struct emq_pool_slab *next;
  void *base;
  size_t size;
} emq_pool_slab;

typedef struct emq_pool_magazine {
  emq_pool_block *freelist[EMQ_POOL_NUM_CLASSES];
  uint32_t count[EMQ_POOL_NUM_CLASSES];
  int initialized;
} emq_pool_magazine;

struct emq_pool {
  emq_pool_depot depots[EMQ_POOL_NUM_CLASSES];
  emq_pool_slab *slabs;
  emq_mutex *slab_mu;
  emq_atomic_u64 allocs;
  emq_atomic_u64 frees;
  emq_atomic_u64 hits;
  emq_atomic_u64 misses;
  emq_atomic_u64 live_bytes;
  emq_atomic_u64 mmap_bytes;
  emq_atomic_u64 malloc_fallbacks;
};

#if defined(_MSC_VER)
static __declspec(thread) emq_pool_magazine emq_pool_tls_mag;
#else
static __thread emq_pool_magazine emq_pool_tls_mag;
#endif

static void emq_pool_mag_init(void) {
  if (!emq_pool_tls_mag.initialized) {
    memset(&emq_pool_tls_mag, 0, sizeof(emq_pool_tls_mag));
    emq_pool_tls_mag.initialized = 1;
  }
}

static uint32_t emq_pool_class_for_size(size_t size) {
  uint32_t i;
  if (size <= emq_pool_class_sizes[0]) return 0;
  for (i = 1; i < EMQ_POOL_NUM_CLASSES; ++i) {
    if (size <= emq_pool_class_sizes[i]) return i;
  }
  return EMQ_POOL_NUM_CLASSES;
}

static uint32_t emq_pool_block_bytes(uint32_t class_idx) {
  return EMQ_POOL_HDR_SIZE + emq_pool_class_sizes[class_idx];
}

static void *emq_pool_user_ptr(void *block) {
  return (uint8_t *)block + EMQ_POOL_HDR_SIZE;
}

static emq_pool_hdr *emq_pool_hdr_from_user(void *ptr) {
  return (emq_pool_hdr *)((uint8_t *)ptr - EMQ_POOL_HDR_SIZE);
}

static void emq_pool_stat_add(emq_atomic_u64 *counter, uint64_t delta) {
  (void)emq_atomic_fetch_add_u64(counter, delta);
}

static void emq_pool_depot_push(emq_pool *pool, uint32_t class_idx,
                                emq_pool_block *blk);

static int emq_pool_carve_slab(emq_pool *pool, uint32_t class_idx) {
  void *base;
  emq_pool_slab *slab;
  uint32_t block_bytes;
  uint32_t count;
  uint32_t i;
  uint8_t *cursor;

  block_bytes = emq_pool_block_bytes(class_idx);
  count = (uint32_t)(EMQ_POOL_SLAB_BYTES / block_bytes);
  if (count == 0) return -1;

  base = emq_os_alloc_pages(EMQ_POOL_SLAB_BYTES);
  if (!base) return -2;

  emq_mutex_lock(pool->slab_mu);
  slab = (emq_pool_slab *)malloc(sizeof(*slab));
  if (!slab) {
    emq_os_free_pages(base, EMQ_POOL_SLAB_BYTES);
    emq_mutex_unlock(pool->slab_mu);
    return -2;
  }
  slab->base = base;
  slab->size = EMQ_POOL_SLAB_BYTES;
  slab->next = pool->slabs;
  pool->slabs = slab;
  emq_mutex_unlock(pool->slab_mu);

  emq_pool_stat_add(&pool->mmap_bytes, EMQ_POOL_SLAB_BYTES);
  cursor = (uint8_t *)base;
  for (i = 0; i < count; ++i) {
    emq_pool_block *blk = (emq_pool_block *)cursor;
    emq_pool_hdr *hdr = (emq_pool_hdr *)cursor;
    hdr->class_idx = class_idx;
    hdr->block_bytes = block_bytes;
    emq_pool_depot_push(pool, class_idx, blk);
    cursor += block_bytes;
  }
  return 0;
}

static emq_pool_block *emq_pool_depot_pop(emq_pool *pool, uint32_t class_idx) {
  emq_pool_depot *depot = &pool->depots[class_idx];
  emq_pool_block *blk;
  emq_mutex_lock(depot->mu);
  blk = depot->freelist;
  if (blk) depot->freelist = blk->next;
  emq_mutex_unlock(depot->mu);
  return blk;
}

static void emq_pool_depot_push(emq_pool *pool, uint32_t class_idx,
                                emq_pool_block *blk) {
  emq_pool_depot *depot = &pool->depots[class_idx];
  emq_mutex_lock(depot->mu);
  blk->next = depot->freelist;
  depot->freelist = blk;
  emq_mutex_unlock(depot->mu);
}

static void emq_pool_mag_flush(emq_pool *pool, uint32_t class_idx) {
  emq_pool_magazine *mag = &emq_pool_tls_mag;
  while (mag->count[class_idx] > EMQ_POOL_MAG_DEPTH / 2u) {
    emq_pool_block *blk = mag->freelist[class_idx];
    if (!blk) break;
    mag->freelist[class_idx] = blk->next;
    mag->count[class_idx]--;
    emq_pool_depot_push(pool, class_idx, blk);
  }
}

static void emq_pool_restore_hdr(emq_pool_block *blk, uint32_t class_idx) {
  emq_pool_hdr *hdr = (emq_pool_hdr *)blk;
  hdr->class_idx = class_idx;
  hdr->block_bytes = emq_pool_block_bytes(class_idx);
}

static emq_pool_block *emq_pool_mag_pop(emq_pool *pool, uint32_t class_idx) {
  emq_pool_magazine *mag = &emq_pool_tls_mag;
  emq_pool_block *blk = mag->freelist[class_idx];
  if (blk) {
    mag->freelist[class_idx] = blk->next;
    mag->count[class_idx]--;
    emq_pool_restore_hdr(blk, class_idx);
    emq_pool_stat_add(&pool->hits, 1);
    return blk;
  }
  emq_pool_stat_add(&pool->misses, 1);
  while (mag->count[class_idx] < EMQ_POOL_MAG_DEPTH) {
    emq_pool_block *refill = emq_pool_depot_pop(pool, class_idx);
    if (!refill) break;
    refill->next = mag->freelist[class_idx];
    mag->freelist[class_idx] = refill;
    mag->count[class_idx]++;
  }
  blk = mag->freelist[class_idx];
  if (blk) {
    mag->freelist[class_idx] = blk->next;
    mag->count[class_idx]--;
    emq_pool_restore_hdr(blk, class_idx);
    emq_pool_stat_add(&pool->hits, 1);
    return blk;
  }
  if (emq_pool_carve_slab(pool, class_idx) != 0) return NULL;
  return emq_pool_mag_pop(pool, class_idx);
}

emq_pool *emq_pool_create(void) {
  emq_pool *pool;
  uint32_t i;
  pool = (emq_pool *)calloc(1, sizeof(*pool));
  if (!pool) return NULL;
  pool->slab_mu = emq_mutex_create();
  if (!pool->slab_mu) {
    free(pool);
    return NULL;
  }
  for (i = 0; i < EMQ_POOL_NUM_CLASSES; ++i) {
    pool->depots[i].mu = emq_mutex_create();
    if (!pool->depots[i].mu) {
      emq_pool_destroy(pool);
      return NULL;
    }
  }
  emq_atomic_init_u64(&pool->allocs, 0);
  emq_atomic_init_u64(&pool->frees, 0);
  emq_atomic_init_u64(&pool->hits, 0);
  emq_atomic_init_u64(&pool->misses, 0);
  emq_atomic_init_u64(&pool->live_bytes, 0);
  emq_atomic_init_u64(&pool->mmap_bytes, 0);
  emq_atomic_init_u64(&pool->malloc_fallbacks, 0);
  return pool;
}

void emq_pool_destroy(emq_pool *pool) {
  emq_pool_slab *slab;
  uint32_t i;
  if (!pool) return;
  for (slab = pool->slabs; slab;) {
    emq_pool_slab *next = slab->next;
    emq_os_free_pages(slab->base, slab->size);
    free(slab);
    slab = next;
  }
  for (i = 0; i < EMQ_POOL_NUM_CLASSES; ++i) {
    emq_mutex_destroy(pool->depots[i].mu);
  }
  emq_mutex_destroy(pool->slab_mu);
  free(pool);
}

void *emq_pool_alloc(emq_pool *pool, size_t size) {
  uint32_t class_idx;
  emq_pool_block *blk;
  if (!pool || size == 0) return NULL;
  emq_pool_mag_init();
  class_idx = emq_pool_class_for_size(size);
  if (class_idx >= EMQ_POOL_NUM_CLASSES) {
    emq_pool_hdr *hdr;
    void *p = malloc(EMQ_POOL_HDR_SIZE + size);
    if (!p) return NULL;
    hdr = (emq_pool_hdr *)p;
    hdr->class_idx = UINT32_MAX;
    hdr->block_bytes = (uint32_t)(EMQ_POOL_HDR_SIZE + size);
    emq_pool_stat_add(&pool->malloc_fallbacks, 1);
    emq_pool_stat_add(&pool->allocs, 1);
    emq_pool_stat_add(&pool->live_bytes, size);
    return emq_pool_user_ptr(p);
  }
  blk = emq_pool_mag_pop(pool, class_idx);
  if (!blk) return NULL;
  emq_pool_stat_add(&pool->allocs, 1);
  emq_pool_stat_add(&pool->live_bytes, emq_pool_class_sizes[class_idx]);
  return emq_pool_user_ptr(blk);
}

void emq_pool_free(emq_pool *pool, void *ptr, size_t size) {
  emq_pool_hdr *hdr;
  uint32_t class_idx;
  emq_pool_block *blk;
  if (!pool || !ptr) return;
  (void)size;
  emq_pool_mag_init();
  hdr = emq_pool_hdr_from_user(ptr);
  class_idx = hdr->class_idx;
  if (class_idx >= EMQ_POOL_NUM_CLASSES) {
    uint64_t user_bytes = hdr->block_bytes > EMQ_POOL_HDR_SIZE
                              ? (uint64_t)(hdr->block_bytes - EMQ_POOL_HDR_SIZE)
                              : 0;
    emq_pool_stat_add(&pool->live_bytes, 0u - user_bytes);
    emq_pool_stat_add(&pool->frees, 1);
    free(hdr);
    return;
  }
  blk = (emq_pool_block *)hdr;
  emq_pool_stat_add(&pool->live_bytes,
                    0u - (uint64_t)emq_pool_class_sizes[class_idx]);
  emq_pool_stat_add(&pool->frees, 1);
  {
    emq_pool_magazine *mag = &emq_pool_tls_mag;
    if (mag->count[class_idx] < EMQ_POOL_MAG_DEPTH) {
      blk->next = mag->freelist[class_idx];
      mag->freelist[class_idx] = blk;
      mag->count[class_idx]++;
      emq_pool_stat_add(&pool->hits, 1);
      return;
    }
  }
  emq_pool_mag_flush(pool, class_idx);
  emq_pool_depot_push(pool, class_idx, blk);
}

void emq_pool_get_stats(const emq_pool *pool, emq_pool_stats *out) {
  if (!pool || !out) return;
  memset(out, 0, sizeof(*out));
  out->allocs = emq_atomic_load_u64(&pool->allocs);
  out->frees = emq_atomic_load_u64(&pool->frees);
  out->hits = emq_atomic_load_u64(&pool->hits);
  out->misses = emq_atomic_load_u64(&pool->misses);
  out->live_bytes = emq_atomic_load_u64(&pool->live_bytes);
  out->mmap_bytes = emq_atomic_load_u64(&pool->mmap_bytes);
  out->malloc_fallbacks = emq_atomic_load_u64(&pool->malloc_fallbacks);
}
