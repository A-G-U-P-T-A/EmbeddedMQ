#include "emq_net.h"
#include "emq_net_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

struct emq_net_endpoint {
  emq_net_driver *drv;
  emq_net_addr bound;
#if defined(_WIN32)
  SOCKET fd;
#else
  int fd;
#endif
};

struct emq_net_driver {
  emq_net_backend backend;
  emq_net_endpoint *endpoints;
  size_t endpoint_count;
  size_t endpoint_cap;
  uint8_t rx_buf[65536];
  emq_net_rx last_rx;
#if defined(_WIN32)
  int wsa_started;
#endif
};

static emq_status emq_net_map_errno(void) {
#if defined(_WIN32)
  return EMQ_ERR_IO;
#else
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return EMQ_ERR_EMPTY;
  }
  return EMQ_ERR_IO;
#endif
}

static void emq_net_sock_init_once(emq_net_driver *drv) {
#if defined(_WIN32)
  if (!drv->wsa_started) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
      drv->wsa_started = 1;
    }
  }
#else
  (void)drv;
#endif
}

static void emq_net_sock_shutdown(emq_net_driver *drv) {
#if defined(_WIN32)
  if (drv->wsa_started) {
    WSACleanup();
    drv->wsa_started = 0;
  }
#else
  (void)drv;
#endif
}

static int emq_net_set_nonblock(
#if defined(_WIN32)
    SOCKET fd
#else
    int fd
#endif
) {
#if defined(_WIN32)
  u_long mode = 1;
  return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static emq_status emq_net_resolve_bind(const emq_net_addr *addr,
                                       struct sockaddr_in *out) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  char port_buf[16];
  int rc;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  (void)snprintf(port_buf, sizeof(port_buf), "%u", (unsigned)addr->port);
  rc = getaddrinfo(addr->host[0] ? addr->host : NULL, port_buf, &hints, &res);
  if (rc != 0 || !res) {
    return EMQ_ERR_IO;
  }
  memcpy(out, res->ai_addr, sizeof(*out));
  freeaddrinfo(res);
  return EMQ_OK;
}

static emq_status emq_net_resolve_dest(const emq_net_addr *addr,
                                       struct sockaddr_in *out) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  char port_buf[16];
  int rc;

  if (!addr || !addr->host[0] || addr->port == 0) {
    return EMQ_ERR_INVALID;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  (void)snprintf(port_buf, sizeof(port_buf), "%u", (unsigned)addr->port);
  rc = getaddrinfo(addr->host, port_buf, &hints, &res);
  if (rc != 0 || !res) {
    return EMQ_ERR_IO;
  }
  memcpy(out, res->ai_addr, sizeof(*out));
  freeaddrinfo(res);
  return EMQ_OK;
}

emq_status emq_net_create(emq_net_backend backend, emq_net_driver **out) {
  emq_net_driver *drv;

  if (!out) {
    return EMQ_ERR_INVALID;
  }
  if (backend != EMQ_NET_BACKEND_SOCKET) {
    return EMQ_ERR_UNSUPPORTED;
  }

  drv = (emq_net_driver *)calloc(1, sizeof(*drv));
  if (!drv) {
    return EMQ_ERR_NOMEM;
  }
  drv->backend = backend;
  drv->endpoint_cap = 4;
  drv->endpoints =
      (emq_net_endpoint *)calloc(drv->endpoint_cap, sizeof(emq_net_endpoint));
  if (!drv->endpoints) {
    free(drv);
    return EMQ_ERR_NOMEM;
  }
  emq_net_sock_init_once(drv);
  *out = drv;
  return EMQ_OK;
}

void emq_net_destroy(emq_net_driver *drv) {
  size_t i;
  if (!drv) {
    return;
  }
  for (i = 0; i < drv->endpoint_count; ++i) {
#if defined(_WIN32)
    if (drv->endpoints[i].fd != INVALID_SOCKET) {
      closesocket(drv->endpoints[i].fd);
    }
#else
    if (drv->endpoints[i].fd >= 0) {
      close(drv->endpoints[i].fd);
    }
#endif
  }
  free(drv->endpoints);
  emq_net_sock_shutdown(drv);
  free(drv);
}

