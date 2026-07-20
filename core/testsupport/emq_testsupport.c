#include "emq_testsupport.h"
#include "platform/emq_platform.h"
#include "core/emq_atomic.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#elif defined(__APPLE__)
#  include <unistd.h>
#  include <sys/resource.h>
#  include <mach/mach.h>
#else
#  include <unistd.h>
#  include <sys/resource.h>
#endif

/* ---- xoshiro256** ---- */
static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

void emq_rng_seed(emq_rng *r, uint64_t seed) {
  uint64_t z = seed + 0x9e3779b97f4a7c15ULL;
  int i;
  if (!r) return;
  for (i = 0; i < 4; ++i) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    r->s[i] = z ^ (z >> 31);
    z += 0x9e3779b97f4a7c15ULL;
  }
  if ((r->s[0] | r->s[1] | r->s[2] | r->s[3]) == 0) r->s[0] = 1;
}

uint64_t emq_rng_u64(emq_rng *r) {
  uint64_t result = rotl(r->s[1] * 5, 7) * 9;
  uint64_t t = r->s[1] << 17;
  r->s[2] ^= r->s[0];
  r->s[3] ^= r->s[1];
  r->s[1] ^= r->s[2];
  r->s[0] ^= r->s[3];
  r->s[2] ^= t;
  r->s[3] = rotl(r->s[3], 45);
  return result;
}

uint32_t emq_rng_u32(emq_rng *r) { return (uint32_t)emq_rng_u64(r); }

uint64_t emq_rng_bounded(emq_rng *r, uint64_t n) {
  if (n == 0) return 0;
  return emq_rng_u64(r) % n;
}

double emq_rng_uniform(emq_rng *r) {
  return (emq_rng_u64(r) >> 11) * (1.0 / 9007199254740992.0);
}

/* ---- CLI ---- */
void emq_cli_defaults(emq_cli *c) {
  if (!c) return;
  memset(c, 0, sizeof(*c));
  c->ops = 100000;
  c->duration_sec = 0;
  c->threads = 4;
  c->queues = 1;
  c->seed = 0;
  c->cycles = 25;
  c->payload = 64;
  c->quick = 0;
}

int emq_cli_parse(emq_cli *c, int argc, char **argv) {
  int i;
  if (!c) return -1;
  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--ops") == 0 && i + 1 < argc)
      c->ops = (uint64_t)strtoull(argv[++i], NULL, 10);
    else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
      c->duration_sec = (uint64_t)strtoull(argv[++i], NULL, 10);
    else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
      c->threads = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--queues") == 0 && i + 1 < argc)
      c->queues = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
      c->seed = (uint64_t)strtoull(argv[++i], NULL, 10);
    else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc)
      c->cycles = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc)
      c->payload = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
      c->csv_path = argv[++i];
    else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc)
      c->path = argv[++i];
    else if (strcmp(argv[i], "--quick") == 0)
      c->quick = 1;
    else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
      return 1;
  }
  return 0;
}

uint64_t emq_cli_seed_or_time(const emq_cli *c) {
  if (c && c->seed != 0) return c->seed;
  return (uint64_t)time(NULL) ^ ((uint64_t)emq_now_ns() << 1);
}

