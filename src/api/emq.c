#include "emq/emq.h"
#include "engine/emq_engine.h"
#include "primitives/emq_primitives.h"
#include "platform/emq_platform.h"
#include "core/emq_atomic.h"
#include "core/emq_hot.h"
#include "core/emq_pool.h"
#include "core/emq_task.h"
#include "core/emq_hist.h"

#include <stdlib.h>
#include <string.h>

static int emq_desc_spsc_lfq(const emq_queue_desc *d) {
  return d && d->lfq &&
         (emq_hot_flags_load(&d->hot) & EMQ_HOT_FLAG_SPSC) != 0;
}

struct emq_runtime {
  emq_engine engine;
};

struct emq_queue {
  emq_runtime *rt;
  emq_queue_desc *desc;
};

struct emq_subscription {
  emq_runtime *rt;
  emq_route_sub *sub;
};

typedef struct emq_async_push {
  emq_runtime *runtime;
  emq_queue_desc *desc;
  void *payload;
  size_t size;
  emq_message meta;
  int has_meta;
  emq_completion_cb callback;
  void *user;
} emq_async_push;

typedef struct emq_async_pop {
  emq_runtime *runtime;
  emq_queue_desc *desc;
  uint32_t timeout_ms;
  emq_message_cb callback;
  void *user;
} emq_async_pop;

void emq_queue_opts_default(emq_queue_opts *opts) {
  if (!opts) return;
  memset(opts, 0, sizeof(*opts));
  opts->storage = EMQ_STORAGE_FAST;
  opts->policy = EMQ_POLICY_FIFO;
  opts->delivery = EMQ_AT_LEAST_ONCE;
  opts->fsync = EMQ_FSYNC_NONE;
  opts->visibility_ms = 30000;
  opts->inline_threshold = EMQ_INLINE_PAYLOAD_MAX;
  opts->producers = 1;
  opts->consumers = 1;
  opts->backpressure = EMQ_BP_MODE_BLOCK;
}

const char *emq_status_string(emq_status s) {
  switch (s) {
    case EMQ_OK: return "ok";
    case EMQ_ERR_INVALID: return "invalid";
    case EMQ_ERR_NOMEM: return "nomem";
    case EMQ_ERR_NOT_FOUND: return "not_found";
    case EMQ_ERR_FULL: return "full";
    case EMQ_ERR_EMPTY: return "empty";
    case EMQ_ERR_IO: return "io";
    case EMQ_ERR_TIMEOUT: return "timeout";
    case EMQ_ERR_EXISTS: return "exists";
    case EMQ_ERR_CLOSED: return "closed";
    case EMQ_ERR_BUSY: return "busy";
    case EMQ_ERR_UNSUPPORTED: return "unsupported";
    default: return "unknown";
  }
}

static emq_status emq_map_rc(int rc) {
  if (rc == 0) return EMQ_OK;
  if (rc == -1) return EMQ_ERR_INVALID;
  if (rc == -2) return EMQ_ERR_NOMEM;
  if (rc == -3) return EMQ_ERR_NOT_FOUND;
  if (rc == -4) return EMQ_ERR_FULL;
  if (rc == -5) return EMQ_ERR_EMPTY;
  if (rc == -6) return EMQ_ERR_IO;
  if (rc == -7) return EMQ_ERR_TIMEOUT;
  if (rc == -8) return EMQ_ERR_EXISTS;
  if (rc == -9) return EMQ_ERR_CLOSED;
  if (rc == -10) return EMQ_ERR_BUSY;
  if (rc == -11) return EMQ_ERR_UNSUPPORTED;
  return EMQ_ERR_INVALID;
}

emq_status emq_runtime_create(emq_runtime **out) {
  return emq_runtime_create_ex(out, 2);
}

