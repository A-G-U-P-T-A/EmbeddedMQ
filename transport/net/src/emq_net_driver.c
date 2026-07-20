#include "emq_net.h"
#include "emq_net_proto.h"

#include <stddef.h>
#include <string.h>

/*
 * Driver loop wiring: validates outbound frames and dispatches poll/send
 * through the selected backend (socket implementation in emq_net_socket.c).
 */

static uint32_t emq_net_hdr_crc(const emq_net_frame_hdr *hdr) {
  /* Lightweight placeholder — production path uses core CRC. */
  const uint8_t *p = (const uint8_t *)hdr;
  uint32_t c = 0;
  size_t i;
  for (i = 0; i < offsetof(emq_net_frame_hdr, hdr_crc); ++i) {
    c = (c * 131u) + p[i];
  }
  return c;
}

emq_status emq_net_encode_frame(emq_net_frame_hdr *hdr, uint32_t session,
                                uint32_t stream, uint64_t term, uint64_t offset,
                                uint16_t flags, uint32_t payload_len) {
  if (!hdr) {
    return EMQ_ERR_INVALID;
  }
  memset(hdr, 0, sizeof(*hdr));
  hdr->magic = EMQ_NET_PROTO_MAGIC;
  hdr->version = (uint16_t)EMQ_NET_PROTO_VERSION;
  hdr->flags = flags;
  hdr->session = session;
  hdr->stream = stream;
  hdr->term = term;
  hdr->offset = offset;
  hdr->payload_len = payload_len;
  hdr->hdr_crc = emq_net_hdr_crc(hdr);
  return EMQ_OK;
}

emq_status emq_net_decode_frame(const emq_net_frame_hdr *hdr) {
  if (!hdr || hdr->magic != EMQ_NET_PROTO_MAGIC ||
      hdr->version != EMQ_NET_PROTO_VERSION) {
    return EMQ_ERR_INVALID;
  }
  if (hdr->hdr_crc != emq_net_hdr_crc(hdr)) {
    return EMQ_ERR_INVALID;
  }
  return EMQ_OK;
}

emq_status emq_net_driver_send_frame(emq_net_endpoint *ep,
                                     const emq_net_addr *dest,
                                     const emq_net_frame_hdr *hdr,
                                     const void *payload) {
  uint8_t buf[EMQ_NET_FRAME_HDR_SIZE + 65536];
  size_t total;

  if (!ep || !dest || !hdr) {
    return EMQ_ERR_INVALID;
  }
  if (hdr->payload_len > 65536u) {
    return EMQ_ERR_INVALID;
  }
  if (emq_net_decode_frame(hdr) != EMQ_OK) {
    return EMQ_ERR_INVALID;
  }

  total = EMQ_NET_FRAME_HDR_SIZE + hdr->payload_len;
  memcpy(buf, hdr, EMQ_NET_FRAME_HDR_SIZE);
  if (hdr->payload_len > 0 && payload) {
    memcpy(buf + EMQ_NET_FRAME_HDR_SIZE, payload, hdr->payload_len);
  }
  return emq_net_send(ep, dest, buf, total);
}

emq_status emq_net_driver_recv_frame(emq_net_driver *drv, emq_net_endpoint *ep,
                                     emq_net_rx *out, emq_net_frame_hdr *hdr,
                                     const uint8_t **payload) {
  emq_status st;

  if (!drv || !ep || !out || !hdr || !payload) {
    return EMQ_ERR_INVALID;
  }

  st = emq_net_poll(drv, ep, out);
  if (st != EMQ_OK) {
    return st;
  }
  if (out->len < EMQ_NET_FRAME_HDR_SIZE) {
    return EMQ_ERR_INVALID;
  }

  memcpy(hdr, out->data, EMQ_NET_FRAME_HDR_SIZE);
  st = emq_net_decode_frame(hdr);
  if (st != EMQ_OK) {
    return st;
  }
  if (out->len < EMQ_NET_FRAME_HDR_SIZE + hdr->payload_len) {
    return EMQ_ERR_INVALID;
  }

  *payload = out->data + EMQ_NET_FRAME_HDR_SIZE;
  return EMQ_OK;
}
