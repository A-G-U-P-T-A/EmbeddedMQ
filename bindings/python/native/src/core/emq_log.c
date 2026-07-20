#include "core/emq_log.h"
#include "core/emq_crc.h"
#include "core/emq_fault.h"
#include "core/emq_mem.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMQ_DEFAULT_CAP 8u
#define EMQ_SEGMENT_BYTES (4u * 1024u * 1024u)
#define EMQ_DEFAULT_BLOB_THRESHOLD (8u * 1024u)
#define EMQ_META_MAGIC 0x454D514Du /* 'EMQM' */
#define EMQ_META_VERSION 1u
#define EMQ_BLOB_MAGIC 0x454D5142u /* 'EMQB' */

#pragma pack(push, 1)
typedef struct emq_log_meta_disk {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint64_t sequence;
  uint32_t storage_generation;
  uint32_t blob_threshold;
  uint64_t disk_base_offset;
  uint64_t trim_offset;
  uint64_t next_offset;
  uint32_t crc32;
  uint32_t reserved;
} emq_log_meta_disk;

typedef struct emq_blob_header {
  uint32_t magic;
  uint32_t payload_len;
  uint32_t crc32;
} emq_blob_header;
#pragma pack(pop)

typedef struct emq_log_segment {
  emq_file *file;
  emq_mmap map;
  uint32_t id;
  uint64_t used;
  uint64_t allocated;
} emq_log_segment;

typedef struct emq_compact_loc {
  uint32_t flags;
  uint32_t segment_id;
  uint64_t record_pos;
  uint64_t blob_offset;
} emq_compact_loc;

typedef struct emq_buffer_node {
  struct emq_buffer_node *next;
} emq_buffer_node;

typedef struct emq_buffer_pool {
  uint32_t block_size;
  emq_buffer_node *free_list;
} emq_buffer_pool;

struct emq_log {
  emq_storage_mode mode;
  emq_fsync_policy fsync;
  char path[512];
  emq_log_entry *entries;
  uint32_t capacity;
  uint32_t hot_limit;
  uint64_t hot_byte_limit;
  uint64_t start;
  uint64_t count;
  uint64_t next_offset;
  uint64_t disk_base_offset;
  uint64_t trim_offset;
  uint64_t meta_sequence;
  uint64_t last_sync_ns;
  uint32_t storage_generation;
  uint32_t blob_threshold;
  emq_mutex *mu;
  emq_log_segment *segments;
  uint32_t segment_count;
  uint32_t segment_capacity;
  emq_file *blob_file;
  uint64_t blob_used;
  emq_file *meta_file;
  uint8_t *scratch;
  size_t scratch_capacity;
  emq_buffer_pool pools[3];
};

static void *emq_log_payload_alloc(emq_log *log, uint32_t len,
                                   uint32_t *capacity) {
  uint32_t i;
  if (len == 0) {
    *capacity = 0;
    return NULL;
  }
  for (i = 0; i < 3; ++i) {
    emq_buffer_pool *pool = &log->pools[i];
    if (len <= pool->block_size) {
      emq_buffer_node *node = pool->free_list;
      if (node) {
        pool->free_list = node->next;
      } else {
        node = (emq_buffer_node *)malloc(pool->block_size);
      }
      if (!node) return NULL;
      *capacity = pool->block_size;
      return node;
    }
  }
  *capacity = len;
  return malloc(len);
}

static void emq_log_payload_free(emq_log *log, uint8_t *payload,
                                 uint32_t capacity) {
  uint32_t i;
  if (!payload) return;
  for (i = 0; i < 3; ++i) {
    emq_buffer_pool *pool = &log->pools[i];
    if (capacity == pool->block_size) {
      emq_buffer_node *node = (emq_buffer_node *)payload;
      node->next = pool->free_list;
      pool->free_list = node;
      return;
    }
  }
  free(payload);
}

static void emq_log_payload_pools_destroy(emq_log *log) {
  uint32_t i;
  for (i = 0; i < 3; ++i) {
    emq_buffer_node *node = log->pools[i].free_list;
    while (node) {
      emq_buffer_node *next = node->next;
      free(node);
      node = next;
    }
    log->pools[i].free_list = NULL;
  }
}

static int emq_log_is_persistent(const emq_log *log) {
  return log->mode == EMQ_STORAGE_DURABLE ||
         log->mode == EMQ_STORAGE_MMAP ||
         log->mode == EMQ_STORAGE_HYBRID ||
         log->mode == EMQ_STORAGE_STREAM;
}

static void emq_log_segment_path(const emq_log *log, uint32_t generation,
                                 uint32_t id, char *dst, size_t dst_size) {
  if (generation == 0) {
    if (id == 0) {
      snprintf(dst, dst_size, "%s/log.seg", log->path);
    } else {
      snprintf(dst, dst_size, "%s/log.%08lu.seg", log->path,
               (unsigned long)id);
    }
  } else if (id == 0) {
    snprintf(dst, dst_size, "%s/log.g%08lu.seg", log->path,
             (unsigned long)generation);
  } else {
    snprintf(dst, dst_size, "%s/log.g%08lu.%08lu.seg", log->path,
             (unsigned long)generation, (unsigned long)id);
  }
}

static void emq_log_blob_path(const emq_log *log, uint32_t generation,
                              char *dst, size_t dst_size) {
  if (generation == 0) {
    snprintf(dst, dst_size, "%s/blob.dat", log->path);
  } else {
    snprintf(dst, dst_size, "%s/blob.g%08lu.dat", log->path,
             (unsigned long)generation);
  }
}

static int emq_file_read_full(emq_file *file, void *buf, size_t len,
                              uint64_t off) {
  uint8_t *p = (uint8_t *)buf;
  size_t done = 0;
  while (done < len) {
    size_t chunk = len - done;
    int n;
    if (chunk > 0x7fffffffu) chunk = 0x7fffffffu;
    n = emq_file_pread(file, p + done, chunk, (int64_t)(off + done));
    if (n <= 0) return -6;
    done += (size_t)n;
  }
  return 0;
}

