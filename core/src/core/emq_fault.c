#include "core/emq_fault.h"

#if defined(EMQ_FAULT_INJECT)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#  include <process.h>
#  define emq_fault_exit(code) _exit(code)
#else
#  include <unistd.h>
#  define emq_fault_exit(code) _exit(code)
#endif

enum { EMQ_FAULT_MAX = 64 };

typedef struct emq_fault_point {
  char name[48];
  emq_fault_mode mode;
  uint32_t n;
  uint32_t p; /* tenths of a percent: 10000 = 100% */
  uint64_t hits;
  uint64_t crash_at; /* 0 = never */
  int in_use;
} emq_fault_point;

static emq_fault_point g_points[EMQ_FAULT_MAX];
static uint64_t g_seed = 1;
static int g_inited;

static uint64_t fault_rng(void) {
  /* xorshift64* */
  uint64_t x = g_seed;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  g_seed = x;
  return x * 0x2545F4914F6CDD1DULL;
}

void emq_fault_set_seed(uint64_t seed) {
  g_seed = seed ? seed : 1;
}

void emq_fault_init(void) {
  if (g_inited) return;
  memset(g_points, 0, sizeof(g_points));
  g_inited = 1;
  emq_fault_load_env();
}

void emq_fault_reset(void) {
  memset(g_points, 0, sizeof(g_points));
  g_inited = 1;
}

static emq_fault_point *find_or_alloc(const char *name) {
  int i;
  int free_slot = -1;
  if (!name) return NULL;
  if (!g_inited) emq_fault_init();
  for (i = 0; i < EMQ_FAULT_MAX; ++i) {
    if (g_points[i].in_use && strcmp(g_points[i].name, name) == 0) {
      return &g_points[i];
    }
    if (!g_points[i].in_use && free_slot < 0) free_slot = i;
  }
  if (free_slot < 0) return NULL;
  strncpy(g_points[free_slot].name, name, sizeof(g_points[free_slot].name) - 1);
  g_points[free_slot].in_use = 1;
  return &g_points[free_slot];
}

void emq_fault_configure(const char *name, emq_fault_mode mode, uint32_t n,
                         uint32_t p_tenths_pct) {
  emq_fault_point *p = find_or_alloc(name);
  if (!p) return;
  p->mode = mode;
  p->n = n;
  p->p = p_tenths_pct;
  p->hits = 0;
}

int emq_fault_should_fail(const char *name) {
  emq_fault_point *p;
  if (!name) return 0;
  if (!g_inited) emq_fault_init();
  p = find_or_alloc(name);
  if (!p || p->mode == EMQ_FAULT_OFF) return 0;
  p->hits++;
  if (p->crash_at != 0 && p->hits == p->crash_at) {
    fprintf(stderr, "EMQ_CRASHPOINT hit name=%s hit=%llu\n", name,
            (unsigned long long)p->hits);
    fflush(stderr);
    emq_fault_exit(137);
  }
  switch (p->mode) {
    case EMQ_FAULT_EVERY_N:
      return p->n != 0 && (p->hits % p->n) == 0;
    case EMQ_FAULT_AFTER_N:
      return p->n != 0 && p->hits > p->n;
    case EMQ_FAULT_PROB:
      if (p->p == 0) return 0;
      if (p->p >= 10000) return 1;
      return (fault_rng() % 10000ULL) < p->p;
    default:
      return 0;
  }
}

void emq_fault_crashpoint(const char *name) {
  emq_fault_point *p;
  if (!name) return;
  if (!g_inited) emq_fault_init();
  p = find_or_alloc(name);
  if (!p) return;
  p->hits++;
  if (p->crash_at != 0 && p->hits == p->crash_at) {
    fprintf(stderr, "EMQ_CRASHPOINT hit name=%s hit=%llu\n", name,
            (unsigned long long)p->hits);
    fflush(stderr);
    emq_fault_exit(137);
  }
}

void emq_fault_load_env(void) {
  const char *fault = getenv("EMQ_FAULT");
  const char *crash = getenv("EMQ_CRASH_AT");
  if (!g_inited) {
    g_inited = 1;
    memset(g_points, 0, sizeof(g_points));
  }
  if (fault && fault[0]) {
    /* name:mode:n  mode = every|after|prob */
    char buf[128];
    char *name;
    char *mode;
    char *nstr;
    strncpy(buf, fault, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    name = buf;
    mode = strchr(buf, ':');
    if (mode) {
      *mode++ = 0;
      nstr = strchr(mode, ':');
      if (nstr) {
        *nstr++ = 0;
        if (strcmp(mode, "every") == 0) {
          emq_fault_configure(name, EMQ_FAULT_EVERY_N, (uint32_t)atoi(nstr), 0);
        } else if (strcmp(mode, "after") == 0) {
          emq_fault_configure(name, EMQ_FAULT_AFTER_N, (uint32_t)atoi(nstr), 0);
        } else if (strcmp(mode, "prob") == 0) {
          emq_fault_configure(name, EMQ_FAULT_PROB, 0, (uint32_t)atoi(nstr));
        }
      }
    }
  }
  if (crash && crash[0]) {
    char buf[128];
    char *name;
    char *hit;
    emq_fault_point *p;
    strncpy(buf, crash, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    name = buf;
    hit = strchr(buf, ':');
    if (hit) {
      *hit++ = 0;
      p = find_or_alloc(name);
      if (p) p->crash_at = (uint64_t)strtoull(hit, NULL, 10);
    }
  }
}

#endif /* EMQ_FAULT_INJECT */
