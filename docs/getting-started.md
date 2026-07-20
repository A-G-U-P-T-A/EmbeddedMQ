# Getting started

## 1. Build the library

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Headers live in `include/emq/`. The static library is `build/libemq.a` (Unix) or the MSVC equivalent under `build/`.

## 2. Minimal queue

```c
#include "emq/emq.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  emq_runtime *rt = NULL;
  emq_queue *q = NULL;
  emq_queue_opts opts;
  emq_message m;

  if (emq_runtime_create(&rt) != EMQ_OK) return 1;

  emq_queue_opts_default(&opts);
  opts.storage = EMQ_STORAGE_FAST;
  opts.producers = 1;   /* SPSC hint */
  opts.consumers = 1;

  if (emq_queue_create(rt, "jobs", &opts, &q) != EMQ_OK) return 1;

  emq_push(q, "hello", 5, NULL);

  if (emq_pop(q, &m, 1000) == EMQ_OK) {
    printf("%.*s\n", (int)m.size, (const char *)m.data);
    emq_message_release(&m);   /* required */
  }

  emq_queue_close(q);
  emq_runtime_destroy(rt);
  return 0;
}
```

Link against `emq` (and `pthread` on POSIX; Windows links platform libs via CMake).

## 3. Pub / sub

```c
emq_subscription *sub = NULL;

emq_subscribe(rt, "sensor/#", NULL, &sub);
emq_publish(rt, "sensor.temp.kitchen", "22.5", 4);

while (emq_sub_next(sub, &m, 100) == EMQ_OK) {
  /* use m.data / m.size */
  emq_message_release(&m);
}

emq_unsubscribe(sub);
```

Pattern matching supports wildcards (see `examples/pubsub_demo.c`).

## 4. Work queue (ack / nack)

```c
emq_queue_opts_default(&opts);
opts.policy = EMQ_POLICY_WORK;
opts.visibility_ms = 5000;
emq_queue_create(rt, "tasks", &opts, &q);

emq_push(q, payload, len, NULL);
if (emq_pop(q, &m, 1000) == EMQ_OK) {
  /* do work… */
  emq_ack(q, m.id);            /* success */
  /* or: emq_nack(q, m.id, 1000);  retry after delay */
  emq_message_release(&m);
}
```

## 5. Durable queue

```c
emq_queue_opts_default(&opts);
opts.storage = EMQ_STORAGE_DURABLE;
opts.path = "./emq_data";      /* directory for log segments */
opts.fsync = EMQ_FSYNC_INTERVAL;
emq_queue_create(rt, "persist", &opts, &q);
```

Re-open later with `emq_queue_open(rt, "persist", &q)` against the same path/runtime layout.

## 6. Capacity and backpressure

```c
opts.capacity = 1024;
opts.backpressure = EMQ_BP_MODE_DROP_OLD;  /* or DROP_NEW, BLOCK, … */
```

## 7. Single-threaded / game loop

Create the runtime with zero workers and drive it yourself:

```c
emq_runtime_create_ex(&rt, 0);   /* no background workers */

for (;;) {
  emq_run_once(rt, 64);          /* bounded work slice */
  /* your frame / tick */
}
```

## Run the shipped demos

```bash
./build/examples/emq_producer_consumer
./build/examples/emq_pubsub_demo
```

## Common mistakes

| Mistake | Fix |
| ------- | --- |
| Forgetting `emq_message_release` | Always release after a successful pop/sub_next |
| Expecting cross-machine delivery | EmbeddedMQ is in-process only |
| Leaving `producers`/`consumers` unset for hot paths | Set both to `1` for SPSC FAST rings |
| Ignoring `emq_status` | Check return codes; use `emq_status_string` |

## Next

- [What EmbeddedMQ is](overview.md)
- Full API: [`include/emq/emq.h`](../include/emq/emq.h)
