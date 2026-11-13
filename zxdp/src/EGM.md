# Zxdp raw packet service

## Overview

Linux AF_XDP packet allocation, receive/transmit service, XDP program attachment, and kernel retirement. Application transport composition and thread scheduling belong to callers.

## Interface

- [Zxdp.hh](./Zxdp.hh): `Params` selects interface, optional destination CIDR, intrusive Tx pools, dedicated Rx pool, receive callback, and spin count. `Instance` exposes `create`, readiness/interface/MTU/MAC/zero-copy queries, `send`, `rx`, `wake`, `pollTx`, and retryable `close`; there is no transmit-completion callback.
- [ZxdpAlloc.hh](./ZxdpAlloc.hh): caller-owned `BufPool` exposes geometry, allocation/return, ownership and direct slot/backing lookup.
- [Makefile.am](./Makefile.am): exports `libZxdp.la` and installs `ZxdpLib.hh`, `Zxdp.hh`, and `ZxdpAlloc.hh`; ring, arena, BPF, and retirement headers are private. `init` and `warmup` have empty implementations in [ZxdpLib.cc](./ZxdpLib.cc).
- Internal `Ring` views manage checked reservations; `UMem` and `Xsk` manage backing and socket registration; `KernelRetirement` reports `Error`, `Pending`, or `Done`; `Filter`, `BPFProgram`, and `BPFAttach` implement selection and attachment. [ZxdpRing.hh](./ZxdpRing.hh), [ZxdpUMem.hh](./ZxdpUMem.hh), [ZxdpRetire.hh](./ZxdpRetire.hh), [ZxdpBPF.hh](./ZxdpBPF.hh)

## Invariants

- `Instance` requires separate single Rx and Tx owners. Allocation/free may run elsewhere. Accepted sends hold a kernel lease independently of the caller allocation lease; backing is recycled only after both end. Bytes remain immutable until kernel retirement. Rejection acquires no kernel lease; only one descriptor may be pending per slot. [Zxdp.hh](./Zxdp.hh), [Zxdp.cc](./Zxdp.cc), [ZxdpAlloc.cc](./ZxdpAlloc.cc)
- Keep pools alive through close and final return; freeze list/count/size while bound. `BufPool` destruction asserts no used slots. `UMem::create` requires unused pools, an acyclic Tx list, supported power-of-two page geometry and bounded fitting counts/sizes. [ZxdpAlloc.hh](./ZxdpAlloc.hh), [ZxdpUMem.cc](./ZxdpUMem.cc)
- Before `Instance::close`, wake/join Rx and quiesce Tx and wake callers. Retain a non-ready creation handle when rollback remains pending. `KernelRetirement` requires original/probe objects and packet holds until `Done`; an `Error` does not authorize release. Closing an XSK descriptor alone does not establish DMA retirement. [Zxdp.hh](./Zxdp.hh), [ZxdpRetire.hh](./ZxdpRetire.hh), [ZxdpUMem.hh](./ZxdpUMem.hh)
- `InstanceData::init` requires interface, Rx pool, callback, no pre-existing XDP program, and enough Rx buffers for all queues. `Ring::mapLength` requires safe aligned nonoverlapping offsets and a power-of-two count; mapping lifetime is external and reservations cannot overlap. [Zxdp.cc](./Zxdp.cc), [ZxdpRing.hh](./ZxdpRing.hh)

## Side Effects

- `InstanceData::init` provisions arenas/bookkeeping, epoll/eventfd, BPF maps/programs and queue sockets; `UMem::create` mmaps writable backing; `Xsk::prepare` opens AF_XDP sockets and maps rings. Kernel calls can fail; the library does not acquire privileges. [Zxdp.cc](./Zxdp.cc), [ZxdpUMem.cc](./ZxdpUMem.cc)
- `BufPool::init` overlays freelist links on payload backing. `send` publishes the submitted arena address, records canonical backing plus wire offset, and may kick the kernel. `pollTx` consumes at most 64 CQ entries, matches pending slots by wire offset excluding bit zero (the Linux generic-Tx destructor tag), releases matched kernel leases, and returns the number retired. Unmatched entries are consumed without incrementing that result. [ZxdpAlloc.cc](./ZxdpAlloc.cc), [Zxdp.cc](./Zxdp.cc)
- `Instance::rx` services queues, spins then blocks in epoll, and invokes receive callbacks on arena bytes. `InstanceData::notify` records a persistent wake pulse and writes eventfd; one pulse ends one receive invocation, including preposted pulses. [Zxdp.cc](./Zxdp.cc)
- `classifierCode` supports destination IPv4/IPv6 filtering through two VLAN headers. Nonmatching/truncated filtered frames pass; selected frames redirect by Rx queue with a drop fallback. An empty filter selects all traffic. Attachment uses guarded program identity and driver-to-SKB fallback only on explicit unsupported-operation rejection; detach preserves foreign programs. [ZxdpBPF.cc](./ZxdpBPF.cc)
- `InstanceData::close` removes destinations and retires one queue per call before bounded buffer return and descriptor/program cleanup. `KernelRetirement::retry` probes rebinding with an empty observer: busy remains pending, successful binding establishes retirement. Arena destruction unmaps backing. [Zxdp.cc](./Zxdp.cc), [ZxdpRetire.cc](./ZxdpRetire.cc), [ZxdpUMem.cc](./ZxdpUMem.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zxdp/test test`. Kernel integration requires the separate fixture environment described in [utility contracts](../util/EGM.md). No commands were executed for this documentation change.
