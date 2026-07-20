/* Feature macros come from CMake (-D_GNU_SOURCE / -D_DARWIN_C_SOURCE). */
#include "platform/emq_platform.h"
#include "core/emq_fault.h"
#include "core/emq_mem.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(EMQ_PLATFORM_WINDOWS) || defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <process.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <errno.h>
#  include <time.h>
#endif

/* ===================== Clock ===================== */

uint64_t emq_now_ns(void) {
#if defined(_WIN32)
  static LARGE_INTEGER freq = {0};
  LARGE_INTEGER counter;
  if (freq.QuadPart == 0) {
    QueryPerformanceFrequency(&freq);
  }
  QueryPerformanceCounter(&counter);
  return (uint64_t)((counter.QuadPart * 1000000000ULL) / (uint64_t)freq.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

void emq_sleep_ms(uint32_t ms) {
#if defined(_WIN32)
  Sleep(ms);
#else
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000u);
  ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
  nanosleep(&ts, NULL);
#endif
}

/* ===================== Threads ===================== */

struct emq_thread {
#if defined(_WIN32)
  HANDLE handle;
#else
  pthread_t handle;
#endif
  emq_thread_fn fn;
  void *arg;
};

#if defined(_WIN32)
static unsigned __stdcall emq_thread_trampoline(void *arg) {
  emq_thread *t = (emq_thread *)arg;
  t->fn(t->arg);
  return 0;
}
#else
static void *emq_thread_trampoline(void *arg) {
  emq_thread *t = (emq_thread *)arg;
  t->fn(t->arg);
  return NULL;
}
#endif

int emq_thread_create(emq_thread **out, emq_thread_fn fn, void *arg) {
  emq_thread *t;
  if (!out || !fn) return -1;
  t = (emq_thread *)calloc(1, sizeof(*t));
  if (!t) return -2;
  t->fn = fn;
  t->arg = arg;
#if defined(_WIN32)
  t->handle = (HANDLE)_beginthreadex(NULL, 0, emq_thread_trampoline, t, 0, NULL);
  if (!t->handle) {
    free(t);
    return -2;
  }
#else
  if (pthread_create(&t->handle, NULL, emq_thread_trampoline, t) != 0) {
    free(t);
    return -2;
  }
#endif
  *out = t;
  return 0;
}

void emq_thread_join(emq_thread *t) {
  if (!t) return;
#if defined(_WIN32)
  WaitForSingleObject(t->handle, INFINITE);
  CloseHandle(t->handle);
  t->handle = NULL;
#else
  pthread_join(t->handle, NULL);
#endif
}

void emq_thread_destroy(emq_thread *t) {
  free(t);
}

/* ===================== Mutex / Cond ===================== */

struct emq_mutex {
#if defined(_WIN32)
  CRITICAL_SECTION cs;
#else
  pthread_mutex_t m;
#endif
};

struct emq_cond {
#if defined(_WIN32)
  CONDITION_VARIABLE cv;
#else
  pthread_cond_t c;
#endif
};

emq_mutex *emq_mutex_create(void) {
  emq_mutex *m = (emq_mutex *)calloc(1, sizeof(*m));
  if (!m) return NULL;
#if defined(_WIN32)
  InitializeCriticalSection(&m->cs);
#else
  if (pthread_mutex_init(&m->m, NULL) != 0) {
    free(m);
    return NULL;
  }
#endif
  return m;
}

void emq_mutex_lock(emq_mutex *m) {
  if (!m) return;
#if defined(_WIN32)
  EnterCriticalSection(&m->cs);
#else
  pthread_mutex_lock(&m->m);
#endif
}

void emq_mutex_unlock(emq_mutex *m) {
  if (!m) return;
#if defined(_WIN32)
  LeaveCriticalSection(&m->cs);
#else
  pthread_mutex_unlock(&m->m);
#endif
}

void emq_mutex_destroy(emq_mutex *m) {
  if (!m) return;
#if defined(_WIN32)
  DeleteCriticalSection(&m->cs);
#else
  pthread_mutex_destroy(&m->m);
#endif
  free(m);
}

emq_cond *emq_cond_create(void) {
  emq_cond *c = (emq_cond *)calloc(1, sizeof(*c));
  if (!c) return NULL;
#if defined(_WIN32)
  InitializeConditionVariable(&c->cv);
#else
  if (pthread_cond_init(&c->c, NULL) != 0) {
    free(c);
    return NULL;
  }
#endif
  return c;
}

void emq_cond_wait(emq_cond *c, emq_mutex *m) {
#if defined(_WIN32)
  SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
#else
  pthread_cond_wait(&c->c, &m->m);
#endif
}

int emq_cond_timedwait(emq_cond *c, emq_mutex *m, uint32_t timeout_ms) {
#if defined(_WIN32)
  if (SleepConditionVariableCS(&c->cv, &m->cs, timeout_ms)) return 0;
  return 1;
#else
  struct timespec ts;
  uint64_t deadline;
  clock_gettime(CLOCK_REALTIME, &ts);
  deadline = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
  deadline += (uint64_t)timeout_ms * 1000000ULL;
  ts.tv_sec = (time_t)(deadline / 1000000000ULL);
  ts.tv_nsec = (long)(deadline % 1000000000ULL);
  if (pthread_cond_timedwait(&c->c, &m->m, &ts) == ETIMEDOUT) return 1;
  return 0;
#endif
}

void emq_cond_signal(emq_cond *c) {
#if defined(_WIN32)
  WakeConditionVariable(&c->cv);
#else
  pthread_cond_signal(&c->c);
#endif
}

void emq_cond_broadcast(emq_cond *c) {
#if defined(_WIN32)
  WakeAllConditionVariable(&c->cv);
#else
  pthread_cond_broadcast(&c->c);
#endif
}

void emq_cond_destroy(emq_cond *c) {
  if (!c) return;
#if !defined(_WIN32)
  pthread_cond_destroy(&c->c);
#endif
  free(c);
}

/* ===================== File IO ===================== */

struct emq_file {
#if defined(_WIN32)
  HANDLE h;
#else
  int fd;
#endif
};

int emq_mkdir_p(const char *path) {
  char tmp[1024];
  size_t len;
  size_t i;
  if (!path || !*path) return -1;
  len = strlen(path);
  if (len >= sizeof(tmp)) return -1;
  memcpy(tmp, path, len + 1);
  for (i = 1; i < len; ++i) {
    if (tmp[i] == '/' || tmp[i] == '\\') {
      char ch = tmp[i];
      tmp[i] = '\0';
#if defined(_WIN32)
      CreateDirectoryA(tmp, NULL);
#else
      mkdir(tmp, 0755);
#endif
      tmp[i] = ch;
    }
  }
#if defined(_WIN32)
  CreateDirectoryA(tmp, NULL);
#else
  mkdir(tmp, 0755);
#endif
  return 0;
}

int emq_file_open(emq_file **out, const char *path, int create, int writable) {
  emq_file *f;
  if (!out || !path) return -1;
  f = (emq_file *)calloc(1, sizeof(*f));
  if (!f) return -2;
#if defined(_WIN32)
  {
    DWORD access = GENERIC_READ | (writable ? GENERIC_WRITE : 0);
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD disp = create ? OPEN_ALWAYS : OPEN_EXISTING;
    f->h = CreateFileA(path, access, share, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f->h == INVALID_HANDLE_VALUE) {
      free(f);
      return -6;
    }
  }
#else
  {
    int flags = writable ? O_RDWR : O_RDONLY;
    if (create) flags |= O_CREAT;
    f->fd = open(path, flags, 0644);
    if (f->fd < 0) {
      free(f);
      return -6;
    }
  }
#endif
  *out = f;
  return 0;
}

int emq_file_close(emq_file *f) {
  if (!f) return 0;
#if defined(_WIN32)
  CloseHandle(f->h);
#else
  close(f->fd);
#endif
  free(f);
  return 0;
}

int64_t emq_file_size(emq_file *f) {
#if defined(_WIN32)
  LARGE_INTEGER li;
  if (!GetFileSizeEx(f->h, &li)) return -1;
  return (int64_t)li.QuadPart;
#else
  struct stat st;
  if (fstat(f->fd, &st) != 0) return -1;
  return (int64_t)st.st_size;
#endif
}

int emq_file_resize(emq_file *f, int64_t size) {
  if (EMQ_FAULT_CHECK("file_resize")) return -6;
#if defined(_WIN32)
  LARGE_INTEGER li;
  li.QuadPart = size;
  if (!SetFilePointerEx(f->h, li, NULL, FILE_BEGIN)) return -6;
  if (!SetEndOfFile(f->h)) return -6;
  return 0;
#else
  if (ftruncate(f->fd, size) != 0) return -6;
  return 0;
#endif
}

int emq_file_pwrite(emq_file *f, const void *buf, size_t len, int64_t off) {
  if (EMQ_FAULT_CHECK("file_pwrite")) return -6;
  if (EMQ_FAULT_CHECK("file_short_write") && len > 1) {
    len = len / 2u;
    if (len == 0) len = 1;
  }
#if defined(_WIN32)
  OVERLAPPED ov;
  DWORD written = 0;
  memset(&ov, 0, sizeof(ov));
  ov.Offset = (DWORD)(off & 0xffffffffu);
  ov.OffsetHigh = (DWORD)((uint64_t)off >> 32);
  if (!WriteFile(f->h, buf, (DWORD)len, &written, &ov)) return -6;
  return (int)written;
#else
  ssize_t n = pwrite(f->fd, buf, len, off);
  if (n < 0) return -6;
  return (int)n;
#endif
}

int emq_file_pread(emq_file *f, void *buf, size_t len, int64_t off) {
  if (EMQ_FAULT_CHECK("file_pread")) return -6;
#if defined(_WIN32)
  OVERLAPPED ov;
  DWORD readn = 0;
  memset(&ov, 0, sizeof(ov));
  ov.Offset = (DWORD)(off & 0xffffffffu);
  ov.OffsetHigh = (DWORD)((uint64_t)off >> 32);
  if (!ReadFile(f->h, buf, (DWORD)len, &readn, &ov)) return -6;
  return (int)readn;
#else
  ssize_t n = pread(f->fd, buf, len, off);
  if (n < 0) return -6;
  return (int)n;
#endif
}

int emq_file_sync(emq_file *f) {
  if (EMQ_FAULT_CHECK("file_sync")) return -6;
#if defined(_WIN32)
  return FlushFileBuffers(f->h) ? 0 : -6;
#else
  return fsync(f->fd) == 0 ? 0 : -6;
#endif
}

/* ===================== mmap ===================== */

int emq_mmap_create(emq_mmap *out, emq_file *f, size_t size, int writable) {
  if (!out || !f || size == 0) return -1;
  memset(out, 0, sizeof(*out));
  out->size = size;
#if defined(_WIN32)
  {
    DWORD protect = writable ? PAGE_READWRITE : PAGE_READONLY;
    DWORD access = writable ? FILE_MAP_WRITE : FILE_MAP_READ;
    HANDLE map = CreateFileMappingA(f->h, NULL, protect,
                                    (DWORD)((uint64_t)size >> 32),
                                    (DWORD)(size & 0xffffffffu), NULL);
    if (!map) return -6;
    out->os_handle = (void *)map;
    out->addr = MapViewOfFile(map, access, 0, 0, size);
    if (!out->addr) {
      CloseHandle(map);
      out->os_handle = NULL;
      return -6;
    }
  }
#else
  {
    int prot = PROT_READ | (writable ? PROT_WRITE : 0);
    if (writable) {
      if (emq_file_resize(f, (int64_t)size) != 0) return -6;
    }
    out->addr = mmap(NULL, size, prot, MAP_SHARED, f->fd, 0);
    if (out->addr == MAP_FAILED) {
      out->addr = NULL;
      return -6;
    }
    out->os_handle = NULL;
  }
#endif
  return 0;
}

int emq_mmap_sync(emq_mmap *m) {
  if (!m || !m->addr || m->size == 0) return 0;
  if (EMQ_FAULT_CHECK("mmap_sync")) return -6;
#if defined(_WIN32)
  return FlushViewOfFile(m->addr, m->size) ? 0 : -6;
#else
  return msync(m->addr, m->size, MS_SYNC) == 0 ? 0 : -6;
#endif
}

int emq_mmap_unmap(emq_mmap *m) {
  if (!m || !m->addr) return 0;
#if defined(_WIN32)
  UnmapViewOfFile(m->addr);
  if (m->os_handle) CloseHandle((HANDLE)m->os_handle);
#else
  munmap(m->addr, m->size);
#endif
  m->addr = NULL;
  m->os_handle = NULL;
  m->size = 0;
  return 0;
}

/* ===================== Page memory ===================== */

void *emq_os_alloc_pages(size_t size) {
  if (size == 0) return NULL;
#if defined(_WIN32)
  {
    /* Prefer large pages when permitted; fall back to normal pages. */
    void *p = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
                           PAGE_READWRITE);
    if (p) return p;
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  }
#else
  {
    void *p;
#  if defined(MAP_HUGETLB)
    p = mmap(NULL, size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANON | MAP_HUGETLB, -1, 0);
    if (p != MAP_FAILED) return p;
#  endif
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return NULL;
#  if defined(MADV_HUGEPAGE)
    (void)madvise(p, size, MADV_HUGEPAGE);
#  endif
    return p;
  }
#endif
}

