# Unifying Ztcp, Ztls, and Zquic Application Interfaces

## Goal

Consolidate application code so one application shape can run over:

- `Ztcp`: raw TCP, one ordered byte stream per link
- `Ztls`: TLS over TCP, one ordered byte stream per link
- `Zquic`: QUIC, multiple ordered byte streams per link

The intended model is:

- `Link` owns connection/session lifecycle.
- `Stream` owns ordered byte-stream application I/O.
- TCP/TLS links expose their single implicit stream as `link->stream() == link`.
  - `Stream` is a type alias for `Link` in the TCP/TLS cases
- QUIC links expose explicit streams through `link->stream(...)` and peer stream events.

This keeps the application/transport interface consistent without pretending TCP/TLS
support QUIC multiplexing.

Compatibility with current application callback shapes is a non-goal. This plan is
allowed to break existing users of `Ztcp`, `Ztls`, and `Zquic` in order to make the
new interface small, explicit, and uniform.

## Current State

`zhttpclient` currently has separate TCP and TLS application/link types mostly because
the `connected` callback differs:

- `Ztcp::Link` calls `impl()->connected()`.
- `Ztls::Link` calls `impl()->connected(const char *alpn, int tlsver)`.
- `Zquic::Link` already uses `connected(const char *alpn, int quicver)` in its
  documented CRTP shape and establishment path.

`Ztcp` and `Ztls` links already expose a byte-stream-like surface:

- `txStream()`
- `txStream_()`
- `process(RxStream &)`

`Zquic` puts those operations on `Stream`, not on `Link`:

- `Zquic::Stream::txStream()`
- `Zquic::Stream::txStream_()`
- `Zquic::Stream::process(RxStream &)`
- `Zquic::Link::stream(StreamType::T type = StreamType::Bidi)`
- `Zquic::Link::acceptPeerStream(uint64_t id)` internally creates peer streams and
  calls `impl()->streamed(stream)`.

## ALPN Type

The current `const char *alpn` shape is primarily a consequence of `zpicotls`:

- `ptls_get_negotiated_protocol(ptls_t *)` returns `const char *`.
- `ptls_set_negotiated_protocol(ptls_t *, const char *, size_t)` is length-aware.
- ALPN input lists use `ptls_iovec_t`, which is also length-aware.

The Z-facing application API should use `ZuCSpan` instead:

```cpp
void connected(Zi::ConnectedInfo info);
```

Reasons:

- ALPN is logically length-delimited protocol data.
- Existing Z configuration types already model ALPN as `ZuSpan<ZuCSpan>` and
  `ParamString`.
- `Zquic::Crypto::negotiatedProtocol()` already returns `ZuCSpan`.
- `const char *` loses length information and leaks backend API details into app code.

For `Ztls`, wrapping `ptls_get_negotiated_protocol()` as a `ZuCSpan` is sufficient
while zpicotls remains the backend:

```cpp
const char *alpn = ptls_get_negotiated_protocol(m_tls);
ZuCSpan alpnSpan = alpn ? ZuCSpan{alpn, unsigned(strlen(alpn))} : ZuCSpan{};
```

For `Zquic`, avoid degrading `ZuCSpan` back to `.data()` before calling the app.

## Connection Metadata

Do not add dummy ALPN parameters directly to `Ztcp::connected()` as the final design.
That would bake TLS/QUIC concepts into raw TCP.

Instead, introduce a transport-neutral metadata object:

```cpp
namespace Zi {

enum class Transport {
  TCP,
  TLS,
  QUIC
};

struct ConnectedInfo {
  Transport transport = Transport::TCP;
  ZuCSpan alpn;
  int version = 0;              // TLS 12/13, QUIC version, 0 for TCP

  // Future fields, added only when needed:
  bool resumed = false;
  bool earlyDataAccepted = false;
};

} // namespace Zi
```

Initial population:

- `Ztcp`: `{ .transport = TCP, .version = 0, .alpn = {} }`
- `Ztls`: `{ .transport = TLS, .version = tlsver, .alpn = negotiatedALPN }`
- `Zquic`: `{ .transport = QUIC, .version = int(Version1), .alpn = negotiatedALPN }`

Future QUIC/TLS fields can be added to this struct without growing callback arity.
Potential future fields include:

- TLS session resumption status
- early data accepted/rejected status
- selected cipher summary
- local/peer address metadata
- QUIC transport parameter summary
- QUIC connection IDs, if needed for diagnostics