emq_status emq_runtime_create_ex(emq_runtime **out, uint32_t worker_threads) {
  emq_runtime *rt;
  int rc;
  if (!out) return EMQ_ERR_INVALID;
  rt = (emq_runtime *)calloc(1, sizeof(*rt));
  if (!rt) return EMQ_ERR_NOMEM;
  rc = emq_engine_init(&rt->engine, worker_threads);
  if (rc != 0) {
    free(rt);
    return emq_map_rc(rc);
  }
  *out = rt;
  return EMQ_OK;
}

void emq_runtime_destroy(emq_runtime *rt) {
  if (!rt) return;
  emq_engine_shutdown(&rt->engine);
  free(rt);
}

emq_status emq_runtime_start(emq_runtime *rt) {
  if (!rt) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_engine_start(&rt->engine));
}

emq_status emq_runtime_stop(emq_runtime *rt) {
  if (!rt) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_engine_stop(&rt->engine));
}

emq_status emq_queue_create(emq_runtime *rt, const char *name,
                            const emq_queue_opts *opts, emq_queue **out) {
  emq_queue_opts local;
  emq_queue_desc *desc = NULL;
  emq_queue *q;
  int rc;
  if (!rt || !name || !out) return EMQ_ERR_INVALID;
  if (opts) local = *opts;
  else emq_queue_opts_default(&local);
  rc = emq_registry_create(&rt->engine.registry, name, &local, &desc);
  if (rc != 0) return emq_map_rc(rc);
  q = (emq_queue *)calloc(1, sizeof(*q));
  if (!q) {
    (void)emq_registry_remove(&rt->engine.registry, name);
    return EMQ_ERR_NOMEM;
  }
  q->rt = rt;
  q->desc = desc;
  emq_mutex_lock((emq_mutex *)desc->op_mu_opaque);
  desc->handle_count++;
  emq_mutex_unlock((emq_mutex *)desc->op_mu_opaque);
  emq_registry_set_active(&rt->engine.registry, desc->id, 1);
  *out = q;
  return EMQ_OK;
}

emq_status emq_queue_open(emq_runtime *rt, const char *name, emq_queue **out) {
  emq_queue_desc *desc;
  emq_queue *q;
  if (!rt || !name || !out) return EMQ_ERR_INVALID;
  desc = emq_registry_find(&rt->engine.registry, name);
  if (!desc) return EMQ_ERR_NOT_FOUND;
  q = (emq_queue *)calloc(1, sizeof(*q));
  if (!q) return EMQ_ERR_NOMEM;
  q->rt = rt;
  q->desc = desc;
  emq_mutex_lock((emq_mutex *)desc->op_mu_opaque);
  desc->handle_count++;
  emq_mutex_unlock((emq_mutex *)desc->op_mu_opaque);
  *out = q;
  return EMQ_OK;
}

emq_status emq_queue_close(emq_queue *q) {
  if (!q) return EMQ_ERR_INVALID;
  if (q->desc) {
    emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
    if (q->desc->handle_count != 0) q->desc->handle_count--;
    emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  }
  free(q);
  return EMQ_OK;
}

emq_status emq_queue_destroy(emq_runtime *rt, const char *name) {
  if (!rt || !name) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_registry_remove(&rt->engine.registry, name));
}

