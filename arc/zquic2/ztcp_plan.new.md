## Summary

Add a new `ztcp` module that mirrors the `ztls` module shape and builds a
separate `libZtcp` library/DLL.  `Ztcp` provides raw TCP/IP transport with the
same application-facing stream model that `Ztls` exposes to HTTP/1.x code:
`App`, `Engine`, `Client`, `Server`, `Link`, `CliLink`, `SrvLink`, `Cxn`,
`RxStream`, `txStream()`, and `txStream_()`.

`Ztcp` is not a TLS shim.  It has the same internal dependencies as the useful
non-cryptographic parts of `Ztls` (`zu`, `zm`, `zt`, `ze`, `zi`) and no
dependency on `zpicotls`, OpenSSL, `@PTLS_CFLAGS@`, or `@PTLS_LIBS@`.

The primary application requirement is that `Zhttp` applications can consume
`Ztcp::RxStream` and produce request/response bytes through `Ztcp` Tx streams
without parser or builder changes.  `zhttpclient` will support `http:` by using
`Ztcp`, while preserving `https:` through `Ztls`.

The most important correction to the original plan is receive framing.  `ZiRx`
is a framed-message helper; it waits until a header parser returns a full frame.
Raw TCP is a byte stream and must deliver whatever bytes arrive to the HTTP
parser.  `Ztcp::Link` should therefore use a custom `ZiConnection::recv_()`
callback rather than copying `Ztls`'s TLS-record `ZiRx::recv<Hdr, Body>()`
path.

## Architecture Documentation

### New Components

- `ztcp/Makefile.am` - module subdirectory list: `src test example`.
- `ztcp/src/ZtcpLib.hh` - DLL/export macros and `Ztcp::lib_init()`.
- `ztcp/src/ZtcpLib.cc` - module initialization implementation.
- `ztcp/src/Ztcp.hh` - public raw TCP transport templates and stream types.
- `ztcp/src/Makefile.am` - builds `libZtcp.la`.
- `ztcp/test/Makefile.am` - builds standalone test binaries and a `prove`
  `test` target.
- `ztcp/example/Makefile.am` - builds raw TCP examples.
- `ztcp/example/ZtcpClient.cc` and `ztcp/example/ZtcpServer.cc` - raw echo
  examples mirroring the role of the TLS examples, without CA/cert/key args.
- `zhttp/src/ZhttpClient.hh` - new library header containing shared HTTP/1
  client helper code in `namespace Zhttp::Client`, factored out of
  `zhttpclient.cc` and parameterized by transport link and stream type.
- `zhttp/example/zhttpclient.cc` - thin CLI and transport selection wrapper.

### Changed Components

- `Makefile.am` will add `ztcp` before `zhttp`.
- `configure.ac` will add `ztcp` to the module symlink loop and
  `AC_CONFIG_FILES`.
- `zhttp/src/Makefile.am` will install `ZhttpClient.hh`.
- `zhttp/example/Makefile.am` will include `ztcp/src` and link `libZtcp.la`.

### Threads and Processes

`Ztcp` keeps the `Ztls` separation between I/O threads and transport-owned
application callback threads:

- `ZiMultiplex` Rx thread owns socket readiness/completion.
- `Ztcp` Rx thread runs `connected()`, `disconnected()`, reconnect scheduling,
  and `process(RxStream &)`.
- `Ztcp` Tx thread sends buffers produced by application threads or by
  `txStream_()`.

No async TLS event loop exists in `Ztcp`.  `EngineParams::asyncThread`,
`ZiEventLoop`, TLS async job state, certificates, ALPN, tickets, and TLS context
state are intentionally absent.

### Interfaces

Keep the non-TLS public shape:

