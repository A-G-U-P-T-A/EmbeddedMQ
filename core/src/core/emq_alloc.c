#include "core/emq_alloc.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

static size_t emq_align_up(size_t v, size_t align) {
  return (v + (align - 1u)) & ~(align - 1u);
}

int emq_arena_init(emq_arena *a, size_t capacity) {
  if (!a || capacity == 0) return -1;
  memset(a, 0, sizeof(*a));
  a->base = (uint8_t *)emq_aligned_alloc(EMQ_CACHE_LINE, capacity);
  if (!a->base) return -2;
  a->capacity = capacity;
  a->used = 0;
  return 0;
}

void *emq_arena_alloc(emq_arena *a, size_t size, size_t align) {
  size_t off;
  if (!a || size == 0) return NULL;
  if (align == 0) align = 8;
  off = emq_align_up(a->used, align);
  if (off + size > a->capacity) return NULL;
  a->used = off + size;
  return a->base + off;
}

void emq_arena_reset(emq_arena *a) {
  if (a) a->used = 0;
}

void emq_arena_destroy(emq_arena *a) {
  if (!a) return;
  emq_aligned_free(a->base);
  memset(a, 0, sizeof(*a));
}

int emq_slab_init(emq_slab *s, size_t obj_size, size_t capacity) {
  size_t i;
  if (!s || obj_size == 0 || capacity == 0) return -1;
  memset(s, 0, sizeof(*s));
  s->obj_size = emq_align_up(obj_size, 8);
  s->capacity = capacity;
  s->base = (uint8_t *)emq_aligned_alloc(EMQ_CACHE_LINE, s->obj_size * capacity);
  s->free_list = (uint32_t *)malloc(sizeof(uint32_t) * capacity);
  if (!s->base || !s->free_list) {
    emq_slab_destroy(s);
    return -2;
  }
  s->free_count = capacity;
  for (i = 0; i < capacity; ++i) {
    s->free_list[i] = (uint32_t)(capacity - 1u - i);
  }
  return 0;
}

void *emq_slab_alloc(emq_slab *s) {
  uint32_t idx;
  if (!s || s->free_count == 0) return NULL;
  idx = s->free_list[--s->free_count];
  return s->base + ((size_t)idx * s->obj_size);
}

void emq_slab_free(emq_slab *s, void *obj) {
  size_t off;
  uint32_t idx;
  if (!s || !obj) return;
  off = (size_t)((uint8_t *)obj - s->base);
  if (off % s->obj_size != 0) return;
  idx = (uint32_t)(off / s->obj_size);
  if (idx >= s->capacity || s->free_count >= s->capacity) return;
  s->free_list[s->free_count++] = idx;
}

void emq_slab_destroy(emq_slab *s) {
  if (!s) return;
  emq_aligned_free(s->base);
  free(s->free_list);
  memset(s, 0, sizeof(*s));
}
