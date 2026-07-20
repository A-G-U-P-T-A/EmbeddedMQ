#ifndef EMQ_COMPRESS_H
#define EMQ_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Adaptive compression hooks (optional, off by default).
 * Tiny payloads: no compression.
 * Mid: LZ4 when EMQ_WITH_LZ4.
 * Large: ZSTD when EMQ_WITH_ZSTD.
 */
typedef enum emq_compress_algo {
  EMQ_COMPRESS_NONE = 0,
  EMQ_COMPRESS_LZ4 = 1,
  EMQ_COMPRESS_ZSTD = 2
} emq_compress_algo;

emq_compress_algo emq_compress_select(size_t len);
int emq_compress(emq_compress_algo algo, const void *src, size_t src_len,
                 void *dst, size_t dst_cap, size_t *out_len);
int emq_decompress(emq_compress_algo algo, const void *src, size_t src_len,
                   void *dst, size_t dst_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_COMPRESS_H */
