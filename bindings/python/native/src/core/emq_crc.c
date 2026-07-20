#include "core/emq_crc.h"
#include "core/emq_cpu.h"

/* IEEE CRC-32 (polynomial 0xEDB88320), table-driven portable implementation. */

static uint32_t emq_crc_table[256];
static int emq_crc_ready = 0;

/* Castagnoli CRC-32C (polynomial 0x82F63B78). */
static uint32_t emq_crc32c_table[256];
static int emq_crc32c_ready = 0;

static void emq_crc_init(void) {
  uint32_t i, j, c;
  if (emq_crc_ready) return;
  for (i = 0; i < 256; ++i) {
    c = i;
    for (j = 0; j < 8; ++j) {
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    emq_crc_table[i] = c;
  }
  emq_crc_ready = 1;
}

static void emq_crc32c_init(void) {
  uint32_t i, j, c;
  if (emq_crc32c_ready) return;
  for (i = 0; i < 256; ++i) {
    c = i;
    for (j = 0; j < 8; ++j) {
      c = (c & 1u) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
    }
    emq_crc32c_table[i] = c;
  }
  emq_crc32c_ready = 1;
}

uint32_t emq_crc32_update(uint32_t crc, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  size_t i;
  emq_crc_init();
  crc = ~crc;
  for (i = 0; i < len; ++i) {
    crc = emq_crc_table[(crc ^ p[i]) & 0xffu] ^ (crc >> 8);
  }
  return ~crc;
}

uint32_t emq_crc32(const void *data, size_t len) {
  return emq_crc32_update(0, data, len);
}

uint32_t emq_crc32c_update(uint32_t crc, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  size_t i;
  emq_crc32c_init();
  crc = ~crc;
  for (i = 0; i < len; ++i) {
    crc = emq_crc32c_table[(crc ^ p[i]) & 0xffu] ^ (crc >> 8);
  }
  return ~crc;
}

#if defined(_MSC_VER) && defined(_M_X64)
#  include <intrin.h>

static uint32_t emq_crc32c_hw_impl(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;
  size_t i = 0;
  if (!data || len == 0) return 0;
  while (i < len && ((uintptr_t)(p + i) & 7u) != 0) {
    crc = (uint32_t)_mm_crc32_u8(crc, p[i]);
    ++i;
  }
  while (i + 8 <= len) {
    crc = (uint32_t)_mm_crc32_u64(crc, *(const uint64_t *)(p + i));
    i += 8;
  }
  while (i < len) {
    crc = (uint32_t)_mm_crc32_u8(crc, p[i]);
    ++i;
  }
  return ~crc;
}

uint32_t emq_crc32c_hw(const void *data, size_t len) {
  return emq_crc32c_hw_impl(data, len);
}

#elif (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__x86_64__) || defined(__i386__))
#  if defined(__SSE4_2__)
#    include <nmmintrin.h>
#  endif
/* target("sse4.2") so this compiles without -msse4.2 in CFLAGS. */
__attribute__((target("sse4.2"))) static uint32_t
emq_crc32c_hw_impl(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;
  size_t i = 0;
  if (!data || len == 0) return 0;
  while (i < len && ((uintptr_t)(p + i) & 7u) != 0) {
    crc = __builtin_ia32_crc32qi(crc, p[i]);
    ++i;
  }
  while (i + 8 <= len) {
    crc = (uint32_t)__builtin_ia32_crc32di(crc, *(const uint64_t *)(p + i));
    i += 8;
  }
  while (i < len) {
    crc = __builtin_ia32_crc32qi(crc, p[i]);
    ++i;
  }
  return ~crc;
}
uint32_t emq_crc32c_hw(const void *data, size_t len) {
  return emq_crc32c_hw_impl(data, len);
}

#else

uint32_t emq_crc32c_hw(const void *data, size_t len) {
  return emq_crc32c_update(0, data, len);
}

#endif

uint32_t emq_crc32c(const void *data, size_t len) {
  const struct emq_cpu_features *f = emq_cpu_features();
  if (f && f->crc32_hw) {
    return emq_crc32c_hw(data, len);
  }
  return emq_crc32c_update(0, data, len);
}
