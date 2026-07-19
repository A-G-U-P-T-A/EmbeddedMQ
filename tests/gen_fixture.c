#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void remove_tree(const char *path) {
  char cmd[768];
#if defined(_WIN32)
  snprintf(cmd, sizeof(cmd), "rmdir /S /Q \"%s\" 2>nul", path);
#else
  snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
#endif
  (void)system(cmd);
}

int main(int argc, char **argv) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  const char *path = "tests/fixtures/log_v1";
  uint8_t buf[64];
  int i;

  if (argc >= 2 && argv[1] && argv[1][0]) {
    path = argv[1];
  }

  remove_tree(path);
  if (emq_mkdir_p(path) != 0) {
    fprintf(stderr, "gen_fixture: mkdir failed: %s\n", path);
    return 1;
  }

  if (emq_runtime_create(&rt) != EMQ_OK) {
    fprintf(stderr, "gen_fixture: runtime create failed\n");
    return 1;
  }

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.path = path;
  opts.fsync = EMQ_FSYNC_EVERY_WRITE;
  opts.delivery = EMQ_AT_MOST_ONCE;

  if (emq_queue_create(rt, "fixture_q", &opts, &q) != EMQ_OK) {
    fprintf(stderr, "gen_fixture: queue create failed\n");
    emq_runtime_destroy(rt);
    return 1;
  }

  for (i = 0; i < 10; ++i) {
    EMQ_REQUIRE(emq_payload_fill(buf, sizeof(buf), (uint64_t)i, 0) == 0);
    if (emq_push(q, buf, sizeof(buf), NULL) != EMQ_OK) {
      fprintf(stderr, "gen_fixture: push %d failed\n", i);
      emq_queue_close(q);
      emq_runtime_destroy(rt);
      return 1;
    }
  }

  if (emq_flush(q) != EMQ_OK) {
    fprintf(stderr, "gen_fixture: flush failed\n");
    emq_queue_close(q);
    emq_runtime_destroy(rt);
    return 1;
  }
  if (emq_queue_snapshot(q) != EMQ_OK) {
    fprintf(stderr, "gen_fixture: snapshot failed\n");
    emq_queue_close(q);
    emq_runtime_destroy(rt);
    return 1;
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  printf("FIXTURE OK\n");
  return 0;
}
