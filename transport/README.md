# Transport plane

Aeron-style media driver: moves frames between EmbeddedMQ log buffers and peers.

| Directory | Role |
| --------- | ---- |
| [`ipc/`](ipc/) | Same-host shared-memory zero-copy (`emq_ipc_*`) |
| [`net/`](net/) | Cross-host UDP media driver (`emq_net_*`) |

## Build

Core-only (default):

```bash
cmake -S core -B build
cmake --build build
ctest --test-dir build
```

Monorepo with transport:

```bash
cmake -S . -B build -DEMQ_BUILD_TRANSPORT=ON
cmake --build build
ctest --test-dir build
```

## Libraries

| Target | Description |
| ------ | ----------- |
| `emq_ipc` | POSIX `shm_open` / Windows `CreateFileMapping` ring |
| `emq_net` | UDP socket backend + frame encode/decode helpers |

## Tests

- `transport/ipc/tests/test_ipc.c` — publish / claim / release roundtrip
- `transport/net/tests/test_net_loopback.c` — UDP loopback send/recv

## NIC backends (net)

See [`net/backends/README.md`](net/backends/README.md) for io_uring, RIO, AF_XDP,
DPDK, and QUIC placement. Only `EMQ_NET_BACKEND_SOCKET` is implemented in-tree.

## Threat models

- IPC: [`ipc/THREAT_MODEL.md`](ipc/THREAT_MODEL.md)
- Formal sketch: [`net/model/nak_flow.tla`](net/model/nak_flow.tla)
