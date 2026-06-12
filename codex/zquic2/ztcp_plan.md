# Ztcp Implementation Plan

## Goal

Add a new `ztcp` module that mirrors the `ztls` directory structure and
exports a separate `libZtcp` library/DLL.  `Ztcp` provides raw TCP/IP transport
with the same application-facing shape as `Ztls`:

- `App`/`Engine`
- `Client`/`Server`
- `CliLink`/`SrvLink`
- `Link`
- `Cxn`
- `RxStream`
- `txStream()`/`txStream_()`

The main compatibility target is `Zhttp`: HTTP/1.x applications should be able
to parse `Ztcp::RxStream` and write via `Ztcp` Tx streams the same way they
currently do with `Ztls`.  `zhttpclient` will support both `https:` via
`Ztls` and `http:` via `Ztcp`.

## Scope

`Ztcp` is a plain TCP transport, not a TLS shim:

- No `zpicotls` or `@PTLS_*@` dependency.
- No TLS handshake, ALPN negotiation, certificates, CA paths, mTLS, tickets,
  key updates, async TLS jobs, or TLS record framing.
- Keep the `Ztls` App/Link threading model where useful: I/O callbacks hand
  buffers to a module Rx thread, application Tx is posted to a module Tx
  thread, and application callbacks run from the module Rx thread.

## Build System

1. Create the module tree:

   - `ztcp/Makefile.am`
   - `ztcp/src/Makefile.am`
   - `ztcp/test/Makefile.am`
   - `ztcp/example/Makefile.am`
   - `ztcp/src/ZtcpLib.hh`
   - `ztcp/src/ZtcpLib.cc`
   - `ztcp/src/Ztcp.hh`

2. Mirror `ztls/Makefile.am`:

   ```make
   SUBDIRS = src test example
   ```

3. Add `libZtcp.la` in `ztcp/src/Makefile.am`.

   Dependencies should match `Ztls`'s internal dependency chain except for
   `zpicotls`/OpenSSL-specific inputs:

   - include paths: `zu`, `zm`, `zt`, `ze`, `zi`
   - `LIBADD`: `libZi.la`, `libZe.la`, `libZt.la`, `libZm.la`, `libZu.la`
   - system libs: `@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@`
   - no `@PTLS_CFLAGS@`
   - no `@PTLS_LIBS@`
   - compile `ZtcpLib.cc`
   - define `-D ZTCP_EXPORTS` for the library target

4. Add DLL/export macros in `ZtcpLib.hh` analogous to `ZtlsLib.hh`:

   - `ZtcpAPI`
   - `ZtcpExplicit`
   - `ZtcpExtern`
   - `Ztcp::lib_init()`

5. Add `ztcp` to top-level build wiring:

   - `Makefile.am`: add `ztcp` before `zhttp`, because `zhttp/example`
     will link both `Ztcp` and `Ztls`.
   - `configure.ac`: add `ztcp` to the module directory loop.
   - `configure.ac`: add `ztcp/Makefile`, `ztcp/src/Makefile`,
     `ztcp/test/Makefile`, and `ztcp/example/Makefile` to `AC_CONFIG_FILES`.

6. Regenerate generated build files with:

   ```sh
   ./z.config -c /opt/z
   ```

## Public API Shape

Use `Ztls.hh` as the structural template, but remove TLS-only concepts.

### Types to Keep

- `Ztcp_::IOQueue`
- `Ztcp_::RxStream`
- `Ztcp_::BufAlloc`
- `Ztcp::LogMsg`
- `Ztcp::Host`
- `Ztcp::ParamString`
- `Ztcp::ParamStrings`
- `Ztcp::ErrorFn`
- `Ztcp::defaultErrorFn()`
- `Ztcp::EngineParams`
- `Ztcp::ClientParams`
- `Ztcp::ServerParams`
- `Ztcp::Cxn`
- `Ztcp::CliCxn`
- `Ztcp::SrvCxn`
- `Ztcp::RxStream`
- `Ztcp::RxBufAlloc`
- `Ztcp::TxBufAlloc`
- `Ztcp::Link`
- `Ztcp::CliLink`
- `Ztcp::SrvLink`
- `Ztcp::Engine`
- `Ztcp::Client`
- `Ztcp::Server`

### Types and Parameters to Drop

Drop or avoid exposing:

- `Ticket`
- `ALPNData`
- `ALPN`
- `TlsInfo`
- `caPath`
- `certPath`
- `keyPath`
- `asyncThread`
- `alpn`
- `mTLS`
- `cacheTimeout`
- `maxEarlyData()`
- `tlsInfo()`
- TLS alert/handshake/key-update APIs

### Callback Compatibility

