#include "core/emq_compress.h"

#include <string.h>

emq_compress_algo emq_compress_select(size_t len) {
  if (len < 256) return EMQ_COMPRESS_NONE;
#if defined(EMQ_WITH_ZSTD)
  if (len >= 64u * 1024u) return EMQ_COMPRESS_ZSTD;
#endif
#if defined(EMQ_WITH_LZ4)
  if (len >= 4096u) return EMQ_COMPRESS_LZ4;
#endif
  (void)len;
  return EMQ_COMPRESS_NONE;
}

int emq_compress(emq_compress_algo algo, const void *src, size_t src_len,
                 void *dst, size_t dst_cap, size_t *out_len) {
  if (!src || !dst || !out_len) return -1;
  if (algo == EMQ_COMPRESS_NONE) {
    if (dst_cap < src_len) return -4;
    memcpy(dst, src, src_len);
    *out_len = src_len;
    return 0;
  }
  /* Optional codecs not linked — identity fallback keeps API stable. */
  if (dst_cap < src_len) return -4;
  memcpy(dst, src, src_len);
  *out_len = src_len;
  return 0;
}

int emq_decompress(emq_compress_algo algo, const void *src, size_t src_len,
                   void *dst, size_t dst_cap, size_t *out_len) {
  (void)algo;
  if (!src || !dst || !out_len) return -1;
  if (dst_cap < src_len) return -4;
  memcpy(dst, src, src_len);
  *out_len = src_len;
  return 0;
}
