#ifndef EMQ_IPC_H
#define EMQ_IPC_H

#include <stddef.h>
#include <stdint.h>

#include "emq/emq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emq_ipc_segment emq_ipc_segment;

/*
 * Zero-copy loan returned by emq_ipc_claim().
 * data/size are valid until emq_ipc_release() on this segment.
 * frame_head is an opaque ring offset — do not interpret or reuse after release.
 */
typedef struct emq_ipc_loan {
  void *data;
  size_t size;
  uint64_t frame_head;
} emq_ipc_loan;

/*
 * Create a named shared-memory segment (0600 on POSIX).
 * ring_capacity is the usable ring byte capacity (rounded up internally).
 */
emq_status emq_ipc_segment_create(const char *name, size_t ring_capacity,
                                  emq_ipc_segment **out);

/* Open an existing segment created by emq_ipc_segment_create(). */
emq_status emq_ipc_segment_open(const char *name, emq_ipc_segment **out);

/*
 * Unmap and close handles. The creator also unlinks the backing object.
 * Safe to call after a peer crash; does not require the peer to release first.
 */
void emq_ipc_segment_destroy(emq_ipc_segment *seg);

/*
 * Copy a payload into the ring (producer path).
 * SPSC contract: at most one concurrent publisher and one concurrent claimer.
 */
emq_status emq_ipc_publish(emq_ipc_segment *seg, const void *data, size_t size);

/*
 * Zero-copy claim (consumer path). out->data points into the mapped segment.
 *
 * Crash-robust ownership:
 * - COMMITTED frames are visible to claim after the producer finishes publish.
 * - claim marks the frame CLAIMED in shared memory; release advances head.
 * - If a consumer dies while holding a CLAIMED frame, the slot stays claimed
 *   until the segment is destroyed and recreated — there is no automatic
 *   reclaim of in-flight loans (see THREAT_MODEL.md).
 * - Producers must not reuse released slots until head catches up (ring semantics).
 */
emq_status emq_ipc_claim(emq_ipc_segment *seg, emq_ipc_loan *out,
                         uint32_t timeout_ms);

/* Release a prior claim and advance the consumer head. */
emq_status emq_ipc_release(emq_ipc_segment *seg, emq_ipc_loan *loan);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_IPC_H */
