# Zxdp raw packet service

## Overview

Linux AF_XDP packet allocation, receive/transmit service, XDP program attachment, and kernel retirement. Application transport composition and thread scheduling belong to callers.

## Interface

- [Zxdp.hh](./Zxdp.hh) exposes synchronous link initialization with a borrowed multiplexer and tagged `Params`, pool allocation, and start/stop callbacks. See the [lifecycle contract](../INTERFACE.md).
- [ZxdpPool.hh](./ZxdpPool.hh): internal `Xdp::BufPool` implements `Zi::IOBufPool`; callers obtain owning pool references through the selected instance's virtual `bufPool`.
- [Makefile.am](./Makefile.am) builds `libZxdp.la` and installs `ZxdpLib.hh` and `Zxdp.hh`. Pool, ring, arena, and BPF headers are private.
- Internal ring views manage reservations. `Xdp::UMem` lays out pools and `Xdp::Xsk` owns ring mappings and socket binds. The BPF layer selects traffic and guards link attachment. [ZxdpRing.hh](./ZxdpRing.hh), [ZxdpUMem.hh](./ZxdpUMem.hh), [ZxdpBPF.hh](./ZxdpBPF.hh)

## Invariants

- `Instance` requires separate single Rx and Tx owners. Allocation/free may run elsewhere. Accepted sends hold a kernel lease independently of the caller allocation lease; backing is recycled only after both end. Bytes remain immutable until kernel retirement. Rejection acquires no kernel lease; only one descriptor may be pending per slot. [Zxdp.hh](./Zxdp.hh), [Zxdp.cc](./Zxdp.cc), [ZxdpPool.cc](./ZxdpPool.cc)
- Allocation retains pool references until finalization, while intrusive membership stays with the caller. Freeze pool geometry while bound and return dependent buffers before final. [ZxdpPool.hh](./ZxdpPool.hh), [ZxdpUMem.cc](./ZxdpUMem.cc)
- Quiesce caller service before stop and keep the multiplexer scheduler running through completion. Posted FILL frames and kernel-owned ingress frames remain accounted for by persistent queue owners across runs. [Zxdp.cc](./Zxdp.cc)
- Link initialization requires an Ethernet interface and a dedicated scheduler sender slot. Allocation requires enough Rx slots for every queue. Ring mappings require valid aligned offsets and power-of-two counts. [Zxdp.cc](./Zxdp.cc), [ZxdpRing.hh](./ZxdpRing.hh)

## Side Effects

- `Instance::init` retains link and wake descriptors. `alloc` maps and registers shared backing and persistent FILL/CQ. Start opens per-run sockets, maps packet descriptor rings, and attaches the classifier. [Zxdp.cc](./Zxdp.cc), [ZxdpUMem.cc](./ZxdpUMem.cc)
- `Xdp::BufPool::init` overlays freelist links on payload backing. `send` publishes the submitted arena address, records canonical backing plus wire offset, and may kick the kernel. `pollTx` consumes at most 64 CQ entries, matches pending slots by wire offset excluding bit zero (the Linux generic-Tx destructor tag), releases matched kernel leases, and returns the number retired. Unmatched entries are consumed without incrementing that result. [ZxdpPool.cc](./ZxdpPool.cc), [Zxdp.cc](./Zxdp.cc)
- `Instance::rx` services queues, spins then blocks in epoll, and invokes receive callbacks on arena bytes. The wake notifier records a persistent wake pulse and writes eventfd; one pulse ends one receive invocation, including preposted pulses. [Zxdp.cc](./Zxdp.cc)
- `Xdp::classifierCode` supports destination IPv4/IPv6 filtering through two VLAN headers. Nonmatching/truncated filtered frames pass; selected frames redirect by Rx queue with a drop fallback. An empty filter selects all traffic. Attachment uses guarded program identity and driver-to-SKB fallback only on explicit unsupported-operation rejection; detach preserves foreign programs. [ZxdpBPF.cc](./ZxdpBPF.cc)
- Stop withdraws ingress destinations, drains TX/CQ and releases the run mapping and callbacks. Final closes the shared registration and releases the arena. [Zxdp.cc](./Zxdp.cc), [ZxdpUMem.cc](./ZxdpUMem.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zxdp/test test`. Kernel integration requires the separate fixture environment described in [utility contracts](../util/EGM.md).
