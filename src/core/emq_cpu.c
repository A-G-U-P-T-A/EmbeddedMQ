#include "core/emq_cpu.h"
#include "core/emq_atomic.h"
#include "core/emq_crc.h"

#include <string.h>

#if defined(_MSC_VER)
#  include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#  if defined(__i386__) || defined(__x86_64__)
#    include <cpuid.h>
#  endif
#endif

static struct emq_cpu_features g_cpu_features;
static emq_isa_ops g_isa_ops;
static int g_cpu_ready = 0;

static void *emq_isa_memcpy(void *dst, const void *src, size_t n) {
  return memcpy(dst, src, n);
}

#if defined(_MSC_VER) || (defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))

static void emq_cpu_detect_x86(struct emq_cpu_features *f) {
  int info[4];
  int n_ids;
  int has_ext;

#if defined(_MSC_VER)
  __cpuid(info, 0);
  n_ids = info[0];
  __cpuid(info, 1);
#else
  n_ids = __get_cpuid_max(0, NULL);
  if (!__get_cpuid(1, (unsigned int *)&info[0], (unsigned int *)&info[1],
                   (unsigned int *)&info[2], (unsigned int *)&info[3])) {
    return;
  }
#endif

  f->sse42 = (info[2] & (1 << 20)) != 0;
  f->popcnt = (info[2] & (1 << 23)) != 0;

#if defined(_MSC_VER)
  if (n_ids >= 7) {
    __cpuidex(info, 7, 0);
    f->avx2 = (info[1] & (1 << 5)) != 0;
    f->bmi2 = (info[1] & (1 << 8)) != 0;
  }
  __cpuidex(info, 0x80000000, 0);
  has_ext = info[0] >= (int)0x80000001;
  if (has_ext) {
    __cpuidex(info, 0x80000001, 0);
    f->lzcnt = (info[2] & (1 << 5)) != 0;
  }
#else
  if (n_ids >= 7 && __get_cpuid_count(7, 0, (unsigned int *)&info[0],
                                      (unsigned int *)&info[1],
                                      (unsigned int *)&info[2],
                                      (unsigned int *)&info[3])) {
    f->avx2 = (info[1] & (1 << 5)) != 0;
    f->bmi2 = (info[1] & (1 << 8)) != 0;
  }
  has_ext = __get_cpuid_max(0x80000000, NULL) >= 0x80000001u;
  if (has_ext &&
      __get_cpuid(0x80000001, (unsigned int *)&info[0], (unsigned int *)&info[1],
                  (unsigned int *)&info[2], (unsigned int *)&info[3])) {
    f->lzcnt = (info[2] & (1 << 5)) != 0;
  }
#endif

  f->crc32_hw = f->sse42;
}

#else

static void emq_cpu_detect_arm(struct emq_cpu_features *f) {
  f->neon = 0;
  f->crc32_hw = 0;
}

#endif

void emq_cpu_init(void) {
  if (g_cpu_ready) {
    return;
  }

  memset(&g_cpu_features, 0, sizeof(g_cpu_features));
  g_isa_ops.copy = emq_isa_memcpy;
  g_isa_ops.ctz64 = emq_ctz64;
  g_isa_ops.crc32c = emq_crc32c;

#if defined(_MSC_VER) || (defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))
  emq_cpu_detect_x86(&g_cpu_features);
#else
  emq_cpu_detect_arm(&g_cpu_features);
#endif

  if (g_cpu_features.crc32_hw) {
    g_isa_ops.crc32c = emq_crc32c_hw;
  }

  g_cpu_ready = 1;
}

const struct emq_cpu_features *emq_cpu_features(void) {
  emq_cpu_init();
  return &g_cpu_features;
}

const emq_isa_ops *emq_isa(void) {
  emq_cpu_init();
  return &g_isa_ops;
}
