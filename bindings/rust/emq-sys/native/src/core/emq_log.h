#ifndef EMQ_LOG_H
#define EMQ_LOG_H

#include "core/emq_record.h"
#include "emq/emq_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_log_entry {
  uint64_t offset;
  uint64_t msg_id;
  uint32_t priority;
  uint64_t timestamp_ns;
  uint64_t deliver_at_ns;
  uint64_t ttl_ns;
  uint32_t flags;
  uint8_t *payload;
  uint32_t payload_len;
  uint32_t capacity;
  /* Internal durable locator.  Callers should treat these fields as opaque. */
  uint32_t queue_id;
  uint32_t segment_id;
  uint64_t record_pos;
  uint64_t blob_offset;
  uint8_t payload_owned;
} emq_log_entry;

typedef struct emq_log emq_log;

int emq_log_open(emq_log **out, emq_storage_mode mode, const char *path,
                 uint32_t capacity, emq_fsync_policy fsync);
void emq_log_close(emq_log *log);

int emq_log_append(emq_log *log, uint32_t queue_id, uint64_t msg_id,
                   uint32_t priority, uint64_t deliver_at_ns,
                   const void *data, uint32_t len, uint64_t *out_offset);
int emq_log_append_ex(emq_log *log, uint32_t queue_id, uint64_t msg_id,
                      uint32_t priority, uint64_t deliver_at_ns,
                      uint64_t ttl_ns, const void *data, uint32_t len,
                      uint64_t *out_offset);

int emq_log_read(emq_log *log, uint64_t offset, emq_log_entry *out);
int emq_log_read_copy(emq_log *log, uint64_t offset, emq_log_entry *out);

uint64_t emq_log_next_offset(const emq_log *log);
uint64_t emq_log_count(const emq_log *log);
int emq_log_sync(emq_log *log);

/* Ring overwrite support: drop oldest until count <= capacity */
int emq_log_trim_front(emq_log *log, uint64_t new_start_offset);

/*
 * Internal storage maintenance APIs.  A snapshot durably records trim and
 * generation metadata.  Compaction rewrites only the currently retained
 * entries into a fresh segment/blob generation and then snapshots it.
 */
int emq_log_snapshot(emq_log *log);
int emq_log_compact(emq_log *log);

/* Set the inline/blob cutoff for subsequent appends (0 restores 8 KiB). */
int emq_log_set_blob_threshold(emq_log *log, uint32_t threshold);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_LOG_H */