void emq_os_free_pages(void *p, size_t size) {
  if (!p || size == 0) return;
#if defined(_WIN32)
  (void)VirtualFree(p, 0, MEM_RELEASE);
#else
  munmap(p, size);
#endif
}

/* ===================== Atomic wait/wake ===================== */

#if defined(_WIN32)
int emq_wait_u64(volatile uint64_t *addr, uint64_t expect,
                 uint32_t timeout_ms) {
  if (!addr) return -1;
  if (*addr != expect) return 0;
  if (timeout_ms == UINT32_MAX) {
    for (;;) {
      if (*addr != expect) return 0;
      if (!WaitOnAddress(addr, &expect, sizeof(expect), INFINITE)) return -1;
    }
  }
  if (!WaitOnAddress(addr, &expect, sizeof(expect), timeout_ms)) {
    if (*addr != expect) return 0;
    return 1;
  }
  return 0;
}

void emq_wake_u64(volatile uint64_t *addr, int count) {
  if (!addr) return;
  if (count <= 0) return;
  if (count == 1) {
    WakeByAddressSingle((void *)addr);
  } else {
    WakeByAddressAll((void *)addr);
  }
}
#else
#  if defined(__linux__)
#    include <linux/futex.h>
#    include <sys/syscall.h>
#    ifndef FUTEX_WAIT_PRIVATE
#      define FUTEX_WAIT_PRIVATE 128
#    endif
#    ifndef FUTEX_WAKE_PRIVATE
#      define FUTEX_WAKE_PRIVATE 129
#    endif

