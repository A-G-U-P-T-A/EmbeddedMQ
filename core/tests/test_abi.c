/*
 * ABI stability: link-complete public API + frozen layouts/enums (64-bit).
 */
#include "emq/emq.h"
#include "emq_test.h"

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#  define EMQ_STATIC_ASSERT(cond, msg) \
    typedef char emq_static_assert_##msg[(cond) ? 1 : -1]
#else
#  define EMQ_STATIC_ASSERT(cond, msg) _Static_assert(cond, #msg)
#endif

/* Enum value freeze — status */
EMQ_STATIC_ASSERT(EMQ_OK == 0, ok);
EMQ_STATIC_ASSERT(EMQ_ERR_INVALID == -1, invalid);
EMQ_STATIC_ASSERT(EMQ_ERR_NOMEM == -2, nomem);
EMQ_STATIC_ASSERT(EMQ_ERR_NOT_FOUND == -3, not_found);
EMQ_STATIC_ASSERT(EMQ_ERR_FULL == -4, full);
EMQ_STATIC_ASSERT(EMQ_ERR_EMPTY == -5, empty);
EMQ_STATIC_ASSERT(EMQ_ERR_IO == -6, io);
EMQ_STATIC_ASSERT(EMQ_ERR_TIMEOUT == -7, timeout);
EMQ_STATIC_ASSERT(EMQ_ERR_EXISTS == -8, exists);
EMQ_STATIC_ASSERT(EMQ_ERR_CLOSED == -9, closed);
EMQ_STATIC_ASSERT(EMQ_ERR_BUSY == -10, busy);
EMQ_STATIC_ASSERT(EMQ_ERR_UNSUPPORTED == -11, unsupported);

/* Storage / policy / delivery / fsync / backpressure */
EMQ_STATIC_ASSERT(EMQ_STORAGE_FAST == 0, storage_fast);
EMQ_STATIC_ASSERT(EMQ_STORAGE_DURABLE == 1, storage_durable);
EMQ_STATIC_ASSERT(EMQ_STORAGE_MMAP == 2, storage_mmap);
EMQ_STATIC_ASSERT(EMQ_STORAGE_HYBRID == 3, storage_hybrid);
EMQ_STATIC_ASSERT(EMQ_STORAGE_RING == 4, storage_ring);
EMQ_STATIC_ASSERT(EMQ_STORAGE_STREAM == 5, storage_stream);

EMQ_STATIC_ASSERT(EMQ_POLICY_FIFO == 0, policy_fifo);
EMQ_STATIC_ASSERT(EMQ_POLICY_PRIORITY == 1, policy_prio);
EMQ_STATIC_ASSERT(EMQ_POLICY_RING == 2, policy_ring);
EMQ_STATIC_ASSERT(EMQ_POLICY_BROADCAST == 3, policy_bcast);
EMQ_STATIC_ASSERT(EMQ_POLICY_STREAM == 4, policy_stream);
EMQ_STATIC_ASSERT(EMQ_POLICY_WORK == 5, policy_work);
EMQ_STATIC_ASSERT(EMQ_POLICY_DELAY == 6, policy_delay);
EMQ_STATIC_ASSERT(EMQ_POLICY_RANDOM == 7, policy_random);
EMQ_STATIC_ASSERT(EMQ_POLICY_LIFO == 8, policy_lifo);
EMQ_STATIC_ASSERT(EMQ_POLICY_PUBSUB == 9, policy_pubsub);

EMQ_STATIC_ASSERT(EMQ_AT_MOST_ONCE == 0, delivery_amo);
EMQ_STATIC_ASSERT(EMQ_AT_LEAST_ONCE == 1, delivery_alo);

EMQ_STATIC_ASSERT(EMQ_FSYNC_NONE == 0, fsync_none);
EMQ_STATIC_ASSERT(EMQ_FSYNC_EVERY_WRITE == 1, fsync_every);
EMQ_STATIC_ASSERT(EMQ_FSYNC_INTERVAL == 2, fsync_interval);

EMQ_STATIC_ASSERT(EMQ_BP_MODE_DROP_NEW == 0, bp_drop_new);
EMQ_STATIC_ASSERT(EMQ_BP_MODE_DROP_OLD == 1, bp_drop_old);
EMQ_STATIC_ASSERT(EMQ_BP_MODE_BLOCK == 2, bp_block);
EMQ_STATIC_ASSERT(EMQ_BP_MODE_SPILL == 3, bp_spill);
EMQ_STATIC_ASSERT(EMQ_BP_MODE_EXPAND == 4, bp_expand);
EMQ_STATIC_ASSERT(EMQ_BP_MODE_OVERWRITE == 5, bp_overwrite);

