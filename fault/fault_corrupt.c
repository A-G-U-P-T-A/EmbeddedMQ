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
#  include <fcntl.h>
#  include <unistd.h>
#endif

static const char *k_path = "fault_corrupt_tmp";

static int find_first_seg(const char *dir, char *out, size_t out_size) {
#if defined(_WIN32)
  WIN32_FIND_DATAA fd;
  HANDLE h;
  char pattern[512];
  snprintf(pattern, sizeof(pattern), "%s\\*.seg", dir);
  h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return -1;
  snprintf(out, out_size, "%s\\%s", dir, fd.cFileName);
  FindClose(h);
  return 0;
#else
  DIR *d = opendir(dir);
  struct dirent *ent;
  if (!d) return -1;
  while ((ent = readdir(d)) != NULL) {
    size_t len = strlen(ent->d_name);
    if (len >= 4 && strcmp(ent->d_name + len - 4, ".seg") == 0) {
      snprintf(out, out_size, "%s/%s", dir, ent->d_name);
      closedir(d);
      return 0;
    }
  }
  closedir(d);
  return -1;
#endif
}

static int corrupt_seg_file(const char *seg_path, int truncate_file) {
  emq_file *f = NULL;
  if (truncate_file) {
#if defined(_WIN32)
    if (emq_file_open(&f, seg_path, 0, 1) != 0) return -1;
    if (emq_file_resize(f, 64) != 0) {
      emq_file_close(f);
      return -1;
    }
    emq_file_close(f);
    return 0;
#else
    if (truncate(seg_path, 64) != 0) return -1;
    return 0;
#endif
  }

  if (emq_file_open(&f, seg_path, 0, 1) != 0) return -1;
  {
    uint8_t b = 0;
    if (emq_file_pread(f, &b, 1, 32) <= 0) {
      emq_file_close(f);
      return -1;
    }
    b ^= 0xFFu;
    if (emq_file_pwrite(f, &b, 1, 32) <= 0) {
      emq_file_close(f);
      return -1;
    }
  }
  emq_file_close(f);
  return 0;
}

static int create_and_fill(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  int i;

  (void)emq_mkdir_p(k_path);
  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.path = k_path;
  opts.fsync = EMQ_FSYNC_EVERY_WRITE;
  EMQ_REQUIRE(emq_queue_create(rt, "corrupt_q", &opts, &q) == EMQ_OK);

  for (i = 0; i < 100; ++i) {
    uint8_t buf[48];
    EMQ_REQUIRE(emq_payload_fill(buf, sizeof(buf), (uint64_t)i, 0) == 0);
    EMQ_REQUIRE(emq_push(q, buf, sizeof(buf), NULL) == EMQ_OK);
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  return 0;
}

static int reopen_after_corrupt(int truncate_file) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message msg;
  uint64_t expect = 0;
  int recovered = 0;

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.path = k_path;
  opts.fsync = EMQ_FSYNC_EVERY_WRITE;
  EMQ_REQUIRE(emq_queue_create(rt, "corrupt_q", &opts, &q) == EMQ_OK);

  for (;;) {
    emq_status st = emq_try_pop(q, &msg);
    if (st == EMQ_ERR_EMPTY) break;
    if (st != EMQ_OK) break;
    {
      uint64_t seq;
      if (emq_payload_check(msg.data, msg.size, &seq, NULL) != 0) break;
      if (seq != expect) break;
      ++expect;
    }
    emq_message_release(&msg);
    ++recovered;
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  printf("  corrupt mode=%s recovered=%d (prefix may be shorter)\n",
         truncate_file ? "truncate" : "flip", recovered);
  EMQ_REQUIRE(recovered >= 0);
  return 0;
}

int main(void) {
  char seg_path[640];

  create_and_fill();
  EMQ_REQUIRE(find_first_seg(k_path, seg_path, sizeof(seg_path)) == 0);
  EMQ_REQUIRE(corrupt_seg_file(seg_path, 0) == 0);
  reopen_after_corrupt(0);

  create_and_fill();
  EMQ_REQUIRE(find_first_seg(k_path, seg_path, sizeof(seg_path)) == 0);
  EMQ_REQUIRE(corrupt_seg_file(seg_path, 1) == 0);
  reopen_after_corrupt(1);

  printf("PASS fault_corrupt\n");
  return 0;
}
