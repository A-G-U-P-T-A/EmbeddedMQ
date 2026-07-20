#include "emq_net.h"
#include "emq_net_proto.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
  g_checks++; \
  if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failures++; \
  } \
} while (0)

int main(void) {
  emq_net_driver *drv = NULL;
  emq_net_endpoint *ep = NULL;
  emq_net_addr bind_addr;
  emq_net_addr dest_addr;
  emq_net_rx rx;
  const char msg[] = "loopback-ping";
  uint8_t frame_buf[256];
  emq_net_frame_hdr hdr;
  emq_status st;

  memset(&bind_addr, 0, sizeof(bind_addr));
  (void)strncpy(bind_addr.host, "127.0.0.1", sizeof(bind_addr.host) - 1u);
  bind_addr.port = 37521;

  CHECK(emq_net_create(EMQ_NET_BACKEND_SOCKET, &drv) == EMQ_OK);
  CHECK(emq_net_bind(drv, &bind_addr, &ep) == EMQ_OK);

  dest_addr = bind_addr;

  memset(&hdr, 0, sizeof(hdr));
  hdr.magic = EMQ_NET_PROTO_MAGIC;
  hdr.version = (uint16_t)EMQ_NET_PROTO_VERSION;
  hdr.flags = EMQ_NET_FLAG_DATA;
  hdr.payload_len = (uint32_t)sizeof(msg);
  memcpy(frame_buf, &hdr, EMQ_NET_FRAME_HDR_SIZE);
  memcpy(frame_buf + EMQ_NET_FRAME_HDR_SIZE, msg, sizeof(msg));

  CHECK(emq_net_send(ep, &dest_addr, frame_buf,
                     EMQ_NET_FRAME_HDR_SIZE + sizeof(msg)) == EMQ_OK);

  st = EMQ_ERR_EMPTY;
  for (int i = 0; i < 100 && st == EMQ_ERR_EMPTY; ++i) {
    st = emq_net_poll(drv, ep, &rx);
  }
  CHECK(st == EMQ_OK);
  CHECK(rx.len >= EMQ_NET_FRAME_HDR_SIZE + sizeof(msg));
  CHECK(memcmp(rx.data + EMQ_NET_FRAME_HDR_SIZE, msg, sizeof(msg)) == 0);

  emq_net_destroy(drv);

  if (g_failures == 0) {
    printf("OK (%d checks)\n", g_checks);
    return 0;
  }
  printf("FAILED %d/%d checks\n", g_failures, g_checks);
  return 1;
}
