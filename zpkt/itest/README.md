# Zpkt integration tests

## AF_XDP

Build the configured dependency layers, then the common library, XDP module
and integration targets, using the shared eight-job make jobserver. The TAP programs skip privileged cases unless
explicitly supplied fixture interfaces. Never supply an interface carrying
traffic you need to retain: AF_XDP redirects all its traffic.

* `zpktpackettest` uses `ZPKT_TEST_IFINDEX` and `ZPKT_TEST_PEER_IFINDEX`, a
  fixture-owned two-queue veth pair. An independent raw packet socket verifies
  transmitted bytes and injects raw Ethernet reception. The default
  `ZPKT_PACKET_CASE=packets` checks registration retry, activation rollback,
  idle stop, restart on the same arena, outstanding CQ completion and backing
  returned before finalization. Use `ZPKT_PACKET_CASE=sender` for
  pending-descriptor accounting and publication ordering,
  including concurrent submission during kernel drain and shutdown while the sender
  callback is active. Use `ZPKT_PACKET_CASE=leases` for caller/CQ return ordering
  and restart. `ZPKT_PACKET_CASE=unstarted` checks finalization after allocation
  without activation. Use a fresh pair for each case because Linux releases a
  finalized registration's device pools asynchronously.
  `ZPKT_PACKET_CASE=metadata` checks real kernel UMEM metadata registration.
  `ZPKT_METADATA_CASE=0` uses the current ABI; `1` injects explicit-flag rejection
  to exercise the legacy retry; `2` rejects both metadata forms to verify clean
  fallback. The legacy behavior is simulated, not an older-kernel result.
  `ZPKT_PACKET_CASE=offload` emulates advertised checksum capability and zero-copy
  reporting while using the real kernel's test-only software-checksum option.
  An independent peer validates IPv4/IPv6 checksums, empty/odd/full-MTU payloads,
  TCP options, padding, refused sends, raw-frame slot reuse and capability changes
  across restart. The fixture checks that the production library never requests
  software checksums. This validates the metadata/wire contract, not NIC offload.
* `zpktniftest` with `ZPKT_NIF_CASE=xdp` and `ZPKT_TEST_IFINDEX` checks the
  real named Env factory: one kernel geometry request, owned configuration,
  stopped/unallocated state, cached lookup, conflicting multiplexer, invalid
  scheduler slot, missing Linux key and exact descriptor retirement. It uses
  the default real core query.
* `zpktfiltertest` uses `ZPKT_TEST_IFINDEX`. Kernel program test runs verify
  unconditional redirection of IPv4, IPv6, ARP and other Ethernet frames.
  A separate attachment checks foreign-owner protection.
* `zpktwaketest` uses the same pair. Real Rx epoll/eventfd observation verifies
  zero-spin blocking, queued/concurrent wake pulses, repeated check-to-wait races,
  packet processing without returning, and interruption during active processing
  with a large spin count. The test also verifies that returning a held frame
  off the Rx thread replenishes FILL without another packet. It can run with a
  single-queue fixture pair and uses a different side of the pair for each case.
* `zpktlooptest` requires root and `ZPKT_RUN_FIXTURE=1`; it invokes the anonymous
  namespace fixture for independent client/server utility processes, full-MTU
  and smaller packets, three application wait policies and oversized input.

```
sudo -n env ZPKT_RUN_FIXTURE=1 ./zpkt/itest/zpktlooptest
```

The tests drive the raw `rx()`/`wake()` pair and Tx completion service.
`ZmEngine` stop completes after the per-run service is drained. Keep the
multiplexer alive through that completion and return dependent buffers before
`final()` releases the registration. Kernel ring tests use actual queues and CQ
entries. Veth verifies software-copy operation. Physical NIC DMA requires a
hardware run.

## DPDK software drivers

Use DPDK 25.11 or newer with the stock `net_ring` and `net_pcap` PMDs installed.
The fixtures initialize EAL with PCI, hugepages, shared configuration and
telemetry disabled. The programs create their own named ports, rings and capture
files. Each Runtime owns an intrusive Env whose loaded DPDK plugin owns EAL.
The typed configuration is retained before ports open. Ring and pcap tests
create dummy kernel Nifs in a private network namespace; root and iproute2 are
required. Named lookup queries core geometry and opens the independently selected
native port. Setup failures appear in TAP diagnostics.

```sh
make -C zpkt/itest -j8
sudo -n prove ./zpkt/itest/zpktdkpackettest ./zpkt/itest/zpktdkwaketest \
  ./zpkt/itest/zpktdklooptest ./zpkt/itest/zpktniftest
```

* `zpktniftest` checks named DPDK creation, core-query failure, absent native
  ports, independent Linux/native names, same-name lookup and multiplexer
  conflicts. It preserves transactional pool allocation, interposed allocation
  and activation failure, native port claims, retained backing, restart,
  deferred engine requests and replacement retirement.
* `zpktdkpackettest` checks direct submission identity including packet
  headroom offsets, refusal on a full ring, caller and PMD leases, one-time
  retirement and exact Rx pool provenance. Its pcap case verifies input and
  output bytes and the PMD's copy classification.
* `zpktdkwaketest` observes real `ppoll` calls on the internal eventfd. It
  checks preposted and concurrent pulses, the configured idle backoff bound,
  off-owner pool return wakes, sustained input, and polling with a zero
  interval.
* `zpktdklooptest` transfers full-MTU and short frames between named ring
  ports. An independent peer copies each transmitted frame into a destination
  ingress-pool mbuf to simulate the wire and releases the original Tx reference.
  The test checks bytes, retirement and teardown in both directions.
