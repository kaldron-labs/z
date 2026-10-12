# Zdb unit tests

`ZdbFBTest` checks the native hash-backed DBState adapter against canonical
message readers and writers. It covers heartbeat serialization, wide sequence
numbers, receive-side merging, repeated loads, save filters, empty states and
absent states.
Run it from the configured build tree with `make -C zdb/test ZdbFBTest` and
`./zdb/test/ZdbFBTest`; it is also included in `make -C zdb/test test`.