static int emq_file_write_full(emq_file *file, const void *buf, size_t len,
                               uint64_t off) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t done = 0;
  while (done < len) {
    size_t chunk = len - done;
    int n;
    if (chunk > 0x7fffffffu) chunk = 0x7fffffffu;
    n = emq_file_pwrite(file, p + done, chunk, (int64_t)(off + done));
    if (n <= 0) return -6;
    done += (size_t)n;
  }
  return 0;
}

static int emq_log_ensure_entry_capacity(emq_log *log) {
  uint32_t ncap;
  emq_log_entry *n;
  uint64_t i;
  if (log->count < log->capacity) return 0;
  if (log->mode == EMQ_STORAGE_RING) return -4;
  if (log->capacity > UINT32_MAX / 2u) return -2;
  ncap = log->capacity ? log->capacity * 2u : EMQ_DEFAULT_CAP;
  n = (emq_log_entry *)calloc(ncap, sizeof(*n));
  if (!n) return -2;
  for (i = 0; i < log->count; ++i) {
    n[i] = log->entries[(log->start + i) % log->capacity];
  }
  free(log->entries);
  log->entries = n;
  log->capacity = ncap;
  log->start = 0;
  return 0;
}

static int emq_log_ensure_segment_capacity(emq_log *log) {
  uint32_t ncap;
  emq_log_segment *n;
  if (log->segment_count < log->segment_capacity) return 0;
  if (log->segment_capacity > UINT32_MAX / 2u) return -2;
  ncap = log->segment_capacity ? log->segment_capacity * 2u : 4u;
  n = (emq_log_segment *)realloc(log->segments, sizeof(*n) * ncap);
  if (!n) return -2;
  memset(n + log->segment_capacity, 0,
         sizeof(*n) * (ncap - log->segment_capacity));
  log->segments = n;
  log->segment_capacity = ncap;
  return 0;
}

static void emq_log_close_segments(emq_log *log) {
  uint32_t i;
  for (i = 0; i < log->segment_count; ++i) {
    emq_mmap_unmap(&log->segments[i].map);
    if (log->segments[i].file) {
      emq_file_close(log->segments[i].file);
      log->segments[i].file = NULL;
    }
  }
  free(log->segments);
  log->segments = NULL;
  log->segment_count = 0;
  log->segment_capacity = 0;
}

static int emq_log_open_segment(emq_log *log, uint32_t id, int create,
                                int truncate_file, uint64_t min_size) {
  char file_path[640];
  emq_log_segment *seg;
  int64_t size;
  uint64_t map_size;
  int rc;

  rc = emq_log_ensure_segment_capacity(log);
  if (rc != 0) return rc;
  emq_log_segment_path(log, log->storage_generation, id, file_path,
                       sizeof(file_path));
  seg = &log->segments[log->segment_count];
  memset(seg, 0, sizeof(*seg));
  if (emq_file_open(&seg->file, file_path, create, 1) != 0) return -6;
  if (truncate_file && emq_file_resize(seg->file, 0) != 0) {
    emq_file_close(seg->file);
    seg->file = NULL;
    return -6;
  }
  size = emq_file_size(seg->file);
  if (size < 0) {
    emq_file_close(seg->file);
    seg->file = NULL;
    return -6;
  }
  seg->id = id;
  seg->allocated = (uint64_t)size;
  if (log->mode == EMQ_STORAGE_MMAP) {
    map_size = EMQ_SEGMENT_BYTES;
    if (min_size > map_size) map_size = min_size;
    if ((uint64_t)size > map_size) map_size = (uint64_t)size;
    if ((uint64_t)size < map_size &&
        emq_file_resize(seg->file, (int64_t)map_size) != 0) {
      emq_file_close(seg->file);
      seg->file = NULL;
      return -6;
    }
    if (emq_mmap_create(&seg->map, seg->file, (size_t)map_size, 1) != 0) {
      emq_file_close(seg->file);
      seg->file = NULL;
      return -6;
    }
    seg->allocated = map_size;
  }
  log->segment_count++;
  return 0;
}

static int emq_log_segment_read(const emq_log *log,
                                const emq_log_segment *seg, void *dst,
                                size_t len, uint64_t pos) {
  (void)log;
  if (pos > seg->allocated || len > seg->allocated - pos) return -6;
  if (seg->map.addr) {
    memcpy(dst, (const uint8_t *)seg->map.addr + (size_t)pos, len);
    return 0;
  }
  return emq_file_read_full(seg->file, dst, len, pos);
}

static int emq_log_segment_write(emq_log_segment *seg, const void *src,
                                 size_t len, uint64_t pos) {
  if (seg->map.addr) {
    if (pos > seg->allocated || len > seg->allocated - pos) return -4;
    memcpy((uint8_t *)seg->map.addr + (size_t)pos, src, len);
    return 0;
  }
  return emq_file_write_full(seg->file, src, len, pos);
}

static uint32_t emq_log_meta_crc(const emq_log_meta_disk *meta) {
  emq_log_meta_disk tmp = *meta;
  tmp.crc32 = 0;
  return emq_crc32(&tmp, sizeof(tmp));
}

static int emq_log_meta_valid(const emq_log_meta_disk *meta) {
  if (meta->magic != EMQ_META_MAGIC ||
      meta->version != EMQ_META_VERSION ||
      meta->size != sizeof(*meta)) {
    return 0;
  }
  return emq_log_meta_crc(meta) == meta->crc32;
}