/* ---- Payloads ---- */
static uint32_t fnv1a(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t h = 2166136261u;
  size_t i;
  for (i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

int emq_payload_fill(uint8_t *buf, size_t len, uint64_t seq, uint32_t producer) {
  uint32_t crc;
  size_t i;
  if (!buf || len < EMQ_PAYLOAD_HDR) return -1;
  memcpy(buf + 0, &seq, 8);
  memcpy(buf + 8, &producer, 4);
  for (i = EMQ_PAYLOAD_HDR; i < len; ++i) {
    buf[i] = (uint8_t)((seq * 131u + producer * 17u + i) & 0xffu);
  }
  crc = fnv1a(buf, 12);
  crc ^= fnv1a(buf + EMQ_PAYLOAD_HDR, len - EMQ_PAYLOAD_HDR);
  memcpy(buf + 12, &crc, 4);
  return 0;
}

int emq_payload_check(const uint8_t *buf, size_t len, uint64_t *seq_out,
                      uint32_t *producer_out) {
  uint64_t seq;
  uint32_t producer;
  uint32_t stored;
  uint32_t crc;
  if (!buf || len < EMQ_PAYLOAD_HDR) return -1;
  memcpy(&seq, buf + 0, 8);
  memcpy(&producer, buf + 8, 4);
  memcpy(&stored, buf + 12, 4);
  crc = fnv1a(buf, 12);
  crc ^= fnv1a(buf + EMQ_PAYLOAD_HDR, len - EMQ_PAYLOAD_HDR);
  if (crc != stored) return -2;
  if (seq_out) *seq_out = seq;
  if (producer_out) *producer_out = producer;
  return 0;
}

/* ---- Watchdog ---- */
struct emq_watchdog {
  emq_atomic_i32 stop;
  emq_atomic_u64 beats;
  uint32_t stall_sec;
  char label[64];
  emq_thread *thread;
};

static void emq_watchdog_main(void *arg) {
  emq_watchdog *w = (emq_watchdog *)arg;
  uint64_t last = 0;
  uint32_t stalled = 0;
  while (emq_atomic_load_i32(&w->stop) == 0) {
    uint64_t beats;
    emq_sleep_ms(1000);
    if (emq_atomic_load_i32(&w->stop) != 0) break;
    beats = emq_atomic_load_u64(&w->beats);
    if (beats == last) {
      stalled++;
      if (stalled >= w->stall_sec) {
        fprintf(stderr,
                "WATCHDOG: stalled %u s label=%s beats=%llu — aborting\n",
                stalled, w->label, (unsigned long long)beats);
        fflush(stderr);
        abort();
      }
    } else {
      last = beats;
      stalled = 0;
    }
  }
}

emq_watchdog *emq_watchdog_start(uint32_t stall_sec, const char *label) {
  emq_watchdog *w = (emq_watchdog *)calloc(1, sizeof(*w));
  if (!w) return NULL;
  w->stall_sec = stall_sec ? stall_sec : 30;
  if (label) {
    strncpy(w->label, label, sizeof(w->label) - 1);
  } else {
    strcpy(w->label, "unnamed");
  }
  emq_atomic_init_i32(&w->stop, 0);
  emq_atomic_init_u64(&w->beats, 1);
  if (emq_thread_create(&w->thread, emq_watchdog_main, w) != 0) {
    free(w);
    return NULL;
  }
  return w;
}

void emq_watchdog_heartbeat(emq_watchdog *w) {
  if (w) (void)emq_atomic_fetch_add_u64(&w->beats, 1);
}

void emq_watchdog_stop(emq_watchdog *w) {
  if (!w) return;
  emq_atomic_store_i32(&w->stop, 1);
  if (w->thread) {
    emq_thread_join(w->thread);
    emq_thread_destroy(w->thread);
  }
  free(w);
}

/* ---- Process sample ---- */
int emq_proc_sample(emq_proc_snap *out) {
  if (!out) return -1;
  memset(out, 0, sizeof(*out));
#if defined(_WIN32)
  {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    FILETIME create, exit_t, kernel, user;
    HANDLE h = GetCurrentProcess();
    DWORD handles = 0;
    if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc))) {
      out->rss_bytes = (uint64_t)pmc.WorkingSetSize;
      out->peak_rss_bytes = (uint64_t)pmc.PeakWorkingSetSize;
    }
    if (GetProcessTimes(h, &create, &exit_t, &kernel, &user)) {
      ULARGE_INTEGER ku, uu;
      ku.LowPart = kernel.dwLowDateTime;
      ku.HighPart = kernel.dwHighDateTime;
      uu.LowPart = user.dwLowDateTime;
      uu.HighPart = user.dwHighDateTime;
      out->kernel_time_ns = ku.QuadPart * 100ULL;
      out->user_time_ns = uu.QuadPart * 100ULL;
    }
    if (GetProcessHandleCount(h, &handles)) {
      out->handle_count = handles;
    }
  }
#elif defined(__APPLE__)
  {
    struct rusage ru;
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info,
                  &count) == KERN_SUCCESS) {
      out->rss_bytes = (uint64_t)info.resident_size;
      out->peak_rss_bytes = (uint64_t)info.resident_size_max;
    }
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
      out->user_time_ns =
          (uint64_t)ru.ru_utime.tv_sec * 1000000000ULL +
          (uint64_t)ru.ru_utime.tv_usec * 1000ULL;
      out->kernel_time_ns =
          (uint64_t)ru.ru_stime.tv_sec * 1000000000ULL +
          (uint64_t)ru.ru_stime.tv_usec * 1000ULL;
    }
  }
#else
  {
    struct rusage ru;
    long pages = 0;
    long page_size = sysconf(_SC_PAGESIZE);
    FILE *f = fopen("/proc/self/statm", "r");
    if (f) {
      if (fscanf(f, "%*s %ld", &pages) == 1 && page_size > 0) {
        out->rss_bytes = (uint64_t)pages * (uint64_t)page_size;
        out->peak_rss_bytes = out->rss_bytes;
      }
      fclose(f);
    }
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
      out->user_time_ns =
          (uint64_t)ru.ru_utime.tv_sec * 1000000000ULL +
          (uint64_t)ru.ru_utime.tv_usec * 1000ULL;
      out->kernel_time_ns =
          (uint64_t)ru.ru_stime.tv_sec * 1000000000ULL +
          (uint64_t)ru.ru_stime.tv_usec * 1000ULL;
#  if defined(__linux__)
      out->handle_count = (uint64_t)ru.ru_nvcsw + (uint64_t)ru.ru_nivcsw;
#  endif
    }
  }
#endif
  return 0;
}
