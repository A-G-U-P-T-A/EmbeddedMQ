#include "emq_ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <time.h>
#  include <unistd.h>
#endif

enum {
  EMQ_IPC_MAGIC = 0x514D4545u, /* "EEMQ" little-endian */
  EMQ_IPC_VERSION = 1u,
  EMQ_IPC_FRAME_HDR = 16u,
  EMQ_IPC_FRAME_EMPTY = 0,
  EMQ_IPC_FRAME_CLAIMED = 1,
  EMQ_IPC_FRAME_COMMITTED = 2
};

typedef struct emq_ipc_frame {
  int32_t length;
  int32_t status;
  uint32_t reserved[2];
} emq_ipc_frame;

typedef struct emq_ipc_ring_hdr {
  uint32_t magic;
  uint32_t version;
  uint32_t capacity;
  uint32_t creator_pid;
  uint64_t head;
  uint64_t tail;
  uint64_t map_bytes;
  char name[64];
} emq_ipc_ring_hdr;

struct emq_ipc_segment {
  char name[64];
  int creator;
  size_t map_size;
  emq_ipc_ring_hdr *hdr;
  uint8_t *ring;
#if defined(_WIN32)
  HANDLE mapping;
#else
  int fd;
#endif
};

static uint32_t emq_ipc_round_up_pow2_u32(uint32_t v) {
  uint32_t c = 1u;
  if (v < 4096u) {
    v = 4096u;
  }
  while (c < v) {
    c <<= 1u;
  }
  return c;
}

static uint32_t emq_ipc_align8(uint32_t v) {
  return (v + 7u) & ~7u;
}

static uint32_t emq_ipc_frame_total(uint32_t data_len) {
  return emq_ipc_align8(EMQ_IPC_FRAME_HDR + data_len);
}

static void emq_ipc_sanitize_name(const char *name, char *out, size_t out_cap) {
  size_t i = 0;
  if (!name || !out || out_cap == 0) {
    return;
  }
  for (; name[i] != '\0' && i + 1u < out_cap; ++i) {
    char c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out[i] = c;
    } else {
      out[i] = '_';
    }
  }
  out[i] = '\0';
}

#if defined(_WIN32)
static emq_status emq_ipc_map_create(const char *obj_name, size_t map_size,
                                     int creator, emq_ipc_segment **out) {
  emq_ipc_segment *seg;
  HANDLE mapping = NULL;
  void *view = NULL;
  DWORD size_high = 0;
  DWORD size_low = (DWORD)map_size;

  seg = (emq_ipc_segment *)calloc(1, sizeof(*seg));
  if (!seg) {
    return EMQ_ERR_NOMEM;
  }

  if (creator) {
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 size_high, size_low, obj_name);
    if (!mapping) {
      free(seg);
      return EMQ_ERR_IO;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      CloseHandle(mapping);
      free(seg);
      return EMQ_ERR_EXISTS;
    }
  } else {
    mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, obj_name);
    if (!mapping) {
      free(seg);
      return EMQ_ERR_IO;
    }
  }

  view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!view) {
    CloseHandle(mapping);
    free(seg);
    return EMQ_ERR_IO;
  }

  seg->mapping = mapping;
  seg->map_size = map_size;
  seg->hdr = (emq_ipc_ring_hdr *)view;
  seg->ring = (uint8_t *)view + sizeof(emq_ipc_ring_hdr);
  seg->creator = creator;
  *out = seg;
  return EMQ_OK;
}

