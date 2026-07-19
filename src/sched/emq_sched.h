#ifndef EMQ_SCHED_H
#define EMQ_SCHED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMQ_SCHED_NUM_BANDS 4u

typedef struct emq_sched emq_sched;

typedef struct emq_sched_stats {
  uint64_t activations;
  uint64_t wakeups;
  uint64_t drained;
} emq_sched_stats;

int emq_sched_init(emq_sched **out, uint32_t max_queues);
void emq_sched_destroy(emq_sched *sched);

int emq_sched_activate(emq_sched *sched, uint32_t queue_id, uint32_t band);
int emq_sched_deactivate(emq_sched *sched, uint32_t queue_id, uint32_t band);

int emq_sched_pop_ready(emq_sched *sched, uint32_t *queue_id_out,
                        uint32_t *credit_out);
int emq_sched_wait(emq_sched *sched, uint32_t timeout_ms);
void emq_sched_wake(emq_sched *sched);

void emq_sched_get_stats(const emq_sched *sched, emq_sched_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_SCHED_H */
