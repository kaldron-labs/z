## Summary

The goal is to unify the application-facing CRTP interface across `Ztcp`, `Ztls`,
and `Zquic` so shared application code can target a consistent connection/stream
model:

- `Link` represents connection/session lifecycle.
- `Stream` represents ordered byte-stream application I/O.
- `Ztcp` and `Ztls` are single-stream transports: `Link::Stream` is `Link`, and
  `link->stream()` returns `link`.
- `Zquic` is a multi-stream transport: `Link::Stream` is the application stream
  type, and `link->stream(type)` opens a QUIC stream.

Compatibility with current callback shapes is explicitly a non-goal. Existing apps
using `connected()`, `connected(const char *, int)`, or QUIC-only stream callback
shapes must be updated. This is a breaking API cleanup intended to leave one clear
interface rather than a compatibility matrix.

The proposed design adds common transport metadata in `Zi`, makes all transports
call `connected(Zi::ConnectedInfo)`, introduces a neutral `Zi::StreamType`, and
standardizes each `Link` with:

```cpp
using Stream = ...;
using StreamRef = ...;

StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Bidi);
```

`Ztcp` and `Ztls` use `StreamRef = Link *`; `Zquic` keeps `StreamRef =
ZmRef<Stream>`. Shared application helpers should accept `StreamRef` and rely on
the existing pointer-like `operator->` behavior rather than normalizing to raw
pointers.

## Architecture Documentation

### New Components

Add a small transport metadata header in `zi/src`:

- `zi/src/ZiTransport.hh`

This header should be low-level enough for `ztcp`, `ztls`, `zquic`, and application
examples to include without creating upward dependencies.

Proposed contents:

```cpp
// zi/src/ZiTransport.hh

#ifndef ZiTransport_HH
#define ZiTransport_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZtEnum.hh>

namespace Zi {

struct Transport {
  ZtEnum(Transport, int8_t, TCP, TLS, QUIC);
};

struct StreamType {
  ZtEnum(StreamType, int8_t, Bidi, Uni);
};

struct ConnectedInfo {
  Transport::T transport = Transport::TCP;
  ZuCSpan alpn;
  int version = 0;              // TLS 12/13, QUIC version, 0 for TCP

  // Later extensions, added only when real callers need them:
  bool resumed = false;
  bool earlyDataAccepted = false;
};

} // namespace Zi

#endif /* ZiTransport_HH */
```

`Zi` is the right namespace because it already owns low-level I/O concepts and is
below `Ztcp`, `Ztls`, and `Zquic`. `ZiMultiplex.hh` already depends on `ZtEnum.hh`,
so using `ZtEnum` for a small `Zi::Transport`/`Zi::StreamType` follows an existing
Zi pattern.

### Changed Interfaces

All link classes must implement or inherit:

```cpp
using Stream = ...;
using StreamRef = ...;

StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Bidi);
void connected(Zi::ConnectedInfo);
```

For `Ztcp` and `Ztls`:

```cpp
using Stream = Impl;
using StreamRef = Impl *;

StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Bidi) {
  return type == Zi::StreamType::Bidi ? impl() : nullptr;
}
```

For `Zquic`:

```cpp
using Stream = Stream_;
using StreamRef = ZmRef<Stream>;

StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Bidi);
void streamed(StreamRef);
```

The QUIC app-facing `streamed(StreamRef)` remains the notification for peer-created
streams. `acceptPeerStream(uint64_t id)` remains internal to `Zquic::Link`.

### Changed Processes and Threads

Threading does not fundamentally change:

- `Ztcp::connected_1()` still runs on the TCP Rx thread before calling the app.
- `Ztls::finishHandshake_()` still runs on the TLS Rx thread after handshake
  completion before calling the app.
- `Zquic::markEstablished_()` still runs in the QUIC Rx/runtime path after transport
  parameter validation and establishment before calling the app.

Only the callback payload changes from transport-specific positional arguments to
`Zi::ConnectedInfo`.

### Changed Data Flows

Connection metadata flow becomes uniform:

- `Ztcp` constructs `{ TCP, {}, 0 }`.
- `Ztls` constructs `{ TLS, negotiatedALPN, tlsver }`.
- `Zquic` constructs `{ QUIC, negotiatedALPN, Version1 }`.

Stream data flow becomes uniform at the application level:

- TCP/TLS receive bytes into the link's existing `RxStream`; the link is also the
  stream.
