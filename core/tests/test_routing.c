#include "emq_test.h"
#include "routing/emq_routing.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
  emq_router r;
  emq_route_sub *sub = NULL;
  emq_message m;
  uint64_t off;

  EMQ_CHECK(emq_topic_match("payments.*", "payments.created") == 1);
  EMQ_CHECK(emq_topic_match("payments.*", "payments.created.extra") == 0);
  EMQ_CHECK(emq_topic_match("sensor/#", "sensor.temp.room1") == 1);
  EMQ_CHECK(emq_topic_match("orders.eu.*", "orders.us.1") == 0);

  EMQ_CHECK(emq_router_init(&r) == 0);
  EMQ_CHECK(emq_router_subscribe(&r, "payments.*", NULL, &sub) == 0);
  EMQ_CHECK(emq_router_publish(&r, "payments.created", "x", 1, &off) == 0);
  EMQ_CHECK(emq_router_next(sub, &m) == 0);
  EMQ_CHECK(m.size == 1);
  free((void *)m.data);
  emq_router_destroy(&r);
  return emq_test_report();
}
