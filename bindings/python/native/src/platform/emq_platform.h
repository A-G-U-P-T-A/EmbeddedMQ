#ifndef EMQ_PLATFORM_H
#define EMQ_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMQ_CACHE_LINE 64

#if defined(_MSC_VER)
#  define EMQ_ALIGN_CACHE __declspec(align(EMQ_CACHE_LINE))
#  define EMQ_FORCEINLINE __forceinline
#else
#  define EMQ_ALIGN_CACHE __attribute__((aligned(EMQ_CACHE_LINE)))
#  define EMQ_FORCEINLINE static inline __attribute__((always_inline))
#endif

/* ---- Clock ---- */
uint64_t emq_now_ns(void);
void emq_sleep_ms(uint32_t ms);

/* ---- Threads ---- */
typedef struct emq_thread emq_thread;
typedef void (*emq_thread_fn)(void *arg);

int emq_thread_create(emq_thread **out, emq_thread_fn fn, void *arg);
void emq_thread_join(emq_thread *t);
void emq_thread_destroy(emq_thread *t);

/* ---- Mutex / Cond ---- */
typedef struct emq_mutex emq_mutex;
typedef struct emq_cond emq_cond;

emq_mutex *emq_mutex_create(void);
void emq_mutex_lock(emq_mutex *m);
void emq_mutex_unlock(emq_mutex *m);
void emq_mutex_destroy(emq_mutex *m);

emq_cond *emq_cond_create(void);
void emq_cond_wait(emq_cond *c, emq_mutex *m);
int emq_cond_timedwait(emq_cond *c, emq_mutex *m, uint32_t timeout_ms); /* 0=ok, 1=timeout */
void emq_cond_signal(emq_cond *c);
void emq_cond_broadcast(emq_cond *c);
void emq_cond_destroy(emq_cond *c);

/* ---- File IO ---- */
typedef struct emq_file emq_file;

int emq_file_open(emq_file **out, const char *path, int create, int writable);
int emq_file_close(emq_file *f);
int64_t emq_file_size(emq_file *f);
int emq_file_resize(emq_file *f, int64_t size);
int emq_file_pwrite(emq_file *f, const void *buf, size_t len, int64_t off);
int emq_file_pread(emq_file *f, void *buf, size_t len, int64_t off);
int emq_file_sync(emq_file *f);
int emq_mkdir_p(const char *path);

/* ---- Memory map ---- */
typedef struct emq_mmap {
  void *addr;
  size_t size;
  void *os_handle;
} emq_mmap;

int emq_mmap_create(emq_mmap *out, emq_file *f, size_t size, int writable);
int emq_mmap_sync(emq_mmap *m);
int emq_mmap_unmap(emq_mmap *m);

/* ---- Page memory ---- */
void *emq_os_alloc_pages(size_t size);
void emq_os_free_pages(void *p, size_t size);

/* ---- Atomic wait/wake (no emq_atomic.h dependency) ---- */
int emq_wait_u64(volatile uint64_t *addr, uint64_t expect,
                 uint32_t timeout_ms); /* 0=woke, 1=timeout, -1=err */
void emq_wake_u64(volatile uint64_t *addr, int count);

/* ---- Misc ---- */
void *emq_aligned_alloc(size_t alignment, size_t size);
void emq_aligned_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_PLATFORM_H */