static long emq_futex_wait(volatile uint32_t *addr, uint32_t val,
                           const struct timespec *ts) {
  return syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val, ts, NULL, 0);
}

static long emq_futex_wake(volatile uint32_t *addr, int count) {
  return syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
}

int emq_wait_u64(volatile uint64_t *addr, uint64_t expect,
                 uint32_t timeout_ms) {
  struct timespec ts;
  struct timespec *tsp = NULL;
  if (!addr) return -1;
  if (*addr != expect) return 0;
  /* FUTEX_WAIT takes a *relative* timeout (CLOCK_MONOTONIC). */
  if (timeout_ms != UINT32_MAX) {
    ts.tv_sec = (time_t)(timeout_ms / 1000u);
    ts.tv_nsec = (long)((timeout_ms % 1000u) * 1000000u);
    tsp = &ts;
  }
  for (;;) {
    long rc;
    if (*addr != expect) return 0;
    rc = emq_futex_wait((volatile uint32_t *)addr, (uint32_t)expect, tsp);
    if (*addr != expect) return 0;
    if (rc == 0) return 0;
    if (rc == -1 && errno == ETIMEDOUT) return 1;
    if (rc == -1 && errno == EINTR) continue;
    /* EAGAIN: value already changed */
    if (rc == -1 && errno == EAGAIN) return 0;
    return -1;
  }
}