emq_status emq_push(emq_queue *q, const void *data, size_t size,
                    const emq_message *meta) {
  int rc;
  uint64_t t0 = 0;
  int sample;
  if (!q || !q->desc) return EMQ_ERR_INVALID;
  /* Sample the enqueue-latency histogram 1-in-16 so the hot path pays for
   * clock reads only occasionally. */
  sample = ((q->desc->stats.enqueued & 15u) == 0u);
  if (sample) t0 = emq_now_ns();

  if (emq_desc_spsc_lfq(q->desc) && q->desc->opts.policy != EMQ_POLICY_DELAY) {
    rc = emq_prim_push(q->desc, data, size, meta);
    if (rc == 0) {
      (void)emq_engine_activate_queue(&q->rt->engine, q->desc->id,
                                      q->desc->hot.band);
      if (!q->desc->active) {
        emq_registry_set_active(&q->rt->engine.registry, q->desc->id, 1);
      }
      if (sample) {
        emq_hist_record(&q->rt->engine.latency_hist, emq_now_ns() - t0);
      }
    }
    return emq_map_rc(rc);
  }

  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  if (q->desc->opts.policy == EMQ_POLICY_DELAY) {
    rc = emq_delay_push(q->desc, data, size, meta, q->rt->engine.wheel);
  } else {
    rc = emq_prim_push(q->desc, data, size, meta);
  }
  if (rc == 0) {
    emq_cond_signal((emq_cond *)q->desc->ready_cond_opaque);
  }
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  if (rc == 0) {
    (void)emq_engine_activate_queue(&q->rt->engine, q->desc->id,
                                    q->desc->hot.band);
    if (!q->desc->active) {
      emq_registry_set_active(&q->rt->engine.registry, q->desc->id, 1);
    }
    if (sample) {
      emq_hist_record(&q->rt->engine.latency_hist, emq_now_ns() - t0);
    }
  }
  return emq_map_rc(rc);
}

emq_status emq_pop(emq_queue *q, emq_message *out, uint32_t timeout_ms) {
  uint64_t deadline;
  int rc;
  if (!q || !q->desc || !out) return EMQ_ERR_INVALID;
  deadline = timeout_ms == 0 ? 0 :
             emq_now_ns() + (uint64_t)timeout_ms * 1000000ULL;

  if (emq_desc_spsc_lfq(q->desc)) {
    for (;;) {
      /* Futex protocol: snapshot the sequence BEFORE the pop attempt so a
       * push racing in between makes the wait return immediately instead
       * of sleeping through the timeout. */
      uint64_t expect =
          emq_atomic_load_u64((emq_atomic_u64 *)&q->desc->seq_wait);
      rc = emq_prim_pop(q->desc, out, 0, q->rt->engine.wheel);
      if (rc != -5) return emq_map_rc(rc);
      if (timeout_ms == 0) return EMQ_ERR_EMPTY;
      {
        uint64_t now = emq_now_ns();
        uint32_t wait_ms;
        if (now >= deadline) return EMQ_ERR_TIMEOUT;
        wait_ms = (uint32_t)(((deadline - now) + 999999ULL) / 1000000ULL);
        if (wait_ms == 0) wait_ms = 1;
        (void)emq_wait_u64(&q->desc->seq_wait, expect, wait_ms);
      }
    }
  }

  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  for (;;) {
    rc = emq_prim_pop(q->desc, out, 0, q->rt->engine.wheel);
    if (rc != -5) {
      emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
      return emq_map_rc(rc);
    }
    if (timeout_ms == 0) {
      emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
      return EMQ_ERR_EMPTY;
    }
    {
      uint64_t now = emq_now_ns();
      uint64_t remain_ns;
      uint32_t wait_ms;
      if (now >= deadline) {
        emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
        return EMQ_ERR_TIMEOUT;
      }
      remain_ns = deadline - now;
      wait_ms = (uint32_t)((remain_ns + 999999ULL) / 1000000ULL);
      if (wait_ms == 0) wait_ms = 1;
      if (q->desc->opts.policy == EMQ_POLICY_DELAY) {
        uint64_t off;
        uint64_t next = emq_log_next_offset(q->desc->log);
        for (off = q->desc->read_offset; off < next; ++off) {
          emq_log_entry entry;
          if (emq_log_read(q->desc->log, off, &entry) == 0 &&
              entry.deliver_at_ns > now) {
            uint64_t delta = entry.deliver_at_ns - now;
            uint64_t candidate64 = (delta + 999999ULL) / 1000000ULL;
            uint32_t candidate = candidate64 > UINT32_MAX ?
                                 UINT32_MAX : (uint32_t)candidate64;
            if (candidate == 0) candidate = 1;
            if (candidate < wait_ms) wait_ms = candidate;
          }
        }
      }
      (void)emq_cond_timedwait((emq_cond *)q->desc->ready_cond_opaque,
                               (emq_mutex *)q->desc->op_mu_opaque, wait_ms);
    }
  }
}

