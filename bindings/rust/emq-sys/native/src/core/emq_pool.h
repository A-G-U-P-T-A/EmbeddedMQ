#ifndef EMQ_POOL_H
#define EMQ_POOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_pool emq_pool;

typedef struct emq_pool_stats {
  uint64_t allocs;
  uint64_t frees;
  uint64_t hits;
  uint64_t misses;
  uint64_t live_bytes;
  uint64_t mmap_bytes;
  uint64_t malloc_fallbacks;
} emq_pool_stats;

emq_pool *emq_pool_create(void);
void emq_pool_destroy(emq_pool *pool);
void *emq_pool_alloc(emq_pool *pool, size_t size);
void emq_pool_free(emq_pool *pool, void *ptr, size_t size);
void emq_pool_get_stats(const emq_pool *pool, emq_pool_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_POOL_H */