`Ztcp` should use callbacks with the same role as `Ztls`, but raw TCP does not
have TLS metadata:

- Client link:
  - `connected()`
  - `disconnected()`
  - `connectFailed(bool transient)`
  - `int process(Ztcp::RxStream &rx)`

- Server link:
  - `connected()`
  - `disconnected()`
  - `int process(Ztcp::RxStream &rx)`

Do not require `connected(const char *alpn, int tlsver)` for `Ztcp`; that would
force raw TCP callers to invent meaningless TLS values.  `Zhttp` code should
remain stream-oriented and not depend on the connected callback signature.

## Link and Stream Implementation

`Ztcp::Link` should be a stripped-down version of `Ztls::Link`.

1. Base classes:

   - keep `ZmPolymorph`
   - keep `ZiRx<Impl, RxBufAlloc_>`
   - keep `ZiTx<Impl>`

2. Connection lifecycle:

   - `Cxn::connected(ZiIOContext &io)` calls `Link::connected_0()`.
   - `connected_0()` posts to the `Ztcp` Rx thread and immediately starts raw
     receive on the I/O context.
   - `connected_1()` records the active connection and calls
     `impl()->connected()`.
   - On overlap, close the previous connection as `Ztls` does.
   - `disconnected_0()` posts to the `Ztcp` Rx thread, clears the active
     connection, drains Tx as needed, and calls `impl()->disconnected()`.

3. Receive path:

   - Use raw stream receive rather than TLS record parsing.
   - Each received `ZiIOBuf` is pushed directly into `m_rxStream`.
   - While `m_rxStream` is non-empty, call `impl()->process(m_rxStream)`.
   - Preserve `Ztls` return semantics:
     - `0`: application needs more data or yielded
     - positive: continue draining
     - negative: disconnect/close

4. Tx stream:

   - Provide `txStream()` and `txStream_()` with the same usage style as
     `Ztls`.
   - No TLS headroom or record overhead is needed.
   - Allocate `TxBufAlloc` buffers with `skip = 0`.
   - `send()` from application threads posts to the `Ztcp` Tx thread.
   - `send_()` on the `Ztcp` Tx thread forwards buffers to `ZiTx<Impl>::send`.

5. Disconnect:

   - `disconnect()` posts to the `Ztcp` Rx thread.
   - `disconnect_(bool notify = true)` marks disconnecting, removes reconnect
     timers, clears the active connection, and either gracefully disconnects or
     closes the underlying `ZiConnection`.
   - There is no close-notify payload.

6. Client reconnect:

   - Preserve `Client::reconnFreq()` behavior from `Ztls`.
   - If `connectFailed(true)` and reconnect frequency is configured, schedule
     `connect_()` on the module Rx thread.

## Engine, Client, and Server

`Ztcp::Engine` should keep the useful non-TLS parts of `Ztls::Engine`.

1. `EngineParams`:

   - `ZiMultiplex *mx`
   - `rxThread`
   - `txThread`
   - `errorFn`

2. Validation:

   - multiplexer must be non-null
   - selected Rx and Tx thread IDs must exist
   - Rx and Tx thread IDs must differ
   - multiplexer must be running
   - no async-thread validation

3. Thread helpers:

   - `rxRun`, `rxInvoke`, `rxInvoked`
   - `txRun`, `txInvoke`, `txInvoked`
   - `mx()`, `rxThread()`, `txThread()`

4. `Client`:

   - derive from `Engine<App>`
   - support `init(ClientParams)`
   - support reconnect frequency if `Ztls::Client` already has it
   - no TLS context initialization

5. `Server`:

   - derive from `Engine<App>`
   - support `init(ServerParams)`
   - expose `listen()` using `app()->localIP()`, `app()->localPort()`, and
     `app()->accepted(const ZiCxnInfo &ci)` as `Ztls::Server` does

## Examples

Add `ztcp/example` programs equivalent to the TLS examples but raw:

- `ZtcpClient.cc`
- `ZtcpServer.cc`

Adjust output and usage:

- `ZtcpClient SERVER PORT [REPEAT]`
- `ZtcpServer SERVER PORT [REPEAT]`
- no CA/cert/key arguments
- `connected()` logs raw TCP connection establishment, not TLS metadata

These examples should be useful as smoke tests and as templates for
`zhttpclient`.

## Tests

Add targeted tests under `ztcp/test`.

1. `ZtcpInitTest.cc`

   - null multiplexer fails
   - invalid Rx thread fails
   - invalid Tx thread fails
   - same Rx/Tx thread fails
   - non-running multiplexer fails
   - valid multiplexer and distinct Rx/Tx threads succeeds

