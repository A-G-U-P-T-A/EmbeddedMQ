#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_hwm(const char *path, uint64_t count) {
  char hwm_path[512];
  FILE *f;
  snprintf(hwm_path, sizeof(hwm_path), "%s/hwm.txt", path);
  f = fopen(hwm_path, "w");
  if (!f) return;
  fprintf(f, "%llu\n", (unsigned long long)count);
  fclose(f);
}

int main(int argc, char **argv) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_rng rng;
  uint64_t seq = 0;
  const char *path;
  uint32_t payload_size;
  uint64_t seed;
  int fsync_policy = EMQ_FSYNC_EVERY_WRITE;

  if (argc < 4) {
    fprintf(stderr, "usage: recovery_writer <path> <payload> <seed> [fsync]\n");
    return 2;
  }

  path = argv[1];
  payload_size = (uint32_t)atoi(argv[2]);
  seed = (uint64_t)strtoull(argv[3], NULL, 10);
  if (argc >= 5) fsync_policy = atoi(argv[4]);
  if (fsync_policy < EMQ_FSYNC_NONE || fsync_policy > EMQ_FSYNC_INTERVAL)
    fsync_policy = EMQ_FSYNC_EVERY_WRITE;
  if (payload_size < EMQ_PAYLOAD_HDR) payload_size = EMQ_PAYLOAD_HDR;
  if (payload_size > 4096) payload_size = 4096;

  emq_rng_seed(&rng, seed);
  (void)emq_mkdir_p(path);

  if (emq_runtime_create(&rt) != EMQ_OK) return 1;
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.path = path;
  opts.fsync = (emq_fsync_policy)fsync_policy;
  opts.delivery = EMQ_AT_MOST_ONCE;
  if (emq_queue_create(rt, "recovery_q", &opts, &q) != EMQ_OK) {
    emq_runtime_destroy(rt);
    return 1;
  }

  for (;;) {
    uint8_t *buf = (uint8_t *)malloc(payload_size);
    if (!buf) break;
    EMQ_REQUIRE(emq_payload_fill(buf, payload_size, seq, 0) == 0);
    if (emq_push(q, buf, payload_size, NULL) != EMQ_OK) {
      free(buf);
      break;
    }
    free(buf);
    if (emq_flush(q) != EMQ_OK) break;
    ++seq;
    write_hwm(path, seq);
    if ((seq & 7u) == 0u) {
      emq_sleep_ms(1 + (uint32_t)(emq_rng_bounded(&rng, 3)));
    }
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  return 0;
}