```c++
namespace Ztcp {
  using ErrorFn = ZmFn<void(ZeException)>;

  struct EngineParams;
  struct ClientParams;
  struct ServerParams;

  template <typename Link, typename LinkRef> class Cxn;
  template <typename Link> using CliCxn = Cxn<Link, Link *>;
  template <typename Link> using SrvCxn = Cxn<Link, ZmRef<Link>>;

  using RxStream = Ztcp_::RxStream;
  template <unsigned Size, unsigned MaxSize, ZuString HeapID>
  using RxBufAlloc = Ztcp_::BufAlloc<Size, MaxSize, HeapID>;
  template <unsigned Size, unsigned MaxSize, ZuString HeapID>
  using TxBufAlloc = Ztcp_::BufAlloc<Size, MaxSize, HeapID>;

  template <typename App, typename Impl,
    typename RxBufAlloc = Ztcp::RxBufAlloc<>,
    typename TxBufAlloc = Ztcp::TxBufAlloc<>>
  class CliLink;

  template <typename App, typename Impl,
    typename RxBufAlloc = Ztcp::RxBufAlloc<>,
    typename TxBufAlloc = Ztcp::TxBufAlloc<>>
  class SrvLink;

  template <typename App> class Engine;
  template <typename App> class Client;
  template <typename App> class Server;
}
```

Drop TLS-only public vocabulary:

- `Ticket`, `ALPNData`, `ALPN`, `TlsInfo`
- `caPath`, `certPath`, `keyPath`, `asyncThread`, `alpn`, `mTLS`,
  `cacheTimeout`
- `tlsInfo()`, `maxEarlyData()`, key update, TLS alert, handshake APIs

Raw callback signatures:

```c++
// Client link, on Ztcp Rx thread
void connected();
void disconnected();
void connectFailed(bool transient);
int process(Ztcp::RxStream &rx);

// Server link, on Ztcp Rx thread
void connected();
void disconnected();
int process(Ztcp::RxStream &rx);
```

The no-argument `connected()` callback is the resolved design.  Passing fake
ALPN or TLS version values would couple raw TCP applications to meaningless TLS
metadata.

### Data Flows

Receive flow:

1. `ZiConnection::connected()` invokes `Cxn::connected(ZiIOContext &io)` on the
   `ZiMultiplex` Rx thread.
2. `Ztcp::Link::connected_0()` posts `connected_1()` to the `Ztcp` Rx thread.
3. `connected_0()` installs a raw receive callback with `ZiConnection::recv_()`
   or directly initializes the supplied `ZiIOContext`.
4. Each socket read fills a `ZiIOBuf`.  The callback sets `buf->length` to the
   actual byte count, posts that buffer to the `Ztcp` Rx thread, allocates a
   fresh receive buffer, reinitializes the `ZiIOContext`, and returns false so
   the receive context remains active.
5. The `Ztcp` Rx thread pushes the buffer into `m_rxStream` and repeatedly calls
   `impl()->process(m_rxStream)` while the stream is non-empty.

Transmit flow:

1. Application code obtains `auto tx = link.txStream()` or, on the `Ztcp` Tx
   thread, `link.txStream_()`.
2. `Zi::TxStream` writes bytes into `ZiIOBuf` buffers with `headRoom = 0` and
   `tailRoom = 0`.
3. App-thread sends post to the `Ztcp` Tx thread.
4. Tx-thread sends call `ZiTx<Impl>::send()`, which queues buffers and uses the
   underlying `ZiConnection::send()`/`send_()` machinery.

### Event-Driven and Timer Processing

- Client reconnect uses the same optional `reconnFreq()` pattern as
  `Ztls::Client`.  A transient connect failure schedules `connect_()` on the
  `Ztcp` Rx thread using `ZmScheduler::Timer`.
- Server rebind/listen retry uses the same optional `rebindFreq()` pattern as
  `Ztls::Server`.
- Disconnect removes reconnect timers and closes or gracefully disconnects the
  active `ZiConnection`.

### Network Programming

`ZiMultiplex::connect()` and `ZiMultiplex::listen()` are the only socket entry
points.  Hostnames are resolved through the existing `ZiIP ip = m_server`
pattern used by `Ztls::CliLink::connect_()`.  `Ztcp` should preserve the same
failure behavior: hostname lookup failure calls `connectFailed(true)`.

There are no new data stores.

## Detailed Design and Implementation Plan