## Callback Transition

All transports should switch to one required callback shape:

```cpp
void connected(Zi::ConnectedInfo);
```

No `if constexpr` fallback is needed. The transport should construct
`ConnectedInfo` and call the app directly:

```cpp
Zi::ConnectedInfo info{...};
impl()->connected(info);
```

This intentionally removes support for:

- `connected()`
- `connected(const char *alpn, int version)`
- `connected(ZuCSpan alpn, int version)`

Required transition order:

1. Add `Zi::ConnectedInfo`.
2. Add `Zi::Transport`.
3. Change `Ztcp`, `Ztls`, and `Zquic` to call only `connected(ConnectedInfo)`.
4. Update all examples and tests in the same change or immediately adjacent changes.
5. Update CRTP documentation blocks to show only the new callback.
6. Remove stale examples of `connected()` and `connected(const char *, int)`.

## Stream Model

Define the application-level concept as:

```cpp
struct StreamLike {
  auto txStream();
  auto txStream_();
  int process(RxStream &);
  void fin();        // if supported
  void reset(...);   // if supported
};
```

The exact type can remain CRTP/static rather than virtual. The important point is
that application code should operate on a stream-like object, not directly on a
transport link unless the link is itself the stream.

### TCP/TLS

TCP and TLS should model one implicit bidirectional stream per link:

```cpp
struct Link {
  using Stream = Link;
  using StreamRef = Stream *;

  StreamRef stream(StreamType::T type = StreamType::Bidi) {
    return type == StreamType::Bidi ? this : nullptr;
  }

  auto txStream();
  auto txStream_();
  int process(RxStream &);
};
```

Notes:

- `Ztcp::stream()` and `Ztls::stream()` return `this`.
- `Ztcp::acceptPeerStream` and `Ztls::acceptPeerStream` should not exist.
- TCP/TLS have no peer-created stream event.
- The application gets the implicit stream at `connected`.

### QUIC

QUIC keeps explicit streams:

```cpp
struct Link {
  using Stream = App::Stream;
  using StreamRef = ZmRef<Stream>;

  StreamRef stream(StreamType::T type = StreamType::Bidi);
  void streamed(StreamRef); // app callback for peer-created streams
};
```

`acceptPeerStream(uint64_t id)` should remain transport-internal. It validates stream
IDs, stream limits, creates the stream, and then notifies the application through
`streamed(stream)`.

The app-facing event is `streamed(...)`, not `acceptPeerStream(...)`.

## Application Shape

The application can be structured around common lifecycle and stream helpers:

```cpp
template <typename Link>
void connected(Link *link, Zi::ConnectedInfo info) {
  typename Link::StreamRef stream = link->stream();
  if (stream) startRequest(stream, info);
}

template <typename Link>
void streamed(Link *, typename Link::StreamRef stream) {
  acceptRequestOrResponse(stream);
}

template <typename StreamRef>
void startRequest(StreamRef stream, const Zi::ConnectedInfo &info) {
  auto tx = stream->txStream();
  // write request
}

template <typename StreamRef, typename RxStream>
int processStream(StreamRef stream, RxStream &rx) {
  // parse response/request bytes
}
```

For TCP/TLS:

- `connected()` obtains `link->stream()` and starts I/O.
- There are no additional stream callbacks.

For QUIC:

- `connected()` may open a local stream with `link->stream()`.
- peer-created streams arrive via `streamed(stream)`.
- HTTP/3 request/response code should bind to a `Stream`, not the `Link`.

## Close, FIN, and Reset Semantics

This is the main semantic mismatch.

QUIC supports per-stream:

- FIN
- RESET_STREAM
- STOP_SENDING
- independent stream lifecycle while the connection remains open

TCP/TLS support only connection-level byte-stream lifecycle:

- FIN maps to connection half-close only if exposed by the lower layer.
- reset/error generally closes the whole link.
- there is no independent stream reset.

Therefore:

- It is fine for TCP/TLS `Stream = Link`.
- It is not fine to imply TCP/TLS support independent per-stream reset.
- Common application code should use only the subset it needs, or branch on stream
  capabilities with `if constexpr`.

Possible capability helpers:

```cpp
template <typename Stream>
concept HasFin = requires(Stream *s) { s->fin(); };

template <typename Stream>
concept HasReset = requires(Stream *s, uint64_t err) { s->reset(err); };
```

