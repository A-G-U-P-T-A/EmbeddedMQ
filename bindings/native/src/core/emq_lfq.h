#ifndef EMQ_LFQ_H
#define EMQ_LFQ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_lfq emq_lfq;

enum {
  EMQ_LFQ_FRAME_EMPTY = 0,
  EMQ_LFQ_FRAME_CLAIMED = 1,
  EMQ_LFQ_FRAME_COMMITTED = 2
};

int emq_lfq_create(emq_lfq **out, uint32_t capacity_bytes, int spsc);
void emq_lfq_destroy(emq_lfq *q);

int emq_lfq_try_push(emq_lfq *q, const void *data, uint32_t len,
                     uint64_t msg_id, uint32_t flags, uint32_t priority);
int emq_lfq_try_pop(emq_lfq *q, void *out_buf, uint32_t out_cap,
                    uint32_t *out_len, uint64_t *out_msg_id, uint32_t *out_flags,
                    uint32_t *out_priority);
/* Consumer-side: length of the frame at head without consuming it.
 * Skips padding frames. Returns 0 on success, -5 when empty. */
int emq_lfq_peek_len(emq_lfq *q, uint32_t *out_len);
/* Zero-copy claim: out_ptr points into the ring until emq_lfq_release_claim. */
int emq_lfq_try_claim(emq_lfq *q, const void **out_ptr, uint32_t *out_len,
                      uint64_t *out_msg_id, uint32_t *out_flags,
                      uint32_t *out_priority, uint64_t *out_head);
int emq_lfq_release_claim(emq_lfq *q, uint64_t head, uint32_t len);
uint32_t emq_lfq_depth_approx(const emq_lfq *q);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_LFQ_H */
