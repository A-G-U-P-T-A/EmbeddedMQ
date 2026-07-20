#ifndef EMQ_TEST_H
#define EMQ_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int emq_test_failures = 0;
static int emq_test_checks = 0;

#define EMQ_CHECK(cond) do { \
  emq_test_checks++; \
  if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    emq_test_failures++; \
  } \
} while (0)

#define EMQ_CHECK_EQ(a, b) EMQ_CHECK((a) == (b))
#define EMQ_CHECK_STREQ(a, b) EMQ_CHECK(strcmp((a), (b)) == 0)

static int emq_test_report(void) {
  if (emq_test_failures == 0) {
    printf("OK (%d checks)\n", emq_test_checks);
    return 0;
  }
  printf("FAILED %d/%d checks\n", emq_test_failures, emq_test_checks);
  return 1;
}

#endif /* EMQ_TEST_H */