EMQ_STATIC_ASSERT(EMQ_MSG_FLAG_RETRY == 0x80000000u, flag_retry);
EMQ_STATIC_ASSERT(EMQ_MSG_FLAG_DEAD_LETTER == 0x40000000u, flag_dead_letter);
EMQ_STATIC_ASSERT(EMQ_MSG_FLAG_BORROWED == 0x20000000u, flag_borrowed);
EMQ_STATIC_ASSERT(EMQ_MSG_FLAG_CLAIMED == 0x10000000u, flag_claimed);
EMQ_STATIC_ASSERT((EMQ_MSG_FLAG_BORROWED & EMQ_MSG_FLAG_RETRY) == 0, flag_no_overlap_retry);
EMQ_STATIC_ASSERT((EMQ_MSG_FLAG_CLAIMED & EMQ_MSG_FLAG_DEAD_LETTER) == 0,
                  flag_no_overlap_dlq);

#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || \
    (defined(__LP64__) && __LP64__)
EMQ_STATIC_ASSERT(sizeof(emq_queue_opts) == 56 || sizeof(emq_queue_opts) == 64,
                  queue_opts_size);
EMQ_STATIC_ASSERT(offsetof(emq_queue_opts, storage) == 0, opts_storage_off);
EMQ_STATIC_ASSERT(offsetof(emq_queue_opts, path) == 24 ||
                      offsetof(emq_queue_opts, path) == 32,
                  opts_path_off);
EMQ_STATIC_ASSERT(offsetof(emq_message, id) == 0, msg_id_off);
EMQ_STATIC_ASSERT(offsetof(emq_message, offset) == 8, msg_offset_off);
EMQ_STATIC_ASSERT(offsetof(emq_message, priority) == 16, msg_prio_off);
EMQ_STATIC_ASSERT(offsetof(emq_message, deliver_at_ns) == 24, msg_deliver_off);
EMQ_STATIC_ASSERT(offsetof(emq_message, ttl_ns) == 32, msg_ttl_off);
EMQ_STATIC_ASSERT(offsetof(emq_message, data) == 40, msg_data_off);
EMQ_STATIC_ASSERT(offsetof(emq_message, size) == 48, msg_size_off);
EMQ_STATIC_ASSERT(offsetof(emq_message, flags) == 56, msg_flags_off);
EMQ_STATIC_ASSERT(sizeof(emq_message) == 64, msg_size);
EMQ_STATIC_ASSERT(sizeof(emq_stats) == 64, stats_size);
EMQ_STATIC_ASSERT(sizeof(emq_runtime_stats) == 64, runtime_stats_size);
EMQ_STATIC_ASSERT(sizeof(emq_batch_item) == sizeof(void *) + sizeof(size_t) +
                                                sizeof(emq_message) ||
                      sizeof(emq_batch_item) >= 80,
                  batch_item_size);
#endif

static void *emq_abi_symbols[] = {
    (void *)emq_runtime_create,
    (void *)emq_runtime_create_ex,
    (void *)emq_runtime_destroy,
    (void *)emq_runtime_start,
    (void *)emq_runtime_stop,
    (void *)emq_queue_create,
    (void *)emq_queue_open,
    (void *)emq_queue_close,
    (void *)emq_queue_destroy,
    (void *)emq_push,
    (void *)emq_pop,
    (void *)emq_try_pop,
    (void *)emq_peek,
    (void *)emq_claim,
    (void *)emq_release_claim,
    (void *)emq_ack,
    (void *)emq_nack,
    (void *)emq_ack_batch,
    (void *)emq_push_batch,
    (void *)emq_pop_batch,
    (void *)emq_push_async,
    (void *)emq_pop_async,
    (void *)emq_publish,
    (void *)emq_publish_ex,
    (void *)emq_publish_batch,
    (void *)emq_subscribe,
    (void *)emq_unsubscribe,
    (void *)emq_sub_next,
    (void *)emq_sub_seek,
    (void *)emq_sub_replay,
    (void *)emq_sub_retry,
    (void *)emq_sub_dead_letter,
    (void *)emq_seek,
    (void *)emq_flush,
    (void *)emq_flush_batch,
    (void *)emq_queue_snapshot,
    (void *)emq_queue_compact,
    (void *)emq_queue_stats,
    (void *)emq_get_runtime_stats,
    (void *)emq_poll,
    (void *)emq_wait,
    (void *)emq_run_once,
    (void *)emq_run,
    (void *)emq_task_submit,
    (void *)emq_task_cancel,
    (void *)emq_task_user,
    (void *)emq_task_line_ptr,
    (void *)emq_task_yield,
    (void *)emq_task_delay,
    (void *)emq_task_await_pop,
    (void *)emq_queue_opts_default,
    (void *)emq_status_string,
    (void *)emq_message_release,
};

int main(void) {
  size_t i;
  size_t n = sizeof(emq_abi_symbols) / sizeof(emq_abi_symbols[0]);
  for (i = 0; i < n; ++i) {
    EMQ_CHECK(emq_abi_symbols[i] != NULL);
  }
  EMQ_CHECK(sizeof(emq_message) >= 40);
  EMQ_CHECK(EMQ_INLINE_PAYLOAD_MAX == 256u);
  EMQ_CHECK(EMQ_NAME_MAX == 128u);
  EMQ_CHECK(EMQ_TOPIC_MAX == 256u);
  return emq_test_report();
}
