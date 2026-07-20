# Memory ordering — LFQ ring

This document describes the happens-before relationships for the lock-free queue
(`emq_lfq`) used by FAST storage and zero-copy claim/release.

## Variables

| Field | Location | Writers | Readers |
| ----- | -------- | ------- | ------- |
| `tail` | ring control | producer(s) | consumer(s), producer reserve |
| `head` | ring control | consumer | producer reserve, consumer |
| `frame.status` | slot header | producer then consumer | consumer |
| `frame.length`, payload | slot body | producer before COMMITTED | consumer after COMMITTED |

`head` and `tail` are `uint64_t` byte offsets into a power-of-two buffer (logical
stream, not a classic index).

## Frame status state machine

```
EMPTY ──(producer fill)──> COMMITTED ──(consumer claim optional)──> CLAIMED
  ^                              │                                    │
  └──────── release/pop ─────────┴────────────────────────────────────┘
```

- Producer sets payload fields, then stores `status = COMMITTED` (release semantics
  implied by data dependency on single-producer paths).
- Consumer loads `status == COMMITTED` before reading payload.
- Pop/claim clears `status` to `EMPTY` and advances `head` with release.

## Ordering rules

### Producer reserve (`emq_lfq_reserve`)

1. Load `head` (relaxed) and `tail` (relaxed).
2. Check free space using `tail - head`.
3. Store new `tail` with **release** (SPSC plain store or MPMC CAS).
4. Write frame header/body in the reserved range.
5. Store `status = COMMITTED`.

The tail advance **releases** the reserved region to the consumer.

### Consumer observe (`try_pop`, `try_claim`, `peek_len`)

1. Load `head` (relaxed).
2. Load `tail` with **acquire**.
3. If `head >= tail`, queue appears empty.
4. Load `status`; must be `COMMITTED` before reading `length`/payload.
5. On consume: store `status = EMPTY`, then store `head` advance with **release**.

The acquire load of `tail` pairs with the producer's release store of `tail`,
ensuring published frames are visible.

### Zero-copy claim

- `try_claim` does **not** advance `head`; it returns a pointer into the ring.
- `release_claim` performs the `status` clear and `head` release advance.
- Callers must not access the loan after `release_claim`.

## SPSC vs MPMC

| Mode | tail update | head update |
| ---- | ----------- | ----------- |
| SPSC (`spsc=1`) | plain release store | plain release store |
| MPMC | CAS on tail | CAS not used on head (single consumer assumed for claim) |

Violating the documented producer/consumer counts is undefined behavior.

## Padding frames

Negative `length` marks wrap padding. Consumers skip padding with a release
advance of `head` without exposing payload data.

## Related APIs

- `emq_push` / `emq_pop` — copy paths; same ordering via LFQ internals.
- `emq_claim` / `emq_release_claim` — public zero-copy; stash `head` in
  `emq_message.offset` for release.

## IPC transport note

Shared-memory IPC (`transport/ipc/`) mirrors the same status/head/tail contract
in mapped memory; see `transport/ipc/THREAT_MODEL.md` for crash behavior.
