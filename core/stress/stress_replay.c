/*
 * Replay verification: durable FIFO, push N messages, random emq_seek
 * and verify self-checking payloads.
 */

#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dirent.h>
#  include <unistd.h>
#endif

#if defined(_WIN32)
static void stress_rmtree(const char *path) {
  char pattern[512];
  WIN32_FIND_DATAA fd;
  HANDLE h;

  snprintf(pattern, sizeof(pattern), "%s\\*", path);
  h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    RemoveDirectoryA(path);
    return;
  }
  do {
    char fp[512];
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
      continue;
    }
    snprintf(fp, sizeof(fp), "%s\\%s", path, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      stress_rmtree(fp);
    } else {
      DeleteFileA(fp);
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  RemoveDirectoryA(path);
}
#else
static void stress_rmtree(const char *path) {
  DIR *d = opendir(path);
  if (!d) {
    rmdir(path);
    return;
  }
  for (;;) {
    struct dirent *ent = readdir(d);
    char fp[512];
    if (!ent) break;
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    snprintf(fp, sizeof(fp), "%s/%s", path, ent->d_name);
    stress_rmtree(fp);
  }
  closedir(d);
  rmdir(path);
}
#endif

static void stress_make_tmpdir(char *buf, size_t len, const char *tag) {
#if defined(_WIN32)
  snprintf(buf, len, "stress_tmp_%lu_%s",
           (unsigned long)GetCurrentProcessId(), tag);
#else
  snprintf(buf, len, "stress_tmp_%d_%s", (int)getpid(), tag);
#endif
  EMQ_REQUIRE(emq_mkdir_p(buf) == 0);
}

static uint64_t *stress_collect_offsets(emq_queue *q, uint64_t expected,
                                        size_t payload_len) {
  uint64_t *offsets;
  uint64_t scan;
  uint64_t found = 0;

  offsets = (uint64_t *)calloc(expected, sizeof(uint64_t));
  EMQ_REQUIRE(offsets != NULL);

  for (scan = 0; found < expected && scan < expected + 64u; ++scan) {
    emq_message m;
    uint64_t seq;
    uint32_t producer;

    EMQ_REQUIRE(emq_seek(q, scan) == EMQ_OK);
    memset(&m, 0, sizeof(m));
    if (emq_peek(q, &m) != EMQ_OK) {
      continue;
    }
    if (m.size != payload_len) {
      continue;
    }
    if (emq_payload_check((const uint8_t *)m.data, m.size, &seq,
                          &producer) != 0) {
      continue;
    }
    EMQ_REQUIRE(producer == 0);
    EMQ_REQUIRE(seq == found);
    offsets[found++] = scan;
  }
  EMQ_REQUIRE(found == expected);
  return offsets;
}

static void peek_verify_at_offset(emq_queue *q, uint64_t offset,
                                  size_t payload_len) {
  emq_message m;
  uint64_t seq;
  uint32_t producer;

  EMQ_REQUIRE(emq_seek(q, offset) == EMQ_OK);
  memset(&m, 0, sizeof(m));
  EMQ_REQUIRE(emq_peek(q, &m) == EMQ_OK);
  EMQ_REQUIRE(m.size == payload_len);
  EMQ_REQUIRE(emq_payload_check((const uint8_t *)m.data, m.size, &seq,
                                &producer) == 0);
  EMQ_REQUIRE(producer == 0);
  (void)seq;
  (void)offset;
}

int main(int argc, char **argv) {
  emq_cli cfg;
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_rng rng;
  uint64_t seed;
  char path[256];
  uint8_t *buf = NULL;
  uint64_t i;
  uint64_t trials;
  emq_stats st;
  uint64_t *offsets = NULL;

  emq_cli_defaults(&cfg);
  if (emq_cli_parse(&cfg, argc, argv) == 1) return 0;

  if (cfg.quick) {
    cfg.ops = 2000;
  } else if (cfg.ops == 100000) {
    cfg.ops = 50000;
  }
  if (cfg.payload < EMQ_PAYLOAD_HDR) {
    cfg.payload = 64;
  }

  seed = emq_cli_seed_or_time(&cfg);
  emq_rng_seed(&rng, seed);
  printf("stress_replay seed=%llu messages=%llu payload=%u quick=%d\n",
         (unsigned long long)seed, (unsigned long long)cfg.ops, cfg.payload,
         cfg.quick);

  if (cfg.path) {
    strncpy(path, cfg.path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    EMQ_REQUIRE(emq_mkdir_p(path) == 0);
  } else {
    stress_make_tmpdir(path, sizeof(path), "replay");
  }

  buf = (uint8_t *)malloc(cfg.payload);
  EMQ_REQUIRE(buf != NULL);

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.policy = EMQ_POLICY_FIFO;
  opts.delivery = EMQ_AT_MOST_ONCE;
  opts.path = path;
  opts.producers = 1;
  opts.consumers = 1;
  opts.fsync = EMQ_FSYNC_INTERVAL;
  EMQ_REQUIRE(emq_queue_create(rt, "replay", &opts, &q) == EMQ_OK);

  for (i = 0; i < cfg.ops; ++i) {
    EMQ_REQUIRE(emq_payload_fill(buf, cfg.payload, i, 0) == 0);
    EMQ_REQUIRE(emq_push(q, buf, cfg.payload, NULL) == EMQ_OK);
  }

  EMQ_REQUIRE(emq_queue_stats(q, &st) == EMQ_OK);
  EMQ_REQUIRE(st.depth == cfg.ops);

  offsets = stress_collect_offsets(q, cfg.ops, cfg.payload);

  trials = cfg.quick ? cfg.ops : cfg.ops * 2u;
  for (i = 0; i < trials; ++i) {
    uint64_t idx = emq_rng_bounded(&rng, cfg.ops);
    peek_verify_at_offset(q, offsets[idx], cfg.payload);
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  free(buf);
  free(offsets);

  if (!cfg.path) {
    stress_rmtree(path);
  }

  printf("stress_replay ok messages=%llu trials=%llu\n",
         (unsigned long long)cfg.ops, (unsigned long long)trials);
  return 0;
}
