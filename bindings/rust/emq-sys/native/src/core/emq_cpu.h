#ifndef EMQ_CPU_H
#define EMQ_CPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct emq_cpu_features {
  int sse42;
  int avx2;
  int popcnt;
  int lzcnt;
  int bmi2;
  int neon;
  int crc32_hw;
};

typedef struct emq_isa_ops {
  uint32_t (*crc32c)(const void *, size_t);
  void *(*copy)(void *, const void *, size_t);
  unsigned (*ctz64)(uint64_t);
} emq_isa_ops;

void emq_cpu_init(void);
const struct emq_cpu_features *emq_cpu_features(void);
const emq_isa_ops *emq_isa(void);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_CPU_H */
