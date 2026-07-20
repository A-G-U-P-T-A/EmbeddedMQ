#include "primitives/emq_primitives.h"
int emq_work_push(emq_queue_desc *q, const void *data, size_t size, const emq_message *meta) {
  return emq_fifo_push(q, data, size, meta);
}

int emq_work_pop(emq_queue_desc *q, emq_message *out) {
  return emq_fifo_pop(q, out);
}
