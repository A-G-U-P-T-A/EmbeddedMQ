#ifndef EMQ_ENGINE_H
#define EMQ_ENGINE_H

#include "registry/emq_registry.h"
#include "routing/emq_routing.h"
#include "sched/emq_wheel.h"
#include "sched/emq_sched.h"
#include "engine/emq_mpmc.h"
#include "engine/emq_eventloop.h"
#include "platform/emq_platform.h"
#include "core/emq_pool.h"
#include "core/emq_domain.h"
#include "core/emq_ebr.h"
#include "core/emq_hist.h"
#include "core/emq_task.h"

#if !defined(_MSC_VER) && !defined(__cplusplus)
#include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*emq_engine_job_fn)(void *arg);

typedef struct emq_engine_job {
  emq_engine_job_fn fn;
  void *arg;
} emq_engine_job;

typedef struct emq_engine_timer emq_engine_timer;

#if defined(_MSC_VER)
typedef volatile long emq_engine_atomic_int;
#elif defined(__cplusplus)
typedef int emq_engine_atomic_int;
#else
typedef _Atomic int emq_engine_atomic_int;
#endif

typedef struct emq_engine {
  emq_registry registry;
  emq_router router;
  emq_wheel *wheel;
  emq_mpmc *work_q;
  emq_sched *sched;
  emq_pool *pool;
  emq_ebr_domain *ebr;
  emq_domain domain;
  emq_hist latency_hist;
  emq_task_runtime tasks;
  emq_eventloop *loop;
  emq_thread **workers;
  uint32_t worker_count;
  emq_engine_atomic_int running;
  emq_thread *scheduler;
  emq_mutex *timer_mu;
  emq_engine_timer *timers;
  uint64_t worker_jobs;
  uint64_t wakeups;
} emq_engine;

int emq_engine_init(emq_engine *e, uint32_t worker_threads);
void emq_engine_shutdown(emq_engine *e);
int emq_engine_start(emq_engine *e);
int emq_engine_stop(emq_engine *e);

/* Allocate and queue an internal job for execution by a worker. */
int emq_engine_submit(emq_engine *e, emq_engine_job_fn fn, void *arg);
/* Schedule a worker job at an absolute monotonic deadline in nanoseconds. */
int emq_engine_schedule(emq_engine *e, uint64_t deadline_ns,
                        emq_engine_job_fn fn, void *arg);

/* Drive scheduler + wheel + tasks once (embedded event loop). */
uint32_t emq_engine_run_once(emq_engine *e, uint32_t budget);
int emq_engine_activate_queue(emq_engine *e, uint32_t queue_id, uint32_t band);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_ENGINE_H */
