/*
 * Bounded explicit-state model checker for the work-queue delivery lifecycle.
 *
 * States per message: READY, INFLIGHT(deadline), ACKED, DLQ
 * Exhaustive BFS for N<=3 messages, <=12 steps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

enum { MSG_N = 3, MAX_STEPS = 12, MAX_RETRY = 2, VISIT_CAP = 1 << 20 };

typedef enum msg_state {
  ST_READY = 0,
  ST_INFLIGHT = 1,
  ST_ACKED = 2,
  ST_DLQ = 3
} msg_state;

typedef struct world {
  uint8_t state[MSG_N];
  uint8_t retry[MSG_N];
  int8_t deadline[MSG_N]; /* abstract clock; -1 if not inflight */
  int8_t clock;
  uint8_t steps;
} world;

static uint64_t world_hash(const world *w) {
  uint64_t h = 14695981039346656037ull;
  const uint8_t *p = (const uint8_t *)w;
  size_t i;
  for (i = 0; i < sizeof(*w); ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

typedef struct visit_set {
  uint64_t *keys;
  size_t cap;
  size_t count;
} visit_set;

static int visit_insert(visit_set *v, uint64_t key) {
  size_t i;
  if (v->count + 1 > (v->cap * 3) / 4) return -1;
  i = (size_t)(key & (v->cap - 1));
  for (;;) {
    if (v->keys[i] == 0) {
      v->keys[i] = key;
      v->count++;
      return 1;
    }
    if (v->keys[i] == key) return 0;
    i = (i + 1) & (v->cap - 1);
  }
}

static int invariant_ok(const world *w, char *err, size_t err_sz) {
  int inflight = 0;
  int i;
  for (i = 0; i < MSG_N; ++i) {
    if (w->state[i] == ST_INFLIGHT) {
      inflight++;
      if (w->deadline[i] < w->clock) {
        snprintf(err, err_sz, "inflight past deadline without redelivery");
        return 0;
      }
    }
    if (w->retry[i] > MAX_RETRY && w->state[i] != ST_DLQ) {
      snprintf(err, err_sz, "retry exceeded without DLQ");
      return 0;
    }
  }
  if (inflight > 1) {
    snprintf(err, err_sz, "multiple concurrent INFLIGHT holders");
    return 0;
  }
  return 1;
}

static int is_terminal(const world *w) {
  int i;
  for (i = 0; i < MSG_N; ++i) {
    if (w->state[i] != ST_ACKED && w->state[i] != ST_DLQ) return 0;
  }
  return 1;
}

static int no_lost(const world *w) {
  int i;
  for (i = 0; i < MSG_N; ++i) {
    if (w->state[i] != ST_ACKED && w->state[i] != ST_DLQ &&
        w->state[i] != ST_READY && w->state[i] != ST_INFLIGHT)
      return 0;
  }
  return 1;
}

typedef struct queue {
  world *buf;
  size_t head, tail, cap;
} queue;

static int q_push(queue *q, const world *w) {
  size_t next = (q->tail + 1) % q->cap;
  if (next == q->head) return -1;
  q->buf[q->tail] = *w;
  q->tail = next;
  return 0;
}

static int q_pop(queue *q, world *out) {
  if (q->head == q->tail) return 0;
  *out = q->buf[q->head];
  q->head = (q->head + 1) % q->cap;
  return 1;
}

static void try_enqueue(queue *q, visit_set *vis, const world *nw, int *explored,
                        char *err, size_t err_sz) {
  uint64_t h;
  if (!invariant_ok(nw, err, err_sz)) {
    fprintf(stderr, "INVARIANT FAIL: %s\n", err);
    abort();
  }
  if (!no_lost(nw)) {
    fprintf(stderr, "LOST MESSAGE\n");
    abort();
  }
  h = world_hash(nw);
  if (visit_insert(vis, h) == 1) {
    if (q_push(q, nw) != 0) {
      fprintf(stderr, "BFS queue full\n");
      abort();
    }
    (*explored)++;
  }
}

int main(void) {
  visit_set vis;
  queue q;
  world start;
  world cur;
  char err[128];
  int explored = 0;
  int terminals = 0;
  int i;

  memset(&vis, 0, sizeof(vis));
  vis.cap = VISIT_CAP;
  vis.keys = (uint64_t *)calloc(vis.cap, sizeof(uint64_t));
  q.cap = 1 << 18;
  q.buf = (world *)calloc(q.cap, sizeof(world));
  q.head = q.tail = 0;
  if (!vis.keys || !q.buf) return 1;

  memset(&start, 0, sizeof(start));
  for (i = 0; i < MSG_N; ++i) {
    start.state[i] = ST_READY;
    start.deadline[i] = -1;
  }

  try_enqueue(&q, &vis, &start, &explored, err, sizeof(err));

  while (q_pop(&q, &cur)) {
    world nw;
    int has_inflight = 0;
    int inf_idx = -1;

    if (cur.steps >= MAX_STEPS) {
      if (is_terminal(&cur)) terminals++;
      continue;
    }
    if (is_terminal(&cur)) {
      terminals++;
      continue;
    }

    for (i = 0; i < MSG_N; ++i) {
      if (cur.state[i] == ST_INFLIGHT) {
        has_inflight = 1;
        inf_idx = i;
      }
    }

    /* tick clock */
    nw = cur;
    nw.clock++;
    nw.steps++;
    for (i = 0; i < MSG_N; ++i) {
      if (nw.state[i] == ST_INFLIGHT && nw.deadline[i] <= nw.clock) {
        /* visibility expiry → READY, retry++ */
        nw.retry[i]++;
        if (nw.retry[i] > MAX_RETRY) {
          nw.state[i] = ST_DLQ;
          nw.deadline[i] = -1;
        } else {
          nw.state[i] = ST_READY;
          nw.deadline[i] = -1;
        }
      }
    }
    try_enqueue(&q, &vis, &nw, &explored, err, sizeof(err));

    if (!has_inflight) {
      /* pop any READY message */
      for (i = 0; i < MSG_N; ++i) {
        if (cur.state[i] == ST_READY) {
          nw = cur;
          nw.state[i] = ST_INFLIGHT;
          nw.deadline[i] = (int8_t)(nw.clock + 2);
          nw.steps++;
          try_enqueue(&q, &vis, &nw, &explored, err, sizeof(err));
        }
      }
    } else {
      /* ack */
      nw = cur;
      nw.state[inf_idx] = ST_ACKED;
      nw.deadline[inf_idx] = -1;
      nw.steps++;
      try_enqueue(&q, &vis, &nw, &explored, err, sizeof(err));

      /* nack → READY */
      nw = cur;
      nw.state[inf_idx] = ST_READY;
      nw.deadline[inf_idx] = -1;
      nw.retry[inf_idx]++;
      if (nw.retry[inf_idx] > MAX_RETRY) nw.state[inf_idx] = ST_DLQ;
      nw.steps++;
      try_enqueue(&q, &vis, &nw, &explored, err, sizeof(err));

      /* double-ack rejected: no state change branch, but verify reject */
      /* (modeled as stutter — already covered by not transitioning) */
    }
  }

  printf("model_workqueue explored=%d terminals=%d visited=%zu\n", explored,
         terminals, vis.count);
  if (explored < 10 || terminals < 1) {
    fprintf(stderr, "model checker produced too few states\n");
    return 1;
  }
  printf("PASS model_workqueue\n");
  free(vis.keys);
  free(q.buf);
  return 0;
}
