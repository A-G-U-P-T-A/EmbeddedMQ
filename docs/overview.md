# What is EmbeddedMQ?

EmbeddedMQ is an **embeddable messaging runtime** written in C11. You compile it into your application and call a small C API. Messages move between threads (and optionally persist on disk) **inside one process**.

It is not a server you deploy. It is not a wire protocol. It is the piece that sits between producers and consumers when they already share memory.

## Problem it solves

Many systems need local queues:

- game simulation → render / audio / network threads
- sensor ingest → processing pipeline
- job fan-out inside a single service
- durable local buffers before a network send

Using an external broker for that adds process hops, serialization, ops cost, and latency you often don’t need. EmbeddedMQ keeps the messaging model (queues, topics, ack, backpressure) without leaving the process.

## Mental model

```
+-----------------------------------------------+
|                 Your process                  |
|                                               |
|   Producer --push--> Queue / Topic            |
|                         |                     |
|                         v                     |
|   Consumer <--pop-- / Subscription            |
|                                               |
|   EmbeddedMQ runtime (libemq)                 |
|     - FAST rings (RAM)                        |
|     - durable / mmap logs (optional)          |
|     - scheduler, pools, timing wheel          |
+-----------------------------------------------+
```

1. Create one **runtime** per process (or logical subsystem).
2. Create **queues** (point-to-point) or **topics** (pub/sub).
3. **Push** / **publish** bytes; **pop** / **subscribe** them back.
4. Destroy the runtime on shutdown.

## Core capabilities

### Point-to-point queues

Named queues with configurable storage and policy. Supports blocking and non-blocking pop, batch ops, and async callbacks on worker threads.

### Pub/sub

Publish to a topic string; subscribe with patterns (including wildcards) and optional consumer groups. Fanout can share payload references instead of copying per subscriber on the hot path.

### Work queues

`EMQ_POLICY_WORK` adds visibility timeouts, `emq_ack` / `emq_nack`, and redelivery — the usual “at-least-once job” pattern without an external broker.

### Storage modes

- **FAST** — in-memory; SPSC uses lock-free log-buffer rings
- **DURABLE / MMAP / HYBRID** — survive process restart with a log on disk
- **RING / STREAM** — overwrite rings and offset/replay style consumption

### Backpressure

When capacity is set: drop-new, drop-old, block, spill, expand, or overwrite.

### Embedded host APIs

For games and single-threaded hosts: `emq_poll`, `emq_wait`, `emq_run_once`, `emq_run`, plus a stackless `EMQ_TASK_*` substrate.

## Architecture (high level)

| Layer | Role |
| ----- | ---- |
| Public API (`emq.h`) | Runtime, queue, pub/sub, event loop |
| Primitives | FIFO, priority, work, ring, delay, stream, pub/sub |
| Engine | Active-queue scheduler, MPMC job rings, workers |
| Core | Lock-free queues, pools, atomics, histograms, EBR |
| Platform | Threads, futex / WaitOnAddress, clocks, I/O |

The design goal of the FAST path: **producers and consumers touch shared rings with minimal locking**, while durable paths trade latency for persistence.

## When to use it

**Use EmbeddedMQ when:**

- producers and consumers are in the same process
- you want queue/topic semantics with a C API
- latency and many queues matter more than multi-host routing

**Don’t use it when:**

- you need messaging across machines out of the box
- you need a managed broker, admin UI, or multi-language clients
- you need distributed exactly-once transactions

## Relationship to other tools

| Tool | Relation |
| ---- | -------- |
| Redis / RabbitMQ / Kafka | Network brokers — use when you need multi-process or multi-host |
| `std::queue` + mutex | Simpler, but no durability, pub/sub, policies, or scheduler |
| SQLite | Great for queries; poor fit as a low-latency message bus |
| Aeron / LMAX disruptor | Similar “in-process / IPC ring” ideas; EmbeddedMQ is a fuller queue+policy runtime |

## Next steps

- [Getting started](getting-started.md)
- [Public API](../include/emq/emq.h)
- [Repository README](../README.md)
