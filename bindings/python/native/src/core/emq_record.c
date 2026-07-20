#include "core/emq_record.h"
#include "core/emq_crc.h"

#include <string.h>

size_t emq_record_encoded_size(uint32_t payload_len, int inline_payload) {
  size_t n = sizeof(emq_record_header);
  if (inline_payload) n += payload_len;
  return n;
}

uint32_t emq_record_compute_crc(const emq_record_header *hdr, const void *payload) {
  emq_record_header tmp = *hdr;
  uint32_t crc;

  tmp.crc32 = 0;
  crc = emq_crc32(&tmp, sizeof(tmp));
  if (payload && hdr->payload_len != 0) {
    crc = emq_crc32_update(crc, payload, hdr->payload_len);
  }
  return crc;
}

size_t emq_record_encode(uint8_t *dst, size_t dst_cap, const emq_record *rec) {
  size_t need;
  emq_record_header hdr;
  int inline_payload;
  if (!dst || !rec) return 0;
  inline_payload = (rec->hdr.flags & EMQ_RECORD_FLAG_INLINE) != 0;
  if (inline_payload == ((rec->hdr.flags & EMQ_RECORD_FLAG_BLOB) != 0)) return 0;
  if (rec->hdr.payload_len != 0 && inline_payload && !rec->payload) return 0;
  need = emq_record_encoded_size(rec->hdr.payload_len, inline_payload);
  if (need < sizeof(emq_record_header)) return 0;
  if (need > dst_cap) return 0;
  hdr = rec->hdr;
  hdr.magic = EMQ_RECORD_MAGIC;
  hdr.version = EMQ_RECORD_VERSION;
  hdr.crc32 = 0;
  hdr.crc32 = emq_record_compute_crc(&hdr, inline_payload ? rec->payload : NULL);
  memcpy(dst, &hdr, sizeof(hdr));
  if (inline_payload && rec->payload && rec->hdr.payload_len > 0) {
    memcpy(dst + sizeof(hdr), rec->payload, rec->hdr.payload_len);
  }
  return need;
}

int emq_record_decode(const uint8_t *src, size_t src_len, emq_record *out) {
  emq_record_header hdr;
  uint32_t expect;
  int inline_payload;
  const uint8_t *payload = NULL;
  if (!src || !out || src_len < sizeof(hdr)) return -1;
  memcpy(&hdr, src, sizeof(hdr));
  if (hdr.magic != EMQ_RECORD_MAGIC || hdr.version != EMQ_RECORD_VERSION) return -1;
  inline_payload = (hdr.flags & EMQ_RECORD_FLAG_INLINE) != 0;
  if (inline_payload == ((hdr.flags & EMQ_RECORD_FLAG_BLOB) != 0)) return -1;
  if (inline_payload) {
    if ((size_t)hdr.payload_len > SIZE_MAX - sizeof(hdr)) return -1;
    if (src_len < sizeof(hdr) + hdr.payload_len) return -1;
    payload = src + sizeof(hdr);
  }
  expect = hdr.crc32;
  hdr.crc32 = 0;
  if (emq_record_compute_crc(&hdr, payload) != expect) return -1;
  hdr.crc32 = expect;
  out->hdr = hdr;
  out->payload = payload;
  return 0;
}

size_t emq_record_v3_encoded_size(uint32_t payload_len, int inline_payload,
                                  int has_ext) {
  size_t n = sizeof(emq_record_header_v3);
  if (has_ext) {
    n += sizeof(emq_record_ext_v3);
  }
  if (inline_payload) {
    n += payload_len;
  }
  return n;
}

uint32_t emq_record_compute_crc_v3(const emq_record_header_v3 *hdr,
                                   const emq_record_ext_v3 *ext,
                                   const void *payload) {
  emq_record_header_v3 tmp = *hdr;
  uint32_t crc;

  tmp.crc32c = 0;
  crc = emq_crc32c(&tmp, sizeof(tmp));
  if (ext && (hdr->flags & EMQ_RECORD_FLAG_EXT) != 0) {
    crc = emq_crc32c_update(crc, ext, sizeof(*ext));
  }
  if (payload && hdr->payload_len != 0) {
    crc = emq_crc32c_update(crc, payload, hdr->payload_len);
  }
  return crc;
}

