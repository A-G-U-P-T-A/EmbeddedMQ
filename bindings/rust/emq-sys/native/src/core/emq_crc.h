#ifndef EMQ_CRC_H
#define EMQ_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t emq_crc32(const void *data, size_t len);
uint32_t emq_crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t emq_crc32c(const void *data, size_t len);
uint32_t emq_crc32c_update(uint32_t crc, const void *data, size_t len);
uint32_t emq_crc32c_hw(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_CRC_H */
