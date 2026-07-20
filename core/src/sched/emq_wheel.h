#ifndef EMQ_WHEEL_H
#define EMQ_WHEEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*emq_wheel_cb)(void *user, uint64_t deadline_ns, void *cookie);

typedef struct emq_wheel emq_wheel;

/* Hierarchical timing wheel: tick_ms granularity, levels with 256 slots each. */
int emq_wheel_create(emq_wheel **out, uint32_t tick_ms, uint32_t levels);
void emq_wheel_destroy(emq_wheel *w);

int emq_wheel_schedule(emq_wheel *w, uint64_t deadline_ns, void *cookie);
int emq_wheel_cancel(emq_wheel *w, void *cookie);

/* Advance wheel to now_ns; invoke cb for each expired timer. Returns # fired. */
uint32_t emq_wheel_tick(emq_wheel *w, uint64_t now_ns, emq_wheel_cb cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_WHEEL_H */