### Phase 1 - Build Skeleton and Export Surface

Add the `ztcp` directory tree and build files without implementing live network
behavior yet.

Files to add:

```text
ztcp/Makefile.am
ztcp/src/Makefile.am
ztcp/src/ZtcpLib.hh
ztcp/src/ZtcpLib.cc
ztcp/src/Ztcp.hh
ztcp/test/Makefile.am
ztcp/example/Makefile.am
```

`ztcp/Makefile.am`:

```make
SUBDIRS = src test example
```

`ztcp/src/Makefile.am` should mirror the internal dependency chain of
`ztls/src/Makefile.am`, minus PTLS:

```make
METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I${top_srcdir}/zi/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZtcpLib.hh Ztcp.hh
lib_LTLIBRARIES = libZtcp.la
libZtcp_la_CPPFLAGS = $(AM_CPPFLAGS) -DZTCP_EXPORTS
libZtcp_la_SOURCES = ZtcpLib.cc
libZtcp_la_LIBADD = $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
```

Wire the top-level build:

- `Makefile.am`: change `SUBDIRS += ztls zquic zhttp` to
  `SUBDIRS += ztls ztcp zquic zhttp`.
- `configure.ac`: add `ztcp` to the symlink loop:
  `for i in zu zm zt ze zi ztls ztcp zquic ...`.
- `configure.ac`: add
  `ztcp/Makefile ztcp/src/Makefile ztcp/test/Makefile ztcp/example/Makefile`
  after the `ztls` entries in `AC_CONFIG_FILES`.

`ZtcpLib.hh` should follow `ZtlsLib.hh` naming exactly, with `ZtcpAPI`,
`ZtcpExplicit`, `ZtcpExtern`, and `Ztcp::lib_init()`.

Dependency scrutiny:

- `libZtcp.la` needs no `@PTLS_*@` inputs because raw TCP uses only `Zi`,
  `Ze`, `Zt`, `Zm`, and `Zu`.
- `@Z_IO_LIBS@`, `@Z_ZT_LIBS@`, `@Z_MT_LIBS@`, and `@Z_LIBS@` remain needed
  because `ZiMultiplex`, logging/errors, threads, PCRE-backed `Zt` objects, and
  platform libraries are still part of the dependency chain.

### Phase 2 - Engine and Parameter Validation

Implement `Ztcp::EngineParams`, `ClientParams`, `ServerParams`, and
`Engine<App>` before live links.  This allows early validation tests and keeps
the first code slice small.

`EngineParams` fields:

```c++
struct EngineParams {
  EngineParams(
    ZiMultiplex *mx_ = nullptr,
    ZuCSpan rxThread_ = {},
    ZuCSpan txThread_ = {});

  EngineParams &&errorFn(ErrorFn v);

  ZiMultiplex *mx = nullptr;
  ParamString rxThread;
  ParamString txThread;
  ErrorFn errorFn_;
};
```

Validation should match the non-TLS subset of `Ztls::Engine::validate_()`:

- multiplexer must be non-null
- selected Rx thread must exist
- selected Tx thread must exist
- selected Rx and Tx thread IDs must differ
- multiplexer must already be running

Do not validate an async thread, certificates, ALPN, mTLS, or cache timeout.

Expose helpers:

```c++
ZiMultiplex *mx() const;
unsigned rxThread() const;
unsigned txThread() const;
template <typename ...Args> void rxRun(Args &&...args);
template <typename ...Args> void rxInvoke(Args &&...args);
bool rxInvoked();
template <typename ...Args> void txRun(Args &&...args);
template <typename ...Args> void txInvoke(Args &&...args);
bool txInvoked();
```

`final()` only clears callbacks/state; it does not stop TLS event loops because
none exist.

Add `ZtcpInitTest.cc` in this phase.  It should create a `ZiMultiplex`, start
it when needed, and verify each validation branch.  This is the first vertical
slice: build wiring, exported header, engine init, and a runnable test.

### Phase 3 - Raw Link Receive/Transmit Slice