/* Returns 0 on success, -11 if a recognizable future meta version is present. */
static int emq_log_load_meta(emq_log *log) {
  emq_log_meta_disk slots[2];
  int valid0 = 0;
  int valid1 = 0;
  int64_t size;
  const emq_log_meta_disk *best = NULL;

  memset(slots, 0, sizeof(slots));
  size = emq_file_size(log->meta_file);
  if (size >= (int64_t)sizeof(slots[0]) &&
      emq_file_read_full(log->meta_file, &slots[0], sizeof(slots[0]), 0) == 0) {
    if (slots[0].magic == EMQ_META_MAGIC &&
        slots[0].version > EMQ_META_VERSION) {
      return -11;
    }
    valid0 = emq_log_meta_valid(&slots[0]);
  }
  if (size >= (int64_t)sizeof(slots) &&
      emq_file_read_full(log->meta_file, &slots[1], sizeof(slots[1]),
                         sizeof(slots[0])) == 0) {
    if (slots[1].magic == EMQ_META_MAGIC &&
        slots[1].version > EMQ_META_VERSION) {
      return -11;
    }
    valid1 = emq_log_meta_valid(&slots[1]);
  }
  if (valid0 && valid1) {
    best = slots[0].sequence >= slots[1].sequence ? &slots[0] : &slots[1];
  } else if (valid0) {
    best = &slots[0];
  } else if (valid1) {
    best = &slots[1];
  }
  if (best) {
    log->meta_sequence = best->sequence;
    log->storage_generation = best->storage_generation;
    log->disk_base_offset = best->disk_base_offset;
    log->trim_offset = best->trim_offset;
    log->next_offset = best->next_offset;
    log->blob_threshold = best->blob_threshold ?
                          best->blob_threshold : EMQ_DEFAULT_BLOB_THRESHOLD;
  }
  return 0;
}

static int emq_log_write_meta(emq_log *log, uint32_t storage_generation,
                              uint64_t disk_base_offset,
                              uint64_t trim_offset, uint64_t next_offset,
                              uint64_t *out_sequence) {
  emq_log_meta_disk meta;
  uint64_t sequence = log->meta_sequence + 1u;
  uint64_t slot = sequence & 1u;
  EMQ_CRASHPOINT("log_meta_write");
  memset(&meta, 0, sizeof(meta));
  meta.magic = EMQ_META_MAGIC;
  meta.version = EMQ_META_VERSION;
  meta.size = (uint16_t)sizeof(meta);
  meta.sequence = sequence;
  meta.storage_generation = storage_generation;
  meta.blob_threshold = log->blob_threshold;
  meta.disk_base_offset = disk_base_offset;
  meta.trim_offset = trim_offset;
  meta.next_offset = next_offset;
  meta.crc32 = emq_log_meta_crc(&meta);
  if (emq_file_write_full(log->meta_file, &meta, sizeof(meta),
                          slot * sizeof(meta)) != 0 ||
      emq_file_sync(log->meta_file) != 0) {
    return -6;
  }
  if (out_sequence) *out_sequence = sequence;
  return 0;
}

static int emq_log_open_blob(emq_log *log, int create, int truncate_file) {
  char blob_path[640];
  int64_t size;
  emq_log_blob_path(log, log->storage_generation, blob_path,
                    sizeof(blob_path));
  if (emq_file_open(&log->blob_file, blob_path, create, 1) != 0) return -6;
  if (truncate_file && emq_file_resize(log->blob_file, 0) != 0) {
    emq_file_close(log->blob_file);
    log->blob_file = NULL;
    return -6;
  }
  size = emq_file_size(log->blob_file);
  if (size < 0) return -6;
  log->blob_used = (uint64_t)size;
  return 0;
}

static int emq_log_blob_read(emq_log *log, uint64_t offset,
                             uint32_t expected_len, uint8_t *out) {
  emq_blob_header hdr;
  uint32_t crc = 0;
  uint8_t buffer[4096];
  uint64_t pos;
  uint32_t left;

  if (!log->blob_file ||
      emq_file_read_full(log->blob_file, &hdr, sizeof(hdr), offset) != 0 ||
      hdr.magic != EMQ_BLOB_MAGIC || hdr.payload_len != expected_len) {
    return -6;
  }
  pos = offset + sizeof(hdr);
  left = hdr.payload_len;
  while (left != 0) {
    size_t n = left > sizeof(buffer) ? sizeof(buffer) : (size_t)left;
    uint8_t *dst = out ? out + (hdr.payload_len - left) : buffer;
    if (emq_file_read_full(log->blob_file, dst, n, pos) != 0) return -6;
    crc = emq_crc32_update(crc, dst, n);
    pos += n;
    left -= (uint32_t)n;
  }
  return crc == hdr.crc32 ? 0 : -6;
}

static int emq_log_recover_blob(emq_log *log) {
  uint64_t pos = 0;
  int64_t file_size = emq_file_size(log->blob_file);
  if (file_size < 0) return -6;
  while (pos < (uint64_t)file_size) {
    emq_blob_header hdr;
    uint64_t end;
    if ((uint64_t)file_size - pos < sizeof(hdr) ||
        emq_file_read_full(log->blob_file, &hdr, sizeof(hdr), pos) != 0 ||
        hdr.magic != EMQ_BLOB_MAGIC) {
      break;
    }
    end = pos + sizeof(hdr) + (uint64_t)hdr.payload_len;
    if (end > (uint64_t)file_size ||
        emq_log_blob_read(log, pos, hdr.payload_len, NULL) != 0) {
      break;
    }
    pos = end;
  }
  if (pos != (uint64_t)file_size &&
      emq_file_resize(log->blob_file, (int64_t)pos) != 0) {
    return -6;
  }
  log->blob_used = pos;
  return 0;
}

static int emq_log_blob_append(emq_log *log, const void *data, uint32_t len,
                               uint64_t *out_offset) {
  emq_blob_header hdr;
  uint64_t offset = log->blob_used;
  EMQ_CRASHPOINT("log_blob_write");
  hdr.magic = EMQ_BLOB_MAGIC;
  hdr.payload_len = len;
  hdr.crc32 = emq_crc32(data, len);
  if (emq_file_write_full(log->blob_file, &hdr, sizeof(hdr), offset) != 0 ||
      (len != 0 &&
       emq_file_write_full(log->blob_file, data, len,
                           offset + sizeof(hdr)) != 0)) {
    return -6;
  }
  log->blob_used += sizeof(hdr) + (uint64_t)len;
  *out_offset = offset;
  return 0;
}