static void emq_ipc_unmap(emq_ipc_segment *seg) {
  if (!seg) {
    return;
  }
  if (seg->hdr) {
    UnmapViewOfFile(seg->hdr);
    seg->hdr = NULL;
    seg->ring = NULL;
  }
  if (seg->mapping) {
    CloseHandle(seg->mapping);
    seg->mapping = NULL;
  }
}
#else
static emq_status emq_ipc_map_create(const char *obj_name, size_t map_size,
                                     int creator, emq_ipc_segment **out) {
  emq_ipc_segment *seg;
  int fd = -1;
  void *view = NULL;
  int flags = creator ? (O_CREAT | O_EXCL | O_RDWR) : O_RDWR;

  seg = (emq_ipc_segment *)calloc(1, sizeof(*seg));
  if (!seg) {
    return EMQ_ERR_NOMEM;
  }

  fd = shm_open(obj_name, flags, (mode_t)0600);
  if (fd < 0) {
    free(seg);
    if (creator && errno == EEXIST) {
      return EMQ_ERR_EXISTS;
    }
    return EMQ_ERR_IO;
  }

  if (creator && ftruncate(fd, (off_t)map_size) != 0) {
    close(fd);
    shm_unlink(obj_name);
    free(seg);
    return EMQ_ERR_IO;
  }

  view = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (view == MAP_FAILED) {
    close(fd);
    if (creator) {
      shm_unlink(obj_name);
    }
    free(seg);
    return EMQ_ERR_IO;
  }

  seg->fd = fd;
  seg->map_size = map_size;
  seg->hdr = (emq_ipc_ring_hdr *)view;
  seg->ring = (uint8_t *)view + sizeof(emq_ipc_ring_hdr);
  seg->creator = creator;
  *out = seg;
  return EMQ_OK;
}

static void emq_ipc_unmap(emq_ipc_segment *seg) {
  if (!seg) {
    return;
  }
  if (seg->hdr) {
    munmap(seg->hdr, seg->map_size);
    seg->hdr = NULL;
    seg->ring = NULL;
  }
  if (seg->fd >= 0) {
    close(seg->fd);
    seg->fd = -1;
  }
}
#endif

static emq_ipc_frame *emq_ipc_frame_at(emq_ipc_segment *seg, uint64_t offset) {
  return (emq_ipc_frame *)(seg->ring + (offset & (seg->hdr->capacity - 1u)));
}

static void emq_ipc_write_padding(emq_ipc_segment *seg, uint64_t offset,
                                  uint32_t pad_len) {
  emq_ipc_frame *f = emq_ipc_frame_at(seg, offset);
  f->length = -(int32_t)pad_len;
  f->status = EMQ_IPC_FRAME_COMMITTED;
}

static emq_status emq_ipc_reserve(emq_ipc_segment *seg, uint32_t need,
                                  uint64_t *out_offset) {
  for (;;) {
    uint64_t head = seg->hdr->head;
    uint64_t tail = seg->hdr->tail;
    uint32_t tail_idx = (uint32_t)(tail & (seg->hdr->capacity - 1u));
    uint32_t pad = 0;
    uint64_t claim_bytes = need;
    uint64_t frame_off;

    if (tail_idx + need > seg->hdr->capacity) {
      pad = seg->hdr->capacity - tail_idx;
      claim_bytes = (uint64_t)pad + need;
    }
    if (tail - head + claim_bytes > seg->hdr->capacity) {
      return EMQ_ERR_FULL;
    }

    frame_off = tail + pad;
    if (pad > 0) {
      emq_ipc_write_padding(seg, tail, pad);
    }
    seg->hdr->tail = tail + claim_bytes;
    *out_offset = frame_off;
    return EMQ_OK;
  }
}

static void emq_ipc_init_header(emq_ipc_segment *seg, const char *name,
                                uint32_t capacity, size_t map_bytes) {
  memset(seg->hdr, 0, sizeof(*seg->hdr));
  seg->hdr->magic = EMQ_IPC_MAGIC;
  seg->hdr->version = EMQ_IPC_VERSION;
  seg->hdr->capacity = capacity;
  seg->hdr->map_bytes = (uint64_t)map_bytes;
#if defined(_WIN32)
  seg->hdr->creator_pid = (uint32_t)GetCurrentProcessId();
#else
  seg->hdr->creator_pid = (uint32_t)getpid();
#endif
  seg->hdr->head = 0;
  seg->hdr->tail = 0;
  emq_ipc_sanitize_name(name, seg->hdr->name, sizeof(seg->hdr->name));
}

