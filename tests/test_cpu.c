#include "emq_test.h"
#include "core/emq_cpu.h"
#include "core/emq_crc.h"

#include <string.h>

int main(void) {
  const char *s = "123456789";
  const struct emq_cpu_features *feat;
  const emq_isa_ops *isa;
  uint32_t soft;
  uint32_t hw;
  uint64_t bits = 0x1000;

  emq_cpu_init();
  feat = emq_cpu_features();
  isa = emq_isa();

  EMQ_CHECK(feat != NULL);
  EMQ_CHECK(isa != NULL);
  EMQ_CHECK(isa->copy != NULL);
  EMQ_CHECK(isa->ctz64 != NULL);
  EMQ_CHECK(isa->crc32c != NULL);
  EMQ_CHECK_EQ(isa->ctz64(bits), 12u);

  soft = emq_crc32c(s, 9);
  EMQ_CHECK_EQ(soft, 0xE3069283u);
  EMQ_CHECK_EQ(emq_crc32c("", 0), 0u);

  hw = emq_crc32c_hw(s, 9);
  EMQ_CHECK_EQ(hw, soft);
  EMQ_CHECK_EQ(isa->crc32c(s, 9), soft);

  emq_cpu_init();
  EMQ_CHECK(isa == emq_isa());

  return emq_test_report();
}