emq_status emq_try_pop(emq_queue *q, emq_message *out) {
  return emq_pop(q, out, 0);
}

emq_status emq_peek(emq_queue *q, emq_message *out) {
  int rc;
  if (!q || !q->desc || !out) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_prim_peek(q->desc, out);
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return emq_map_rc(rc);
}

emq_status emq_ack(emq_queue *q, uint64_t message_id) {
  int rc;
  if (!q || !q->desc) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_prim_ack(q->desc, message_id);
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return emq_map_rc(rc);
}

emq_status emq_nack(emq_queue *q, uint64_t message_id, uint32_t delay_ms) {
  int rc;
  if (!q || !q->desc) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_prim_nack(q->desc, message_id, delay_ms, q->rt->engine.wheel);
  if (rc == 0) {
    emq_cond_signal((emq_cond *)q->desc->ready_cond_opaque);
  }
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return emq_map_rc(rc);
}

emq_status emq_ack_batch(emq_queue *q, const uint64_t *ids, size_t count) {
  int rc;
  if (!q || !q->desc) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_prim_ack_batch(q->desc, ids, count);
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return emq_map_rc(rc);
}

emq_status emq_push_batch(emq_queue *q, const emq_batch_item *items,
                          size_t count, size_t *pushed) {
  size_t i;
  if (pushed) *pushed = 0;
  if (!q || (!items && count != 0)) return EMQ_ERR_INVALID;
  for (i = 0; i < count; ++i) {
    emq_status status = emq_push(q, items[i].data, items[i].size,
                                 &items[i].meta);
    if (status != EMQ_OK) {
      if (pushed) *pushed = i;
      return status;
    }
  }
  if (pushed) *pushed = count;
  return EMQ_OK;
}

emq_status emq_pop_batch(emq_queue *q, emq_message *messages,
                         size_t capacity, size_t *popped) {
  size_t i;
  emq_status status = EMQ_OK;
  if (popped) *popped = 0;
  if (!q || (!messages && capacity != 0)) return EMQ_ERR_INVALID;
  for (i = 0; i < capacity; ++i) {
    status = emq_try_pop(q, &messages[i]);
    if (status != EMQ_OK) break;
  }
  if (popped) *popped = i;
  return i != 0 ? EMQ_OK : status;
}

static void emq_async_push_run(void *arg) {
  emq_async_push *job = (emq_async_push *)arg;
  emq_queue queue;
  emq_status status;
  queue.rt = job->runtime;
  queue.desc = job->desc;
  status = emq_push(&queue, job->payload, job->size,
                               job->has_meta ? &job->meta : NULL);
  if (job->callback) job->callback(status, job->user);
  emq_mutex_lock((emq_mutex *)job->desc->op_mu_opaque);
  if (job->desc->handle_count != 0) job->desc->handle_count--;
  emq_mutex_unlock((emq_mutex *)job->desc->op_mu_opaque);
  free(job->payload);
  free(job);
}

static void emq_async_pop_run(void *arg) {
  emq_async_pop *job = (emq_async_pop *)arg;
  emq_queue queue;
  emq_message message;
  emq_status status;
  memset(&message, 0, sizeof(message));
  queue.rt = job->runtime;
  queue.desc = job->desc;
  status = emq_pop(&queue, &message, job->timeout_ms);
  job->callback(status, status == EMQ_OK ? &message : NULL, job->user);
  emq_mutex_lock((emq_mutex *)job->desc->op_mu_opaque);
  if (job->desc->handle_count != 0) job->desc->handle_count--;
  emq_mutex_unlock((emq_mutex *)job->desc->op_mu_opaque);
  free(job);
}

