#ifndef EMQ_RECORD_H
#define EMQ_RECORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMQ_RECORD_MAGIC 0x454D5152u /* 'EMQR' */
#define EMQ_RECORD_VERSION 2u
#define EMQ_RECORD_VERSION_3 3u
#define EMQ_RECORD_V3_SIZE 32u
#define EMQ_INLINE_PAYLOAD_V3 256u
#define EMQ_RECORD_FLAG_INLINE 0x01u
#define EMQ_RECORD_FLAG_BLOB   0x02u
#define EMQ_RECORD_FLAG_TOMBSTONE 0x04u
#define EMQ_RECORD_FLAG_EXT 0x08u
#define EMQ_RECORD_FLAG_COMPRESSED 0x10u

#pragma pack(push, 1)
typedef struct emq_record_header {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t crc32;
  uint32_t queue_id;
  uint64_t msg_id;
  uint64_t timestamp_ns;
  uint64_t deliver_at_ns;
  uint64_t ttl_ns;
  uint32_t priority;
  uint32_t payload_len;
  uint64_t blob_offset; /* valid when FLAG_BLOB */
} emq_record_header;

typedef struct emq_record_header_v3 {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t crc32c;
  uint32_t queue_id;
  uint64_t msg_id;
  uint32_t payload_len;
  uint32_t meta_packed; /* low 16 = priority, high bits reserved */
} emq_record_header_v3;

typedef struct emq_record_ext_v3 {
  uint64_t deliver_at_ns;
  uint64_t ttl_ns;
} emq_record_ext_v3;
#pragma pack(pop)

typedef struct emq_record {
  emq_record_header hdr;
  const uint8_t *payload; /* points into buffer or blob */
} emq_record;

typedef struct emq_record_v3 {
  emq_record_header_v3 hdr;
  emq_record_ext_v3 ext;
  int has_ext;
  const uint8_t *payload;
} emq_record_v3;

size_t emq_record_encoded_size(uint32_t payload_len, int inline_payload);
size_t emq_record_encode(uint8_t *dst, size_t dst_cap, const emq_record *rec);
int emq_record_decode(const uint8_t *src, size_t src_len, emq_record *out);
uint32_t emq_record_compute_crc(const emq_record_header *hdr, const void *payload);

size_t emq_record_v3_encoded_size(uint32_t payload_len, int inline_payload, int has_ext);
size_t emq_record_encode_v3(uint8_t *dst, size_t dst_cap, const emq_record_v3 *rec);
int emq_record_decode_v3(const uint8_t *src, size_t src_len, emq_record_v3 *out);
uint32_t emq_record_compute_crc_v3(const emq_record_header_v3 *hdr,
                                     const emq_record_ext_v3 *ext,
                                     const void *payload);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_RECORD_H */