static int emq_log_add_recovered(emq_log *log, uint64_t offset,
                                 const emq_record *rec, uint32_t segment_id,
                                 uint64_t record_pos) {
  emq_log_entry *e;
  int rc;
  if (offset < log->trim_offset) return 0;
  rc = emq_log_ensure_entry_capacity(log);
  if (rc != 0) return rc;
  e = &log->entries[(log->start + log->count) % log->capacity];
  memset(e, 0, sizeof(*e));
  e->offset = offset;
  e->queue_id = rec->hdr.queue_id;
  e->msg_id = rec->hdr.msg_id;
  e->priority = rec->hdr.priority;
  e->timestamp_ns = rec->hdr.timestamp_ns;
  e->deliver_at_ns = rec->hdr.deliver_at_ns;
  e->ttl_ns = rec->hdr.ttl_ns;
  e->flags = rec->hdr.flags;
  e->payload_len = rec->hdr.payload_len;
  e->segment_id = segment_id;
  e->record_pos = record_pos;
  e->blob_offset = rec->hdr.blob_offset;
  if (log->mode == EMQ_STORAGE_MMAP &&
      (rec->hdr.flags & EMQ_RECORD_FLAG_INLINE) != 0) {
    e->payload = (uint8_t *)rec->payload;
  }
  log->count++;
  return 0;
}

static int emq_log_recover_segment(emq_log *log, emq_log_segment *seg,
                                   uint64_t *logical_offset,
                                   int *out_corrupt) {
  uint64_t pos = 0;
  uint64_t limit = seg->allocated;
  *out_corrupt = 0;
  while (pos < limit) {
    emq_record_header hdr;
    emq_record rec;
    uint8_t *heap = NULL;
    const uint8_t *src;
    size_t encoded_size;
    int all_zero = 1;
    size_t i;

    if (limit - pos < sizeof(hdr)) {
      if (limit != pos) *out_corrupt = 1;
      break;
    }
    if (emq_log_segment_read(log, seg, &hdr, sizeof(hdr), pos) != 0) {
      return -6;
    }
    for (i = 0; i < sizeof(hdr); ++i) {
      if (((const uint8_t *)&hdr)[i] != 0) {
        all_zero = 0;
        break;
      }
    }
    if (all_zero) break;
    if (hdr.magic != EMQ_RECORD_MAGIC) {
      *out_corrupt = 1;
      break;
    }
    /* A known-but-different version requires migration. Never classify it as
     * a torn tail and truncate user data. */
    if (hdr.version != EMQ_RECORD_VERSION) return -11;
    if ((((hdr.flags & EMQ_RECORD_FLAG_INLINE) != 0) ==
         ((hdr.flags & EMQ_RECORD_FLAG_BLOB) != 0))) {
      *out_corrupt = 1;
      break;
    }
    encoded_size = emq_record_encoded_size(
        hdr.payload_len, (hdr.flags & EMQ_RECORD_FLAG_INLINE) != 0);
    if (encoded_size < sizeof(hdr) ||
        (uint64_t)encoded_size > limit - pos) {
      *out_corrupt = 1;
      break;
    }
    if (seg->map.addr) {
      src = (const uint8_t *)seg->map.addr + (size_t)pos;
    } else {
      heap = (uint8_t *)malloc(encoded_size);
      if (!heap) return -2;
      if (emq_log_segment_read(log, seg, heap, encoded_size, pos) != 0) {
        free(heap);
        return -6;
      }
      src = heap;
    }
    if (emq_record_decode(src, encoded_size, &rec) != 0 ||
        ((rec.hdr.flags & EMQ_RECORD_FLAG_BLOB) != 0 &&
         emq_log_blob_read(log, rec.hdr.blob_offset,
                           rec.hdr.payload_len, NULL) != 0)) {
      free(heap);
      *out_corrupt = 1;
      break;
    }
    if (emq_log_add_recovered(log, *logical_offset, &rec, seg->id, pos) != 0) {
      free(heap);
      return -2;
    }
    (*logical_offset)++;
    pos += encoded_size;
    free(heap);
  }
  seg->used = pos;
  if (*out_corrupt && !seg->map.addr &&
      emq_file_resize(seg->file, (int64_t)pos) != 0) {
    return -6;
  }
  if (*out_corrupt && !seg->map.addr) seg->allocated = pos;
  return 0;
}

static void emq_log_remove_generation_files(const emq_log *log,
                                            uint32_t generation,
                                            uint32_t segment_count) {
  char file_path[640];
  uint32_t i;
  for (i = 0; i < segment_count; ++i) {
    emq_log_segment_path(log, generation, i, file_path, sizeof(file_path));
    remove(file_path);
  }
  /* Also clear a longer orphan generation left by an interrupted rewrite. */
  for (; i != UINT32_MAX; ++i) {
    emq_log_segment_path(log, generation, i, file_path, sizeof(file_path));
    if (remove(file_path) != 0) break;
  }
  emq_log_blob_path(log, generation, file_path, sizeof(file_path));
  remove(file_path);
}

static void emq_log_remove_later_segments(const emq_log *log,
                                          uint32_t first_id) {
  char file_path[640];
  uint32_t i;
  for (i = first_id; i != UINT32_MAX; ++i) {
    emq_log_segment_path(log, log->storage_generation, i, file_path,
                         sizeof(file_path));
    if (remove(file_path) != 0) break;
  }
}