static emq_status emq_ipc_validate_header(const emq_ipc_ring_hdr *hdr) {
  if (!hdr || hdr->magic != EMQ_IPC_MAGIC || hdr->version != EMQ_IPC_VERSION) {
    return EMQ_ERR_INVALID;
  }
  if (hdr->capacity < 4096u || (hdr->capacity & (hdr->capacity - 1u)) != 0u) {
    return EMQ_ERR_INVALID;
  }
  return EMQ_OK;
}

emq_status emq_ipc_segment_create(const char *name, size_t ring_capacity,
                                  emq_ipc_segment **out) {
  emq_ipc_segment *seg = NULL;
  char obj_name[128];
  char safe[64];
  uint32_t cap;
  size_t map_size;
  emq_status st;

  if (!name || !out || ring_capacity == 0) {
    return EMQ_ERR_INVALID;
  }

  emq_ipc_sanitize_name(name, safe, sizeof(safe));
#if defined(_WIN32)
  (void)snprintf(obj_name, sizeof(obj_name), "Local\\emq_ipc_%s", safe);
#else
  (void)snprintf(obj_name, sizeof(obj_name), "/emq_ipc_%s", safe);
#endif

  cap = emq_ipc_round_up_pow2_u32((uint32_t)ring_capacity);
  map_size = sizeof(emq_ipc_ring_hdr) + cap;

  st = emq_ipc_map_create(obj_name, map_size, 1, &seg);
  if (st != EMQ_OK) {
    return st;
  }

  emq_ipc_init_header(seg, safe, cap, map_size);
  emq_ipc_sanitize_name(safe, seg->name, sizeof(seg->name));
  *out = seg;
  return EMQ_OK;
}

emq_status emq_ipc_segment_open(const char *name, emq_ipc_segment **out) {
  emq_ipc_segment *seg = NULL;
  char obj_name[128];
  char safe[64];
  size_t map_size;
  emq_status st;

  if (!name || !out) {
    return EMQ_ERR_INVALID;
  }

  emq_ipc_sanitize_name(name, safe, sizeof(safe));
#if defined(_WIN32)
  (void)snprintf(obj_name, sizeof(obj_name), "Local\\emq_ipc_%s", safe);
  map_size = 0;
#else
  (void)snprintf(obj_name, sizeof(obj_name), "/emq_ipc_%s", safe);
  {
    int fd = shm_open(obj_name, O_RDWR, 0);
    struct stat st;
    if (fd < 0) {
      return EMQ_ERR_IO;
    }
    if (fstat(fd, &st) != 0) {
      close(fd);
      return EMQ_ERR_IO;
    }
    map_size = (size_t)st.st_size;
    close(fd);
  }
#endif

  st = emq_ipc_map_create(obj_name, map_size, 0, &seg);
  if (st != EMQ_OK) {
    return st;
  }

  st = emq_ipc_validate_header(seg->hdr);
  if (st != EMQ_OK) {
    emq_ipc_segment_destroy(seg);
    return st;
  }
  if (seg->hdr->map_bytes != 0) {
    seg->map_size = (size_t)seg->hdr->map_bytes;
  }
#if !defined(_WIN32)
  if (seg->hdr->map_bytes != 0 && seg->hdr->map_bytes != (uint64_t)map_size) {
    emq_ipc_segment_destroy(seg);
    return EMQ_ERR_INVALID;
  }
#endif

  emq_ipc_sanitize_name(safe, seg->name, sizeof(seg->name));
  *out = seg;
  return EMQ_OK;
}

void emq_ipc_segment_destroy(emq_ipc_segment *seg) {
  char obj_name[128];

  if (!seg) {
    return;
  }

  if (seg->creator) {
#if defined(_WIN32)
    (void)snprintf(obj_name, sizeof(obj_name), "Local\\emq_ipc_%s", seg->name);
#else
    (void)snprintf(obj_name, sizeof(obj_name), "/emq_ipc_%s", seg->name);
#endif
  }

  emq_ipc_unmap(seg);

#if !defined(_WIN32)
  if (seg->creator) {
    (void)shm_unlink(obj_name);
  }
#endif

  free(seg);
}