size_t emq_record_encode_v3(uint8_t *dst, size_t dst_cap, const emq_record_v3 *rec) {
  size_t need;
  size_t off;
  emq_record_header_v3 hdr;
  int inline_payload;
  int has_ext;

  if (!dst || !rec) {
    return 0;
  }

  inline_payload = (rec->hdr.flags & EMQ_RECORD_FLAG_INLINE) != 0;
  has_ext = (rec->hdr.flags & EMQ_RECORD_FLAG_EXT) != 0;
  if (inline_payload == ((rec->hdr.flags & EMQ_RECORD_FLAG_BLOB) != 0)) {
    return 0;
  }
  if (has_ext != rec->has_ext) {
    return 0;
  }
  if (rec->hdr.payload_len != 0 && inline_payload && !rec->payload) {
    return 0;
  }

  need = emq_record_v3_encoded_size(rec->hdr.payload_len, inline_payload, has_ext);
  if (need < sizeof(emq_record_header_v3) || need > dst_cap) {
    return 0;
  }

  hdr = rec->hdr;
  hdr.magic = EMQ_RECORD_MAGIC;
  hdr.version = EMQ_RECORD_VERSION_3;
  hdr.crc32c = 0;
  hdr.crc32c = emq_record_compute_crc_v3(
      &hdr, has_ext ? &rec->ext : NULL, inline_payload ? rec->payload : NULL);

  off = 0;
  memcpy(dst + off, &hdr, sizeof(hdr));
  off += sizeof(hdr);
  if (has_ext) {
    memcpy(dst + off, &rec->ext, sizeof(rec->ext));
    off += sizeof(rec->ext);
  }
  if (inline_payload && rec->payload && rec->hdr.payload_len > 0) {
    memcpy(dst + off, rec->payload, rec->hdr.payload_len);
  }
  return need;
}

int emq_record_decode_v3(const uint8_t *src, size_t src_len, emq_record_v3 *out) {
  emq_record_header_v3 hdr;
  emq_record_ext_v3 ext;
  uint32_t expect;
  int inline_payload;
  int has_ext;
  size_t off;
  const uint8_t *payload = NULL;

  if (!src || !out || src_len < sizeof(hdr)) {
    return -1;
  }

  memcpy(&hdr, src, sizeof(hdr));
  if (hdr.magic != EMQ_RECORD_MAGIC || hdr.version != EMQ_RECORD_VERSION_3) {
    return -1;
  }

  inline_payload = (hdr.flags & EMQ_RECORD_FLAG_INLINE) != 0;
  has_ext = (hdr.flags & EMQ_RECORD_FLAG_EXT) != 0;
  if (inline_payload == ((hdr.flags & EMQ_RECORD_FLAG_BLOB) != 0)) {
    return -1;
  }

  off = sizeof(hdr);
  memset(&ext, 0, sizeof(ext));
  if (has_ext) {
    if (src_len < off + sizeof(ext)) {
      return -1;
    }
    memcpy(&ext, src + off, sizeof(ext));
    off += sizeof(ext);
  }

  if (inline_payload) {
    if ((size_t)hdr.payload_len > SIZE_MAX - off) {
      return -1;
    }
    if (src_len < off + hdr.payload_len) {
      return -1;
    }
    payload = src + off;
  }

  expect = hdr.crc32c;
  hdr.crc32c = 0;
  if (emq_record_compute_crc_v3(&hdr, has_ext ? &ext : NULL, payload) != expect) {
    return -1;
  }

  hdr.crc32c = expect;
  out->hdr = hdr;
  out->ext = ext;
  out->has_ext = has_ext;
  out->payload = payload;
  return 0;
}