emq_status emq_push_async(emq_queue *q, const void *data, size_t size,
                          const emq_message *meta, emq_completion_cb callback,
                          void *user) {
  emq_async_push *job;
  int rc;
  if (!q || (!data && size != 0) || !callback) return EMQ_ERR_INVALID;
  job = (emq_async_push *)calloc(1, sizeof(*job));
  if (!job) return EMQ_ERR_NOMEM;
  if (size != 0) {
    job->payload = malloc(size);
    if (!job->payload) {
      free(job);
      return EMQ_ERR_NOMEM;
    }
    memcpy(job->payload, data, size);
  }
  job->runtime = q->rt;
  job->desc = q->desc;
  job->size = size;
  job->callback = callback;
  job->user = user;
  if (meta) {
    job->meta = *meta;
    job->has_meta = 1;
  }
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  q->desc->handle_count++;
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_engine_start(&q->rt->engine);
  if (rc == 0) rc = emq_engine_submit(&q->rt->engine, emq_async_push_run, job);
  if (rc != 0) {
    emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
    if (q->desc->handle_count != 0) q->desc->handle_count--;
    emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
    free(job->payload);
    free(job);
  }
  return emq_map_rc(rc);
}

emq_status emq_pop_async(emq_queue *q, uint32_t timeout_ms,
                         emq_message_cb callback, void *user) {
  emq_async_pop *job;
  int rc;
  if (!q || !callback) return EMQ_ERR_INVALID;
  job = (emq_async_pop *)calloc(1, sizeof(*job));
  if (!job) return EMQ_ERR_NOMEM;
  job->runtime = q->rt;
  job->desc = q->desc;
  job->timeout_ms = timeout_ms;
  job->callback = callback;
  job->user = user;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  q->desc->handle_count++;
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_engine_start(&q->rt->engine);
  if (rc == 0) rc = emq_engine_submit(&q->rt->engine, emq_async_pop_run, job);
  if (rc != 0) {
    emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
    if (q->desc->handle_count != 0) q->desc->handle_count--;
    emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
    free(job);
  }
  return emq_map_rc(rc);
}

emq_status emq_publish(emq_runtime *rt, const char *topic,
                       const void *data, size_t size) {
  uint64_t off;
  if (!rt || !topic) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_router_publish(&rt->engine.router, topic, data, size, &off));
}

emq_status emq_publish_ex(emq_runtime *rt, const char *topic,
                          const void *data, size_t size,
                          const emq_message *meta, uint64_t *offset) {
  uint64_t local_offset = 0;
  if (!rt || !topic || (!data && size != 0)) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_router_publish_ex(&rt->engine.router, topic, data,
                                          size, meta,
                                          offset ? offset : &local_offset));
}

emq_status emq_publish_batch(emq_runtime *rt, const char *topic,
                             const emq_batch_item *items, size_t count,
                             size_t *published) {
  if (!rt || !topic || (!items && count != 0)) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_router_publish_batch(&rt->engine.router, topic, items,
                                             count, published));
}

emq_status emq_subscribe(emq_runtime *rt, const char *pattern,
                         const char *group, emq_subscription **out) {
  emq_route_sub *sub = NULL;
  emq_subscription *s;
  int rc;
  if (!rt || !pattern || !out) return EMQ_ERR_INVALID;
  rc = emq_router_subscribe(&rt->engine.router, pattern, group, &sub);
  if (rc != 0) return emq_map_rc(rc);
  s = (emq_subscription *)calloc(1, sizeof(*s));
  if (!s) return EMQ_ERR_NOMEM;
  s->rt = rt;
  s->sub = sub;
  *out = s;
  return EMQ_OK;
}

emq_status emq_unsubscribe(emq_subscription *sub) {
  if (!sub) return EMQ_ERR_INVALID;
  if (sub->sub) {
    emq_router_unsubscribe(&sub->rt->engine.router, sub->sub->id);
  }
  free(sub);
  return EMQ_OK;
}

