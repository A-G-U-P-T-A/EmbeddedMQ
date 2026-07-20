#ifndef EMQ_BITMAP_H
#define EMQ_BITMAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_bitmap {
  uint32_t capacity;
  uint32_t l0_words;
  uint32_t l1_words;
  uint32_t l2_words;
  uint64_t *l0;
  uint64_t *l1;
  uint64_t *l2;
} emq_bitmap;

int emq_bitmap_init(emq_bitmap *bm, uint32_t capacity);
void emq_bitmap_destroy(emq_bitmap *bm);

/* Returns 1 if bit transitioned 0->1, 0 if already set. */
int emq_bitmap_set(emq_bitmap *bm, uint32_t bit);
/* Returns 1 if bit transitioned 1->0, 0 if already clear. */
int emq_bitmap_clear(emq_bitmap *bm, uint32_t bit);
int emq_bitmap_test(const emq_bitmap *bm, uint32_t bit);

/* Returns bit index or UINT32_MAX if none. */
uint32_t emq_bitmap_find_first_set(const emq_bitmap *bm);
uint32_t emq_bitmap_find_next_set(const emq_bitmap *bm, uint32_t after);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_BITMAP_H */
