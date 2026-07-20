#include "core/emq_task.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

int emq_task_runtime_init(emq_task_runtime *tr) {
  if (!tr) return -1;
  memset(tr, 0, sizeof(*tr));
  tr->mu = emq_mutex_create();
  if (!tr->mu) return -2;
  tr->running = 1;
  return 0;
}

void emq_task_runtime_destroy(emq_task_runtime *tr) {
  struct emq_task *t;
  if (!tr) return;
  if (tr->mu) emq_mutex_lock((emq_mutex *)tr->mu);
  tr->running = 0;
  while (tr->ready) {
    t = tr->ready;
    tr->ready = t->next;
    free(t);
  }
  while (tr->sleeping) {
    t = tr->sleeping;
    tr->sleeping = t->next;
    free(t);
  }
  if (tr->mu) {
    emq_mutex_unlock((emq_mutex *)tr->mu);
    emq_mutex_destroy((emq_mutex *)tr->mu);
  }
  memset(tr, 0, sizeof(*tr));
}

int emq_task_runtime_submit(emq_task_runtime *tr, int (*fn)(struct emq_task *),
                            void *user, struct emq_runtime *rt,
                            struct emq_task **out) {
  struct emq_task *t;
  if (!tr || !fn || !out) return -1;
  t = (struct emq_task *)calloc(1, sizeof(*t));
  if (!t) return -2;
  t->fn_int = fn;
  t->user = user;
  t->runtime = rt;
  emq_mutex_lock((emq_mutex *)tr->mu);
  t->next = tr->ready;
  tr->ready = t;
  emq_mutex_unlock((emq_mutex *)tr->mu);
  *out = t;
  return 0;
}

int emq_task_runtime_cancel(emq_task_runtime *tr, struct emq_task *task) {
  if (!tr || !task) return -1;
  emq_mutex_lock((emq_mutex *)tr->mu);
  task->cancelled = 1;
  emq_mutex_unlock((emq_mutex *)tr->mu);
  return 0;
}

static void emq_task_enqueue_ready(emq_task_runtime *tr, struct emq_task *t) {
  t->next = tr->ready;
  tr->ready = t;
}

uint32_t emq_task_runtime_pump(emq_task_runtime *tr, uint32_t budget) {
  uint32_t ran = 0;
  uint64_t now;
  if (!tr || budget == 0) return 0;
  now = emq_now_ns();

  emq_mutex_lock((emq_mutex *)tr->mu);
  /* Wake delayed tasks */
  {
    struct emq_task **link = &tr->sleeping;
    while (*link) {
      struct emq_task *t = *link;
      if (t->cancelled || (t->wake_at_ns != 0 && t->wake_at_ns <= now)) {
        *link = t->next;
        t->wake_at_ns = 0;
        emq_task_enqueue_ready(tr, t);
      } else {
        link = &t->next;
      }
    }
  }

  while (tr->ready && ran < budget) {
    struct emq_task *t = tr->ready;
    int rc;
    tr->ready = t->next;
    t->next = NULL;
    emq_mutex_unlock((emq_mutex *)tr->mu);

    if (t->cancelled) {
      free(t);
      ran++;
      emq_mutex_lock((emq_mutex *)tr->mu);
      continue;
    }
    rc = t->fn_int ? t->fn_int(t) : 0;
    emq_mutex_lock((emq_mutex *)tr->mu);
    if (rc == 1 && !t->cancelled) {
      /* yielded — if delay set, sleep list; else ready */
      if (t->wake_at_ns != 0) {
        t->next = tr->sleeping;
        tr->sleeping = t;
      } else {
        emq_task_enqueue_ready(tr, t);
      }
    } else {
      free(t);
    }
    ran++;
  }
  emq_mutex_unlock((emq_mutex *)tr->mu);
  return ran;
}
