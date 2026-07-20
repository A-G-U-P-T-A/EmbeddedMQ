#ifndef EMQ_H
#define EMQ_H

#include "emq_types.h"
#include "emq_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime lifecycle */
EMQ_API emq_status emq_runtime_create(emq_runtime **out);
EMQ_API emq_status emq_runtime_create_ex(emq_runtime **out,
                                         uint32_t worker_threads);
EMQ_API void emq_runtime_destroy(emq_runtime *rt);
EMQ_API emq_status emq_runtime_start(emq_runtime *rt);
EMQ_API emq_status emq_runtime_stop(emq_runtime *rt);

/* Queue lifecycle */
EMQ_API emq_status emq_queue_create(emq_runtime *rt, const char *name,
                                    const emq_queue_opts *opts,
                                    emq_queue **out);
EMQ_API emq_status emq_queue_open(emq_runtime *rt, const char *name,
                                  emq_queue **out);
EMQ_API emq_status emq_queue_close(emq_queue *q);
EMQ_API emq_status emq_queue_destroy(emq_runtime *rt, const char *name);

/* Point-to-point messaging */
EMQ_API emq_status emq_push(emq_queue *q, const void *data, size_t size,
                            const emq_message *meta);
/* Successful pop/sub-next/claim transfer ownership semantics documented per API.
 * Owned payloads: release with emq_message_release().
 * Peek (borrowed): sets EMQ_MSG_FLAG_BORROWED — release is a no-op free. */
EMQ_API emq_status emq_pop(emq_queue *q, emq_message *out, uint32_t timeout_ms);
EMQ_API emq_status emq_try_pop(emq_queue *q, emq_message *out);
EMQ_API emq_status emq_peek(emq_queue *q, emq_message *out);

/*
 * Hot-path consume: copy payload into caller buffer (no malloc/free).
 * meta_opt may be NULL; when non-NULL, id/priority/flags/size are filled and
 * meta_opt->data is left NULL (caller owns dst). Returns EMQ_ERR_INVALID if
 * the message does not fit in dst_cap (message remains queued).
 */
EMQ_API emq_status emq_pop_into(emq_queue *q, void *dst, size_t dst_cap,
                                size_t *out_size, emq_message *meta_opt,
                                uint32_t timeout_ms);

/*
 * Push the same payload `count` times in one call (amortize FFI).
 * *pushed is set to the number successfully enqueued.
 */
EMQ_API emq_status emq_push_n(emq_queue *q, const void *data, size_t size,
                              size_t count, size_t *pushed);

/*
 * Batch try-pop into a stride buffer: max_count slots of msg_cap bytes each.
 * First wait uses timeout_ms; subsequent pops are non-blocking.
 * out_sizes_opt may be NULL; when non-NULL it must hold max_count size_t.
 * *out_count is messages copied. Returns EMQ_OK if out_count > 0.
 */
EMQ_API emq_status emq_pop_into_n(emq_queue *q, void *dst, size_t msg_cap,
                                  size_t max_count, size_t *out_count,
                                  size_t *out_sizes_opt, uint32_t timeout_ms);

EMQ_API emq_status emq_ack(emq_queue *q, uint64_t message_id);
EMQ_API emq_status emq_nack(emq_queue *q, uint64_t message_id,
                            uint32_t delay_ms);
EMQ_API emq_status emq_ack_batch(emq_queue *q, const uint64_t *ids,
                                 size_t count);
EMQ_API emq_status emq_push_batch(emq_queue *q, const emq_batch_item *items,
                                  size_t count, size_t *pushed);
EMQ_API emq_status emq_pop_batch(emq_queue *q, emq_message *messages,
                                 size_t capacity, size_t *popped);

/*
 * Zero-copy claim: out->data points into the FAST ring until
 * emq_release_claim(). Sets EMQ_MSG_FLAG_CLAIMED. Not valid after release.
 */
EMQ_API emq_status emq_claim(emq_queue *q, emq_message *out,
                             uint32_t timeout_ms);
EMQ_API emq_status emq_release_claim(emq_queue *q, emq_message *message);

/* Async callbacks execute on runtime workers (requires worker_threads > 0). */
EMQ_API emq_status emq_push_async(emq_queue *q, const void *data, size_t size,
                                  const emq_message *meta,
                                  emq_completion_cb callback, void *user);