static int emq_log_recover(emq_log *log) {
  uint64_t logical_offset = log->disk_base_offset;
  uint32_t id = 0;
  int found = 0;
  int corrupt = 0;
  int rc;

  for (;;) {
    rc = emq_log_open_segment(log, id, 0, 0, 0);
    if (rc != 0) break;
    found = 1;
    rc = emq_log_recover_segment(log,
                                 &log->segments[log->segment_count - 1],
                                 &logical_offset, &corrupt);
    if (rc != 0) return rc;
    if (corrupt) {
      emq_log_remove_later_segments(log, id + 1u);
      break;
    }
    id++;
  }
  if (!found) {
    if (log->storage_generation != 0 || log->meta_sequence != 0) return -6;
    rc = emq_log_open_segment(log, 0, 1, 0, EMQ_SEGMENT_BYTES);
    if (rc != 0) return rc;
  }
  /*
   * The valid record prefix is authoritative.  In particular, do not retain
   * a snapshotted next_offset past a CRC-invalid/torn final record.
   */
  log->next_offset = logical_offset;
  if (log->trim_offset > log->next_offset) {
    log->next_offset = log->trim_offset;
  }
  return 0;
}

static int emq_log_rotate_if_needed(emq_log *log, size_t record_size,
                                    emq_log_segment **out_seg) {
  emq_log_segment *seg;
  uint64_t desired = EMQ_SEGMENT_BYTES;
  int rc;
  if (record_size > desired) desired = (uint64_t)record_size;
  if (log->segment_count == 0) {
    rc = emq_log_open_segment(log, 0, 1, 0, desired);
    if (rc != 0) return rc;
  }
  seg = &log->segments[log->segment_count - 1];
  if (seg->used != 0 &&
      ((uint64_t)record_size > EMQ_SEGMENT_BYTES ||
       seg->used > EMQ_SEGMENT_BYTES - (uint64_t)record_size)) {
    EMQ_CRASHPOINT("log_segment_rotate");
    rc = emq_log_open_segment(log, seg->id + 1u, 1, 1, desired);
    if (rc != 0) return rc;
    seg = &log->segments[log->segment_count - 1];
  } else if (seg->map.addr &&
             (uint64_t)record_size > seg->allocated - seg->used) {
    EMQ_CRASHPOINT("log_segment_rotate");
    rc = emq_log_open_segment(log, seg->id + 1u, 1, 1, desired);
    if (rc != 0) return rc;
    seg = &log->segments[log->segment_count - 1];
  }
  *out_seg = seg;
  return 0;
}

static int emq_log_persist_fields(emq_log *log, uint32_t queue_id,
                                  uint64_t msg_id, uint32_t priority,
                                  uint64_t timestamp_ns,
                                  uint64_t deliver_at_ns,
                                  uint64_t ttl_ns,
                                  const void *data, uint32_t len,
                                  emq_compact_loc *loc) {
  uint8_t stack[4096];
  uint8_t *heap = NULL;
  uint8_t *encoded = stack;
  emq_record rec;
  emq_log_segment *seg;
  size_t record_size;
  size_t encoded_size;
  int inline_payload = len <= log->blob_threshold;
  int rc;

  memset(&rec, 0, sizeof(rec));
  rec.hdr.flags = (uint16_t)(inline_payload ? EMQ_RECORD_FLAG_INLINE :
                                             EMQ_RECORD_FLAG_BLOB);
  rec.hdr.queue_id = queue_id;
  rec.hdr.msg_id = msg_id;
  rec.hdr.timestamp_ns = timestamp_ns;
  rec.hdr.deliver_at_ns = deliver_at_ns;
  rec.hdr.ttl_ns = ttl_ns;
  rec.hdr.priority = priority;
  rec.hdr.payload_len = len;
  rec.payload = (const uint8_t *)data;
  if (!inline_payload) {
    rc = emq_log_blob_append(log, data, len, &rec.hdr.blob_offset);
    if (rc != 0) return rc;
    if (log->fsync == EMQ_FSYNC_EVERY_WRITE &&
        emq_file_sync(log->blob_file) != 0) {
      return -6;
    }
  }
  record_size = emq_record_encoded_size(len, inline_payload);
  if (record_size > sizeof(stack)) {
    heap = (uint8_t *)malloc(record_size);
    if (!heap) return -2;
    encoded = heap;
  }
  encoded_size = emq_record_encode(encoded, record_size, &rec);
  if (encoded_size != record_size) {
    free(heap);
    return -1;
  }
  rc = emq_log_rotate_if_needed(log, record_size, &seg);
  if (rc == 0) {
    loc->flags = rec.hdr.flags;
    loc->segment_id = seg->id;
    loc->record_pos = seg->used;
    loc->blob_offset = rec.hdr.blob_offset;
    rc = emq_log_segment_write(seg, encoded, encoded_size, seg->used);
    if (rc == 0) {
      int should_sync = log->fsync == EMQ_FSYNC_EVERY_WRITE;
      if (log->fsync == EMQ_FSYNC_INTERVAL) {
        uint64_t now = emq_now_ns();
        if (log->last_sync_ns == 0 ||
            now - log->last_sync_ns >= 100000000ULL) {
          should_sync = 1;
          log->last_sync_ns = now;
        }
      }
      seg->used += encoded_size;
      if (seg->used > seg->allocated) seg->allocated = seg->used;
      if (should_sync) {
        if (seg->map.addr && emq_mmap_sync(&seg->map) != 0) rc = -6;
        if (rc == 0 && emq_file_sync(seg->file) != 0) rc = -6;
        if (rc == 0 && log->blob_file &&
            emq_file_sync(log->blob_file) != 0) rc = -6;
      }
    }
  }
  free(heap);
  return rc;
}

static emq_log_entry *emq_log_find(emq_log *log, uint64_t offset) {
  uint64_t i;
  for (i = 0; i < log->count; ++i) {
    emq_log_entry *e = &log->entries[(log->start + i) % log->capacity];
    if (e->offset == offset) return e;
  }
  return NULL;
}

static int emq_log_read_payload(emq_log *log, const emq_log_entry *e,
                                uint8_t *dst) {
  emq_log_segment *seg;
  if (e->payload_len == 0) return 0;
  if (e->payload) {
    memcpy(dst, e->payload, e->payload_len);
    return 0;
  }
  if (!emq_log_is_persistent(log) || e->segment_id >= log->segment_count) {
    return -3;
  }
  if ((e->flags & EMQ_RECORD_FLAG_BLOB) != 0) {
    return emq_log_blob_read(log, e->blob_offset, e->payload_len, dst);
  }
  seg = &log->segments[e->segment_id];
  return emq_log_segment_read(log, seg, dst, e->payload_len,
                              e->record_pos + sizeof(emq_record_header));
}

