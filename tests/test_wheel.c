#include "emq_test.h"
#include "sched/emq_wheel.h"
#include "platform/emq_platform.h"

static int fired;
static void *last_cookie;

static void on_fire(void *user, uint64_t deadline_ns, void *cookie) {
  (void)user;
  (void)deadline_ns;
  fired++;
  last_cookie = cookie;
}

int main(void) {
  emq_wheel *w = NULL;
  uint64_t now = emq_now_ns();
  EMQ_CHECK(emq_wheel_create(&w, 1, 3) == 0);
  EMQ_CHECK(emq_wheel_schedule(w, now + 5000000ULL, (void *)(uintptr_t)99) == 0);
  fired = 0;
  emq_wheel_tick(w, now + 1000000ULL, on_fire, NULL);
  EMQ_CHECK_EQ(fired, 0);
  emq_wheel_tick(w, now + 10000000ULL, on_fire, NULL);
  EMQ_CHECK(fired >= 1);
  EMQ_CHECK(last_cookie == (void *)(uintptr_t)99);
  emq_wheel_destroy(w);
  return emq_test_report();
}
