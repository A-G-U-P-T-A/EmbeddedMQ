#include "engine/emq_engine.h"
#include "platform/emq_platform.h"
#include "core/emq_cpu.h"

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#  include <intrin.h>

static void emq_engine_running_init(emq_engine *e) {
  (void)_InterlockedExchange(&e->running, 0);
}

static int emq_engine_running_load(const emq_engine *e) {
  return (int)_InterlockedCompareExchange(
      (volatile long *)&e->running, 0, 0);
}

static void emq_engine_running_store(emq_engine *e, int running) {
  (void)_InterlockedExchange(&e->running, (long)running);
}
#else
static void emq_engine_running_init(emq_engine *e) {
  atomic_init(&e->running, 0);
}

static int emq_engine_running_load(const emq_engine *e) {
  return atomic_load_explicit(&e->running, memory_order_acquire);
}

static void emq_engine_running_store(emq_engine *e, int running) {
  atomic_store_explicit(&e->running, running, memory_order_release);
}
#endif

struct emq_engine_timer {
  emq_engine_job job;
  struct emq_engine_timer *next;
};

static void emq_engine_discard_jobs(emq_engine *e) {
  void *item = NULL;
  if (!e || !e->work_q) return;
  while (emq_mpmc_pop(e->work_q, &item) == 0) {
    free(item);
    item = NULL;
  }
}

static int emq_engine_enqueue_job(emq_engine *e, emq_engine_job *job) {
  int rc;
  if (!e || !e->work_q || !job || !job->fn) return -1;
  rc = emq_mpmc_push(e->work_q, job);
  if (rc == 0 && e->sched) emq_sched_wake(e->sched);
  return rc;
}

static void emq_wheel_submit(void *user, uint64_t deadline_ns, void *cookie) {
  emq_engine *e = (emq_engine *)user;
  emq_engine_timer **link;
  emq_engine_timer *timer = NULL;
  int rc;
  (void)deadline_ns;

  if (!e || !e->timer_mu) return;
  emq_mutex_lock(e->timer_mu);
  link = &e->timers;
  while (*link) {
    if ((void *)*link == cookie) {
      timer = *link;
      *link = timer->next;
      timer->next = NULL;
      break;
    }
    link = &(*link)->next;
  }
  emq_mutex_unlock(e->timer_mu);

  if (!timer) return;
  do {
    if (!emq_engine_running_load(e)) {
      free(timer);
      return;
    }
    rc = emq_engine_enqueue_job(e, &timer->job);
    if (rc == -4) emq_sleep_ms(1);
  } while (rc == -4);
  if (rc != 0) free(timer);
}

static void emq_engine_clear_timers(emq_engine *e) {
  emq_engine_timer *timer;
  if (!e || !e->timer_mu) return;
  emq_mutex_lock(e->timer_mu);
  timer = e->timers;
  e->timers = NULL;
  emq_mutex_unlock(e->timer_mu);

  while (timer) {
    emq_engine_timer *next = timer->next;
    if (e->wheel) (void)emq_wheel_cancel(e->wheel, timer);
    free(timer);
    timer = next;
  }
}

static void emq_scheduler_main(void *arg) {
  emq_engine *e = (emq_engine *)arg;
  while (emq_engine_running_load(e)) {
    emq_wheel_tick(e->wheel, emq_now_ns(), emq_wheel_submit, e);
    (void)emq_task_runtime_pump(&e->tasks, 16);
    if (e->ebr) (void)emq_ebr_try_reclaim(e->ebr);
    if (e->loop) {
      emq_eventloop_poll(e->loop, 1);
    } else {
      emq_sleep_ms(1);
    }
  }
}

static void emq_worker_main(void *arg) {
  emq_engine *e = (emq_engine *)arg;
  for (;;) {
    void *item = NULL;
    uint32_t qid = 0;
    uint32_t credit = 0;

    /* Pop ready queue ids (never scan). Credit is informational for now;
     * async jobs still arrive via the work MPMC. */
    if (e->sched) {
      (void)emq_sched_pop_ready(e->sched, &qid, &credit);
      (void)credit;
    }

    if (emq_mpmc_pop(e->work_q, &item) == 0) {
      emq_engine_job *job = (emq_engine_job *)item;
      job->fn(job->arg);
      free(job);
      e->worker_jobs++;
    } else {
      if (!emq_engine_running_load(e)) break;
      if (e->sched) {
        (void)emq_sched_wait(e->sched, 1);
        e->wakeups++;
      } else {
        emq_sleep_ms(1);
      }
    }
  }
}

int emq_engine_init(emq_engine *e, uint32_t worker_threads) {
  if (!e) return -1;
  memset(e, 0, sizeof(*e));
  emq_cpu_init();
  emq_engine_running_init(e);
  emq_hist_init(&e->latency_hist);

  e->pool = emq_pool_create();
  if (!e->pool) return -2;
  if (emq_ebr_domain_create(&e->ebr) != 0) {
    emq_engine_shutdown(e);
    return -2;
  }
  if (emq_domain_init(&e->domain, 0) != 0) {
    emq_engine_shutdown(e);
    return -2;
  }
  e->domain.pool = e->pool;

  if (emq_registry_init(&e->registry, 1024) != 0) {
    emq_engine_shutdown(e);
    return -2;
  }
  emq_registry_set_pool(&e->registry, e->pool);

  if (emq_router_init(&e->router) != 0) {
    emq_engine_shutdown(e);
    return -2;
  }
  if (emq_wheel_create(&e->wheel, 1, 4) != 0) {
    emq_engine_shutdown(e);
    return -2;
  }
  if (emq_mpmc_create(&e->work_q, 1024) != 0) {
    emq_engine_shutdown(e);
    return -2;
  }
  if (emq_sched_init(&e->sched, EMQ_MAX_QUEUES) != 0) {
    emq_engine_shutdown(e);
    return -2;
  }
  e->domain.scheduler = e->sched;

  if (emq_task_runtime_init(&e->tasks) != 0) {
    emq_engine_shutdown(e);
    return -2;
  }

  e->timer_mu = emq_mutex_create();
  if (!e->timer_mu) {
    emq_engine_shutdown(e);
    return -2;
  }
  emq_eventloop_create(&e->loop);
  e->worker_count = worker_threads; /* 0 = caller-driven event loop */
  e->domain.worker_count = e->worker_count;
  if (e->worker_count) {
    e->workers = (emq_thread **)calloc(e->worker_count, sizeof(emq_thread *));
    if (!e->workers) {
      emq_engine_shutdown(e);
      return -2;
    }
  }
  return 0;
}

