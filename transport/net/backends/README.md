# Net transport backends

Placement map for EmbeddedMQ media-driver backends. Only `SOCKET` is implemented
in-tree; others are integration stubs with documented privilege requirements.

| Backend | Directory (planned) | Role | Privileges |
| ------- | ------------------- | ---- | ---------- |
| `EMQ_NET_BACKEND_SOCKET` | `src/emq_net_socket.c` | Portable UDP default | None |
| `EMQ_NET_BACKEND_URING` | `backends/io_uring/` | Linux io_uring TX/RX batching | None (kernel 5.1+) |
| `EMQ_NET_BACKEND_RIO` | `backends/rio/` | Windows Registered I/O | None |
| `EMQ_NET_BACKEND_AF_XDP` | `backends/af_xdp/` | Linux AF_XDP zero-copy | `CAP_NET_RAW`, `CAP_BPF`, pinned maps |
| `EMQ_NET_BACKEND_DPDK` | `backends/dpdk/` | Userspace poll-mode driver | Hugepages, VFIO/IOMMU, often root |
| QUIC (msquic) | `backends/msquic/` | WAN path over QUIC | Depends on cert store / TLS policy |

## Selection order (production target)

1. **SOCKET** — development, CI, and hosts without NIC offload.
2. **io_uring / RIO** — same-host or low-latency datacenter when syscalls dominate.
3. **AF_XDP** — Linux edge nodes with controlled NIC firmware/driver pairs.
4. **DPDK** — dedicated appliances only; opt-in build (`EMQ_NET_DPDK=ON`).
5. **msquic** — cross-Internet fan-out; pairs with `EMQ_NET_FLAG_SM` session frames.

## Stub policy

Backend directories may contain README + CMake option guards only. They must not
be linked by default. Full DPDK/AF_XDP bring-up belongs in deployment guides,
not the default monorepo build graph.

## Build flags (future)

```
-DEMQ_NET_BACKEND=SOCKET|URING|RIO|AF_XDP|DPDK
-DEMQ_NET_MSQUIC=OFF
-DEMQ_NET_DPDK=OFF
```

When adding a backend, implement the `emq_net_*` surface in `include/emq_net.h`
and register it in `emq_net_create()`.