emq_status emq_ipc_publish(emq_ipc_segment *seg, const void *data, size_t size) {
  emq_ipc_frame *f;
  uint64_t offset;
  uint32_t total;
  uint32_t len;

  if (!seg || !seg->hdr) {
    return EMQ_ERR_INVALID;
  }
  if (size > UINT32_MAX) {
    return EMQ_ERR_INVALID;
  }
  len = (uint32_t)size;
  if (len > 0 && !data) {
    return EMQ_ERR_INVALID;
  }

  total = emq_ipc_frame_total(len);
  if (total > seg->hdr->capacity) {
    return EMQ_ERR_FULL;
  }

  {
    emq_status st = emq_ipc_reserve(seg, total, &offset);
    if (st != EMQ_OK) {
      return st;
    }
  }

  f = emq_ipc_frame_at(seg, offset);
  f->length = (int32_t)len;
  f->status = EMQ_IPC_FRAME_EMPTY;
  if (len > 0) {
    memcpy((uint8_t *)f + EMQ_IPC_FRAME_HDR, data, len);
  }
  f->status = EMQ_IPC_FRAME_COMMITTED;
  return EMQ_OK;
}

emq_status emq_ipc_claim(emq_ipc_segment *seg, emq_ipc_loan *out,
                         uint32_t timeout_ms) {
  uint64_t deadline = 0;

  if (!seg || !seg->hdr || !out) {
    return EMQ_ERR_INVALID;
  }

  if (timeout_ms > 0) {
#if defined(_WIN32)
    deadline = GetTickCount64() + (uint64_t)timeout_ms;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    deadline = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull +
               (uint64_t)timeout_ms;
#endif
  }

  for (;;) {
    uint64_t head = seg->hdr->head;
    uint64_t tail = seg->hdr->tail;
    emq_ipc_frame *f;
    int32_t len;
    uint32_t total;

    if (head >= tail) {
      if (timeout_ms == 0) {
        return EMQ_ERR_EMPTY;
      }
      {
#if defined(_WIN32)
        if (GetTickCount64() >= deadline) {
          return EMQ_ERR_TIMEOUT;
        }
        Sleep(1);
#else
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t now =
            (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
        if (now >= deadline) {
          return EMQ_ERR_TIMEOUT;
        }
        {
          struct timespec nap = {0, 1000000L};
          (void)nanosleep(&nap, NULL);
        }
#endif
      }
      continue;
    }

    f = emq_ipc_frame_at(seg, head);
    if (f->status != EMQ_IPC_FRAME_COMMITTED) {
      return EMQ_ERR_BUSY;
    }

    len = f->length;
    if (len < 0) {
      seg->hdr->head = head + (uint32_t)(-len);
      continue;
    }

    total = emq_ipc_frame_total((uint32_t)len);
    (void)total;
    f->status = EMQ_IPC_FRAME_CLAIMED;
    out->data = (uint8_t *)f + EMQ_IPC_FRAME_HDR;
    out->size = (size_t)len;
    out->frame_head = head;
    return EMQ_OK;
  }
}

emq_status emq_ipc_release(emq_ipc_segment *seg, emq_ipc_loan *loan) {
  emq_ipc_frame *f;
  uint32_t total;

  if (!seg || !seg->hdr || !loan) {
    return EMQ_ERR_INVALID;
  }
  if (loan->frame_head == 0 && loan->size == 0 && loan->data == NULL) {
    return EMQ_ERR_INVALID;
  }

  f = emq_ipc_frame_at(seg, loan->frame_head);
  if (f->status != EMQ_IPC_FRAME_CLAIMED) {
    return EMQ_ERR_INVALID;
  }

  total = emq_ipc_frame_total((uint32_t)f->length);
  f->status = EMQ_IPC_FRAME_EMPTY;
  seg->hdr->head = loan->frame_head + total;
  memset(loan, 0, sizeof(*loan));
  return EMQ_OK;
}
