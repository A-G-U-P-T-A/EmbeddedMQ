#include "emq_test.h"
#include "core/emq_alloc.h"

int main(void) {
  emq_arena a;
  emq_slab s;
  void *p1, *p2, *p3;

  EMQ_CHECK(emq_arena_init(&a, 1024) == 0);
  p1 = emq_arena_alloc(&a, 16, 8);
  p2 = emq_arena_alloc(&a, 32, 16);
  EMQ_CHECK(p1 != NULL);
  EMQ_CHECK(p2 != NULL);
  EMQ_CHECK(p2 > p1);
  emq_arena_reset(&a);
  p3 = emq_arena_alloc(&a, 16, 8);
  EMQ_CHECK(p3 == p1);
  emq_arena_destroy(&a);

  EMQ_CHECK(emq_slab_init(&s, 64, 4) == 0);
  p1 = emq_slab_alloc(&s);
  p2 = emq_slab_alloc(&s);
  EMQ_CHECK(p1 != NULL && p2 != NULL && p1 != p2);
  emq_slab_free(&s, p1);
  p3 = emq_slab_alloc(&s);
  EMQ_CHECK(p3 == p1);
  emq_slab_destroy(&s);

  return emq_test_report();
}
