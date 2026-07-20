#include "emq_test.h"
#include "core/emq_record.h"

#include <string.h>

int main(void) {
  uint8_t buf[512];
  emq_record_v3 rec, out;
  const char *payload = "hello-v3";
  size_t n;

  EMQ_CHECK_EQ(sizeof(emq_record_header_v3), (size_t)EMQ_RECORD_V3_SIZE);

  memset(&rec, 0, sizeof(rec));
  rec.hdr.flags = EMQ_RECORD_FLAG_INLINE | EMQ_RECORD_FLAG_EXT;
  rec.hdr.queue_id = 9;
  rec.hdr.msg_id = 100;
  rec.hdr.payload_len = (uint32_t)strlen(payload);
  rec.hdr.meta_packed = 7;
  rec.ext.deliver_at_ns = 5000;
  rec.ext.ttl_ns = 9000;
  rec.has_ext = 1;
  rec.payload = (const uint8_t *)payload;

  n = emq_record_encode_v3(buf, sizeof(buf), &rec);
  EMQ_CHECK(n > sizeof(emq_record_header_v3) + sizeof(emq_record_ext_v3));
  EMQ_CHECK(emq_record_decode_v3(buf, n, &out) == 0);
  EMQ_CHECK_EQ(out.hdr.msg_id, 100u);
  EMQ_CHECK_EQ(out.hdr.queue_id, 9u);
  EMQ_CHECK_EQ(out.hdr.meta_packed & 0xffffu, 7u);
  EMQ_CHECK_EQ(out.ext.deliver_at_ns, 5000u);
  EMQ_CHECK_EQ(out.ext.ttl_ns, 9000u);
  EMQ_CHECK(out.has_ext);
  EMQ_CHECK(memcmp(out.payload, payload, strlen(payload)) == 0);

  buf[8] ^= 0xff;
  EMQ_CHECK(emq_record_decode_v3(buf, n, &out) != 0);

  return emq_test_report();
}
