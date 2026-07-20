#include "sched/emq_wheel.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

#define EMQ_WHEEL_SLOTS 256u

typedef struct emq_timer {
  uint64_t deadline_ns;
  void *cookie;
  struct emq_timer *next;
} emq_timer;

typedef struct emq_wheel_level {
  emq_timer *slots[EMQ_WHEEL_SLOTS];
  uint32_t cursor;
} emq_wheel_level;

struct emq_wheel {
  uint32_t tick_ms;
  uint32_t levels;
  uint64_t current_ns;
  emq_wheel_level *lvl;
  emq_mutex *mu;
};

int emq_wheel_create(emq_wheel **out, uint32_t tick_ms, uint32_t levels) {
  emq_wheel *w;
  if (!out) return -1;
  if (tick_ms == 0) tick_ms = 1;
  if (levels == 0) levels = 4;
  w = (emq_wheel *)calloc(1, sizeof(*w));
  if (!w) return -2;
  w->tick_ms = tick_ms;
  w->levels = levels;
  w->current_ns = emq_now_ns();
  w->lvl = (emq_wheel_level *)calloc(levels, sizeof(emq_wheel_level));
  w->mu = emq_mutex_create();
  if (!w->lvl || !w->mu) {
    emq_wheel_destroy(w);
    return -2;
  }
  *out = w;
  return 0;
}

void emq_wheel_destroy(emq_wheel *w) {
  uint32_t L, s;
  if (!w) return;
  if (w->lvl) {
    for (L = 0; L < w->levels; ++L) {
      for (s = 0; s < EMQ_WHEEL_SLOTS; ++s) {
        emq_timer *t = w->lvl[L].slots[s];
        while (t) {
          emq_timer *n = t->next;
          free(t);
          t = n;
        }
      }
    }
    free(w->lvl);
  }
  emq_mutex_destroy(w->mu);
  free(w);
}

static uint64_t emq_ticks_until(emq_wheel *w, uint64_t deadline_ns) {
  uint64_t tick_ns;
  uint64_t delta;
  if (deadline_ns <= w->current_ns) return 0;
  tick_ns = (uint64_t)w->tick_ms * 1000000ULL;
  delta = deadline_ns - w->current_ns;
  return 1u + (delta - 1u) / tick_ns;
}

static void emq_wheel_insert_locked(emq_wheel *w, emq_timer *t) {
  uint64_t ticks = emq_ticks_until(w, t->deadline_ns);
  uint32_t level = 0;
  uint64_t slot_ticks = ticks;
  uint64_t slot;
  while (level + 1 < w->levels && slot_ticks > EMQ_WHEEL_SLOTS) {
    slot_ticks = (slot_ticks - 1u) / EMQ_WHEEL_SLOTS;
    level++;
  }
  slot = w->lvl[level].cursor;
  if (slot_ticks > 0) {
    slot = (slot + ((slot_ticks - 1u) % EMQ_WHEEL_SLOTS)) %
           EMQ_WHEEL_SLOTS;
  }
  t->next = w->lvl[level].slots[slot];
  w->lvl[level].slots[slot] = t;
}

int emq_wheel_schedule(emq_wheel *w, uint64_t deadline_ns, void *cookie) {
  emq_timer *t;

  if (!w) return -1;
  t = (emq_timer *)calloc(1, sizeof(*t));
  if (!t) return -2;
  t->deadline_ns = deadline_ns;
  t->cookie = cookie;

  emq_mutex_lock(w->mu);
  emq_wheel_insert_locked(w, t);
  emq_mutex_unlock(w->mu);
  return 0;
}

int emq_wheel_cancel(emq_wheel *w, void *cookie) {
  uint32_t L, s;
  int found = 0;
  if (!w) return -1;
  emq_mutex_lock(w->mu);
  for (L = 0; L < w->levels; ++L) {
    for (s = 0; s < EMQ_WHEEL_SLOTS; ++s) {
      emq_timer **pp = &w->lvl[L].slots[s];
      while (*pp) {
        if ((*pp)->cookie == cookie) {
          emq_timer *dead = *pp;
          *pp = dead->next;
          free(dead);
          found = 1;
        } else {
          pp = &(*pp)->next;
        }
      }
    }
  }
  emq_mutex_unlock(w->mu);
  return found ? 0 : -3;
}

static void emq_wheel_cascade(emq_wheel *w, uint32_t level) {
  uint32_t slot;
  emq_timer *list;
  if (level + 1 >= w->levels) return;
  slot = w->lvl[level + 1].cursor;
  list = w->lvl[level + 1].slots[slot];
  w->lvl[level + 1].slots[slot] = NULL;
  w->lvl[level + 1].cursor = (slot + 1u) % EMQ_WHEEL_SLOTS;
  if (w->lvl[level + 1].cursor == 0) {
    emq_wheel_cascade(w, level + 1);
  }
  while (list) {
    emq_timer *t = list;
    list = list->next;
    emq_wheel_insert_locked(w, t);
  }
}

uint32_t emq_wheel_tick(emq_wheel *w, uint64_t now_ns, emq_wheel_cb cb, void *user) {
  uint32_t fired = 0;
  uint64_t tick_ns;
  if (!w) return 0;
  tick_ns = (uint64_t)w->tick_ms * 1000000ULL;
  emq_mutex_lock(w->mu);
  while (w->current_ns + tick_ns <= now_ns) {
    uint32_t slot = w->lvl[0].cursor;
    emq_timer *list = w->lvl[0].slots[slot];
    w->lvl[0].slots[slot] = NULL;
    w->lvl[0].cursor = (slot + 1u) % EMQ_WHEEL_SLOTS;
    w->current_ns += tick_ns;
    if (w->lvl[0].cursor == 0) {
      emq_wheel_cascade(w, 0);
    }
    while (list) {
      emq_timer *t = list;
      list = list->next;
      if (t->deadline_ns > w->current_ns) {
        emq_wheel_insert_locked(w, t);
      } else {
        if (cb) {
          emq_mutex_unlock(w->mu);
          cb(user, t->deadline_ns, t->cookie);
          emq_mutex_lock(w->mu);
        }
        free(t);
        fired++;
      }
    }
  }
  emq_mutex_unlock(w->mu);
  return fired;
}
