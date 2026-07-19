#include "emq_test.h"
#include "emq/emq.h"
#include "emq_testsupport.h"
#include "core/emq_crc.h"
#include "core/emq_log.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#define EMQ_ACCESS _access
#define EMQ_R_OK 04
#else
#include <sys/stat.h>
#include <unistd.h>
#define EMQ_ACCESS access
#define EMQ_R_OK R_OK
#endif

#define EMQ_META_MAGIC 0x454D514Du /* 'EMQM' */
#define EMQ_META_VERSION 1u

#pragma pack(push, 1)
typedef struct emq_meta_disk {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint64_t sequence;
  uint32_t storage_generation;
  uint32_t blob_threshold;
  uint64_t disk_base_offset;
  uint64_t trim_offset;
  uint64_t next_offset;
  uint32_t crc32;
  uint32_t reserved;
} emq_meta_disk;
#pragma pack(pop)

static void fixture_src_path(char *out, size_t out_sz) {
  const char *file = __FILE__;
  const char *slash = strrchr(file, '/');
  if (!slash) slash = strrchr(file, '\\');
  if (slash) {
    snprintf(out, out_sz, "%.*s/fixtures/log_v1", (int)(slash - file), file);
  } else {
    snprintf(out, out_sz, "tests/fixtures/log_v1");
  }
}

static int path_is_dir(const char *path) {
#if defined(_WIN32)
  struct _stat st;
  if (_stat(path, &st) != 0) return 0;
  return (st.st_mode & _S_IFDIR) != 0;
#else
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode);
#endif
}

static int fixture_ready(const char *path) {
  char probe[640];
  if (!path_is_dir(path)) return 0;
  snprintf(probe, sizeof(probe), "%s/log.meta", path);
  if (EMQ_ACCESS(probe, EMQ_R_OK) == 0) return 1;
  snprintf(probe, sizeof(probe), "%s/log.seg", path);
  return EMQ_ACCESS(probe, EMQ_R_OK) == 0;
}

static int copy_tree(const char *src, const char *dst) {
  char cmd[1024];
#if defined(_WIN32)
  snprintf(cmd, sizeof(cmd), "xcopy /E /I /Y /Q \"%s\" \"%s\\\"", src, dst);
#else
  snprintf(cmd, sizeof(cmd), "cp -r \"%s\" \"%s\"", src, dst);
#endif
  return system(cmd) == 0 ? 0 : -1;
}

static void remove_tree(const char *path) {
  char cmd[768];
#if defined(_WIN32)
  snprintf(cmd, sizeof(cmd), "rmdir /S /Q \"%s\" 2>nul", path);
#else
  snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
#endif
  (void)system(cmd);
}

static void meta_set_crc(emq_meta_disk *meta) {
  meta->crc32 = 0;
  meta->crc32 = emq_crc32(meta, sizeof(*meta));
}

static int write_future_meta(const char *dir) {
  char meta_path[640];
  emq_meta_disk meta;
  emq_file *f = NULL;

  memset(&meta, 0, sizeof(meta));
  meta.magic = EMQ_META_MAGIC;
  meta.version = (uint16_t)(EMQ_META_VERSION + 1u);
  meta.size = (uint16_t)sizeof(meta);
  meta.sequence = 1u;
  meta.storage_generation = 1u;
  meta.blob_threshold = 8192u;
  meta_set_crc(&meta);

  snprintf(meta_path, sizeof(meta_path), "%s/log.meta", dir);
  if (emq_file_open(&f, meta_path, 1, 1) != 0) return -1;
  if (emq_file_pwrite(f, &meta, sizeof(meta), 0) <= 0) {
    emq_file_close(f);
    return -1;
  }
  emq_file_close(f);
  return 0;
}

static int test_backward_compat(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message msg;
  char fixture[512];
  char temp[512];
  uint64_t expect = 0;
  int got = 0;

  fixture_src_path(fixture, sizeof(fixture));
  if (!fixture_ready(fixture)) {
    printf("SKIP compat fixture missing (run gen_fixture)\n");
    return 0;
  }

  snprintf(temp, sizeof(temp), "build/compat-%llu",
           (unsigned long long)emq_now_ns());
  remove_tree(temp);
  if (copy_tree(fixture, temp) != 0) {
    fprintf(stderr, "FAIL copy fixture -> temp\n");
    return 1;
  }

  if (emq_runtime_create(&rt) != EMQ_OK) goto fail;
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.path = temp;
  opts.fsync = EMQ_FSYNC_EVERY_WRITE;
  opts.delivery = EMQ_AT_MOST_ONCE;
  if (emq_queue_create(rt, "compat_q", &opts, &q) != EMQ_OK) goto fail;

  for (;;) {
    uint64_t seq = 0;
    if (emq_pop(q, &msg, 0) != EMQ_OK) break;
    EMQ_CHECK(msg.size >= EMQ_PAYLOAD_HDR);
    EMQ_CHECK(emq_payload_check(msg.data, msg.size, &seq, NULL) == 0);
    EMQ_CHECK(seq == expect);
    ++expect;
    ++got;
    emq_message_release(&msg);
  }

  EMQ_CHECK(got >= 1);
  EMQ_CHECK(expect == (uint64_t)got);

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  remove_tree(temp);
  return 0;

fail:
  if (q) emq_queue_close(q);
  if (rt) emq_runtime_destroy(rt);
  remove_tree(temp);
  return 1;
}

static int test_forward_compat(void) {
  emq_log *log = NULL;
  char dir[512];
  int rc;

  snprintf(dir, sizeof(dir), "build/fwd-compat-%llu",
           (unsigned long long)emq_now_ns());
  remove_tree(dir);
  if (emq_mkdir_p(dir) != 0) return 1;
  EMQ_CHECK(write_future_meta(dir) == 0);

  rc = emq_log_open(&log, EMQ_STORAGE_DURABLE, dir, 256, EMQ_FSYNC_NONE);
  /* Future meta version must fail cleanly — no crash, no silent misread. */
  EMQ_CHECK(rc != 0);
  EMQ_CHECK(log == NULL);

  remove_tree(dir);
  return 0;
}

int main(void) {
  if (test_backward_compat() != 0) return 1;
  if (test_forward_compat() != 0) return 1;
  return emq_test_report();
}
