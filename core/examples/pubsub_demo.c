#include "emq/emq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_subscription *sub = NULL;
  emq_message m;

  if (emq_runtime_create(&rt) != EMQ_OK) return 1;
  if (emq_subscribe(rt, "sensor/#", NULL, &sub) != EMQ_OK) return 1;

  emq_publish(rt, "sensor.temp.kitchen", "22.5", 4);
  emq_publish(rt, "sensor.humidity.lab", "40", 2);
  emq_publish(rt, "other.topic", "nope", 4);

  while (emq_sub_next(sub, &m, 10) == EMQ_OK) {
    printf("event: %.*s\n", (int)m.size, (const char *)m.data);
    emq_message_release(&m);
  }

  emq_unsubscribe(sub);
  emq_runtime_destroy(rt);
  return 0;
}
