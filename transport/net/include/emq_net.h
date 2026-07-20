#ifndef EMQ_NET_H
#define EMQ_NET_H

#include <stddef.h>
#include <stdint.h>

#include "emq/emq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum emq_net_backend {
  EMQ_NET_BACKEND_SOCKET = 0,
  EMQ_NET_BACKEND_URING,
  EMQ_NET_BACKEND_RIO,
  EMQ_NET_BACKEND_AF_XDP,
  EMQ_NET_BACKEND_DPDK
} emq_net_backend;

typedef struct emq_net_driver emq_net_driver;
typedef struct emq_net_endpoint emq_net_endpoint;

typedef struct emq_net_addr {
  char host[256];
  uint16_t port;
} emq_net_addr;

typedef struct emq_net_rx {
  emq_net_addr from;
  const uint8_t *data;
  size_t len;
} emq_net_rx;

/* Create a media driver instance for the selected backend. */
emq_status emq_net_create(emq_net_backend backend, emq_net_driver **out);

/* Tear down driver state and close all endpoints. */
void emq_net_destroy(emq_net_driver *drv);

/* Bind a local UDP (or backend-specific) endpoint. */
emq_status emq_net_bind(emq_net_driver *drv, const emq_net_addr *addr,
                        emq_net_endpoint **out);

/* Send a datagram/frame to dest. */
emq_status emq_net_send(emq_net_endpoint *ep, const emq_net_addr *dest,
                        const void *data, size_t len);

/*
 * Poll for one inbound datagram. Returns EMQ_ERR_EMPTY when none ready.
 * out->data points into driver-owned storage valid until the next poll/send.
 */
emq_status emq_net_poll(emq_net_driver *drv, emq_net_endpoint *ep,
                         emq_net_rx *out);

/* Drive the media loop for up to budget_ms (socket backend: poll I/O). */
emq_status emq_net_driver_run_once(emq_net_driver *drv, uint32_t budget_ms);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_NET_H */