- QUIC receives bytes into a `Zquic::Stream`'s `RxStream`; the stream is separate
  from the link.
- Application helpers accept `StreamRef` and call `stream->txStream()` and
  stream-level parse/process helpers.

### Changed Network Programming

No socket or packet I/O behavior changes are required for the first unification
slice. QUIC stream flow control, stream limits, and peer stream validation remain in
`Zquic::Link`. TCP/TLS connection-level behavior remains connection-level.

### Event-Driven and Timer Processing

No new timers are required. Existing callbacks remain event-driven from their
current Rx paths:

- TCP connection established
- TLS handshake completed
- QUIC runtime established
- QUIC peer stream accepted

### Data Stores

No data store changes.

## Detailed Design and Implementation Plan

### Phase 1: Add Shared Transport Metadata and Convert Connected Callbacks

Area of focus: establish a uniform connection lifecycle callback end-to-end.

Add `zi/src/ZiTransport.hh` with `Zi::Transport`, `Zi::StreamType`, and
`Zi::ConnectedInfo`. Include it from:

- `ztcp/src/Ztcp.hh`
- `ztls/src/Ztls.hh`
- `zquic/src/Zquic.hh`
- examples/tests that reference `ConnectedInfo` or `StreamType`

Modify connection callbacks:

- `Ztcp::Link::connected_1()` currently calls `impl()->connected()`. Change it to:

  ```cpp
  impl()->connected(Zi::ConnectedInfo{
    .transport = Zi::Transport::TCP
  });
  ```

- `Ztls::Link::finishHandshake_()` currently calls:

  ```cpp
  impl()->connected(ptls_get_negotiated_protocol(m_tls), tlsver_(...));
  ```

  Change it to construct `ZuCSpan` from `ptls_get_negotiated_protocol(m_tls)` and
  call `connected(ConnectedInfo)`.

- `Zquic::CliLink`/`SrvLink` establishment currently use `if constexpr` to call
  `connected(const char *, int)`. Remove that compatibility branch and call
  `connected(ConnectedInfo)` directly with `alpn` as `ZuCSpan`.

API scrutiny:

- `ptls_get_negotiated_protocol()` returns `const char *`; the set/list APIs are
  length-aware. Wrapping the returned pointer into `ZuCSpan` with `strlen` is
  acceptable at the app boundary, but the span should be empty for null.
- `Zquic::Crypto::negotiatedProtocol()` already returns `ZuCSpan`; do not degrade it
  to `.data()`.
- `ZmRef<T>` has `operator T *()` and `operator->()`, so `StreamRef` can be used in
  boolean checks and with `stream->txStream()`.

Update CRTP documentation blocks in `Ztcp.hh`, `Ztls.hh`, and `Zquic.hh` to show
only `connected(Zi::ConnectedInfo)`.

Dependencies:

- This phase depends only on adding the new Zi header.
- It is intentionally breaking; all directly impacted tests/examples must be
  updated in the same phase.

Tests in this phase:

- Update `ztcp/test/ZtcpLoopTest.cc`.
- Update `ztls/test/ZtlsAsyncTest.cc`.
- Update QUIC runtime test links in `zhttp/test/Zhttp3InteropTest.cc`.
- Add direct checks that `info.transport`, `info.version`, and `info.alpn` are
  populated as expected where the tests already establish runtime connections.

### Phase 2: Add the Single-Stream Link Adapter Surface to TCP/TLS

Area of focus: make TCP/TLS links satisfy the same stream acquisition shape as QUIC.

Modify `Ztcp::Link`:

```cpp
using Stream = Impl;
using StreamRef = Impl *;

StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Bidi) {
  return type == Zi::StreamType::Bidi ? impl() : nullptr;
}
```

Modify `Ztls::Link` similarly.

Behavior:

- `stream(Bidi)` returns the link implementation pointer.
- `stream(Uni)` returns null for TCP/TLS; they do not have QUIC unidirectional
  streams.
- No `acceptPeerStream` exists for TCP/TLS.
- No `streamed` event is emitted for TCP/TLS.

API scrutiny:

- TCP `TxStream_::allocBuf_()` rejects non-zero headroom. This is fine because TCP
  stream sends are raw byte-stream sends.
- TLS `TxStream_` manages record headroom internally. Treating the link as the
  stream does not change TLS record staging.
