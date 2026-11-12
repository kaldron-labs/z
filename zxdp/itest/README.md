# Zxdp kernel integration

Build the tree with `make -j8`. The TAP programs skip privileged cases unless
explicitly supplied fixture interfaces. Never supply an interface carrying
traffic you need to retain: an absent mask selects all its traffic.

* `zxdppackettest` uses `ZXDP_TEST_IFINDEX` and `ZXDP_TEST_PEER_IFINDEX`, a
  fixture-owned two-queue veth pair. An independent raw packet socket verifies
  transmitted bytes and injects unmasked Ethernet reception. The test requires
  actual CQ completion, small Tx-pool transmission, kernel retirement during
  close, late Rx return and removal of the owned XDP program.
* `zxdpfiltertest` uses `ZXDP_TEST_IFINDEX`. Kernel program test runs verify
  IPv4/IPv6 masks, high-bit addresses, truncated headers, zero/one/two VLAN tags
  and absent-mask behavior. A separate attachment checks foreign-owner protection.
* `zxdpwaketest` uses the same pair. Real epoll/eventfd observation verifies
  zero-spin blocking, queued/concurrent wake pulses, repeated check-to-wait races,
  packet processing without returning, and interruption during active processing
  with a large spin count. It can also run with a single-queue fixture pair.
* `zxdplooptest` requires root and `ZXDP_RUN_FIXTURE=1`; it invokes the anonymous
  namespace fixture for independent client/server utility processes, full-MTU
  and smaller packets, three application wait policies and oversized input.

```
sudo -n env ZXDP_RUN_FIXTURE=1 ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  ./zxdp/itest/zxdplooptest
```

The tests drive the raw `rx()`/`wake()` pair and Tx completion service. Close retries
observe deferred kernel retirement with an absolute deadline; expiry never
permits backing release. Kernel ring tests use actual queues, not a synthetic
completion as evidence of retirement. Veth establishes software-copy behavior;
physical NIC DMA requires a hardware run.
