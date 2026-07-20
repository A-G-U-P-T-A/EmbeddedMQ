#include "emq/emq.h"
#include "emq_testsupport.h"
#include "platform/emq_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <signal.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

static const char *k_crash_points[] = {
    "log_append_pre",     "log_sync_pre",       "log_sync_post",
    "log_meta_write",     "log_blob_write",     "log_snapshot",
    "log_compact",        "log_trim_front",     "log_segment_rotate",
};

static const int k_fsync_policies[] = {
    EMQ_FSYNC_NONE,
    EMQ_FSYNC_EVERY_WRITE,
    EMQ_FSYNC_INTERVAL,
};

static void writer_exe_path(char *out, size_t out_size, const char *argv0) {
#if defined(_WIN32)
  char self[MAX_PATH];
  char *slash;
  (void)argv0;
  GetModuleFileNameA(NULL, self, (DWORD)sizeof(self));
  slash = strrchr(self, '\\');
  if (slash) *(slash + 1) = '\0';
  snprintf(out, out_size, "%srecovery_writer.exe", self);
#else
  const char *slash = strrchr(argv0 ? argv0 : "", '/');
  if (slash && slash > argv0) {
    size_t dir_len = (size_t)(slash - argv0 + 1);
    snprintf(out, out_size, "%.*srecovery_writer", (int)dir_len, argv0);
  } else {
    snprintf(out, out_size, "./recovery_writer");
  }
#endif
}

#if defined(_WIN32)
typedef struct writer_proc {
  PROCESS_INFORMATION pi;
  int started;
} writer_proc;

static int spawn_writer(writer_proc *wp, const char *writer, const char *path,
                        uint32_t payload, uint64_t seed, int fsync_policy,
                        const char *crash_at) {
  char cmd[1024];
  char crash_env[128];
  STARTUPINFOA si;
  memset(wp, 0, sizeof(*wp));
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" %u %llu %d", writer, path,
           (unsigned)payload, (unsigned long long)seed, fsync_policy);
  if (crash_at) {
    snprintf(crash_env, sizeof(crash_env), "%s:1", crash_at);
    SetEnvironmentVariableA("EMQ_CRASH_AT", crash_env);
  }
  if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si,
                      &wp->pi)) {
    if (crash_at) SetEnvironmentVariableA("EMQ_CRASH_AT", NULL);
    return -1;
  }
  if (crash_at) SetEnvironmentVariableA("EMQ_CRASH_AT", NULL);
  wp->started = 1;
  return 0;
}

static void kill_writer(writer_proc *wp) {
  if (!wp->started) return;
  TerminateProcess(wp->pi.hProcess, 137);
  WaitForSingleObject(wp->pi.hProcess, 5000);
  CloseHandle(wp->pi.hThread);
  CloseHandle(wp->pi.hProcess);
  wp->started = 0;
}

static int wait_writer(writer_proc *wp, unsigned *exit_code_out) {
  DWORD ec = 0;
  if (!wp->started) return -1;
  WaitForSingleObject(wp->pi.hProcess, INFINITE);
  if (!GetExitCodeProcess(wp->pi.hProcess, &ec)) {
    CloseHandle(wp->pi.hThread);
    CloseHandle(wp->pi.hProcess);
    wp->started = 0;
    return -1;
  }
  CloseHandle(wp->pi.hThread);
  CloseHandle(wp->pi.hProcess);
  wp->started = 0;
  if (exit_code_out) *exit_code_out = (unsigned)ec;
  return 0;
}
#else
typedef struct writer_proc {
  pid_t pid;
  int started;
} writer_proc;