- Both TCP and TLS already call `impl()->process(m_rxStream)`; moving app code to a
  stream helper does not require moving Rx queues.

Dependencies:

- This phase depends on `Zi::StreamType` from Phase 1.
- It does not depend on refactoring `zhttpclient`.

Tests in this phase:

- Extend `ZtcpLoopTest` or add a small `ZtcpStreamShapeTest` to verify:
  - `link.stream(Bidi) == &link`
  - `link.stream(Uni) == nullptr`
  - `Link::StreamRef` is usable with `operator->`
- Add analogous checks for `Ztls` in a lightweight compile/runtime test if an
  established TLS link is already available; otherwise compile-only checks in
  `ZtlsAsyncTest` are sufficient for the alias/method shape.

### Phase 3: Rename QUIC StreamType Use to Zi::StreamType at the Boundary

Area of focus: avoid forcing TCP/TLS to depend on `Zquic::StreamType`.

`ZquicTypes.hh` currently defines:

```cpp
struct StreamType {
  ZtEnum(StreamType, int8_t, Bidi, Uni);
};
```

The plan is to move this public concept to `Zi::StreamType` and update `Zquic` to
use `Zi::StreamType::T` for link-facing APIs:

```cpp
StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Bidi);
uint64_t peerStreamLimit(Zi::StreamType::T type) const;
...
```

Internally this can be a direct replacement because the values remain `Bidi` and
`Uni`. Do not keep `Zquic::StreamType` as a public app-facing alias; update call
sites to use `Zi::StreamType` so the boundary is unambiguous.

API scrutiny:

- `Zquic::Link::stream()` currently queues blocked local stream opens by incrementing
  `queued_(type)` and returning null. This behavior must remain unchanged.
- `Zquic::Link::acceptPeerStream()` currently validates ID direction, stream type,
  ordinal, and peer stream limits before calling `impl()->streamed(stream)`. This
  remains internal and should not be replicated in TCP/TLS.
- Existing HTTP/3 tests use `Zquic::StreamType::Bidi` and `Uni`; move those call
  sites to `Zi::StreamType::Bidi` and `Uni`.

Dependencies:

- Depends on `Zi::StreamType` from Phase 1.
- Should follow Phase 2 so all three transports expose the same `stream(type)`
  spelling.

Tests in this phase:

- Update QUIC tests and HTTP/3 tests that call `stream(Zquic::StreamType::...)`.
- Verify queued stream behavior remains covered by existing QUIC diagnostics/tests.

### Phase 4: Refactor zhttpclient Around StreamRef

Area of focus: prove the interface consolidation vertically in a real application.

Refactor `zhttp/example/zhttpclient.cc` to remove separate `App` and `TLSApp` link
logic. The minimum consolidation target is one app/link template or one shared base
for TCP/TLS with transport-specific inheritance.

Shared helpers should accept `StreamRef`:

```cpp
template <typename StreamRef>
void sendRequest(State &state, StreamRef stream) {
  auto tx = stream->txStream();
  RequestBuilder builder{state};
  builder.request(tx);
  builder.finish(tx);
  tx << Zi::flush();
}

template <typename StreamRef, typename Rx>
int processResponse(StreamRef stream, State &state, Rx &rx) {
  ...
}
```

For TCP/TLS `StreamRef` is `Link *`; for QUIC it will be `ZmRef<Stream>`. Both
support `operator->`.

`connected` should become:

```cpp
void connected(Zi::ConnectedInfo info) {
  logConnected(info);
  typename Link::StreamRef stream = this->stream();
  if (!stream) { app()->done(); return; }
  sendRequest(app()->state, stream);
}
```

`process` on TCP/TLS links can call:

```cpp
return processResponse(this->stream(), app()->state, rx);
```

Logging should use:

- `info.transport` for TCP/TLS/QUIC
- `info.version` for TLS/QUIC version
- `info.alpn` for negotiated protocol

Dependencies:

- Depends on Phases 1 and 2.
- Does not require QUIC support in `zhttpclient` yet.

Tests in this phase:

- Rebuild `zhttp/example/zhttpclient`.
- Keep existing URL parsing behavior unchanged.
- If practical, add a compile-level example-only assertion that `Link::StreamRef`
  works for both TCP and TLS link types.

### Phase 5: Prepare QUIC Application Integration Without Implementing Full HTTP/3

Area of focus: make the app shape ready for multi-stream QUIC while avoiding a large
HTTP/3 implementation in this unification step.

