# Zxdp integration tests

## AF_XDP

Build the tree with `make -j8`. The TAP programs skip privileged cases unless
explicitly supplied fixture interfaces. Never supply an interface carrying
traffic you need to retain: an absent mask selects all its traffic.

* `zxdppackettest` uses `ZXDP_TEST_IFINDEX` and `ZXDP_TEST_PEER_IFINDEX`, a
  fixture-owned two-queue veth pair. An independent raw packet socket verifies
  transmitted bytes and injects unmasked Ethernet reception. The default
  `ZXDP_PACKET_CASE=packets` checks registration retry, activation rollback,
  idle stop, restart on the same arena, outstanding CQ completion and backing
  returned before finalization. Use `ZXDP_PACKET_CASE=sender` for
  pending-descriptor accounting and publication ordering,
  including concurrent submission during kernel drain and shutdown while the sender
  callback is active. Use `ZXDP_PACKET_CASE=leases` for caller/CQ return ordering
  and restart. `ZXDP_PACKET_CASE=unstarted` checks finalization after allocation
  without activation. Use a fresh pair for each case because Linux releases a
  finalized registration's device pools asynchronously.
* `zxdpfiltertest` uses `ZXDP_TEST_IFINDEX`. Kernel program test runs verify
  IPv4/IPv6 masks, high-bit addresses, truncated headers, zero/one/two VLAN tags
  and absent-mask behavior. A separate attachment checks foreign-owner protection.
* `zxdpwaketest` uses the same pair. Real Rx epoll/eventfd observation verifies
  zero-spin blocking, queued/concurrent wake pulses, repeated check-to-wait races,
  packet processing without returning, and interruption during active processing
  with a large spin count. The test also verifies that returning a held frame
  off the Rx thread replenishes FILL without another packet. It can run with a
  single-queue fixture pair and uses a different side of the pair for each case.
* `zxdplooptest` requires root and `ZXDP_RUN_FIXTURE=1`; it invokes the anonymous
  namespace fixture for independent client/server utility processes, full-MTU
  and smaller packets, three application wait policies and oversized input.

```
sudo -n env ZXDP_RUN_FIXTURE=1 ./zxdp/itest/zxdplooptest
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
files. Run them directly in the current network namespace. Setup failures
appear in TAP diagnostics.

```sh
pkg-config --atleast-version=25.11 libdpdk
./z.config -c -L /usr/local
make clean
make -j8
make -C zxdp/itest -j8
prove ./zxdp/itest/zxdpdkpackettest ./zxdp/itest/zxdpdkwaketest \
  ./zxdp/itest/zxdpdklooptest
```

* `zxdpdkpackettest` checks direct submission identity including packet
  headroom offsets, refusal on a full ring, caller and PMD leases, one-time
  retirement and exact Rx pool provenance. Its pcap case verifies input and
  output bytes and the PMD's copy classification.
* `zxdpdkwaketest` observes real `ppoll` calls on the internal eventfd. It
  checks preposted and concurrent pulses, the configured idle backoff bound,
  off-owner pool return wakes, sustained input, and polling with a zero
  interval.
* `zxdpdklooptest` transfers full-MTU and short frames between named ring
  ports. An independent peer copies each transmitted frame into a destination
  ingress-pool mbuf to simulate the wire and releases the original Tx reference.
  The test checks bytes, retirement and teardown in both directions.
