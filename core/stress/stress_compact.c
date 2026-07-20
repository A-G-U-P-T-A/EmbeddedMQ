/*
 * Compact-under-load: durable FIFO with producer + consumer while
 * emq_queue_compact / emq_queue_snapshot run periodically.
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

typedef struct compact_ctx {
  emq_queue *q;
  uint64_t ops_target;
  volatile uint64_t produced;
  volatile uint64_t consumed;
  volatile int stop;
  size_t payload_len;
  emq_rng *rng;
  emq_watchdog *wd;
} compact_ctx;

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

static void producer_main(void *arg) {
  compact_ctx *ctx = (compact_ctx *)arg;
  uint8_t *buf;
  uint64_t seq;

  buf = (uint8_t *)malloc(ctx->payload_len);
  EMQ_REQUIRE(buf != NULL);

  for (seq = 0; seq < ctx->ops_target; ++seq) {
    EMQ_REQUIRE(emq_payload_fill(buf, ctx->payload_len, seq, 0) == 0);
    while (emq_push(ctx->q, buf, ctx->payload_len, NULL) != EMQ_OK) {
      emq_sleep_ms(0);
    }
    ctx->produced = seq + 1;
    if (((seq + 1) % 1000u) == 0) {
      emq_watchdog_heartbeat(ctx->wd);
    }
  }
  free(buf);
}

static void consumer_main(void *arg) {
  compact_ctx *ctx = (compact_ctx *)arg;
  emq_message m;
  uint64_t expected = 0;

  memset(&m, 0, sizeof(m));
  while (ctx->consumed < ctx->ops_target) {
    if (emq_rng_u32(ctx->rng) % 500u == 0u) {
      (void)emq_queue_snapshot(ctx->q);
      (void)emq_queue_compact(ctx->q);
      emq_watchdog_heartbeat(ctx->wd);
    }

    if (emq_pop(ctx->q, &m, 50) == EMQ_OK) {
      uint64_t seq;
      uint32_t producer;
      EMQ_REQUIRE(emq_payload_check((const uint8_t *)m.data, m.size, &seq,
                                  &producer) == 0);
      EMQ_REQUIRE(seq == expected);
      EMQ_REQUIRE(producer == 0);
      expected++;
      ctx->consumed = expected;
      EMQ_REQUIRE(emq_ack(ctx->q, m.id) == EMQ_OK);
      emq_message_release(&m);
    }
  }
}

int main(int argc, char **argv) {
  emq_cli cfg;
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  compact_ctx ctx;
  emq_thread *prod = NULL;
  emq_thread *cons = NULL;
  emq_watchdog *wd = NULL;
  emq_rng rng;
  uint64_t seed;
  char path[256];

  emq_cli_defaults(&cfg);
  if (emq_cli_parse(&cfg, argc, argv) == 1) return 0;

  if (cfg.quick) {
    cfg.ops = 5000;
  } else if (cfg.ops == 100000) {
    cfg.ops = 500000;
  }
  if (cfg.payload < EMQ_PAYLOAD_HDR) {
    cfg.payload = 64;
  }

  seed = emq_cli_seed_or_time(&cfg);
  emq_rng_seed(&rng, seed);
  printf("stress_compact seed=%llu ops=%llu payload=%u quick=%d\n",
         (unsigned long long)seed, (unsigned long long)cfg.ops, cfg.payload,
         cfg.quick);

  if (cfg.path) {
    strncpy(path, cfg.path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    EMQ_REQUIRE(emq_mkdir_p(path) == 0);
  } else {
    stress_make_tmpdir(path, sizeof(path), "compact");
  }

  memset(&ctx, 0, sizeof(ctx));
  ctx.ops_target = cfg.ops;
  ctx.payload_len = cfg.payload;
  ctx.rng = &rng;

  wd = emq_watchdog_start(30, "stress_compact");
  EMQ_REQUIRE(wd != NULL);
  ctx.wd = wd;

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.policy = EMQ_POLICY_FIFO;
  opts.path = path;
  opts.producers = 1;
  opts.consumers = 1;
  opts.backpressure = EMQ_BP_MODE_BLOCK;
  opts.capacity = 8192;
  opts.fsync = EMQ_FSYNC_INTERVAL;
  EMQ_REQUIRE(emq_queue_create(rt, "compact", &opts, &q) == EMQ_OK);
  ctx.q = q;

  EMQ_REQUIRE(emq_thread_create(&prod, producer_main, &ctx) == 0);
  EMQ_REQUIRE(emq_thread_create(&cons, consumer_main, &ctx) == 0);
  emq_thread_join(prod);
  emq_thread_destroy(prod);
  emq_thread_join(cons);
  emq_thread_destroy(cons);

  EMQ_REQUIRE(ctx.produced == cfg.ops);
  EMQ_REQUIRE(ctx.consumed == cfg.ops);

  emq_watchdog_stop(wd);
  emq_queue_close(q);
  emq_runtime_destroy(rt);

  if (!cfg.path) {
    stress_rmtree(path);
  }

  printf("stress_compact ok produced=%llu consumed=%llu\n",
         (unsigned long long)ctx.produced, (unsigned long long)ctx.consumed);
  return 0;
}
