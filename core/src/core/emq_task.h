#ifndef EMQ_TASK_H
#define EMQ_TASK_H

#include "emq/emq_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct emq_task {
  int line;
  int done;
  int cancelled;
  uint64_t wake_at_ns;
  void *user;
  emq_status (*fn)(struct emq_task *task);
  /* stored as void* to avoid circular include; cast to emq_task_fn in api */
  int (*fn_int)(struct emq_task *task);
  struct emq_runtime *runtime;
  struct emq_task *next;
};

typedef struct emq_task_runtime {
  struct emq_task *ready;
  struct emq_task *sleeping;
  void *mu;
  int running;
} emq_task_runtime;

int emq_task_runtime_init(emq_task_runtime *tr);
void emq_task_runtime_destroy(emq_task_runtime *tr);
int emq_task_runtime_submit(emq_task_runtime *tr, int (*fn)(struct emq_task *),
                            void *user, struct emq_runtime *rt,
                            struct emq_task **out);
int emq_task_runtime_cancel(emq_task_runtime *tr, struct emq_task *task);
uint32_t emq_task_runtime_pump(emq_task_runtime *tr, uint32_t budget);

#ifdef __cplusplus
}
#endif

#endif /* EMQ_TASK_H */