Implement `Ztcp::Cxn`, `RxStream`, buffer allocators, and `Link` with live raw
receive and transmit.  This phase should include a tiny in-process loopback
test before examples.

Key implementation decisions:

- Keep `Ztcp_::IOQueue`, `Ztcp_::RxStream = ZiRxStream<IOQueue>`, and
  `Ztcp_::BufAlloc` analogous to `Ztls_`.
- Do not inherit from `ZiRx`.  `ZiRx` is correct for framed TLS records but
  wrong for raw TCP byte streams because its header function must identify a
  full message length before the body callback fires.
- Inherit from `ZiTx<Impl>` for the existing queued send behavior.

Suggested `Link` base:

```c++
template <
  typename App, typename Impl, typename RxBufAlloc_, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_>
class Link :
  public ZmPolymorph,
  public ZiTx<Impl>
{
  // ...
};
```

Receive callback behavior:

```c++
void connected_0(Cxn *cxn, ZiIOContext &io) {
  app()->rxRun([impl = ZmMkRef(this->impl()), cxn = ZmMkRef(cxn)]() {
    impl->connected_1(ZuMv(cxn));
  });
  recvRaw_(io);
}
```

`recvRaw_(ZiIOContext &io)` should install a `ZiIOFn` whose object is a
`ZiIOBuf`.  On each invocation after `ZiConnection::executedRecv()`:

- add `io.length` to the buffer length
- if `io.length < 0`, complete/disconnect
- if bytes were read, post the filled buffer to `Ztcp` Rx thread
- allocate a fresh `RxBufAlloc` and initialize `io.ptr`, `io.size`,
  `io.offset = 0`, `io.length = 0`
- return `false` to keep the receive context active

The Rx-thread delivery function should enforce connection identity:

```c++
void rcvd_(Cxn *cxn, ZmRef<ZiIOBuf> buf) {
  if (ZuUnlikely(m_cxn != cxn)) return;
  m_rxStream.push(ZuMv(buf));
  while (m_rxStream) {
    int n = impl()->process(m_rxStream);
    if (!n) return;
    if (n < 0) { disconnect_(true); return; }
  }
}
```

Transmit stream:

- Use `Zi::TxStream` with `maxSize = TxBufAlloc::Size` or the allocated buffer
  size, `headRoom = 0`, and `tailRoom = 0`.
- `allocBuf_(0)` creates `new TxBufAlloc{impl()}`, sets `skip = 0`,
  `length = 0`.
- App-thread `send()` posts to `app()->txInvoke(...)`.
- Tx-thread `send_()` asserts `app()->txInvoked()` and calls
  `ZiTx<Impl>::send(ZuMv(buf))`.

Disconnect:

- `disconnect()` posts to `Ztcp` Rx thread.
- `disconnect_(bool notify = true)` removes reconnect timers, marks
  disconnecting, clears `m_cxn`, and either calls `cxn->disconnect()` on the
  `ZiMultiplex` Tx thread or `cxn->close()` for abrupt close.
- There is no TLS close-notify payload.

Add `ZtcpLoopTest.cc` in this phase:

- start server on `127.0.0.1` with port `0` if `ZiListenInfo` reliably exposes
  the chosen port; otherwise use a test-selected high port and handle bind
  failure as a test failure
- connect a client
- client sends `ping\r\n`
- server reads through `Ztcp::RxStream` and echoes `pong\r\n`
- client parses response and posts a semaphore
- test clean disconnect

If port `0` chosen-port discovery is not exposed through the existing
`ZiListenInfo`, document that exact limitation in the test code and use a fixed
loopback port for the initial test.

### Phase 4 - Client and Server Convenience APIs

Implement `Ztcp::CliLink`, `Ztcp::SrvLink`, `Ztcp::Client<App>`, and
`Ztcp::Server<App>` around the working link.

`CliLink` should mirror the non-TLS parts of `Ztls::CliLink`:

- store `Host m_server` and `uint16_t m_port`
- `connect()`
- `connect(Host server, uint16_t port)`
- `server()`, `port()`
- `connect_()` on the `Ztcp` Rx thread
- hostname lookup through `ZiIP ip = m_server`
- `ZiMultiplex::connect(...)`
- default `connectFailed(bool transient)` that schedules reconnect when
  `app()->reconnFreq() > 0`, otherwise logs an error

`SrvLink` only needs construction and inherited raw connection behavior; it has
no server TLS reset/client-hello path.

`Client<App>`:

- derives from `Engine<App>`
- `bool init(ClientParams params)` delegates to `Base::init_(...)`
- default `unsigned reconnFreq() const { return 0; }`

`Server<App>`:

- derives from `Engine<App>`
- `bool init(ServerParams params)` delegates to `Base::init_(...)`
- `listen()` calls `mx()->listen(...)` with `app()->accepted(ci)`
- default `nAccepts()`, `rebindFreq()`, `listening()`, and `listenFailed()`
  match the `Ztls::Server` non-TLS behavior but log as `Ztcp`
- `stopListening()` removes the rebind timer and calls
  `mx()->stopListening(...)`

This phase completes the public transport API expected by applications.

### Phase 5 - Stream Boundary and Stress Tests

Add `ZtcpStreamTest.cc` after the link/client/server path works.

Test cases:

- payload larger than one default `ZiIOBuf` allocation
- HTTP-like parse across buffer boundaries using `Ztcp::RxStream::consume()`
- partial consumption leaves unread bytes queued
- `process()` return `0` leaves queued data intact until more bytes arrive
- `process()` return negative closes the connection
- empty buffers are not pushed into `RxStream`

This phase specifically validates the corrected raw TCP receive design.  It
should catch accidental regression back to framed-message behavior.

### Phase 6 - Raw TCP Examples

Add examples only after tests exercise the transport.

`ztcp/example/Makefile.am` should mirror `ztls/example/Makefile.am` but link
`libZtcp.la` and omit `@PTLS_*@`:

```make
AM_CPPFLAGS = ... -I$(top_srcdir)/ztcp/src @Z_CPPFLAGS@
LDADD = $(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = ZtcpClient ZtcpServer
```

Example usage:

```text
ZtcpServer SERVER PORT [REPEAT]
ZtcpClient SERVER PORT [REPEAT]
```

No CA, certificate, key, ALPN, or TLS version arguments.  `connected()` should
log raw TCP establishment only.

### Phase 7 - Factor Shared HTTP Client Helper

Create the new library header `zhttp/src/ZhttpClient.hh` and move
transport-agnostic HTTP/1 client logic out of
`zhttp/example/zhttpclient.cc`.

This implements the resolved old-plan decision: shared client code belongs in
the `zhttp` library rather than remaining local to the example.

Put all helper types and functions in `namespace Zhttp::Client`.

Use `Zhttp::Client::State` for the shared request/session state.  This is the
replacement for the earlier `HttpSessionState` name; it keeps the state close
to the new client helper namespace and avoids confusion with
`Zhttp::ParserState`.

The helper state is not duplicative of `ParserState`:

- `Zhttp::ParserState` is parser progress (`Initial`, `Headers`, `Body`,
  `ChunkHdr`, `Complete`, `Error`).
- `Zhttp::Client::State` is application/request state: hostname, path, output
  file path, parser instance, body counters, content-length/chunked flags,
  completion flag.

Suggested structure:

```c++
namespace Zhttp::Client {

struct URL {
  ZuCSpan scheme;
  ZtString<> host;
  uint16_t port = 0;
  ZtString<> target;
};

struct Options {
  ZtString<> output{"index.html"};
};

struct State {
  URL url;
  Options options;
  ZiFile bodyFile;
  int64_t contentLength = -1;
  uint64_t bodyBytes = 0;
  unsigned bodyChunks = 0;
  bool bodyFileOpen = false;
  bool chunked = false;
  bool framingLogged = false;
  bool done = false;
};

template <typename Link>
void sendRequest(Link &link, State &state);

template <typename Link, typename Stream>
int processResponse(Link &link, State &state, Stream &rx);

bool parseURL(ZuCSpan input, URL &url, ZeException *error = nullptr);

template <typename App_>
struct LinkState {
  using App = App_;
  App *app = nullptr;
  State *state = nullptr;
};

} // namespace Zhttp::Client
```