void emq_wake_u64(volatile uint64_t *addr, int count) {
  if (!addr || count <= 0) return;
  (void)emq_futex_wake((volatile uint32_t *)addr, count);
}
#  else
typedef struct emq_wait_entry {
  volatile uint64_t *addr;
  emq_mutex *mu;
  emq_cond *cv;
  struct emq_wait_entry *next;
} emq_wait_entry;

static emq_mutex *emq_wait_table_mu;
static emq_wait_entry *emq_wait_table[256];

static emq_wait_entry *emq_wait_get_entry(volatile uint64_t *addr) {
  unsigned idx = (unsigned)(((uintptr_t)addr >> 3) & 255u);
  emq_wait_entry *e;
  emq_wait_entry *it;
  if (!emq_wait_table_mu) {
    emq_wait_table_mu = emq_mutex_create();
  }
  emq_mutex_lock(emq_wait_table_mu);
  for (it = emq_wait_table[idx]; it; it = it->next) {
    if (it->addr == addr) {
      emq_mutex_unlock(emq_wait_table_mu);
      return it;
    }
  }
  e = (emq_wait_entry *)calloc(1, sizeof(*e));
  if (e) {
    e->addr = addr;
    e->mu = emq_mutex_create();
    e->cv = emq_cond_create();
    e->next = emq_wait_table[idx];
    emq_wait_table[idx] = e;
  }
  emq_mutex_unlock(emq_wait_table_mu);
  return e;
}

