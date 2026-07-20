#ifndef EMQ_EVENTLOOP_H
#define EMQ_EVENTLOOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_eventloop emq_eventloop;
typedef void (*emq_event_cb)(void *user, int fd, uint32_t events);

#define EMQ_EV_READ  1u
#define EMQ_EV_WRITE 2u

int emq_eventloop_create(emq_eventloop **out);
void emq_eventloop_destroy(emq_eventloop *loop);
int emq_eventloop_add(emq_eventloop *loop, int fd, uint32_t events, emq_event_cb cb, void *user);
int emq_eventloop_del(emq_eventloop *loop, int fd);
/* Poll for up to timeout_ms; returns number of events handled. */
int emq_eventloop_poll(emq_eventloop *loop, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_EVENTLOOP_H */