`Zhttp::Client::parseURL()` should be a focused parser for:

- `http://host[:port][/path][?query]`
- `https://host[:port][/path][?query]`
- IPv4/domain names initially
- default target `/`
- default ports 80 and 443

Do not use `ZtURI` as a full URL parser.  The inspected `ZtURI` API is
query/object serialization and parsing support, not scheme/authority parsing.
It may be used later for query-specific work, but not for splitting the input
URL.

Request writing should use only transport-neutral stream operations:

```c++
auto tx = link.txStream();
Zhttp::Client::sendRequest(link, state, tx);
tx << Zi::flush();
```

The request builder should support at least:

- method: GET
- host header from `state.url.host`
- target path/query from `state.url.target`
- existing headers: `user-agent: zhttptest/1.0`, `accept: */*`

Add parser/helper tests under `zhttp/test` if the helper contains URL parsing
or non-trivial response-state behavior:

- `ZhttpClientURLTest.cc`
- valid `http` and `https`
- default ports
- explicit ports
- default `/` target
- path plus query preservation
- reject unsupported schemes
- reject missing host

### Phase 8 - Update zhttpclient for http: and https:

Modify `zhttp/example/zhttpclient.cc` into a transport-selecting wrapper.

Use `ZtCLI` for command-line flags, following existing local patterns such as
`zdb_pq/test/zdbpqtest.cc`:

```c++
struct Options {
  ZuCSpan ca;
  ZuCSpan output;
  bool help = false;
};

ZtStruct((Options, CLI),
  (((ca),     (CLI::Opt<'c'>, CLI::Long<"ca">)),     (String)),
  (((output), (CLI::Opt<'o'>, CLI::Long<"output">)), (String, "index.html")),
  (((help),   (CLI::Flag<'h'>)),                     (Bool)));
```

CLI:

```text
Usage: zhttpclient [OPTION]... URL

Options:
  -c, --ca=PATH       CA path for https:
  -o, --output=PATH   response body output path
  -h, --help          show help
```

Behavior:

- `http:` creates `Zhttp::Client::App : Ztcp::Client<Zhttp::Client::App>`
  for the raw TCP transport.
- `http:` creates
  `Zhttp::Client::Link : Ztcp::CliLink<Zhttp::Client::App, Zhttp::Client::Link>`
  or an equivalently named transport-specialized link under
  `namespace Zhttp::Client`.
- `https:` creates
  `Zhttp::Client::TLSApp : Ztls::Client<Zhttp::Client::TLSApp>` for the TLS
  transport.
- `https:` creates
  `Zhttp::Client::TLSLink : Ztls::CliLink<Zhttp::Client::TLSApp, Zhttp::Client::TLSLink>`
  or an equivalently named TLS-specialized link under
  `namespace Zhttp::Client`.
- `https:` passes ALPN `http/1.1` and optional CA path to `Ztls`.
- `http:` does not pass CA path, ALPN, or TLS metadata.
- Both link types call the same `ZhttpClient.hh` request and response helpers.
- Preserve the existing TLS behavior for `https:` other than the CLI shape.

`Zhttp::Client::TLSLink::connected(const char *alpn, int tlsver)` should log
TLS metadata then call shared request sending.
`Zhttp::Client::Link::connected()` should log raw TCP connection then call the
same request sending.  The shared code lives below the transport callback
layer, so the callback signature difference does not leak into `Zhttp` parser
or builder code.

Update `zhttp/example/Makefile.am`:

- add `-I$(top_srcdir)/ztcp/src`
- link `$(top_builddir)/ztcp/src/libZtcp.la`
- keep `libZhttp.la`, `libZquic.la`, `libZtls.la`, and `@PTLS_LIBS@` because
  the example still supports HTTPS and the library currently links QUIC/TLS

Update `zhttp/src/Makefile.am`:

