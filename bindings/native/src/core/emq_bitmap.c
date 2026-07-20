#include "core/emq_bitmap.h"
#include "core/emq_atomic.h"

#include <stdlib.h>
#include <string.h>

static uint32_t emq_bitmap_l1_words_for(uint32_t l0_words) {
  return (l0_words + 63u) / 64u;
}

static uint32_t emq_bitmap_l2_words_for(uint32_t l1_words) {
  return (l1_words + 63u) / 64u;
}

int emq_bitmap_init(emq_bitmap *bm, uint32_t capacity) {
  if (!bm || capacity == 0) return -1;
  memset(bm, 0, sizeof(*bm));
  bm->capacity = capacity;
  bm->l0_words = (capacity + 63u) / 64u;
  bm->l1_words = emq_bitmap_l1_words_for(bm->l0_words);
  bm->l2_words = emq_bitmap_l2_words_for(bm->l1_words);
  if (bm->l2_words == 0) bm->l2_words = 1;

  bm->l0 = (uint64_t *)calloc(bm->l0_words, sizeof(uint64_t));
  bm->l1 = (uint64_t *)calloc(bm->l1_words, sizeof(uint64_t));
  bm->l2 = (uint64_t *)calloc(bm->l2_words, sizeof(uint64_t));
  if (!bm->l0 || !bm->l1 || !bm->l2) {
    emq_bitmap_destroy(bm);
    return -2;
  }
  return 0;
}

void emq_bitmap_destroy(emq_bitmap *bm) {
  if (!bm) return;
  free(bm->l0);
  free(bm->l1);
  free(bm->l2);
  memset(bm, 0, sizeof(*bm));
}

static void emq_bitmap_update_parents(emq_bitmap *bm, uint32_t l0_idx) {
  uint32_t l1_idx = l0_idx / 64u;
  uint32_t l2_idx = l1_idx / 64u;
  uint64_t l0_val = bm->l0[l0_idx];
  uint64_t l1_mask = 1ull << (l0_idx % 64u);

  if (l0_val) {
    bm->l1[l1_idx] |= l1_mask;
  } else {
    bm->l1[l1_idx] &= ~l1_mask;
  }

  if (bm->l1[l1_idx]) {
    bm->l2[l2_idx] |= 1ull << (l1_idx % 64u);
  } else {
    bm->l2[l2_idx] &= ~(1ull << (l1_idx % 64u));
  }
}

int emq_bitmap_set(emq_bitmap *bm, uint32_t bit) {
  uint32_t word;
  uint64_t mask;
  uint64_t old;
  uint64_t neu;
  if (!bm || bit >= bm->capacity) return 0;
  word = bit / 64u;
  mask = 1ull << (bit % 64u);
  /* CAS so concurrent activators sharing a word are TSan-clean. */
  old = bm->l0[word];
  for (;;) {
    if (old & mask) return 0;
    neu = old | mask;
    if (emq_atomic_cas_u64((emq_atomic_u64 *)&bm->l0[word], &old, neu)) break;
  }
  emq_bitmap_update_parents(bm, word);
  return 1;
}

int emq_bitmap_clear(emq_bitmap *bm, uint32_t bit) {
  uint32_t word;
  uint64_t mask;
  uint64_t old;
  uint64_t neu;
  if (!bm || bit >= bm->capacity) return 0;
  word = bit / 64u;
  mask = 1ull << (bit % 64u);
  old = bm->l0[word];
  for (;;) {
    if (!(old & mask)) return 0;
    neu = old & ~mask;
    if (emq_atomic_cas_u64((emq_atomic_u64 *)&bm->l0[word], &old, neu)) break;
  }
  emq_bitmap_update_parents(bm, word);
  return 1;
}

int emq_bitmap_test(const emq_bitmap *bm, uint32_t bit) {
  if (!bm || bit >= bm->capacity) return 0;
  return (int)((bm->l0[bit / 64u] >> (bit % 64u)) & 1ull);
}

static uint32_t emq_bitmap_scan_word(uint64_t word, uint32_t base_bit,
                                     uint32_t capacity) {
  unsigned tz;
  uint32_t bit;
  if (word == 0) return UINT32_MAX;
  tz = emq_ctz64(word);
  bit = base_bit + tz;
  if (bit >= capacity) return UINT32_MAX;
  return bit;
}

uint32_t emq_bitmap_find_first_set(const emq_bitmap *bm) {
  uint32_t l0_i;
  if (!bm) return UINT32_MAX;

  for (l0_i = 0; l0_i < bm->l0_words; ++l0_i) {
    uint32_t l1_i = l0_i / 64u;
    uint32_t l2_i = l1_i / 64u;
    if (!(bm->l2[l2_i] & (1ull << (l1_i % 64u)))) continue;
    if (!(bm->l1[l1_i] & (1ull << (l0_i % 64u)))) continue;
    {
      uint32_t bit =
          emq_bitmap_scan_word(bm->l0[l0_i], l0_i * 64u, bm->capacity);
      if (bit != UINT32_MAX) return bit;
    }
  }
  return UINT32_MAX;
}

uint32_t emq_bitmap_find_next_set(const emq_bitmap *bm, uint32_t after) {
  uint32_t start_word;
  uint32_t start_bit;
  uint32_t l0_i;
  if (!bm) return UINT32_MAX;
  if (after >= bm->capacity - 1u) return UINT32_MAX;

  start_bit = after + 1u;
  start_word = start_bit / 64u;
  {
    uint64_t word = bm->l0[start_word];
    word &= ~((1ull << (start_bit % 64u)) - 1ull);
    {
      uint32_t bit = emq_bitmap_scan_word(word, start_word * 64u, bm->capacity);
      if (bit != UINT32_MAX) return bit;
    }
  }
  for (l0_i = start_word + 1u; l0_i < bm->l0_words; ++l0_i) {
    uint32_t bit = emq_bitmap_scan_word(bm->l0[l0_i], l0_i * 64u, bm->capacity);
    if (bit != UINT32_MAX) return bit;
  }
  return UINT32_MAX;
}
