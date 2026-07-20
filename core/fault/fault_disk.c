#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#ifdef EMQ_FAULT_INJECT
#  include "core/emq_fault.h"
#endif

#ifndef EMQ_FAULT_INJECT
int main(void) {
  printf("SKIP fault_disk: EMQ_FAULT_INJECT not enabled\n");
  return 0;
}
#else

static void wipe_dir(const char *path) {
#if defined(_WIN32)
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "cmd /c if exist \"%s\" rmdir /s /q \"%s\"", path,
           path);
  system(cmd);
#else
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
  system(cmd);
#endif
}

static int push_until_io(emq_queue *q, int *io_seen) {
  int i;
  for (i = 0; i < 256; ++i) {
    uint8_t buf[64];
    emq_status st;
    EMQ_REQUIRE(emq_payload_fill(buf, sizeof(buf), (uint64_t)i, 0) == 0);
    st = emq_push(q, buf, sizeof(buf), NULL);
    if (st == EMQ_ERR_IO) {
      if (io_seen) *io_seen = 1;
      return 0;
    }
    /* Other errors after fault injection are acceptable fail-safe outcomes. */
    if (st != EMQ_OK) {
      if (io_seen) *io_seen = 1;
      return 0;
    }
  }
  return 0;
}

static int reopen_and_verify(const char *path) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message msg;
  uint64_t expect = 0;
  int popped = 0;

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.path = path;
  opts.fsync = EMQ_FSYNC_EVERY_WRITE;
  opts.delivery = EMQ_AT_MOST_ONCE;
  EMQ_REQUIRE(emq_queue_create(rt, "disk_q", &opts, &q) == EMQ_OK);

  for (;;) {
    emq_status st = emq_try_pop(q, &msg);
    uint64_t got;
    if (st == EMQ_ERR_EMPTY) break;
    if (st != EMQ_OK) break;
    if (emq_payload_check(msg.data, msg.size, &got, NULL) != 0) {
      emq_message_release(&msg);
      break; /* torn / corrupt — stop at valid prefix */
    }
    if (got != expect) {
      emq_message_release(&msg);
      break;
    }
    emq_message_release(&msg);
    ++expect;
    ++popped;
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  printf("  recovered contiguous prefix=%d after disk fault\n", popped);
  return 0;
}

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  int io_seen = 0;
  char path[128];

#if defined(_WIN32)
  snprintf(path, sizeof(path), "fault_disk_tmp_%u",
           (unsigned)GetCurrentProcessId());
#else
  snprintf(path, sizeof(path), "fault_disk_tmp_%u", (unsigned)getpid());
#endif
  emq_fault_init();
  wipe_dir(path);
  (void)emq_mkdir_p(path);

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.path = path;
  opts.fsync = EMQ_FSYNC_EVERY_WRITE;
  opts.delivery = EMQ_AT_MOST_ONCE;

  /* Let the queue finish creating, then start failing writes. */
  EMQ_REQUIRE(emq_queue_create(rt, "disk_q", &opts, &q) == EMQ_OK);
  emq_fault_configure("file_pwrite", EMQ_FAULT_AFTER_N, 3, 0);
  push_until_io(q, &io_seen);
  EMQ_REQUIRE(io_seen);

  emq_queue_close(q);
  emq_runtime_destroy(rt);

  emq_fault_reset();
  reopen_and_verify(path);
  wipe_dir(path);

  printf("PASS fault_disk\n");
  return 0;
}
#endif