- add `ZhttpClient.hh` to `pkginclude_HEADERS`
- no new library dependency on `Ztcp`; `Zhttp` remains transport-neutral

### Phase 9 - Regenerate, Build, and Verify

Regenerate generated build files only after all source and `Makefile.am` inputs
are in place:

```sh
./z.config -c /opt/z
```

Focused verification:

```sh
make -j
./ztcp/test/ZtcpInitTest
./ztcp/test/ZtcpLoopTest
./ztcp/test/ZtcpStreamTest
./zhttp/test/ZhttpClientURLTest
./ztcp/example/ZtcpServer 127.0.0.1 8080
./ztcp/example/ZtcpClient 127.0.0.1 8080
./zhttp/example/zhttpclient http://example.com/
./zhttp/example/zhttpclient https://example.com/
```

If external network is unavailable in the test environment, replace the final
two commands with a local `Ztcp` HTTP test server or a loopback fixture.

## Code References to Impacted Code

- `Makefile.am:3` - insert `ztcp` before `zhttp` in `SUBDIRS`.
- `configure.ac:196` - add `ztcp` to the module symlink loop.
- `configure.ac:215` - add `ztcp` `AC_CONFIG_FILES` entries after `ztls`.
- `ztls/src/Makefile.am:1` - build-file template for `ztcp/src/Makefile.am`,
  excluding PTLS flags/libs.
- `ztls/src/ZtlsLib.hh:1` - export macro template for `ZtcpLib.hh`.
- `ztls/src/Ztls.hh:183` - `Cxn` ownership and callback pattern to mirror.
- `ztls/src/Ztls.hh:299` - TLS `connected_0()` currently uses framed receive;
  `Ztcp` should not copy the `ZiRx::recv<parseHdr,...>()` call.
- `ztls/src/Ztls.hh:568` - disconnection posting and Tx drain pattern.
- `ztls/src/Ztls.hh:637` - `txStream()` / `txStream_()` API style to mirror,
  with zero headroom/tailroom.
- `ztls/src/Ztls.hh:1048` - client connect/reconnect structure to reuse
  without TLS setup.
- `ztls/src/Ztls.hh:1387` - `Engine` thread helpers and validation shape.
- `ztls/src/Ztls.hh:1832` - server listen/rebind structure to reuse without
  TLS context setup.
- `zi/src/ZiMultiplex.hh:452` - `ZiConnection` virtual API and
  `recv()`/`send()` entry points.
- `zi/src/ZiMultiplex.cc:1109` - `ZiConnection::connected()` receive-context
  startup behavior.
- `zi/src/ZiMultiplex.cc:1385` - receive callback execution semantics.
- `zi/src/ZiTx.hh:41` - queued Tx helper suitable for `Ztcp`.
- `zi/src/ZiRx.hh:42` - framed Rx helper that should not be used for raw TCP.
- `zi/src/ZiRxStream.hh:28` - stream abstraction `Ztcp::RxStream` should expose.
- `zi/src/ZiTxStream.hh:35` - Tx stream abstraction used by HTTP builders.
- `zhttp/src/Zhttp.hh:208` - `ParserState`, distinct from HTTP client request
  state.
- `zhttp/src/Zhttp.hh:396` - parser consumes generic stream-like input, so no
  parser change is expected.
- `zhttp/src/Zhttp.hh:592` - builder writes through generic Tx stream layers,
  so no builder change is expected.
- `zhttp/example/zhttpclient.cc:1` - current TLS-only client to factor into
  transport-specific wrapper plus shared `ZhttpClient.hh`.
- `zhttp/example/Makefile.am:1` - add `ztcp` include path and library link.
- `zhttp/src/Makefile.am:6` - add `ZhttpClient.hh` to installed headers.
- `zt/src/ZtCLI.hh:1226` - `ZtCLI::load()` API for command-line options.
- `zdb_pq/test/zdbpqtest.cc:23` - local `ZtCLI` option-struct usage pattern.

## Detailed Test Plan

`ztcp/test/ZtcpInitTest.cc`:

- null multiplexer fails
- invalid named Rx thread fails
- invalid named Tx thread fails
- same Rx/Tx thread fails
- non-running multiplexer fails
- valid running multiplexer with distinct Rx/Tx threads succeeds

`ztcp/test/ZtcpLoopTest.cc`:

- start `ZiMultiplex`
- initialize `Ztcp::Server` and `Ztcp::Client`
- server listens on loopback
- client connects
- client sends a small payload through `txStream()`
- server receives through `Ztcp::RxStream`, echoes a response
- client receives response, verifies bytes, signals completion
- both sides disconnect cleanly

`ztcp/test/ZtcpStreamTest.cc`:

- send payload larger than default buffer capacity
- verify parser consumption across multiple buffers
- verify partial consumption leaves data queued
- verify `process()` return values: positive continues, zero pauses, negative
  disconnects

`zhttp/test/ZhttpClientURLTest.cc`:

- parse `http://example.com/`
- parse `https://example.com/`
- default ports 80 and 443
- explicit ports
- path and query preservation
- reject unsupported schemes
- reject missing host

Existing tests to rerun:

- `zhttp/test/ZhttpParserTest`
- `zhttp/test/zhttptest` if still applicable
- `ztls/test/ZtlsBufHookTest`
- `ztls/test/ZtlsAsyncTest`

Manual smoke:

- `./zhttp/example/zhttpclient http://127.0.0.1:PORT/`
- `./zhttp/example/zhttpclient https://example.com/`

## Acceptance Criteria

- `ztcp` is part of the autotools build and installs `ZtcpLib.hh` and
  `Ztcp.hh`.
- `libZtcp.la` builds without `@PTLS_CFLAGS@`, `@PTLS_LIBS@`, OpenSSL, or
  zpicotls linkage.
- `Ztcp` exposes the planned App/Engine/Client/Server/Link/Cxn/RxStream/Tx
  stream API.
- Raw TCP receive delivers available byte spans to `RxStream` without waiting
  for protocol frames.
- `Zhttp::Parser` and `Zhttp::Builder` require no changes to work with
  `Ztcp` streams.
- `zhttpclient` accepts a URL positional argument and selects `Ztcp` for
  `http:` and `Ztls` for `https:`.
- `--ca` is a command-line option for HTTPS, not a positional argument.
- `Zhttp` library code remains transport-neutral and does not link `libZtcp`.
- New `ztcp` tests and relevant `zhttp` helper tests pass.

## Non-goals

- No TLS compatibility layer inside `Ztcp`.
- No ALPN, certificates, CA loading, mTLS, session tickets, early data, TLS
  alerts, or key updates in `Ztcp`.
- No HTTP parser or builder redesign.
- No HTTP/2 or HTTP/3 support through `Ztcp` in this plan.
- No proxy, redirect, cookie, compression, or authentication support in
  `zhttpclient`.
- No guarantee that old `zhttpclient SERVER PORT [CA]` positional CLI remains
  supported; the resolved requirement is URL-first CLI with CA as an option.

## Options and Open Questions

No blocking open questions remain from the original plan.  The previous open
decisions are resolved as follows:

- `Ztcp::connected()` is no-argument.
- `zhttpclient` uses a standard URL positional argument.
- CA path is a `ZtCLI` option.
- shared HTTP client logic is factored into `zhttp/src/ZhttpClient.hh`.

Implementation options that remain but do not block the design:

- Port selection in `ZtcpLoopTest`: prefer loopback port `0` if
  `ZiListenInfo` exposes the bound port reliably; otherwise use a fixed or
  test-selected high port.
- URL parser scope: start with `http`/`https`, host, optional port, path, and
  query.  IPv6 literals can be added later if required.
- Shared HTTP client naming: use `Zhttp::Client::State`,
  `Zhttp::Client::App`, `Zhttp::Client::Link`,
  `Zhttp::Client::TLSApp`, and `Zhttp::Client::TLSLink`.  Avoid the old
  `HttpSessionState`, `HttpApp`, `HttpLink`, `HttpsApp`, and `HttpsLink`
  names outside explanatory migration notes.
