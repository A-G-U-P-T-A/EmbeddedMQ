#include "emq/emq.h"
#include "emq_test.h"

#include <string.h>

typedef struct {
  int steps;
} task_ctx;

static int demo_task(emq_task *task) {
  task_ctx *ctx = (task_ctx *)emq_task_user(task);
  EMQ_TASK_BEGIN(task);
  ctx->steps++;
  EMQ_TASK_YIELD(task);
  ctx->steps++;
  EMQ_TASK_END(task);
}

int main(void) {
  emq_runtime *rt = NULL;
  emq_task *task = NULL;
  task_ctx ctx;
  memset(&ctx, 0, sizeof(ctx));

  EMQ_CHECK(emq_runtime_create_ex(&rt, 0) == EMQ_OK);
  EMQ_CHECK(emq_task_submit(rt, demo_task, &ctx, &task) == EMQ_OK);
  EMQ_CHECK(emq_run_once(rt, 8) == EMQ_OK);
  EMQ_CHECK(ctx.steps >= 1);
  EMQ_CHECK(emq_run_once(rt, 8) == EMQ_OK);
  EMQ_CHECK(ctx.steps >= 2);
  emq_runtime_destroy(rt);
  return emq_test_report();
}