2. `ZtcpLoopTest.cc`

   - start server on `127.0.0.1` with port `0`
   - connect a client
   - client sends a small payload using `txStream()`
   - server parses it from `Ztcp::RxStream` and echoes a response
   - client receives the response and completes
   - exercise clean disconnect on both sides

3. `ZtcpStreamTest.cc`

   - send payloads larger than one default buffer
   - verify parser consumption across buffer boundaries
   - verify partial consumption leaves data in `RxStream`

Add a `test` target in `ztcp/test/Makefile.am` using `prove`, following
`ztls/test/Makefile.am`.

## Zhttp Integration

`Zhttp` itself should not need parser or builder changes.  The parser already
operates on a generic stream-like input; the concrete example/client code is
where transport selection belongs.

1. Update `zhttp/example/Makefile.am`:

   - add `-I$(top_srcdir)/ztcp/src`
   - link `$(top_builddir)/ztcp/src/libZtcp.la`
   - keep `Ztls` and `Zquic` links for existing HTTPS/QUIC behavior
   - no `@PTLS_*@` needed for `Ztcp`, but still required while the target also
     links `Ztls`

2. Modify `zhttp/example/zhttpclient.cc`:

   - accept URLs or an explicit scheme:
     - `http://host[:port]/path`
     - `https://host[:port]/path`
   - default ports:
     - `http`: 80
     - `https`: 443
   - choose transport:
     - `http:` -> `Ztcp::Client`
     - `https:` -> `Ztls::Client`
   - keep a shared HTTP request builder/parser implementation that is templated
     on the link type or stream type.
   - preserve existing TLS behavior for `https:`.
   - for `http:`, do not request CA path, ALPN, or TLS metadata.

3. Suggested client structure:

   - `HttpSessionState`: hostname, port, path, body file, parser, counters
   - `HttpsApp : Ztls::Client<HttpsApp>`
   - `HttpApp : Ztcp::Client<HttpApp>`
   - `HttpsLink : Ztls::CliLink<...>`
   - `HttpLink : Ztcp::CliLink<...>`
   - shared `ResponseParser<Link, Stream>` and `sendRequest(Link &link)`

4. Preserve `Zhttp` stream compatibility:

   - `process(Ztls::RxStream &rx)` and `process(Ztcp::RxStream &rx)` should
     both call the same parser code.
   - Request writing should use only `tx.append(...)`, `builder.request(tx)`,
     `builder.finish(tx)`, and `tx << Zi::flush()`.

## Compatibility Notes

- The `connected()` callback signature intentionally differs from `Ztls`
  because raw TCP has no ALPN or TLS version.  Shared HTTP code should be
  factored below the transport callback layer.
- `Ztcp::RxStream` should be a distinct type from `Ztls::RxStream` but expose
  the same operations through `ZiRxStream`.
- Do not make `Zhttp` depend directly on `Ztcp`; only examples or applications
  that need `http:` should link it.
- Keep heap IDs and log component names under `Ztcp.*` to avoid mixing
  allocator/log names with `Ztls`.

## Implementation Order

1. Add build-system skeleton and `ZtcpLib`.
2. Port and simplify `Ztls.hh` into `Ztcp.hh`.
3. Build `libZtcp.la`.
4. Add `ZtcpInitTest` and get validation behavior passing.
5. Add `ZtcpClient`/`ZtcpServer` examples.
6. Add loopback stream tests.
7. Update `zhttpclient` and its makefile for `http:` support.
8. Run focused builds/tests:

   ```sh
   make -j
   ./ztcp/test/ZtcpInitTest
   ./ztcp/test/ZtcpLoopTest
   ./ztcp/test/ZtcpStreamTest
   ./ztcp/example/ZtcpServer 127.0.0.1 8080
   ./ztcp/example/ZtcpClient 127.0.0.1 8080
   ./zhttp/example/zhttpclient http://example.com/
   ./zhttp/example/zhttpclient https://example.com/
   ```

## Open Decisions

- Whether `Ztcp::connected()` should pass the peer/local `ZiCxnInfo` or keep
  the minimal no-argument callback for closer parity with post-handshake
  `Ztls` application flow.
  - ANSWER: minimal no-argument callback
- Whether `zhttpclient` should keep the legacy `SERVER PORT [CA]` CLI in
  addition to URL syntax.  Keeping it for `https:` reduces compatibility risk.
  - ANSWER: `CA` should be a command line option; adopt `ZtCLI` for command-line
    argument processing; `zhttpclient` should take a standard URL as it's primary
    positional argument, and interpret the protocl `http:` vs `https:`
- Whether to add a small transport-agnostic helper header for HTTP example
  code, or keep the shared client code local to `zhttpclient.cc`.
  - ANSWER: factor out shared client code to `zhttp/src/ZhttpClient.hh` in the library,
