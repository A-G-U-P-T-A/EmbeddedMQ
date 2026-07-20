#include "emq_ipc.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
  g_checks++; \
  if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    g_failures++; \
  } \
} while (0)

int main(void) {
  emq_ipc_segment *producer = NULL;
  emq_ipc_segment *consumer = NULL;
  emq_ipc_loan loan;
  const char payload[] = "hello-ipc";
  char copy[64];

  CHECK(emq_ipc_segment_create("test_roundtrip", 65536, &producer) == EMQ_OK);
  CHECK(emq_ipc_segment_open("test_roundtrip", &consumer) == EMQ_OK);
  CHECK(emq_ipc_publish(producer, payload, sizeof(payload)) == EMQ_OK);
  CHECK(emq_ipc_claim(consumer, &loan, 1000) == EMQ_OK);
  CHECK(loan.size == sizeof(payload));
  CHECK(loan.data != NULL);
  memcpy(copy, loan.data, loan.size);
  CHECK(memcmp(copy, payload, sizeof(payload)) == 0);
  CHECK(emq_ipc_release(consumer, &loan) == EMQ_OK);
  CHECK(emq_ipc_claim(consumer, &loan, 0) == EMQ_ERR_EMPTY);

  emq_ipc_segment_destroy(consumer);
  emq_ipc_segment_destroy(producer);

  if (g_failures == 0) {
    printf("OK (%d checks)\n", g_checks);
    return 0;
  }
  printf("FAILED %d/%d checks\n", g_failures, g_checks);
  return 1;
}
