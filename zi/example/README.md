# Zi examples

These programs demonstrate logging and network I/O in `libZi`. The protobuf
example lives in [zpb/example](../../zpb/example/README.md).


The TCP examples (`zimxclient`, `zimxserver`) and UDP examples
(`zimxudpclient`, `zimxudpserver`) own compatible Rx and Tx policies. They
initialize the multiplexer, construct allocator payload pools, register both
immediately on that control setup path, then start I/O. Allocator constructor
counts are independent of native kernel queue capacities. Windows compiles out
payload count arguments and uses zero-argument allocator construction.

Listeners, outbound connections and UDP setup explicitly select their registered
Rx allocator. Each transmitted buffer originates from a separately registered Tx
policy; the UDP echo server copies received payload into compatible Tx backing
instead of submitting an Rx buffer for output. Application `owner` metadata
remains independent of allocator provenance.

When `Zi::uring()` is false, policies construct existing builtin `IOBufAlloc`
storage without allocating rings. Bindings remain permanent: stop concludes I/O
and retains registered resources until final Mx teardown. There is no allocator
unbind/restart/reassignment path. See the
[buffer API guide](../README.md#buffers-and-streams) and
[test coverage notes](../test/README.md) for invariants and verification limits.
