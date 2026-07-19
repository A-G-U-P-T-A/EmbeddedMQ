#include "emq_test.h"
#include "core/emq_crc.h"
#include <stdint.h>

int main(void) {
  const char *s = "123456789";
  uint32_t crc = emq_crc32(s, 9);
  EMQ_CHECK_EQ(crc, 0xCBF43926u);
  EMQ_CHECK_EQ(emq_crc32("", 0), 0u);
  return emq_test_report();
}