emq_status emq_net_bind(emq_net_driver *drv, const emq_net_addr *addr,
                        emq_net_endpoint **out) {
  emq_net_endpoint *ep;
  struct sockaddr_in sa;
#if defined(_WIN32)
  SOCKET fd = INVALID_SOCKET;
#else
  int fd = -1;
#endif

  if (!drv || !addr || !out) {
    return EMQ_ERR_INVALID;
  }
  if (drv->endpoint_count >= drv->endpoint_cap) {
    return EMQ_ERR_NOMEM;
  }
  if (emq_net_resolve_bind(addr, &sa) != EMQ_OK) {
    return EMQ_ERR_IO;
  }

#if defined(_WIN32)
  fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd == INVALID_SOCKET) {
    return EMQ_ERR_IO;
  }
#else
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return EMQ_ERR_IO;
  }
#endif

  {
    int yes = 1;
#if defined(_WIN32)
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes,
                     (int)sizeof(yes));
#else
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes));
#endif
  }

  if (bind(fd, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) != 0) {
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
    return EMQ_ERR_IO;
  }

  if (emq_net_set_nonblock(fd) != 0) {
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
    return EMQ_ERR_IO;
  }

  ep = &drv->endpoints[drv->endpoint_count++];
  memset(ep, 0, sizeof(*ep));
  ep->drv = drv;
  ep->fd = fd;
  ep->bound = *addr;
  if (!ep->bound.host[0]) {
    (void)strncpy(ep->bound.host, "0.0.0.0", sizeof(ep->bound.host) - 1u);
  }
  *out = ep;
  return EMQ_OK;
}

emq_status emq_net_send(emq_net_endpoint *ep, const emq_net_addr *dest,
                        const void *data, size_t len) {
  struct sockaddr_in sa;
  int sent;

  if (!ep || !dest || !data || len == 0) {
    return EMQ_ERR_INVALID;
  }
  if (emq_net_resolve_dest(dest, &sa) != EMQ_OK) {
    return EMQ_ERR_IO;
  }

#if defined(_WIN32)
  sent = sendto(ep->fd, (const char *)data, (int)len, 0,
                (struct sockaddr *)&sa, (int)sizeof(sa));
  if (sent == SOCKET_ERROR) {
    return emq_net_map_errno();
  }
#else
  sent = (int)sendto(ep->fd, data, len, 0, (struct sockaddr *)&sa,
                     (socklen_t)sizeof(sa));
  if (sent < 0) {
    return emq_net_map_errno();
  }
#endif
  if ((size_t)sent != len) {
    return EMQ_ERR_IO;
  }
  return EMQ_OK;
}

emq_status emq_net_poll(emq_net_driver *drv, emq_net_endpoint *ep,
                         emq_net_rx *out) {
  struct sockaddr_in from;
  socklen_t from_len = (socklen_t)sizeof(from);
  int n;

  if (!drv || !ep || !out) {
    return EMQ_ERR_INVALID;
  }

#if defined(_WIN32)
  n = recvfrom(ep->fd, (char *)drv->rx_buf, (int)sizeof(drv->rx_buf), 0,
               (struct sockaddr *)&from, &from_len);
  if (n == SOCKET_ERROR) {
    return emq_net_map_errno();
  }
#else
  n = (int)recvfrom(ep->fd, drv->rx_buf, sizeof(drv->rx_buf), 0,
                    (struct sockaddr *)&from, &from_len);
  if (n < 0) {
    return emq_net_map_errno();
  }
#endif

  memset(&drv->last_rx, 0, sizeof(drv->last_rx));
  if (inet_ntop(AF_INET, &from.sin_addr, drv->last_rx.from.host,
                (socklen_t)sizeof(drv->last_rx.from.host)) == NULL) {
    (void)strncpy(drv->last_rx.from.host, "0.0.0.0",
                  sizeof(drv->last_rx.from.host) - 1u);
  }
  drv->last_rx.from.port = ntohs(from.sin_port);
  drv->last_rx.data = drv->rx_buf;
  drv->last_rx.len = (size_t)n;
  *out = drv->last_rx;
  return EMQ_OK;
}

emq_status emq_net_driver_run_once(emq_net_driver *drv, uint32_t budget_ms) {
  size_t i;
  emq_net_rx rx;

  if (!drv) {
    return EMQ_ERR_INVALID;
  }
  (void)budget_ms;

  for (i = 0; i < drv->endpoint_count; ++i) {
    emq_status st = emq_net_poll(drv, &drv->endpoints[i], &rx);
    if (st == EMQ_OK) {
      return EMQ_OK;
    }
    if (st != EMQ_ERR_EMPTY) {
      return st;
    }
  }
  return EMQ_ERR_EMPTY;
}