emq_status emq_sub_next(emq_subscription *sub, emq_message *out,
                        uint32_t timeout_ms) {
  uint64_t deadline;
  int rc;
  if (!sub || !sub->sub || !out) return EMQ_ERR_INVALID;
  deadline = timeout_ms == 0 ? 0 : emq_now_ns() + (uint64_t)timeout_ms * 1000000ULL;
  for (;;) {
    rc = emq_router_next(sub->sub, out);
    if (rc == 0) {
      return EMQ_OK;
    }
    if (timeout_ms == 0) return EMQ_ERR_EMPTY;
    if (emq_now_ns() >= deadline) return EMQ_ERR_TIMEOUT;
    emq_sleep_ms(1);
  }
}

emq_status emq_sub_seek(emq_subscription *sub, uint64_t offset) {
  if (!sub || !sub->sub) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_router_seek(sub->sub, offset));
}

emq_status emq_sub_replay(emq_subscription *sub) {
  if (!sub || !sub->sub) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_router_replay(sub->sub));
}

emq_status emq_sub_retry(emq_subscription *sub, const char *retry_topic,
                         uint64_t message_id, uint32_t attempt,
                         uint32_t delay_ms) {
  uint64_t deadline;
  if (!sub || !sub->sub || !retry_topic) return EMQ_ERR_INVALID;
  deadline = delay_ms == 0 ? 0 :
             emq_now_ns() + (uint64_t)delay_ms * 1000000ULL;
  return emq_map_rc(emq_router_retry(&sub->rt->engine.router, retry_topic,
                                     message_id, attempt, deadline));
}

emq_status emq_sub_dead_letter(emq_subscription *sub, const char *topic,
                               uint64_t message_id, uint32_t attempts) {
  if (!sub || !sub->sub || !topic) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_router_dead_letter(&sub->rt->engine.router, topic,
                                           message_id, attempts));
}

emq_status emq_seek(emq_queue *q, uint64_t offset) {
  int rc;
  if (!q || !q->desc) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_prim_seek(q->desc, offset);
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return emq_map_rc(rc);
}

emq_status emq_flush(emq_queue *q) {
  int rc;
  if (!q || !q->desc) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_log_sync(q->desc->log);
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return emq_map_rc(rc);
}

emq_status emq_queue_snapshot(emq_queue *q) {
  int rc;
  if (!q || !q->desc) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_log_snapshot(q->desc->log);
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return emq_map_rc(rc);
}

emq_status emq_queue_compact(emq_queue *q) {
  int rc;
  if (!q || !q->desc) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  rc = emq_log_compact(q->desc->log);
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return emq_map_rc(rc);
}

emq_status emq_queue_stats(emq_queue *q, emq_stats *out) {
  if (!q || !q->desc || !out) return EMQ_ERR_INVALID;
  emq_mutex_lock((emq_mutex *)q->desc->op_mu_opaque);
  *out = q->desc->stats;
  if (q->desc->lfq) {
    /* Ring messages (hot.depth) plus TTL/delayed spill still in the log. */
    out->depth = emq_atomic_load_u64(&q->desc->hot.depth) +
                 emq_log_count(q->desc->log);
  } else {
    out->depth = emq_log_count(q->desc->log);
  }
  emq_mutex_unlock((emq_mutex *)q->desc->op_mu_opaque);
  return EMQ_OK;
}

emq_status emq_get_runtime_stats(emq_runtime *rt, emq_runtime_stats *out) {
  emq_pool_stats ps;
  emq_sched_stats ss;
  if (!rt || !out) return EMQ_ERR_INVALID;
  memset(out, 0, sizeof(*out));
  out->queues = rt->engine.registry.count;
  out->worker_jobs = rt->engine.worker_jobs;
  out->wakeups = rt->engine.wakeups;
  if (rt->engine.pool) {
    emq_pool_get_stats(rt->engine.pool, &ps);
    out->allocator_hits = ps.hits;
    out->allocator_misses = ps.misses;
    out->allocator_live_bytes = ps.live_bytes;
    out->malloc_fallbacks = ps.malloc_fallbacks;
  }
  if (rt->engine.sched) {
    emq_sched_get_stats(rt->engine.sched, &ss);
    out->scheduler_activations = ss.activations;
    out->wakeups += ss.wakeups;
  }
  return EMQ_OK;
}

