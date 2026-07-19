#include "emq/emq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message m;
  emq_stats st;
  int i;

  if (emq_runtime_create(&rt) != EMQ_OK) return 1;
  emq_queue_opts_default(&opts);
  if (emq_queue_create(rt, "jobs", &opts, &q) != EMQ_OK) return 1;

  for (i = 0; i < 5; ++i) {
    char buf[64];
    snprintf(buf, sizeof(buf), "job-%d", i);
    emq_push(q, buf, strlen(buf), NULL);
  }

  while (emq_pop(q, &m, 0) == EMQ_OK) {
    printf("got: %.*s\n", (int)m.size, (const char *)m.data);
    emq_ack(q, m.id);
    free((void *)m.data);
  }

  emq_queue_stats(q, &st);
  printf("enqueued=%llu dequeued=%llu\n",
         (unsigned long long)st.enqueued, (unsigned long long)st.dequeued);

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  return 0;
}
