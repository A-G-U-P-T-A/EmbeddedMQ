#include "emq_test.h"
#include "emq/emq.h"

#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_subscription *independent = NULL;
  emq_subscription *g1 = NULL;
  emq_subscription *g2 = NULL;
  emq_subscription *retry = NULL;
  emq_subscription *dlq = NULL;
  emq_message message;
  uint64_t first_id;

  EMQ_CHECK_EQ(emq_runtime_create(&rt), EMQ_OK);
  EMQ_CHECK_EQ(emq_subscribe(rt, "orders.#", NULL, &independent), EMQ_OK);
  EMQ_CHECK_EQ(emq_subscribe(rt, "orders.#", "workers", &g1), EMQ_OK);
  EMQ_CHECK_EQ(emq_subscribe(rt, "orders.#", "workers", &g2), EMQ_OK);
  EMQ_CHECK_EQ(emq_subscribe(rt, "retry.#", NULL, &retry), EMQ_OK);
  EMQ_CHECK_EQ(emq_subscribe(rt, "dlq.#", NULL, &dlq), EMQ_OK);

  EMQ_CHECK_EQ(emq_publish(rt, "orders.created", "one", 3), EMQ_OK);
  EMQ_CHECK_EQ(emq_publish(rt, "orders.created", "two", 3), EMQ_OK);

  EMQ_CHECK_EQ(emq_sub_next(independent, &message, 0), EMQ_OK);
  first_id = message.id;
  EMQ_CHECK(message.size == 3 && memcmp(message.data, "one", 3) == 0);
  emq_message_release(&message);

  /* Group members compete for one shared stream, rather than each receiving
   * duplicate payloads. */
  EMQ_CHECK_EQ(emq_sub_next(g1, &message, 0), EMQ_OK);
  EMQ_CHECK(message.size == 3 && memcmp(message.data, "one", 3) == 0);
  emq_message_release(&message);
  EMQ_CHECK_EQ(emq_sub_next(g2, &message, 0), EMQ_OK);
  EMQ_CHECK(message.size == 3 && memcmp(message.data, "two", 3) == 0);
  emq_message_release(&message);

  EMQ_CHECK_EQ(emq_sub_retry(independent, "retry.orders", first_id, 2, 0),
               EMQ_OK);
  EMQ_CHECK_EQ(emq_sub_next(retry, &message, 0), EMQ_OK);
  EMQ_CHECK((message.flags & EMQ_MSG_FLAG_RETRY) != 0);
  emq_message_release(&message);

  EMQ_CHECK_EQ(emq_sub_dead_letter(independent, "dlq.orders", first_id, 3),
               EMQ_OK);
  EMQ_CHECK_EQ(emq_sub_next(dlq, &message, 0), EMQ_OK);
  EMQ_CHECK((message.flags & EMQ_MSG_FLAG_DEAD_LETTER) != 0);
  emq_message_release(&message);

  EMQ_CHECK_EQ(emq_sub_replay(independent), EMQ_OK);
  EMQ_CHECK_EQ(emq_sub_next(independent, &message, 0), EMQ_OK);
  EMQ_CHECK(message.id == first_id);
  emq_message_release(&message);

  emq_unsubscribe(dlq);
  emq_unsubscribe(retry);
  emq_unsubscribe(g2);
  emq_unsubscribe(g1);
  emq_unsubscribe(independent);
  emq_runtime_destroy(rt);
  return emq_test_report();
}
