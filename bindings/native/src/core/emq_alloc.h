#ifndef EMQ_ALLOC_H
#define EMQ_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Arena: bump allocator, free all at once */
typedef struct emq_arena {
  uint8_t *base;
  size_t capacity;
  size_t used;
} emq_arena;

int emq_arena_init(emq_arena *a, size_t capacity);
void *emq_arena_alloc(emq_arena *a, size_t size, size_t align);
void emq_arena_reset(emq_arena *a);
void emq_arena_destroy(emq_arena *a);

/* Slab: fixed-size object pool */
typedef struct emq_slab {
  uint8_t *base;
  size_t obj_size;
  size_t capacity;
  size_t free_count;
  uint32_t *free_list;
} emq_slab;

int emq_slab_init(emq_slab *s, size_t obj_size, size_t capacity);
void *emq_slab_alloc(emq_slab *s);
void emq_slab_free(emq_slab *s, void *obj);
void emq_slab_destroy(emq_slab *s);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_ALLOC_H */
