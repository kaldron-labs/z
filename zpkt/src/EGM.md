# Zpkt raw packet service

## Overview

Linux AF_XDP packet allocation, receive/transmit service, XDP program attachment, and kernel retirement. Application transport composition and thread scheduling belong to callers.

## Interface

- [Zpkt.hh](./Zpkt.hh) exposes `Nif`, pool allocation and start/stop callbacks.
- [ZpktEnv.hh](./ZpktEnv.hh) owns the name-keyed device tree and loaded plugin;
  named creation queries kernel metadata and returns an initialized device.
- [ZpktPlugin.hh](./ZpktPlugin.hh) defines the native module boundary.
- [ZpktNif.hh](./ZpktNif.hh) is the internal kernel-query/rtnetlink interface.
- [Makefile.am](./Makefile.am) builds common `libZpkt.la` and installs the four
  public headers. Native implementations live in [xdp](../xdp/Makefile.am) and
  [dpdk](../dpdk/Makefile.am); their headers remain private.

## Invariants

- `Nif` requires separate single Rx and Tx owners. Allocation/free may run elsewhere. Accepted sends hold a kernel lease independently of the caller allocation lease; backing is recycled only after both end. Bytes remain immutable until kernel retirement. Rejection acquires no kernel lease; only one descriptor may be pending per slot. [Zpkt.hh](./Zpkt.hh), [ZpktXDP.cc](../xdp/ZpktXDP.cc), [ZpktPool.cc](../xdp/ZpktPool.cc)
- Allocation retains pool references until finalization, while intrusive membership stays with the caller. Freeze pool geometry while bound and return dependent buffers before final. [ZpktPool.hh](../xdp/ZpktPool.hh), [ZpktUMem.cc](../xdp/ZpktUMem.cc)
- Quiesce caller service before stop and keep the multiplexer scheduler running through completion. Posted FILL frames and kernel-owned ingress frames remain accounted for by persistent queue owners across runs. [ZpktXDP.cc](../xdp/ZpktXDP.cc)
- Link initialization requires an Ethernet interface and a dedicated scheduler sender slot. Allocation requires enough Rx slots for every queue. Ring mappings require valid aligned offsets and power-of-two counts. [ZpktXDP.cc](../xdp/ZpktXDP.cc), [ZpktRing.hh](../xdp/ZpktRing.hh)

## Side Effects

- The AF_XDP Nif constructor retains link and wake descriptors. `alloc` maps and registers shared backing and persistent FILL/CQ. Start opens per-run sockets, maps packet descriptor rings, and attaches the classifier. [ZpktXDP.cc](../xdp/ZpktXDP.cc), [ZpktUMem.cc](../xdp/ZpktUMem.cc)
- `Xdp::BufPool::init` overlays freelist links on payload backing. `send` publishes the submitted arena address, records canonical backing plus wire offset, and may kick the kernel. `pollTx` consumes at most 64 CQ entries, matches pending slots by wire offset excluding bit zero (the Linux generic-Tx destructor tag), releases matched kernel leases, and returns the number retired. Unmatched entries are consumed without incrementing that result. [ZpktPool.cc](../xdp/ZpktPool.cc), [ZpktXDP.cc](../xdp/ZpktXDP.cc)
- `Nif::rx` services queues, spins then blocks in epoll, and invokes receive callbacks on arena bytes. The wake notifier records a persistent wake pulse and writes eventfd; one pulse ends one receive invocation, including preposted pulses. [ZpktXDP.cc](../xdp/ZpktXDP.cc)
- `Xdp::classifierCode` supports destination IPv4/IPv6 filtering through two VLAN headers. Nonmatching/truncated filtered frames pass; selected frames redirect by Rx queue with a drop fallback. An empty filter selects all traffic. Attachment uses guarded program identity and driver-to-SKB fallback only on explicit unsupported-operation rejection; detach preserves foreign programs. [ZpktBPF.cc](../xdp/ZpktBPF.cc)
- Stop withdraws ingress destinations, drains TX/CQ and releases the run mapping and callbacks. Final closes the shared registration and releases the arena. [ZpktXDP.cc](../xdp/ZpktXDP.cc), [ZpktUMem.cc](../xdp/ZpktUMem.cc)

## Verification

From a configured repository root, run the scoped build/test protocol in [zpkt_plugin_plan.md](../../zpkt_plugin_plan.md). Kernel integration requires the separate fixture environment described in [utility contracts](../util/EGM.md).