For HTTP clients, this is usually manageable: request write, response read, then
close/finish. HTTP/3 will need per-stream FIN; HTTP/1.1 over TCP/TLS may not need
the same explicit operation if connection close or content framing is enough.

## Naming

Recommended names:

- `connected(ConnectedInfo)` for link/session establishment.
- `stream(type)` for locally opened or implicit stream.
- `streamed(stream)` for peer-created stream notification.
- Keep `acceptPeerStream(id)` internal to QUIC.

Avoid naming TCP/TLS APIs `acceptPeerStream`, because nothing is being accepted.

## Path for `zhttpclient`

TCP/TLS consolidation:

1. Define a common request/response parser object independent of transport.
2. Define a templated stream helper:

   ```cpp
   template <typename StreamRef>
   void sendRequest(State &, StreamRef);

   template <typename StreamRef, typename Rx>
   int processResponse(StreamRef, State &, Rx &);
   ```

3. Make `Ztcp::Link` and `Ztls::Link` call those helpers through `this`.
4. Add `stream()` returning `this` to TCP/TLS links.
5. Convert `connected()` to `connected(ConnectedInfo)` and call:

   ```cpp
   StreamRef stream = this->stream();
   sendRequest(state, stream);
   ```

QUIC support:

1. Add a QUIC app/link/stream using the same app state.
2. In QUIC `connected`, open a bidirectional stream:

   ```cpp
   StreamRef stream = link->stream(Zquic::StreamType::Bidi);
   if (stream) sendRequest(state, stream);
   ```

3. In QUIC `streamed`, attach parser state to peer-created streams as needed.
4. Move HTTP/3-specific encoding/parsing into stream-level code.

## Implementation Phases

### Phase 1: Metadata

- Add `Zi::ConnectedInfo`.
- Add `Zi::Transport`.
- Update `Ztcp`, `Ztls`, and `Zquic` to require `connected(ConnectedInfo)`.
- Remove old callback forms from examples, tests, and documentation.

### Phase 2: Single-Stream Link Adapters

- Add `using Stream = Link` to `Ztcp::Link` and `Ztls::Link` or to the concrete app
  link types if adding this to the base templates is too disruptive.
- Add `stream(StreamType::Bidi)` returning `this`.
- Decide where `StreamType` lives. If it is currently QUIC-specific, consider a
  small transport-neutral enum in `Zi`, with QUIC mapping it to `Zquic::StreamType`.

### Phase 3: App Refactor

- Refactor `zhttpclient` application logic around stream helpers.
- Remove duplicate TCP/TLS app structs.
- Keep transport-specific setup only in `runHTTP`, `runHTTPS`, and eventually
  `runHTTP3`.

### Phase 4: QUIC Integration

- Keep QUIC stream creation and peer stream acceptance in `Zquic::Link`.
- Use app-facing `stream()` and `streamed()` only.
- Add stream-level HTTP/3 request/response handling.

### Phase 5: Cleanup

- Update CRTP documentation blocks.
- Convert examples/tests to `ConnectedInfo`.
- Remove stale references to the old callback shapes.

## Risks and Open Questions

- `ConnectedInfo` needs a stable namespace. `Zi` is a good candidate because it is
  below TCP/TLS/QUIC and already owns I/O concepts.
- If `StreamType` remains in `Zquic`, TCP/TLS cannot expose `stream(StreamType)`
  without depending upward on QUIC. A neutral enum or overload without arguments is
  cleaner.
- `Link::StreamRef` is the common handle type, but it intentionally differs by
  transport: `Link *` for TCP/TLS and `ZmRef<Stream>` for QUIC. Shared app helpers
  should accept `StreamRef` and rely on `operator->` rather than normalizing to raw
  pointers.
- TCP/TLS reset semantics must be documented as link-level, not stream-level.
- QUIC has stream flow control and stream limits; `stream()` can fail/queue. TCP/TLS
  `stream()` should not fail except for wrong stream type or disconnected state.

## Recommended End State

Application code sees:

```cpp
void connected(Zi::ConnectedInfo);
typename Link::StreamRef stream = link->stream();
stream->txStream();
stream->process(rx);
```

Transport differences remain where they belong:

- TCP/TLS: one implicit stream, no peer stream creation.
- QUIC: many streams, stream limits, peer stream notification.

This is a consistent abstraction with minimal semantic lying. It gives `zhttpclient`
a path to one application implementation while leaving enough room for HTTP/3 and
QUIC-specific stream behavior.
