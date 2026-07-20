# IPC transport threat model

This document covers the same-host shared-memory plane (`transport/ipc/`).
Cross-host threats live under `transport/net/THREAT_MODEL.md` (future).

## Assets

- Payload bytes in the mapped ring buffer
- Segment metadata (`head`, `tail`, frame `status`)
- Segment name (used to join producer/consumer processes)

## Trust boundaries

| Actor | Trust |
| ----- | ----- |
| Creator process | Trusted to initialize header (`magic`, `version`, `capacity`) |
| Peer opener | Untrusted except it shares the host and knows the segment name |
| Other local users | Must **not** obtain read/write via filesystem permissions |

## Permissions (POSIX)

- Segments are created with `shm_open(..., O_CREAT|O_EXCL, 0600)`.
- Mode `0600` limits access to the owning uid.
- Consumers open with `O_RDWR` — they must run as the same uid or as root.

## Permissions (Windows)

- Objects are created under `Local\emq_ipc_<name>` (session-local).
- DACL inherits from the creator process token; treat the segment name as a
  shared secret on multi-user hosts.

## TOCTOU

1. **Open after unlink** — A creator may destroy/unlink while a peer still maps
   the view. Openers validate `magic`/`version`/`map_bytes` before use.
2. **Header vs ring** — Frame `status` transitions are ordered: EMPTY →
   COMMITTED (publish) → CLAIMED (claim) → EMPTY (release). A peer must not
   assume a CLAIMED frame is safe to overwrite until head advances.
3. **Name squatting** — `O_EXCL` on create prevents replacing an live segment;
   openers that see invalid headers must fail closed.

## Stale process cleanup

- `creator_pid` is recorded at create time for diagnostics only.
- **Claimed-but-never-released frames are not auto-reclaimed.** If a consumer
  crashes mid-loan, that ring slot remains CLAIMED until the segment is
  destroyed and recreated.
- Operators should:
  - Restart consumers before producers when possible
  - Recreate the segment after confirmed consumer death
  - Size the ring for worst-case in-flight claims

## Crash-robust ownership summary

| Phase | Producer | Consumer | On crash |
| ----- | -------- | -------- | -------- |
| publish | Writes payload, sets COMMITTED | — | COMMITTED frame may be visible |
| claim | — | Sets CLAIMED, reads loan | Slot stuck CLAIMED |
| release | — | Clears frame, advances head | Same as claim stall |

There is no kernel-enforced lease timeout; higher layers must implement
heartbeats or segment recycling policies.