emq_status emq_flush_batch(emq_runtime *rt, emq_queue **queues, size_t count) {
  size_t i;
  if (!rt) return EMQ_ERR_INVALID;
  for (i = 0; i < count; ++i) {
    emq_status st = emq_flush(queues[i]);
    if (st != EMQ_OK) return st;
  }
  return EMQ_OK;
}

emq_status emq_poll(emq_runtime *rt) {
  if (!rt) return EMQ_ERR_INVALID;
  (void)emq_engine_run_once(&rt->engine, 8);
  return EMQ_OK;
}

emq_status emq_wait(emq_runtime *rt, uint32_t timeout_ms) {
  if (!rt) return EMQ_ERR_INVALID;
  if (rt->engine.sched) (void)emq_sched_wait(rt->engine.sched, timeout_ms);
  else emq_sleep_ms(timeout_ms ? timeout_ms : 1);
  (void)emq_engine_run_once(&rt->engine, 32);
  return EMQ_OK;
}

emq_status emq_run_once(emq_runtime *rt, uint32_t budget) {
  if (!rt) return EMQ_ERR_INVALID;
  (void)emq_engine_run_once(&rt->engine, budget ? budget : 64);
  return EMQ_OK;
}

emq_status emq_run(emq_runtime *rt) {
  int rc;
  if (!rt) return EMQ_ERR_INVALID;
  rc = emq_engine_start(&rt->engine);
  if (rc != 0) return emq_map_rc(rc);
  /* Single-threaded host loop until emq_runtime_stop(). */
  for (;;) {
    (void)emq_engine_run_once(&rt->engine, 64);
    if (rt->engine.sched) {
      if (emq_sched_wait(rt->engine.sched, 10) != 0 &&
          emq_engine_run_once(&rt->engine, 1) == 0) {
        /* still idle — check stop by attempting a zero-budget poll after stop */
      }
    } else {
      emq_sleep_ms(1);
    }
    /* Stop cooperatively: if start was reversed by stop, workers are gone. */
    if (rt->engine.scheduler == NULL && rt->engine.worker_count == 0)
      break;
    if (rt->engine.scheduler == NULL && rt->engine.workers == NULL) break;
  }
  return EMQ_OK;
}

emq_status emq_task_submit(emq_runtime *rt, emq_task_fn fn, void *user,
                           emq_task **out) {
  if (!rt || !fn || !out) return EMQ_ERR_INVALID;
  return emq_map_rc(emq_task_runtime_submit(&rt->engine.tasks, fn, user, rt,
                                            (struct emq_task **)out));
}

emq_status emq_task_cancel(emq_task *task) {
  emq_runtime *rt;
  if (!task) return EMQ_ERR_INVALID;
  rt = (emq_runtime *)((struct emq_task *)task)->runtime;
  if (!rt) return EMQ_ERR_INVALID;
  return emq_map_rc(
      emq_task_runtime_cancel(&rt->engine.tasks, (struct emq_task *)task));
}

void *emq_task_user(emq_task *task) {
  return task ? ((struct emq_task *)task)->user : NULL;
}

int *emq_task_line_ptr(emq_task *task) {
  return task ? &((struct emq_task *)task)->line : NULL;
}

int emq_task_yield(emq_task *task) {
  (void)task;
  return 1;
}

int emq_task_delay(emq_task *task, uint32_t ms) {
  if (!task) return 0;
  ((struct emq_task *)task)->wake_at_ns =
      emq_now_ns() + (uint64_t)ms * 1000000ULL;
  return 1;
}

int emq_task_await_pop(emq_task *task, emq_queue *q, emq_message *out) {
  emq_status st;
  (void)task;
  if (!q || !out) return 0;
  st = emq_try_pop(q, out);
  if (st == EMQ_OK) return 0;
  return 1; /* yield */
}

void emq_message_release(emq_message *message) {
  if (!message) return;
  free((void *)message->data);
  memset(message, 0, sizeof(*message));
}