void emq_engine_shutdown(emq_engine *e) {
  if (!e) return;
  emq_engine_stop(e);
  emq_engine_clear_timers(e);
  emq_mutex_destroy(e->timer_mu);
  e->timer_mu = NULL;
  free(e->workers);
  e->workers = NULL;
  emq_eventloop_destroy(e->loop);
  e->loop = NULL;
  emq_task_runtime_destroy(&e->tasks);
  emq_sched_destroy(e->sched);
  e->sched = NULL;
  emq_mpmc_destroy(e->work_q);
  e->work_q = NULL;
  emq_wheel_destroy(e->wheel);
  e->wheel = NULL;
  emq_router_destroy(&e->router);
  emq_registry_destroy(&e->registry);
  emq_domain_destroy(&e->domain);
  emq_ebr_domain_destroy(e->ebr);
  e->ebr = NULL;
  emq_pool_destroy(e->pool);
  e->pool = NULL;
}

int emq_engine_start(emq_engine *e) {
  uint32_t i;
  if (!e) return -1;
  if (emq_engine_running_load(e)) return 0;
  emq_engine_running_store(e, 1);

  /* Always run timing-wheel thread; workers optional. */
  if (emq_thread_create(&e->scheduler, emq_scheduler_main, e) != 0) {
    emq_engine_running_store(e, 0);
    return -2;
  }
  for (i = 0; i < e->worker_count; ++i) {
    if (emq_thread_create(&e->workers[i], emq_worker_main, e) != 0) {
      emq_engine_stop(e);
      return -2;
    }
  }
  return 0;
}

int emq_engine_stop(emq_engine *e) {
  uint32_t i;
  if (!e) return -1;
  emq_engine_running_store(e, 0);
  if (e->sched) emq_sched_wake(e->sched);
  if (e->scheduler) {
    emq_thread_join(e->scheduler);
    emq_thread_destroy(e->scheduler);
    e->scheduler = NULL;
  }
  if (e->workers) {
    for (i = 0; i < e->worker_count; ++i) {
      if (e->workers[i]) {
        emq_thread_join(e->workers[i]);
        emq_thread_destroy(e->workers[i]);
        e->workers[i] = NULL;
      }
    }
  }
  emq_engine_discard_jobs(e);
  return 0;
}

int emq_engine_submit(emq_engine *e, emq_engine_job_fn fn, void *arg) {
  emq_engine_job *job;
  int rc;
  if (!e || !fn || !e->work_q) return -1;
  job = (emq_engine_job *)malloc(sizeof(*job));
  if (!job) return -2;
  job->fn = fn;
  job->arg = arg;
  rc = emq_engine_enqueue_job(e, job);
  if (rc != 0) free(job);
  return rc;
}

int emq_engine_schedule(emq_engine *e, uint64_t deadline_ns,
                        emq_engine_job_fn fn, void *arg) {
  emq_engine_timer *timer;
  emq_engine_timer **link;
  int rc;
  if (!e || !fn || !e->wheel || !e->timer_mu) return -1;
  timer = (emq_engine_timer *)calloc(1, sizeof(*timer));
  if (!timer) return -2;
  timer->job.fn = fn;
  timer->job.arg = arg;

  emq_mutex_lock(e->timer_mu);
  timer->next = e->timers;
  e->timers = timer;
  emq_mutex_unlock(e->timer_mu);

  rc = emq_wheel_schedule(e->wheel, deadline_ns, timer);
  if (rc != 0) {
    emq_mutex_lock(e->timer_mu);
    link = &e->timers;
    while (*link && *link != timer) link = &(*link)->next;
    if (*link == timer) *link = timer->next;
    emq_mutex_unlock(e->timer_mu);
    free(timer);
  }
  return rc;
}

int emq_engine_activate_queue(emq_engine *e, uint32_t queue_id, uint32_t band) {
  if (!e || !e->sched) return -1;
  return emq_sched_activate(e->sched, queue_id, band);
}

uint32_t emq_engine_run_once(emq_engine *e, uint32_t budget) {
  uint32_t did = 0;
  uint32_t qid = 0;
  uint32_t credit = 0;
  if (!e) return 0;
  emq_wheel_tick(e->wheel, emq_now_ns(), emq_wheel_submit, e);
  did += emq_task_runtime_pump(&e->tasks, budget ? budget : 16);
  while (did < budget && e->sched &&
         emq_sched_pop_ready(e->sched, &qid, &credit) == 0) {
    did++;
  }
  if (e->loop) emq_eventloop_poll(e->loop, 0);
  if (e->ebr) (void)emq_ebr_try_reclaim(e->ebr);
  return did;
}
