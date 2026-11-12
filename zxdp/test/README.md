# Zxdp isolated tests

Build from the top level with `make -j8`, then `make -C zxdp/test test`.
These four TAP programs need no network privileges:

* `ZxdpRingTest`: queried layouts, capacity, descriptor/cursor wrap, reservations,
  cancellation and release/acquire publication between concurrent kernel peers.
* `ZxdpPoolTest`: intrusive configuration, count/size layout, independent
  reserves, exhaustion, advanced pointer return, retained mapping lifetime,
  malformed configuration and contended payload-overlay recycling.
* `ZxdpInstanceTest`: creation failure leaves caller-owned pools unbound.
* `ZxdpClassifierTest`: mask normalization, instruction relocation and branches,
  and guarded attachment preconditions.

Ring publication tests intentionally run producer/consumer service loops to
exercise their memory ordering. They join both owners before examining results.
Actual sockets and packet exchange belong to [itest](../itest/README.md).

Run a selected release binary under Memcheck with the repository wrapper:

```
./libtool exec valgrind --error-exitcode=99 --leak-check=full \
  --errors-for-leak-kinds=definite,indirect ./zxdp/test/ZxdpPoolTest
```
