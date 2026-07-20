#include "emq_test.h"
#include "emq/emq.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int exercise_mode(emq_storage_mode mode, const char *tag) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message message;
  char path[256];
  uint8_t *large;
  size_t large_size = 16u * 1024u;
  size_t i;

  snprintf(path, sizeof(path), "build/test-data-%s-%llu",
           tag, (unsigned long long)emq_now_ns());
  large = (uint8_t *)malloc(large_size);
  if (!large) return 0;
  for (i = 0; i < large_size; ++i) large[i] = (uint8_t)(i & 0xffu);

  if (emq_runtime_create(&rt) != EMQ_OK) goto fail;
  emq_queue_opts_default(&opts);
  opts.storage = mode;
  opts.path = path;
  opts.inline_threshold = 1024;
  opts.fsync = EMQ_FSYNC_EVERY_WRITE;
  if (emq_queue_create(rt, "durable", &opts, &q) != EMQ_OK) goto fail;
  if (emq_push(q, "small", 5, NULL) != EMQ_OK) goto fail;
  if (emq_push(q, large, large_size, NULL) != EMQ_OK) goto fail;
  if (emq_queue_snapshot(q) != EMQ_OK) goto fail;
  if (emq_queue_compact(q) != EMQ_OK) goto fail;
  emq_queue_close(q);
  q = NULL;
  emq_runtime_destroy(rt);
  rt = NULL;

  if (emq_runtime_create(&rt) != EMQ_OK) goto fail;
  if (emq_queue_create(rt, "durable", &opts, &q) != EMQ_OK) goto fail;
  if (emq_pop(q, &message, 0) != EMQ_OK) goto fail;
  if (message.size != 5 || memcmp(message.data, "small", 5) != 0) {
    emq_message_release(&message);
    goto fail;
  }
  emq_message_release(&message);
  if (emq_pop(q, &message, 0) != EMQ_OK) goto fail;
  if (message.size != large_size ||
      memcmp(message.data, large, large_size) != 0) {
    emq_message_release(&message);
    goto fail;
  }
  emq_message_release(&message);
  emq_queue_close(q);
  emq_runtime_destroy(rt);
  free(large);
  return 1;

fail:
  if (q) emq_queue_close(q);
  if (rt) emq_runtime_destroy(rt);
  free(large);
  return 0;
}

int main(void) {
  EMQ_CHECK(exercise_mode(EMQ_STORAGE_DURABLE, "durable"));
  EMQ_CHECK(exercise_mode(EMQ_STORAGE_MMAP, "mmap"));
  EMQ_CHECK(exercise_mode(EMQ_STORAGE_HYBRID, "hybrid"));
  return emq_test_report();
}