Add or document the QUIC app shape:

```cpp
void connected(Zi::ConnectedInfo info) {
  typename Link::StreamRef stream = this->stream(Zi::StreamType::Bidi);
  if (stream) startRequest(stream, info);
}

void streamed(typename Link::StreamRef stream) {
  acceptRequestOrResponse(stream);
}
```

The actual HTTP/3 request/response implementation remains stream-level and can use
existing `Zhttp::H3` helpers. This phase should not mix transport-interface
unification with full HTTP/3 client behavior unless a test already exercises it.

Dependencies:

- Depends on Phases 1 and 3.
- Can run before or after `zhttpclient` TCP/TLS consolidation if it is limited to
  tests/documentation.

Tests in this phase:

- Update `zhttp/test/Zhttp3InteropTest.cc` to use `connected(ConnectedInfo)` and
  `Zi::StreamType`.
- Add assertions for QUIC `ConnectedInfo`:
  - `transport == Zi::Transport::QUIC`
  - `version == int(Zquic::Version1)`
  - `alpn == "h3"` when configured

### Phase 6: Documentation and Cleanup

Area of focus: remove stale interface descriptions and lock in the new model.

Update:

- `ztcp/src/Ztcp.hh` CRTP comments
- `ztls/src/Ztls.hh` CRTP comments
- `zquic/src/Zquic.hh` CRTP comments
- `zhttp/example/zhttpclient.cc` comments if any mention TCP/TLS split
- Any local plan docs that reference old callback signatures

Delete or rewrite references to:

- `connected()`
- `connected(const char *, int)`
- `Zquic::StreamType` as a public app-facing enum, if replaced by `Zi::StreamType`

Run focused builds/tests listed below.

## Code References to Impacted Code

- `zi/src/ZiTransport.hh` - New header for `Zi::Transport`, `Zi::StreamType`, and
  `Zi::ConnectedInfo`.
- `ztcp/src/Ztcp.hh:198` - `connected_1()` currently calls `impl()->connected()`;
  change to `connected(ConnectedInfo)`.
- `ztcp/src/Ztcp.hh:250` - TCP `TxStream_` already provides stream send behavior;
  add `Stream`, `StreamRef`, and `stream()` near public link aliases/methods.
- `ztls/src/Ztls.hh:394` - `finishHandshake_()` currently constructs TLS state and
  calls `connected(const char *, int)`; change to `connected(ConnectedInfo)`.
- `ztls/src/Ztls.hh:603` - TLS `TxStream_` already provides stream send behavior;
  add `Stream`, `StreamRef`, and `stream()` near public link aliases/methods.
- `zquic/src/ZquicTypes.hh:62` - `Zquic::StreamType` currently lives here; replace
  with `Zi::StreamType` or make it a non-preferred alias.
- `zquic/src/Zquic.hh:1561` - QUIC `Link` already defines `StreamRef =
  ZmRef<Stream>`; keep this shape and update type spelling to `Zi::StreamType`.
- `zquic/src/Zquic.hh:1613` - `stream(type)` behavior must remain unchanged except
  for enum namespace.
- `zquic/src/Zquic.hh:1621` - `acceptPeerStream()` remains internal and continues
  to notify apps via `streamed(stream)`.
- `zquic/src/Zquic.hh:2848` - Client-side establishment currently calls
  `connected(const char *, int)` through a compatibility branch; replace with
  direct `connected(ConnectedInfo)`.
- `zquic/src/Zquic.hh:3228` - Server-side establishment has the same callback
  replacement.
- `zhttp/example/zhttpclient.cc:241` - TCP app/link split starts here; consolidate
  with TLS app/link logic.
- `zhttp/example/zhttpclient.cc:283` - TLS app/link split starts here; fold into
  common stream-ref helpers.
- `ztcp/test/ZtcpLoopTest.cc:116` - Existing TCP test callback shape must change to
  `connected(ConnectedInfo)`.
- `ztls/test/ZtlsAsyncTest.cc:20` - Existing TLS test callback shape must change to
  `connected(ConnectedInfo)`.
- `zhttp/test/Zhttp3InteropTest.cc:91` - Existing QUIC test callback shape must
  change to `connected(ConnectedInfo)`.

## Detailed Test Plan

Build and run focused tests after each phase, not only at the end.

### Metadata and Callback Tests

