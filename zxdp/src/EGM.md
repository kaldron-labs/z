# Zxdp raw packet service

## Overview

Linux AF_XDP packet allocation, receive/transmit service, XDP program attachment, and kernel retirement. Application transport composition and thread scheduling belong to callers.

## Interface

- [Zxdp.hh](./Zxdp.hh): `Params` is a tagged union; `Xdp::Params` selects interface index, optional destination CIDR, intrusive Tx pools, dedicated Rx pool, receive callback, and spin count. `instance(Type::Xdp)` selects a cold polymorphic `Instance` exposing union-dispatched `init(const Params &)` after in-place payload construction with `Params::New<Params::Index<Xdp::Params>{}>{}`, readiness/MTU/MAC/zero-copy queries, `send`, `rx`, `wake`, `pollTx`, and retryable `close`; there is no transmit-completion callback.
- [ZxdpPool.hh](./ZxdpPool.hh): internal `Xdp::BufPool` implements `Zi::IOBufPool`; callers obtain owning pool references through the selected instance's virtual `bufPool`.
- [Makefile.am](./Makefile.am): exports `libZxdp.la` and installs `ZxdpLib.hh` and `Zxdp.hh`; pool, ring, arena, BPF, and retirement headers are private. `init` and `warmup` have empty implementations in [ZxdpLib.cc](./ZxdpLib.cc).
- Internal `Xdp::Ring` views manage checked reservations; `Xdp::UMem` and `Xdp::Xsk` manage backing and socket registration; `Xdp::KernelRetirement` reports `Error`, `Pending`, or `Done`; `Xdp::Filter`, `Xdp::BPFProgram`, and `Xdp::BPFAttach` implement selection and attachment. [ZxdpRing.hh](./ZxdpRing.hh), [ZxdpUMem.hh](./ZxdpUMem.hh), [ZxdpRetire.hh](./ZxdpRetire.hh), [ZxdpBPF.hh](./ZxdpBPF.hh)

## Invariants

- `Instance` requires separate single Rx and Tx owners. Allocation/free may run elsewhere. Accepted sends hold a kernel lease independently of the caller allocation lease; backing is recycled only after both end. Bytes remain immutable until kernel retirement. Rejection acquires no kernel lease; only one descriptor may be pending per slot. [Zxdp.hh](./Zxdp.hh), [Zxdp.cc](./Zxdp.cc), [ZxdpPool.cc](./ZxdpPool.cc)
- Initialization retains its own reference per pool through close; intrusive membership remains only in the caller configuration list. Retain a pool reference for packets that outlive the instance, and freeze links/count/size while bound. `Xdp::BufPool` destruction asserts no used slots. `Xdp::UMem::create` requires unused pools, an owning Tx list, supported power-of-two page geometry and bounded fitting counts/sizes. [ZxdpPool.hh](./ZxdpPool.hh), [ZxdpUMem.cc](./ZxdpUMem.cc)
- Before `Instance::close`, wake/join Rx and quiesce Tx and wake callers. Retain the selected instance after init failure until rollback and close finish. `Xdp::KernelRetirement` requires original/probe objects and packet holds until `Done`; an `Error` does not authorize release. Closing an XSK descriptor alone does not establish DMA retirement. [Zxdp.hh](./Zxdp.hh), [ZxdpRetire.hh](./ZxdpRetire.hh), [ZxdpUMem.hh](./ZxdpUMem.hh)
- `Instance::init` requires interface, Rx pool, callback, no pre-existing XDP program, and enough Rx buffers for all queues. `Xdp::Ring::mapLength` requires safe aligned nonoverlapping offsets and a power-of-two count; mapping lifetime is external and reservations cannot overlap. [Zxdp.cc](./Zxdp.cc), [ZxdpRing.hh](./ZxdpRing.hh)

## Side Effects

- `Instance::init` provisions arenas/bookkeeping, epoll/eventfd, BPF maps/programs and queue sockets; `Xdp::UMem::create` mmaps writable backing; `Xdp::Xsk::prepare` opens AF_XDP sockets and maps rings. Kernel calls can fail; the library does not acquire privileges. [Zxdp.cc](./Zxdp.cc), [ZxdpUMem.cc](./ZxdpUMem.cc)
- `Xdp::BufPool::init` overlays freelist links on payload backing. `send` publishes the submitted arena address, records canonical backing plus wire offset, and may kick the kernel. `pollTx` consumes at most 64 CQ entries, matches pending slots by wire offset excluding bit zero (the Linux generic-Tx destructor tag), releases matched kernel leases, and returns the number retired. Unmatched entries are consumed without incrementing that result. [ZxdpPool.cc](./ZxdpPool.cc), [Zxdp.cc](./Zxdp.cc)
- `Instance::rx` services queues, spins then blocks in epoll, and invokes receive callbacks on arena bytes. The backend wake notifier records a persistent wake pulse and writes eventfd; one pulse ends one receive invocation, including preposted pulses. [Zxdp.cc](./Zxdp.cc)
- `Xdp::classifierCode` supports destination IPv4/IPv6 filtering through two VLAN headers. Nonmatching/truncated filtered frames pass; selected frames redirect by Rx queue with a drop fallback. An empty filter selects all traffic. Attachment uses guarded program identity and driver-to-SKB fallback only on explicit unsupported-operation rejection; detach preserves foreign programs. [ZxdpBPF.cc](./ZxdpBPF.cc)
- `Instance::close` removes destinations and retires one queue per call before bounded buffer return and descriptor/program cleanup. `Xdp::KernelRetirement::retry` probes rebinding with an empty observer: busy remains pending, successful binding establishes retirement. Arena destruction unmaps backing. [Zxdp.cc](./Zxdp.cc), [ZxdpRetire.cc](./ZxdpRetire.cc), [ZxdpUMem.cc](./ZxdpUMem.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zxdp/test test`. Kernel integration requires the separate fixture environment described in [utility contracts](../util/EGM.md). No commands were executed for this documentation change.
