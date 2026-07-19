#include "emq_test.h"
#include "core/emq_bitmap.h"

int main(void) {
  emq_bitmap bm;

  EMQ_CHECK(emq_bitmap_init(&bm, 100000) == 0);
  EMQ_CHECK(emq_bitmap_find_first_set(&bm) == UINT32_MAX);

  EMQ_CHECK(emq_bitmap_set(&bm, 0) == 1);
  EMQ_CHECK(emq_bitmap_set(&bm, 0) == 0);
  EMQ_CHECK(emq_bitmap_test(&bm, 0) == 1);
  EMQ_CHECK(emq_bitmap_find_first_set(&bm) == 0u);

  EMQ_CHECK(emq_bitmap_set(&bm, 99999) == 1);
  EMQ_CHECK(emq_bitmap_find_next_set(&bm, 0) == 99999u);

  EMQ_CHECK(emq_bitmap_clear(&bm, 0) == 1);
  EMQ_CHECK(emq_bitmap_find_first_set(&bm) == 99999u);

  EMQ_CHECK(emq_bitmap_clear(&bm, 99999) == 1);
  EMQ_CHECK(emq_bitmap_find_first_set(&bm) == UINT32_MAX);

  emq_bitmap_destroy(&bm);
  return emq_test_report();
}
