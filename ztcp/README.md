# Ztcp: asynchronous TCP links

Ztcp provides CRTP client/server links over `ZiMultiplex`, with pooled receive
and transmit streams and explicit Rx/Tx ownership. It supplies TCP connection
lifecycle and byte transport; application framing belongs in the link's parser.
Include `<zlib/Ztcp.hh>` and link `libZtcp` and its framework dependencies.
For encrypted transport see [Ztls](../ztls/README.md); for HTTP applications use
[Zhttp](../zhttp/README.md).

## Clients and servers

A client derives from `Ztcp::Client<App>` and defines a link derived from
`Ztcp::CliLink<App, Link, RxBufAlloc, TxBufAlloc>`. A server uses
`Ztcp::Server<App>` and `Ztcp::SrvLink` instead. Select pooled buffer sizes with
`Ztcp::RxBufAlloc<N, MaxSize, HeapID>` and `Ztcp::TxBufAlloc<N>` on the link.
The application supplies primary allocators matching `App::Link::RxAllocator`
and `App::Link::TxAllocator` through the hub parameters' `rxAllocator()` and
`txAllocator()` setters. Hub initialization registers them with the multiplexer
before links or listeners are published. The receive allocator preserves the
`ZiRxBuf` queue node base. Keep the hub alive through multiplexer stop; allocators
must outlive all buffers allocated from them.

Client links implement `connected(Ztcp::Connected)`, `connectFailed(bool)`,
`disconnected(bool)` and `process(Ztcp::RxStream &)`. The application may set
its reconnect policy through `reconnFreq()`. On the server, supply `localIP()`,
`localPort()`, `nAccepts()` and `accepted(const ZiCxnInfo &)`, plus listener
success/failure callbacks. The example's accept callback creates a link and
its connection wrapper:

```cpp
// Member definition after App::Link has been declared/defined.
ZiConnection *App::accepted(const ZiCxnInfo &info) {
  return new Link::Cxn(new Link(this), info);
}
```

The full types and their lifetimes are demonstrated by
[ztcpclient.cc](example/ztcpclient.cc) and
[ztcpserver.cc](example/ztcpserver.cc). These are complete applications, whereas
the snippets here are extracts used inside those application/link types.

## Multiplexer and lifecycle

Initialize and start a configured `ZiMultiplex`, then initialize the client/server
with `Ztcp::ClientParams(&mx, rxThreadID, txThreadID)` or `ServerParams`. The hub
requires a running scheduler. Create the application-owned allocators after
multiplexer initialization and supply them before hub initialization:

```cpp
ZmRef<App::Link::RxAllocator> rx = new App::Link::RxAllocator{};
ZmRef<App::Link::TxAllocator> tx = new App::Link::TxAllocator{};
Ztcp::ClientParams params(&mx, rxThreadID, txThreadID);
params.rxAllocator(rx.ptr()).txAllocator(tx.ptr());
if (!app.init(ZuMv(params))) return false;
```

Check initialization and start results. The thread IDs select transport work
destinations; they may differ from the low-level socket Rx/Tx threads. Both
application and Tx streams allocate through the supplied primary Tx policy.
Additional application-owned Tx TCP allocators register through
`hub.allocator(allocator)`; send buffers allocated from them directly with
`link.send(buf)`. Callers of `allocBuf()` know their concrete allocator type.
Ordinary `IOBufAlloc` supplies built-in storage, with growth through the original
allocator. Each allocator stays dedicated to its multiplexer; the hub and
multiplexer retain registered allocators until their owning lifetimes end.

For clients, retain the link with `ZmRef`, call `connect(host, port)` and handle
completion asynchronously. For servers, call `start()` after successful
initialization, then `listen()`; call `stop()` to stop accepting and drain
connections before `final()`. Keep the application, multiplexer and
buffer infrastructure alive until links and queued callbacks have quiesced,
then call `final()` and stop the multiplexer. Do not block an Rx/Tx callback
waiting for work that needs the same scheduler.

A server retains the actual bound address, so `localPort() == 0` requests an
ephemeral listener that `stop()` can remove. A success callback arriving after
stop begins closes that new listener; late failures/retries cannot reopen it.
`listen()` accepts requests only while the hub is starting/running. After stop
completes, the same hub may start/listen again with its retained configuration.
Call `final()` after failed initialization before correcting parameters and
initializing again. Zfix's [integration suite](../zfix/itest/README.md) exercises
failed startup, bind conflicts, exact-port rebinding and pending-listen shutdown
through the production transport hubs.

## Receiving and framing

TCP delivers a byte stream: one read can contain a partial message or several
messages. `process()` receives a `Ztcp::RxStream`; use its `consume()` API to
recognize a complete frame before acting on it. The ping/pong examples use a
six-byte frame and preserve incomplete input until enough bytes arrive.
Return parser progress as the stream contract expects; do not equate one
callback with one application message.

The two callbacks to `consume()` separate boundary detection from delivery of
the recognized bytes. Inspect [ZtcpStreamTest](test/ZtcpStreamTest.cc) for
fragmentation and consumption behavior. Spans into received buffers are
borrowed; copy required fields or retain appropriate buffer ownership before
posting application work elsewhere.

## Transmitting and failures

Inside a link, write through the pooled transmit stream and flush explicitly:

```cpp
void sendPing() {
  auto tx = txStream();
  tx << "ping\r\n" << Zi::flush();
}
```

Install `txErrorFn(ZiTxErrorFn{...})` to handle `ZeException` transmit failures;
the examples log the exception and return `false`. Physical send failures notify
on I/O Tx; Zi closes that original socket independently of the callback result.
App Tx validation failures notify through an Rx post, where `false` requests
close only if the originating socket is still current after the callback.
Rejections detected by public `send()` are diagnostic only and notify on the
calling thread without capturing a socket origin. Callbacks must dispatch
mutations to their owner shard; they do not all run on Rx. A local flush is not a peer
application acknowledgement. Preserve Rx/Tx ownership when building replies,
and use the public dispatching entry points rather than calling internal
trailing-underscore methods from an arbitrary thread.

To try the examples in separate terminals after building:

```sh
./ztcp/example/ztcpserver 127.0.0.1 18080
./ztcp/example/ztcpclient 127.0.0.1 18080 3
```

Choose an available port for manual use; automated test reservations are listed
in the top-level testing guide.

## Examples and tests

Run `make -C ztcp test` from a configured build tree. The [test directory](test/)
contains executable API examples and boundary cases. ZtcpLoopTest collects
worker callback diagnostics and reports them on the test thread after owner
drain; the TAP manager is single-threaded. See the top-level
[testing guide](docs/TESTING.md) for environment, diagnostics and port allocations,
and [dependencies](docs/DEPENDENCIES.md) for build prerequisites.

Outgoing TCP links resolve hostnames asynchronously and consume the resolver's
terminal continuation before submitting a socket connect. Submitted DNS and TCP
operations complete normally; applications keep links, hubs and the multiplex
alive until their continuations and connection shutdown have drained. Stopping
prevents new setup work and closes a late successful socket through the ordinary
connection path. There is no protocol-imposed DNS/connect cancellation or native
setup deadline. Busy links reject overlapping connects before address/SNI mutation.
`connectRetired_()` starts protocol owner drain after pre-readiness failure;
`connectFailed()` reports native failures. Reconnect timers retain the owner and
ignore queued callbacks from earlier timer generations.