static int emq_log_ensure_scratch(emq_log *log, size_t size) {
  uint8_t *n;
  if (size <= log->scratch_capacity) return 0;
  n = (uint8_t *)realloc(log->scratch, size);
  if (!n) return -2;
  log->scratch = n;
  log->scratch_capacity = size;
  return 0;
}

static void emq_log_bound_hybrid_hot_set(emq_log *log) {
  uint64_t i;
  uint64_t hot = 0;
  uint64_t hot_bytes = 0;
  if (log->mode != EMQ_STORAGE_HYBRID) return;
  for (i = 0; i < log->count; ++i) {
    emq_log_entry *e = &log->entries[(log->start + i) % log->capacity];
    if (e->payload_owned) {
      hot++;
      hot_bytes += e->payload_len;
    }
  }
  for (i = 0;
       (hot > log->hot_limit || hot_bytes > log->hot_byte_limit) &&
       i < log->count;
       ++i) {
    emq_log_entry *e = &log->entries[(log->start + i) % log->capacity];
    if (e->payload_owned) {
      hot_bytes -= e->payload_len;
      emq_log_payload_free(log, e->payload, e->capacity);
      e->payload = NULL;
      e->payload_owned = 0;
      e->capacity = 0;
      hot--;
    }
  }
}

static int emq_log_sync_unlocked(emq_log *log) {
  uint32_t i;
  int rc = 0;
  for (i = 0; i < log->segment_count; ++i) {
    if (log->segments[i].map.addr &&
        emq_mmap_sync(&log->segments[i].map) != 0) {
      rc = -6;
    }
    if (emq_file_sync(log->segments[i].file) != 0) rc = -6;
  }
  if (log->blob_file && emq_file_sync(log->blob_file) != 0) rc = -6;
  return rc;
}

int emq_log_open(emq_log **out, emq_storage_mode mode, const char *path,
                 uint32_t capacity, emq_fsync_policy fsync) {
  emq_log *log;
  char meta_path[640];
  int rc;
  if (!out) return -1;
  *out = NULL;
  log = (emq_log *)calloc(1, sizeof(*log));
  if (!log) return -2;
  log->mode = mode;
  log->fsync = fsync;
  log->capacity = capacity ? capacity : EMQ_DEFAULT_CAP;
  log->hot_limit = capacity ? capacity : EMQ_DEFAULT_CAP;
  log->blob_threshold = EMQ_DEFAULT_BLOB_THRESHOLD;
  log->pools[0].block_size = 256u;
  log->pools[1].block_size = 1024u;
  log->pools[2].block_size = 8192u;
  log->hot_byte_limit = (uint64_t)log->hot_limit * log->blob_threshold;
  log->entries = (emq_log_entry *)calloc(log->capacity, sizeof(*log->entries));
  log->mu = emq_mutex_create();
  if (!log->entries || !log->mu) {
    emq_log_close(log);
    return -2;
  }
  if (path) {
    if (strlen(path) >= sizeof(log->path)) {
      emq_log_close(log);
      return -1;
    }
    memcpy(log->path, path, strlen(path) + 1u);
  }
  if (emq_log_is_persistent(log)) {
    if (!path || !*path) {
      emq_log_close(log);
      return -1;
    }
    if (emq_mkdir_p(path) != 0) {
      emq_log_close(log);
      return -6;
    }
    snprintf(meta_path, sizeof(meta_path), "%s/log.meta", path);
    if (emq_file_open(&log->meta_file, meta_path, 1, 1) != 0) {
      emq_log_close(log);
      return -6;
    }
    rc = emq_log_load_meta(log);
    if (rc != 0) {
      emq_log_close(log);
      return rc;
    }
    log->hot_byte_limit = (uint64_t)log->hot_limit * log->blob_threshold;
    rc = emq_log_open_blob(log, log->storage_generation == 0, 0);
    if (rc == 0) rc = emq_log_recover_blob(log);
    if (rc == 0) rc = emq_log_recover(log);
    if (rc != 0) {
      emq_log_close(log);
      return rc;
    }
  }
  *out = log;
  return 0;
}

void emq_log_close(emq_log *log) {
  uint64_t i;
  if (!log) return;
  if (log->entries) {
    for (i = 0; i < log->capacity; ++i) {
      if (log->entries[i].payload_owned) {
        free(log->entries[i].payload);
      }
    }
    free(log->entries);
  }
  emq_log_payload_pools_destroy(log);
  emq_log_close_segments(log);
  if (log->blob_file) emq_file_close(log->blob_file);
  if (log->meta_file) emq_file_close(log->meta_file);
  free(log->scratch);
  emq_mutex_destroy(log->mu);
  free(log);
}

int emq_log_append(emq_log *log, uint32_t queue_id, uint64_t msg_id,
                   uint32_t priority, uint64_t deliver_at_ns,
                   const void *data, uint32_t len, uint64_t *out_offset) {
  return emq_log_append_ex(log, queue_id, msg_id, priority, deliver_at_ns, 0,
                           data, len, out_offset);
}

