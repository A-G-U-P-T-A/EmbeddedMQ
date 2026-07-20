#ifndef EMQ_NET_PROTO_H
#define EMQ_NET_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMQ_NET_PROTO_MAGIC 0x514D454Eu /* "EMQN" */
#define EMQ_NET_PROTO_VERSION 1u

#define EMQ_NET_FLAG_NAK 0x0001u
#define EMQ_NET_FLAG_SM 0x0002u
#define EMQ_NET_FLAG_DATA 0x0004u

typedef struct emq_net_frame_hdr {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t session;
  uint32_t stream;
  uint64_t term;
  uint64_t offset;
  uint32_t payload_len;
  uint32_t hdr_crc;
} emq_net_frame_hdr;

enum { EMQ_NET_FRAME_HDR_SIZE = 40 };

#ifdef __cplusplus
}
#endif

#endif /* EMQ_NET_PROTO_H */