int emq_wait_u64(volatile uint64_t *addr, uint64_t expect,
                 uint32_t timeout_ms) {
  emq_wait_entry *e;
  int rc;
  if (!addr) return -1;
  if (*addr != expect) return 0;
  e = emq_wait_get_entry(addr);
  if (!e || !e->mu || !e->cv) return -1;
  emq_mutex_lock(e->mu);
  while (*addr == expect) {
    rc = emq_cond_timedwait(e->cv, e->mu, timeout_ms);
    if (*addr != expect) break;
    if (rc == 1) {
      emq_mutex_unlock(e->mu);
      return 1;
    }
  }
  emq_mutex_unlock(e->mu);
  return 0;
}

void emq_wake_u64(volatile uint64_t *addr, int count) {
  emq_wait_entry *e;
  (void)count;
  if (!addr) return;
  e = emq_wait_get_entry(addr);
  if (!e || !e->mu || !e->cv) return;
  emq_mutex_lock(e->mu);
  emq_cond_broadcast(e->cv);
  emq_mutex_unlock(e->mu);
}
#  endif
#endif

/* ===================== Aligned alloc ===================== */

void *emq_aligned_alloc(size_t alignment, size_t size) {
  if (EMQ_FAULT_CHECK("malloc")) return NULL;
#if defined(_WIN32)
  return _aligned_malloc(size, alignment);
#elif defined(__APPLE__) || defined(__linux__)
  void *p = NULL;
  if (posix_memalign(&p, alignment, size) != 0) return NULL;
  return p;
#else
  (void)alignment;
  return emq_malloc(size);
#endif
}

void emq_aligned_free(void *p) {
#if defined(_WIN32)
  _aligned_free(p);
#else
  free(p);
#endif
}
