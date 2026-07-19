#ifndef EMQ_ROUTING_H
#define EMQ_ROUTING_H

#include "emq/emq_types.h"
#include "core/emq_log.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Route flags are returned in emq_message.flags.  The low bits carry the
 * retry attempt for messages routed through emq_router_retry().
 */
#define EMQ_ROUTE_FLAG_RETRY       0x80000000u
#define EMQ_ROUTE_FLAG_DEAD_LETTER 0x40000000u
#define EMQ_ROUTE_ATTEMPT_MASK     0x0000FFFFu

typedef struct emq_router emq_router;
typedef struct emq_route_group emq_route_group;

typedef struct emq_route_sub {
  char pattern[EMQ_TOPIC_MAX];
  char group[EMQ_NAME_MAX];
  uint64_t offset;
  emq_log *inbox; /* per-subscriber or per-group inbox */
  uint32_t id;
  int active;
  emq_router *router;
  emq_route_group *shared_group;
  struct emq_route_sub *group_next;
  void *consume_mu_opaque;
} emq_route_sub;

struct emq_router {
  emq_route_sub **subs;
  uint32_t capacity;
  uint32_t count;
  uint32_t next_id;
  emq_log *payload_log; /* fan-out: store payload once */
  emq_route_group **groups;
  uint32_t group_capacity;
  uint32_t group_count;
  void *mu_opaque;
};

int emq_router_init(emq_router *r);
void emq_router_destroy(emq_router *r);

int emq_topic_match(const char *pattern, const char *topic);

int emq_router_subscribe(emq_router *r, const char *pattern, const char *group,
                         emq_route_sub **out);
int emq_router_unsubscribe(emq_router *r, uint32_t sub_id);

/* Publish stores payload bytes once; matching inboxes contain metadata refs. */
int emq_router_publish(emq_router *r, const char *topic,
                       const void *data, size_t size, uint64_t *out_offset);
int emq_router_publish_ex(emq_router *r, const char *topic,
                          const void *data, size_t size,
                          const emq_message *meta, uint64_t *out_offset);

/* Batch publish: one topic, N payloads (still one payload-log append each). */
int emq_router_publish_batch(emq_router *r, const char *topic,
                             const emq_batch_item *items, size_t count,
                             size_t *published);

int emq_router_next(emq_route_sub *sub, emq_message *out);
int emq_router_seek(emq_route_sub *sub, uint64_t offset);
int emq_router_replay(emq_route_sub *sub);

/*
 * Route an already-stored payload without copying its bytes.  payload_offset
 * is the id returned by emq_router_next(), or the offset returned by publish.
 * deliver_at_ns is an absolute monotonic deadline; zero means immediately.
 */
int emq_router_retry(emq_router *r, const char *retry_topic,
                     uint64_t payload_offset, uint32_t attempt,
                     uint64_t deliver_at_ns);
int emq_router_dead_letter(emq_router *r, const char *dead_letter_topic,
                           uint64_t payload_offset, uint32_t attempts);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_ROUTING_H */
