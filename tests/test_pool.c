#include "emq_test.h"
#include "core/emq_pool.h"

#include <string.h>

int main(void) {
  emq_pool *pool;
  emq_pool_stats stats;
  void *p64;
  void *p128;
  void *big;

  pool = emq_pool_create();
  EMQ_CHECK(pool != NULL);

  p64 = emq_pool_alloc(pool, 32);
  p128 = emq_pool_alloc(pool, 100);
  EMQ_CHECK(p64 != NULL);
  EMQ_CHECK(p128 != NULL);
  EMQ_CHECK(p64 != p128);

  memset(p64, 0xAA, 32);
  memset(p128, 0xBB, 100);

  emq_pool_free(pool, p64, 32);
  emq_pool_free(pool, p128, 100);

  p64 = emq_pool_alloc(pool, 32);
  EMQ_CHECK(p64 != NULL);

  big = emq_pool_alloc(pool, 70000);
  EMQ_CHECK(big != NULL);
  emq_pool_free(pool, big, 70000);

  emq_pool_get_stats(pool, &stats);
  EMQ_CHECK(stats.allocs >= 3);
  EMQ_CHECK(stats.frees >= 2);
  EMQ_CHECK(stats.malloc_fallbacks >= 1);
  EMQ_CHECK(stats.mmap_bytes >= 2u * 1024u * 1024u);

  emq_pool_destroy(pool);
  return emq_test_report();
}
