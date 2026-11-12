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

The TCP client/server and UDP client use `ZiRx` for message framing. Their
connection information supplies the network RxTCP/RxUDP allocator; the existing
`rxAllocator()` hook supplies the separate App framing allocator. Split headers
and messages accumulate outside `ZiRxContext`, while an exact complete message
retains the original received packet. Request completion in `zimxserver`
disarms reception before the response send; the framer preserves that change.
The server counts the complete response before emitting it and disconnects only
after all output buffers complete, including responses split at connection MPS.

For a builtin TCP working pair, run `zimxserver -f -c 4 -i 0 127.0.0.1 PORT`
and `zimxclient -f -c 4 -r 1 -i 0 127.0.0.1 PORT`. In a debug build `-f`
limits the initial read of each fresh receive slot to five bytes, exercising
split HTTP headers and incremental bodies. Both exit after four connections.
For UDP, run `zimxudpserver -n 4 -b 127.0.0.1:PORT` and
`zimxudpclient -n 4 -d 127.0.0.1:PORT`; both exit after four echoes. Start the
server and wait for its socket bind before launching its client. Choose an
unused loopback port, capture logs in a caller-owned directory, and wait for
both processes to exit before removing successful-run logs. The synthetic
builtin/stub framing and metadata matrix is in `../test/ZiRxTest.cc`.