EMQ_API emq_status emq_pop_async(emq_queue *q, uint32_t timeout_ms,
                                 emq_message_cb callback, void *user);

/* Pub/Sub */
EMQ_API emq_status emq_publish(emq_runtime *rt, const char *topic,
                               const void *data, size_t size);
EMQ_API emq_status emq_publish_ex(emq_runtime *rt, const char *topic,
                                  const void *data, size_t size,
                                  const emq_message *meta, uint64_t *offset);
EMQ_API emq_status emq_publish_batch(emq_runtime *rt, const char *topic,
                                     const emq_batch_item *items, size_t count,
                                     size_t *published);
EMQ_API emq_status emq_subscribe(emq_runtime *rt, const char *pattern,
                                 const char *group, emq_subscription **out);
EMQ_API emq_status emq_unsubscribe(emq_subscription *sub);
EMQ_API emq_status emq_sub_next(emq_subscription *sub, emq_message *out,
                                uint32_t timeout_ms);
EMQ_API emq_status emq_sub_seek(emq_subscription *sub, uint64_t offset);
EMQ_API emq_status emq_sub_replay(emq_subscription *sub);
EMQ_API emq_status emq_sub_retry(emq_subscription *sub, const char *retry_topic,
                                 uint64_t message_id, uint32_t attempt,
                                 uint32_t delay_ms);
EMQ_API emq_status emq_sub_dead_letter(emq_subscription *sub, const char *topic,
                                       uint64_t message_id, uint32_t attempts);

/* Stream / replay / maintenance */
EMQ_API emq_status emq_seek(emq_queue *q, uint64_t offset);
EMQ_API emq_status emq_flush(emq_queue *q);
EMQ_API emq_status emq_flush_batch(emq_runtime *rt, emq_queue **queues,
                                   size_t count);
EMQ_API emq_status emq_queue_snapshot(emq_queue *q);
EMQ_API emq_status emq_queue_compact(emq_queue *q);
EMQ_API emq_status emq_queue_stats(emq_queue *q, emq_stats *out);
EMQ_API emq_status emq_get_runtime_stats(emq_runtime *rt,
                                         emq_runtime_stats *out);

/*
 * Embedded event loop (games / single-threaded hosts).
 * With worker_threads=0, the caller drives the engine via these APIs.
 */
EMQ_API emq_status emq_poll(emq_runtime *rt);
EMQ_API emq_status emq_wait(emq_runtime *rt, uint32_t timeout_ms);
EMQ_API emq_status emq_run_once(emq_runtime *rt, uint32_t budget);
EMQ_API emq_status emq_run(emq_runtime *rt);

/* Stackless task substrate (protothread-style). C-only macros; not for FFI. */
typedef struct emq_task emq_task;
typedef int (*emq_task_fn)(emq_task *task);
EMQ_API emq_status emq_task_submit(emq_runtime *rt, emq_task_fn fn, void *user,
                                   emq_task **out);
EMQ_API emq_status emq_task_cancel(emq_task *task);
EMQ_API void *emq_task_user(emq_task *task);
EMQ_API int *emq_task_line_ptr(emq_task *task);
EMQ_API int emq_task_yield(emq_task *task);
EMQ_API int emq_task_delay(emq_task *task, uint32_t ms);
EMQ_API int emq_task_await_pop(emq_task *task, emq_queue *q, emq_message *out);

#define EMQ_TASK_BEGIN(task) switch (*emq_task_line_ptr(task)) { case 0:
#define EMQ_TASK_YIELD(task)                                                       \
  do {                                                                             \
    *emq_task_line_ptr(task) = __LINE__;                                           \
    return 1;                                                                      \
    case __LINE__:;                                                                \
  } while (0)
#define EMQ_TASK_END(task)                                                         \
  }                                                                                \
  (void)(task);                                                                    \
  return 0

/* Helpers */
EMQ_API void emq_queue_opts_default(emq_queue_opts *opts);
EMQ_API const char *emq_status_string(emq_status s);
EMQ_API void emq_message_release(emq_message *message);

/*
 * SPSC contract: producers=1 and consumers=1 means at most one thread may
 * push and at most one thread may pop. Violating this corrupts the ring.
 * In debug builds, owner-thread checks abort on violation.
 */

#ifdef __cplusplus
}
#endif

#endif /* EMQ_H */
