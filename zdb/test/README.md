# Zdb unit tests

`ZdbFBTest` checks the native hash-backed DBState adapter against canonical
message readers and writers. It covers heartbeat serialization, wide sequence
numbers, receive-side merging, repeated loads, save filters, empty states and
absent states.
Run it from the configured build tree with `make -C zdb/test ZdbFBTest` and
`./zdb/test/ZdbFBTest`; it is also included in `make -C zdb/test test`.

`ZdbSagaRepeatTest` exercises zero and four repeated effects, compensation in
expanded-step order, overflowing-count admission rejection, and activation
failure for malformed or overflowing saved payloads. It runs with the other
unit binaries through `make test`; it can also run directly from this directory.

Saga definitions, phase bodies, codec metadata, and CRTP instantiations live in
the individual `ZdbTest*Saga.cc` files. `ZdbSagaTestDefs.hh` exposes contexts
and non-template factories; the test callers assemble context-typed descriptor
arrays without including concrete saga definitions. `ZdbRepairDefs.hh` exposes
the shared repair row/context and its saga factory.