int emq_log_append_ex(emq_log *log, uint32_t queue_id, uint64_t msg_id,
                      uint32_t priority, uint64_t deliver_at_ns,
                      uint64_t ttl_ns, const void *data, uint32_t len,
                      uint64_t *out_offset) {
  emq_log_entry *e;
  emq_compact_loc loc;
  uint64_t offset;
  uint64_t timestamp_ns;
  uint8_t *copy = NULL;
  uint32_t allocation_capacity = 0;
  int rc;

  if (!log || (len != 0 && !data)) return -1;
  EMQ_CRASHPOINT("log_append_pre");
  emq_mutex_lock(log->mu);
  if (log->mode == EMQ_STORAGE_RING && log->count >= log->capacity) {
    e = &log->entries[log->start % log->capacity];
    if (e->payload_owned) {
      emq_log_payload_free(log, e->payload, e->capacity);
    }
    memset(e, 0, sizeof(*e));
    log->start++;
    log->count--;
  }
  rc = emq_log_ensure_entry_capacity(log);
  if (rc != 0) {
    emq_mutex_unlock(log->mu);
    return rc;
  }
  if (len != 0) {
    copy = (uint8_t *)emq_log_payload_alloc(log, len, &allocation_capacity);
    if (!copy) {
      emq_mutex_unlock(log->mu);
      return -2;
    }
    memcpy(copy, data, len);
  }
  offset = log->next_offset;
  timestamp_ns = emq_now_ns();
  memset(&loc, 0, sizeof(loc));
  if (emq_log_is_persistent(log)) {
    rc = emq_log_persist_fields(log, queue_id, msg_id, priority,
                                timestamp_ns, deliver_at_ns, ttl_ns,
                                data, len, &loc);
    if (rc != 0) {
      emq_log_payload_free(log, copy, allocation_capacity);
      emq_mutex_unlock(log->mu);
      return rc;
    }
  } else {
    loc.flags = EMQ_RECORD_FLAG_INLINE;
  }
  e = &log->entries[(log->start + log->count) % log->capacity];
  memset(e, 0, sizeof(*e));
  e->offset = offset;
  e->queue_id = queue_id;
  e->msg_id = msg_id;
  e->priority = priority;
  e->timestamp_ns = timestamp_ns;
  e->deliver_at_ns = deliver_at_ns;
  e->ttl_ns = ttl_ns;
  e->flags = loc.flags;
  e->payload = copy;
  e->payload_len = len;
  e->capacity = copy ? allocation_capacity : 0;
  e->payload_owned = copy != NULL;
  e->segment_id = loc.segment_id;
  e->record_pos = loc.record_pos;
  e->blob_offset = loc.blob_offset;
  if (log->mode == EMQ_STORAGE_MMAP &&
      (loc.flags & EMQ_RECORD_FLAG_INLINE) != 0) {
    emq_log_payload_free(log, copy, allocation_capacity);
    e->payload_owned = 0;
    e->capacity = 0;
    e->payload = (uint8_t *)log->segments[loc.segment_id].map.addr +
                 (size_t)loc.record_pos + sizeof(emq_record_header);
  }
  log->count++;
  log->next_offset++;
  emq_log_bound_hybrid_hot_set(log);
  if (out_offset) *out_offset = offset;
  emq_mutex_unlock(log->mu);
  return 0;
}

int emq_log_read(emq_log *log, uint64_t offset, emq_log_entry *out) {
  emq_log_entry *e;
  int rc = 0;
  if (!log || !out) return -1;
  emq_mutex_lock(log->mu);
  e = emq_log_find(log, offset);
  if (!e) {
    emq_mutex_unlock(log->mu);
    return -3;
  }
  *out = *e;
  if (e->payload_len != 0 && !e->payload) {
    rc = emq_log_ensure_scratch(log, e->payload_len);
    if (rc == 0) rc = emq_log_read_payload(log, e, log->scratch);
    if (rc == 0) {
      out->payload = log->scratch;
      out->capacity = e->payload_len;
      out->payload_owned = 0;
    }
  }
  emq_mutex_unlock(log->mu);
  return rc;
}

int emq_log_read_copy(emq_log *log, uint64_t offset, emq_log_entry *out) {
  emq_log_entry *e;
  uint8_t *copy = NULL;
  int rc = 0;
  if (!log || !out) return -1;
  emq_mutex_lock(log->mu);
  e = emq_log_find(log, offset);
  if (!e) {
    emq_mutex_unlock(log->mu);
    return -3;
  }
  if (e->payload_len != 0) {
    copy = (uint8_t *)malloc(e->payload_len);
    if (!copy) {
      emq_mutex_unlock(log->mu);
      return -2;
    }
    rc = emq_log_read_payload(log, e, copy);
    if (rc != 0) {
      free(copy);
      emq_mutex_unlock(log->mu);
      return rc;
    }
  }
  *out = *e;
  out->payload = copy;
  out->capacity = e->payload_len;
  out->payload_owned = copy != NULL;
  emq_mutex_unlock(log->mu);
  return 0;
}

uint64_t emq_log_next_offset(const emq_log *log) {
  return log ? log->next_offset : 0;
}

uint64_t emq_log_count(const emq_log *log) {
  return log ? log->count : 0;
}

int emq_log_sync(emq_log *log) {
  int rc;
  if (!log) return -1;
  EMQ_CRASHPOINT("log_sync_pre");
  emq_mutex_lock(log->mu);
  rc = emq_log_sync_unlocked(log);
  emq_mutex_unlock(log->mu);
  EMQ_CRASHPOINT("log_sync_post");
  return rc;
}

static uint64_t emq_log_retained_start(const emq_log *log) {
  if (log->count != 0) {
    return log->entries[log->start % log->capacity].offset;
  }
  return log->next_offset;
}

int emq_log_snapshot(emq_log *log) {
  uint64_t sequence;
  int rc;
  if (!log) return -1;
  if (!emq_log_is_persistent(log)) return 0;
  EMQ_CRASHPOINT("log_snapshot");
  emq_mutex_lock(log->mu);
  rc = emq_log_sync_unlocked(log);
  if (rc == 0) {
    log->trim_offset = emq_log_retained_start(log);
    rc = emq_log_write_meta(log, log->storage_generation,
                            log->disk_base_offset, log->trim_offset,
                            log->next_offset, &sequence);
    if (rc == 0) log->meta_sequence = sequence;
  }
  emq_mutex_unlock(log->mu);
  return rc;
}

