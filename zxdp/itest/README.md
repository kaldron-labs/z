# Zxdp kernel integration

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
