#include "primitives/emq_primitives.h"

/* Pub/Sub queue policy: store once; consumers use stream offsets.
 * Fan-out across topics is handled by the routing engine. */

int emq_pubsub_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta) {
  return emq_stream_push(q, data, size, meta);
}

int emq_pubsub_pop(emq_queue_desc *q, emq_message *out) {
  return emq_stream_pop(q, out);
}
