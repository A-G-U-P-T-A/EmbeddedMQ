#include "emq_testsupport.h"
#include "core/emq_log.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_dir = "fuzz_log_tmp";

static void write_random_seg(const char *dir, emq_rng *rng, uint32_t idx) {
  char path[512];
  emq_file *f = NULL;
  uint32_t size = 64u + (uint32_t)emq_rng_bounded(rng, 8192);
  uint8_t *buf = (uint8_t *)malloc(size);
  uint32_t i;

  EMQ_REQUIRE(buf != NULL);
  for (i = 0; i < size; ++i) buf[i] = (uint8_t)emq_rng_u32(rng);

  if (idx & 1u) {
    snprintf(path, sizeof(path), "%s/log.seg", dir);
  } else {
    snprintf(path, sizeof(path), "%s/log.%08u.seg", dir, (unsigned)idx);
  }

  EMQ_REQUIRE(emq_file_open(&f, path, 1, 1) == 0);
  if (size > 0) {
    EMQ_REQUIRE(emq_file_pwrite(f, buf, size, 0) > 0);
  }
  emq_file_close(f);
  free(buf);
}

static void exercise_log_dir(const char *dir, emq_rng *rng) {
  emq_log *log = NULL;
  int rc;
  uint64_t off;
  uint64_t count;
  uint64_t max_read;

  rc = emq_log_open(&log, EMQ_STORAGE_DURABLE, dir, 256, EMQ_FSYNC_NONE);
  if (rc != 0) {
    /* Garbage input must not crash; open may legitimately fail. */
    return;
  }

  count = emq_log_count(log);
  max_read = emq_log_next_offset(log);
  if (count > 64) count = 64;
  if (max_read > 64) max_read = 64;

  for (off = 0; off <= max_read; ++off) {
    emq_log_entry entry;
    memset(&entry, 0, sizeof(entry));
    (void)emq_log_read(log, off, &entry);
    if (entry.payload_owned && entry.payload) free(entry.payload);
  }

  (void)rng;
  (void)count;
  emq_log_close(log);
}

int main(int argc, char **argv) {
  emq_cli cli;
  emq_rng rng;
  uint32_t files;
  uint32_t i;

  emq_cli_defaults(&cli);
  if (emq_cli_parse(&cli, argc, argv) != 0) {
    fprintf(stderr, "usage: fuzz_log [--quick] [--iters N] [--seed S]\n");
    return 2;
  }

  files = cli.quick ? 200u : (uint32_t)(cli.ops > UINT32_MAX ? UINT32_MAX : cli.ops);
  if (cli.quick) cli.ops = files;

  emq_rng_seed(&rng, emq_cli_seed_or_time(&cli));
  (void)emq_mkdir_p(k_dir);
  printf("fuzz_log seed=%llu files=%u\n", (unsigned long long)rng.s[0], files);

  for (i = 0; i < files; ++i) {
    char sub[512];
    snprintf(sub, sizeof(sub), "%s/case_%u", k_dir, (unsigned)i);
    (void)emq_mkdir_p(sub);
    write_random_seg(sub, &rng, i);
    exercise_log_dir(sub, &rng);
  }

  printf("PASS fuzz_log\n");
  return 0;
}