static int spawn_writer(writer_proc *wp, const char *writer, const char *path,
                        uint32_t payload, uint64_t seed, int fsync_policy,
                        const char *crash_at) {
  wp->pid = fork();
  if (wp->pid < 0) return -1;
  if (wp->pid == 0) {
    char ps[32], ss[32], fs[8];
    snprintf(ps, sizeof(ps), "%u", (unsigned)payload);
    snprintf(ss, sizeof(ss), "%llu", (unsigned long long)seed);
    snprintf(fs, sizeof(fs), "%d", fsync_policy);
    if (crash_at) {
      char crash_env[128];
      snprintf(crash_env, sizeof(crash_env), "%s:1", crash_at);
      setenv("EMQ_CRASH_AT", crash_env, 1);
    }
    execl(writer, writer, path, ps, ss, fs, (char *)NULL);
    _exit(127);
  }
  if (crash_at) unsetenv("EMQ_CRASH_AT");
  wp->started = 1;
  return 0;
}

static void kill_writer(writer_proc *wp) {
  if (!wp->started) return;
  kill(wp->pid, SIGKILL);
  (void)waitpid(wp->pid, NULL, 0);
  wp->started = 0;
}

static int wait_writer(writer_proc *wp, unsigned *exit_code_out) {
  int status = 0;
  if (!wp->started) return -1;
  if (waitpid(wp->pid, &status, 0) < 0) {
    wp->started = 0;
    return -1;
  }
  wp->started = 0;
  if (exit_code_out) {
    if (WIFEXITED(status))
      *exit_code_out = (unsigned)WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      *exit_code_out = (unsigned)(128 + WTERMSIG(status));
    else
      *exit_code_out = (unsigned)-1;
  }
  return 0;
}
#endif

static uint64_t read_hwm(const char *path) {
  char hwm_path[512];
  FILE *f;
  uint64_t hwm = 0;
  snprintf(hwm_path, sizeof(hwm_path), "%s/hwm.txt", path);
  f = fopen(hwm_path, "r");
  if (!f) return 0;
  if (fscanf(f, "%llu", (unsigned long long *)&hwm) != 1) hwm = 0;
  fclose(f);
  return hwm;
}

static int verify_queue(const char *path, uint64_t *recovered_out) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message msg;
  uint64_t expect = 0;
  uint64_t recovered = 0;

  EMQ_REQUIRE(emq_runtime_create(&rt) == EMQ_OK);
  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_DURABLE;
  opts.path = path;
  opts.fsync = EMQ_FSYNC_EVERY_WRITE;
  opts.delivery = EMQ_AT_MOST_ONCE;
  EMQ_REQUIRE(emq_queue_create(rt, "recovery_q", &opts, &q) == EMQ_OK);

  for (;;) {
    emq_status st = emq_try_pop(q, &msg);
    uint64_t seq;
    if (st == EMQ_ERR_EMPTY) break;
    if (st != EMQ_OK) break;
    /* Crash may tear the last record; keep the contiguous verified prefix. */
    if (emq_payload_check(msg.data, msg.size, &seq, NULL) != 0) {
      emq_message_release(&msg);
      break;
    }
    if (seq != expect) {
      emq_message_release(&msg);
      break;
    }
    emq_message_release(&msg);
    ++expect;
    ++recovered;
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  if (recovered_out) *recovered_out = recovered;
  return 0;
}

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

static int parse_crashpoint_flag(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--crashpoint") == 0) {
      if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
        return atoi(argv[++i]);
      return 1;
    }
  }
  {
    const char *env = getenv("EMQ_RECOVERY_CRASHPOINT");
    if (env && env[0] && strcmp(env, "0") != 0) return 1;
  }
  return 0;
}

static int run_cycle(emq_rng *rng, const char *writer, uint32_t cycle) {
  char path[256];
  writer_proc wp;
  uint64_t recovered = 0;
  uint32_t sleep_ms;

  snprintf(path, sizeof(path), "recovery_tmp_%u", (unsigned)cycle);
  wipe_dir(path);
  (void)emq_mkdir_p(path);

  EMQ_REQUIRE(spawn_writer(&wp, writer, path, 64,
                           (uint64_t)emq_rng_u64(rng), EMQ_FSYNC_EVERY_WRITE,
                           NULL) == 0);
  sleep_ms = 20u + (uint32_t)emq_rng_bounded(rng, 131);
  emq_sleep_ms(sleep_ms);
  kill_writer(&wp);

  verify_queue(path, &recovered);
  EMQ_REQUIRE(recovered >= 0);
  return 0;
}