int emq_log_trim_front(emq_log *log, uint64_t new_start_offset) {
  if (!log) return -1;
  EMQ_CRASHPOINT("log_trim_front");
  emq_mutex_lock(log->mu);
  while (log->count > 0) {
    emq_log_entry *e = &log->entries[log->start % log->capacity];
    if (e->offset >= new_start_offset) break;
    if (e->payload_owned) {
      emq_log_payload_free(log, e->payload, e->capacity);
    }
    memset(e, 0, sizeof(*e));
    log->start++;
    log->count--;
  }
  log->trim_offset = emq_log_retained_start(log);
  emq_mutex_unlock(log->mu);
  return 0;
}

int emq_log_set_blob_threshold(emq_log *log, uint32_t threshold) {
  uint64_t sequence;
  int rc = 0;
  if (!log) return -1;
  emq_mutex_lock(log->mu);
  log->blob_threshold = threshold ? threshold : EMQ_DEFAULT_BLOB_THRESHOLD;
  log->hot_byte_limit = (uint64_t)log->hot_limit * log->blob_threshold;
  emq_log_bound_hybrid_hot_set(log);
  if (emq_log_is_persistent(log)) {
    rc = emq_log_write_meta(log, log->storage_generation,
                            log->disk_base_offset, log->trim_offset,
                            log->next_offset, &sequence);
    if (rc == 0) log->meta_sequence = sequence;
  }
  emq_mutex_unlock(log->mu);
  return rc;
}

int emq_log_compact(emq_log *log) {
  emq_log fresh;
  emq_compact_loc *locations = NULL;
  uint32_t old_generation;
  uint32_t old_segment_count;
  uint32_t new_generation;
  uint32_t fresh_segment_count;
  uint64_t new_disk_base;
  uint64_t sequence = 0;
  uint64_t i;
  int rc = 0;

  if (!log) return -1;
  if (!emq_log_is_persistent(log)) return 0;
  EMQ_CRASHPOINT("log_compact");
  emq_mutex_lock(log->mu);
  memset(&fresh, 0, sizeof(fresh));
  fresh.mode = log->mode;
  fresh.fsync = EMQ_FSYNC_NONE;
  fresh.blob_threshold = log->blob_threshold;
  memcpy(fresh.path, log->path, sizeof(fresh.path));
  old_generation = log->storage_generation;
  old_segment_count = log->segment_count;
  new_generation = old_generation + 1u;
  if (new_generation == 0) {
    rc = -6;
    goto done;
  }
  fresh.storage_generation = new_generation;
  emq_log_remove_generation_files(log, new_generation,
                                  old_segment_count + 1u);
  rc = emq_log_open_blob(&fresh, 1, 1);
  if (rc != 0) goto done;
  rc = emq_log_open_segment(&fresh, 0, 1, 1, EMQ_SEGMENT_BYTES);
  if (rc != 0) goto done;
  if (log->count != 0) {
    if (log->count > SIZE_MAX / sizeof(*locations)) {
      rc = -2;
      goto done;
    }
    locations = (emq_compact_loc *)calloc((size_t)log->count,
                                          sizeof(*locations));
    if (!locations) {
      rc = -2;
      goto done;
    }
  }
  for (i = 0; i < log->count; ++i) {
    emq_log_entry *e = &log->entries[(log->start + i) % log->capacity];
    uint8_t *payload = NULL;
    if (e->payload_len != 0) {
      payload = (uint8_t *)malloc(e->payload_len);
      if (!payload) {
        rc = -2;
        goto done;
      }
      rc = emq_log_read_payload(log, e, payload);
      if (rc != 0) {
        free(payload);
        goto done;
      }
    }
    rc = emq_log_persist_fields(&fresh, e->queue_id, e->msg_id, e->priority,
                                e->timestamp_ns, e->deliver_at_ns, e->ttl_ns,
                                payload, e->payload_len, &locations[i]);
    free(payload);
    if (rc != 0) goto done;
  }
  rc = emq_log_sync_unlocked(&fresh);
  if (rc != 0) goto done;
  new_disk_base = emq_log_retained_start(log);
  log->trim_offset = new_disk_base;
  rc = emq_log_write_meta(log, new_generation, new_disk_base,
                          log->trim_offset, log->next_offset, &sequence);
  if (rc != 0) goto done;

  for (i = 0; i < log->count; ++i) {
    emq_log_entry *e = &log->entries[(log->start + i) % log->capacity];
    if (!e->payload_owned) e->payload = NULL;
  }
  emq_log_close_segments(log);
  if (log->blob_file) emq_file_close(log->blob_file);
  log->blob_file = fresh.blob_file;
  fresh.blob_file = NULL;
  log->blob_used = fresh.blob_used;
  log->segments = fresh.segments;
  log->segment_count = fresh.segment_count;
  log->segment_capacity = fresh.segment_capacity;
  fresh.segments = NULL;
  fresh.segment_count = 0;
  fresh.segment_capacity = 0;
  log->storage_generation = new_generation;
  log->disk_base_offset = new_disk_base;
  log->meta_sequence = sequence;
  for (i = 0; i < log->count; ++i) {
    emq_log_entry *e = &log->entries[(log->start + i) % log->capacity];
    e->flags = locations[i].flags;
    e->segment_id = locations[i].segment_id;
    e->record_pos = locations[i].record_pos;
    e->blob_offset = locations[i].blob_offset;
    if (log->mode == EMQ_STORAGE_MMAP &&
        !e->payload_owned &&
        (e->flags & EMQ_RECORD_FLAG_INLINE) != 0) {
      e->payload = (uint8_t *)log->segments[e->segment_id].map.addr +
                   (size_t)e->record_pos + sizeof(emq_record_header);
    }
  }
  emq_log_remove_generation_files(log, old_generation, old_segment_count);

done:
  free(locations);
  fresh_segment_count = fresh.segment_count;
  emq_log_close_segments(&fresh);
  if (fresh.blob_file) emq_file_close(fresh.blob_file);
  if (rc != 0) {
    emq_log_remove_generation_files(log, new_generation,
                                    fresh_segment_count);
  }
  emq_mutex_unlock(log->mu);
  return rc;
}