- Update `ztcp/test/ZtcpLoopTest.cc`:
  - Client and server links implement `connected(Zi::ConnectedInfo)`.
  - Assert TCP info has `transport == Zi::Transport::TCP`, `version == 0`, and no
    ALPN.

- Update `ztls/test/ZtlsAsyncTest.cc`:
  - Client and server links implement `connected(Zi::ConnectedInfo)`.
  - Existing async parameter validation still compiles.
  - If a handshake path is exercised elsewhere, assert TLS version and ALPN.

- Update `zhttp/test/Zhttp3InteropTest.cc`:
  - Runtime client/server links implement `connected(Zi::ConnectedInfo)`.
  - Assert QUIC info has `transport == Zi::Transport::QUIC`, `version ==
    int(Zquic::Version1)`, and expected ALPN.

### Stream Shape Tests

- Add TCP checks that `Link::StreamRef` is `Link *` and `link.stream(Bidi)` returns
  the link.
- Add TLS compile/runtime checks for the same shape.
- Update QUIC tests to use `Zi::StreamType::Bidi` and `Zi::StreamType::Uni`.
- Confirm `StreamRef` works in generic helpers with `stream->txStream()`.

### zhttpclient Build Test

- Build:

  ```sh
  make -C zhttp/example zhttpclient
  ```

- The example should compile with one consolidated TCP/TLS application path.

### Regression Tests

Run the focused binaries that exercise changed CRTP surfaces:

```sh
make -C ztcp/test ZtcpLoopTest ZtcpStreamTest
./ztcp/test/ZtcpLoopTest
./ztcp/test/ZtcpStreamTest

make -C ztls/test ZtlsAsyncTest
./ztls/test/ZtlsAsyncTest

make -C zhttp/test Zhttp3InteropTest
./zhttp/test/Zhttp3InteropTest
```

If environment-dependent HTTP/3 interop tests require external tools or network
setup, run the subset that is already local/runtime-only and document skipped cases.

## Acceptance Criteria

- `Ztcp`, `Ztls`, and `Zquic` all call `connected(Zi::ConnectedInfo)` directly.
- No library CRTP documentation shows `connected()` or `connected(const char *, int)`
  as the app callback.
- `Ztcp::Link` and `Ztls::Link` expose:
  - `using Stream = Impl`
  - `using StreamRef = Impl *`
  - `stream(Zi::StreamType::Bidi)` returning `impl()`
  - `stream(Zi::StreamType::Uni)` returning null
- `Zquic::Link` keeps `using StreamRef = ZmRef<Stream>` and app-facing stream
  creation uses `Zi::StreamType`.
- `acceptPeerStream()` remains internal to QUIC.
- `zhttpclient` no longer needs separate duplicated application logic solely for TCP
  versus TLS connected signatures.
- Focused TCP, TLS, and QUIC tests compile and pass, or environment-dependent skips
  are documented.

## Non-goals

- Preserving source compatibility with current apps.
- Keeping old callback forms as fallback overloads.
- Making TCP/TLS support peer-created streams.
- Making TCP/TLS support independent per-stream reset semantics.
- Implementing a full HTTP/3 client in `zhttpclient` as part of the interface
  cleanup.
- Changing socket, TLS record, QUIC packet, congestion, or recovery behavior.

## Options and Open Questions

No blocking open questions remain. The plan makes the following decisions:

- `ConnectedInfo`, `Transport`, and `StreamType` live in `Zi`.
- `ConnectedInfo` is the only connected callback shape.
- `ZuCSpan` is the app-facing ALPN type.
- `StreamRef` is the common app handle, not a normalized raw pointer.
- TCP/TLS expose `StreamRef = Link *`.
- QUIC keeps `StreamRef = ZmRef<Stream>`.
- `acceptPeerStream()` remains QUIC-internal.

Rejected options:

- Add dummy ALPN/version parameters to `Ztcp::connected()`: rejected because it
  leaks TLS/QUIC concepts into TCP and still leaves positional callback growth.
- Keep compatibility fallbacks with `if constexpr`: rejected because compatibility
  is a non-goal and fallback branches obscure the required app contract.
- Put `StreamType` in `Zquic` and make TCP/TLS depend on it: rejected because it
  creates an upward dependency from lower transports to QUIC.
- Normalize all stream handles to raw pointers: rejected because QUIC stream
  lifetime is naturally represented by `ZmRef<Stream>`.