static int run_crashpoint_case(const char *writer, const char *crash_at,
                               int fsync_policy, uint32_t case_id,
                               uint64_t seed) {
  char path[256];
  writer_proc wp;
  uint64_t recovered = 0;
  unsigned exit_code = 0;

  snprintf(path, sizeof(path), "recovery_cp_%u", (unsigned)case_id);
  wipe_dir(path);
  (void)emq_mkdir_p(path);

  EMQ_REQUIRE(spawn_writer(&wp, writer, path, 64, seed, fsync_policy,
                           crash_at) == 0);
  EMQ_REQUIRE(wait_writer(&wp, &exit_code) == 0);
  EMQ_REQUIRE(exit_code == 137u);

  verify_queue(path, &recovered);
  if (fsync_policy == EMQ_FSYNC_EVERY_WRITE) {
    uint64_t hwm = read_hwm(path);
    EMQ_REQUIRE(recovered >= hwm);
  }

  printf("  OK crash=%s fsync=%d recovered=%llu\n", crash_at, fsync_policy,
         (unsigned long long)recovered);
  return 0;
}

static int run_crashpoint_matrix(const char *writer, int quick, uint64_t seed) {
  size_t n_points =
      quick ? 3u : (sizeof(k_crash_points) / sizeof(k_crash_points[0]));
  size_t n_fsync =
      quick ? 1u : (sizeof(k_fsync_policies) / sizeof(k_fsync_policies[0]));
  size_t pi, fi;
  uint32_t case_id = 0;

  for (pi = 0; pi < n_points; ++pi) {
    for (fi = 0; fi < n_fsync; ++fi) {
      int fsync = quick ? EMQ_FSYNC_EVERY_WRITE : k_fsync_policies[fi];
      if (run_crashpoint_case(writer, k_crash_points[pi], fsync, case_id,
                              seed + case_id) != 0) {
        fprintf(stderr, "FAIL crashpoint %s fsync=%d\n", k_crash_points[pi],
                fsync);
        return -1;
      }
      ++case_id;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  emq_cli cli;
  emq_rng rng;
  char writer[512];
  uint32_t i;
  int failed = 0;
  int crashpoint = 0;

  emq_cli_defaults(&cli);
  if (emq_cli_parse(&cli, argc, argv) != 0) {
    fprintf(stderr,
            "usage: recovery_supervisor [--quick] [--cycles N] [--seed S] "
            "[--crashpoint [N]]\n");
    return 2;
  }
  crashpoint = parse_crashpoint_flag(argc, argv);
  if (cli.quick) cli.cycles = 5;

  emq_rng_seed(&rng, emq_cli_seed_or_time(&cli));
  writer_exe_path(writer, sizeof(writer), argc > 0 ? argv[0] : NULL);

  if (crashpoint) {
    printf("recovery_supervisor crashpoint matrix seed=%llu quick=%d writer=%s\n",
           (unsigned long long)rng.s[0], cli.quick, writer);
    if (run_crashpoint_matrix(writer, cli.quick, rng.s[0]) != 0) {
      printf("FAIL recovery_supervisor crashpoint matrix\n");
      return 1;
    }
    printf("PASS recovery_supervisor crashpoint matrix\n");
    return 0;
  }

  printf("recovery_supervisor seed=%llu cycles=%u writer=%s\n",
         (unsigned long long)rng.s[0], cli.cycles, writer);

  for (i = 0; i < cli.cycles; ++i) {
    if (run_cycle(&rng, writer, i) != 0) {
      fprintf(stderr, "FAIL cycle %u\n", (unsigned)i);
      failed = 1;
      break;
    }
  }

  if (failed) {
    printf("FAIL recovery_supervisor\n");
    return 1;
  }
  printf("PASS recovery_supervisor (%u cycles)\n", cli.cycles);
  return 0;
}
