#include "emq_test.h"
#include "core/emq_record.h"

#include <string.h>

int main(void) {
  uint8_t buf[256];
  emq_record rec, out;
  const char *payload = "hello-emq";
  size_t n;

  memset(&rec, 0, sizeof(rec));
  rec.hdr.flags = EMQ_RECORD_FLAG_INLINE;
  rec.hdr.queue_id = 7;
  rec.hdr.msg_id = 42;
  rec.hdr.timestamp_ns = 123456789ULL;
  rec.hdr.priority = 3;
  rec.hdr.payload_len = (uint32_t)strlen(payload);
  rec.payload = (const uint8_t *)payload;

  n = emq_record_encode(buf, sizeof(buf), &rec);
  EMQ_CHECK(n > sizeof(emq_record_header));
  EMQ_CHECK(emq_record_decode(buf, n, &out) == 0);
  EMQ_CHECK_EQ(out.hdr.msg_id, 42u);
  EMQ_CHECK_EQ(out.hdr.queue_id, 7u);
  EMQ_CHECK(memcmp(out.payload, payload, strlen(payload)) == 0);

  buf[20] ^= 0xff;
  EMQ_CHECK(emq_record_decode(buf, n, &out) != 0);

  return emq_test_report();
}
