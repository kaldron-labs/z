# Zxdp isolated tests

Build from the top level with `make -j8`, then `make -C zxdp/test test`.
The AF_XDP TAP programs need no network privileges:

* `ZxdpRingTest`: queried layouts, capacity, descriptor/cursor wrap, reservations,
  cancellation and release/acquire publication between concurrent kernel peers.
* `ZxdpPoolTest`: intrusive configuration, count/size layout, independent
  reserves, exhaustion, advanced pointer return, retained mapping lifetime,
  malformed configuration, direct Zi metadata allocation/return over native slots,
  and contended payload-overlay recycling.
* `ZxdpOffloadTest`: bounded TCP header classification, VLAN/IP/TCP options,
  padding, malformed input, unaligned metadata placement, generic-netlink scalar
  reply parsing and private arena-prefix geometry including full-page fallback.
* `ZxdpInstanceTest`: cold selection, synchronous init failure, idempotent final,
  independent pool factory lifetime and unchanged caller configuration.
* `ZxdpClassifierTest`: unconditional XSK redirect instruction emission,
  and guarded attachment preconditions.

Ring publication tests intentionally run producer/consumer service loops to
exercise their memory ordering. They join both owners before examining results.
Actual sockets and packet exchange belong to [itest](../itest/README.md).

Run a selected release binary under Memcheck with the repository wrapper:

```
./libtool exec valgrind --error-exitcode=99 --leak-check=full \
  --errors-for-leak-kinds=definite,indirect ./zxdp/test/ZxdpPoolTest
```

## DPDK

The build adds two TAP programs when DPDK is enabled. Use DPDK 25.11 or newer
with the stock `net_ring` driver installed. Run them as an unprivileged user
with regular memory and software ports.

* `ZxdpDpdkInstanceTest` runs EAL option cases in fresh processes. It checks
  automatic and explicit lcore affinity, named and raw arguments, native errors
  and one-time initialization. Its device cases check discovery, exclusive port
  binding, injected allocation and activation failures, exact pool-set retry,
  idle stop, retained backing across restart, and reopening after finalization.
* `ZxdpDpdkPoolTest` checks native mbuf identity, offset lookup, pool geometry,
  exhaustion, caller and device leases, and rollback of provisional allocations.

After a DPDK-enabled build, run both programs from the repository root.

```sh
prove ./zxdp/test/ZxdpDpdkInstanceTest ./zxdp/test/ZxdpDpdkPoolTest
```
