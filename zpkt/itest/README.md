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
  without activation. `ZPKT_PACKET_CASE=vector` uses the private test module
  to observe ordered producer commits, first-frame handoff, prefix admission,
  capacity pressure and retry.
  Use a fresh pair for each case because Linux releases a
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
  with a large spin count. Healthy off-owner frame returns produce no eventfd write.
  After the receiver exhausts its FILL supply, the test returns a single frame
  and verifies one wake, one repost and delivery of the next peer packet. With
  queue steering enabled by the namespace fixture, it drains both queues and
  verifies that successive sole returns restore traffic on each. It also runs
  with a single-queue fixture pair, using a different side for each case.
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
The typed configuration is retained before ports open. The fixtures use a
private network namespace and require root and iproute2. Named lookup opens
the selected native port and takes geometry from ethdev. Setup failures appear
in TAP diagnostics.

```sh
make -C zpkt/itest -j8
sudo -n prove ./zpkt/itest/zpktdkpackettest ./zpkt/itest/zpktdkwaketest \
  ./zpkt/itest/zpktdklooptest ./zpkt/itest/zpktniftest
```

* `zpktniftest` checks named DPDK creation without host lookup, absent native
  ports, independent Linux/native names, same-name lookup and multiplexer
  conflicts. It preserves transactional pool allocation, interposed allocation
  and activation failure, native port claims, retained backing, restart,
  deferred engine requests and replacement retirement.
* `zpktdkpackettest` checks direct submission identity including packet
  headroom offsets, refusal on a full ring, caller and PMD leases, return
  order across pools, exact-once retirement and zero slot visits on an empty
  poll. Test-only callbacks gate return publication and pool scheduling races.
  The pcap case verifies immediate driver returns, wire bytes and copy mode.
  A ring ethdev with interposed hardware steering calls checks whole-port
  flow validation, install failure, handle retention on destroy failure and restart.
  Physical mlx5 acceptance additionally needs an independent Ethernet peer,
  unicast, multicast, broadcast, VLAN and non-IP probes, and a kernel receiver
  that resumes delivery after finalization.
* `zpktdkwaketest` observes real `ppoll` calls on the internal eventfd. It
  checks preposted and concurrent pulses, the configured idle backoff bound,
  off-owner pool return wakes, sustained input, and polling with a zero
  interval.
* `zpktdklooptest` queries loopback before creation, opens `_loop` without
  performing an OS lookup, and verifies its Env cache and native port claim.
  It transfers full-MTU and short frames over a ring pair. An independent
  peer copies each transmitted frame into a destination
  ingress-pool mbuf to simulate the wire and releases the original Tx reference.
  The test checks bytes, retirement and teardown in both directions.

## AF_PACKET

Build `zpkt/afp` and the integration targets, then run the privileged wire
fixture. It loads `libZpkt_afp.so` through Env and uses isolated veth
endpoints. It checks bidirectional Ethernet exchange at peer ingress with
bypass enabled and disabled, outgoing suppression, and restart. An independent
UDP socket checks host delivery before open and after finalization, and
suppression while the AFP interface is open, running and stopped. A competing
Env attempts to claim the same interface. In a DPDK build the same probe runs
against `net_af_packet`, including a competing native AFP open. A burst of 65
valid frames into an idle 64-frame ring must yield 64 deliveries, one overflow,
and continued delivery after draining.

```sh
sudo -n env ZPKT_RUN_FIXTURE=1 prove ./zpkt/itest/zpktafp_test
```

The fixture checks the software socket path. For a loader smoke test with
the installed module on the library search path, run
`ZPKT_NATIVE_MODULE=libZpkt_afp.so prove ./zpkt/test/ZpktEnvTest`.
