# Zhttp HTTP/3 Unification Plan

## Goal

`Zhttp` will expose one HTTP application model for HTTP/3 over `Zquic` and
HTTP/1.1 over `Ztls`. Applications select headers at compile time, receive
request and response events through callbacks, stream bodies, and let `Zhttp`
own protocol framing, QPACK, fallback, and transport setup.

The first production surface is REST-oriented:

- HTTP/3 is preferred for HTTPS origins when enabled.
- HTTP/1.1 over TLS is the configured fallback path.
- Request and response bodies stream by default.
- Header selection uses the current `Keys`/`KVs` typelist pattern.
- QPACK dynamic indexing is opt-in through `Zhttp::H3::Params`.
- QUIC remains transport-owned by `Zquic`; production HTTP behavior lives in
  `Zhttp`.

## Current Implementation

### Build Boundary

The build order already supports the layering:

- Top-level `SUBDIRS` includes `ztls zquic zhttp`.
- `zhttp/src/Makefile.am` installs `ZhttpLib.hh`, `Zhttp.hh`,
  `Zhttp3.hh`, `ZhttpHPack.hh`, and `ZhttpQPack.hh`.
- `libZhttp.la` compiles `ZhttpLib.cc`, `Zhttp3.cc`, `ZhttpHPack.cc`, and
  `ZhttpQPack.cc`.
- `libZhttp.la` links against `libZquic.la` and `libZtls.la`.
- `zquic/test/ZquicH3Lite.hh` and `.cc` are built only into
  `ZquicH3InteropTest`.
- `zhttp/test` already covers parser, HPACK, QPACK, H3 state, H3 loopback,
  H3 interop, QPACK dynamic behavior, and fallback policy.

### Current `Zquic`

`zquic/src/ZquicTypes.hh` defines the production QUIC vocabulary with
`ZtEnum`: stream type/error, packet space/type, close/link state, transport
error, frame type, CID/path/PMTUD state, path hints, server packet action,
recovery event, sent frame kind, and crypto level.

`zquic/src/ZquicDiag.cc` now formats packet spaces, frame types, and stream
types through the generated `ZtEnum::name()` path, preserving the enum token
spelling in diagnostics.

`zquic/src/Zquic.hh` currently has two stream-related surfaces:

- Runtime engines: `Zquic::Engine<App>`, `Zquic::Client<App>`, and
  `Zquic::Server<App>`.
- Runtime params: `EngineParams`, `ClientParams`, and `ServerParams`, with
  `ZiMultiplex *`, Rx/Tx thread IDs, optional async thread, certificate paths,
  transport limits, `maxUDP`, ALPN, and `ErrorFn`.
- Runtime lifecycle: `Client::connect()` opens a connected UDP endpoint and
  starts QUIC/TLS; `Server::listen()` opens an unconnected UDP endpoint.
- Runtime diagnostics: endpoint readiness/failure, datagrams, packets, frames,
  crypto bytes, STREAM bytes, protection failures, TLS failures, and handshake
  completion.
- Current direct runtime STREAM helpers: `zquicStream(streamID, offset,
  payload, fin)`, `sendStream()`, `sendBidi()`, and `sendUni()`.
- Stream object model: `Zquic::Link<...>` creates local streams with
  `stream()`, accepts peer streams with `acceptPeerStream()`, reports streams
  through `streamed(ZmRef<Stream>)`, and `Zquic::Stream` exposes
  `txStream()`, `fin()`, `reset()`, `stop()`, receive buffering, final-size
  validation, and stream state.

The HTTP/3 implementation path promotes the stream object model for higher
protocols. Production H3 stream bytes are sent through
`Zquic::Stream::txStream()` and the resulting `ZiTxStream`; stream completion
and cancellation use `fin()`, `reset()`, and `stop()`. The direct runtime
STREAM helper surface is a cleanup item for production higher-protocol code.

### Current HTTP/1.1

`zhttp/src/Zhttp.hh` is the stable HTTP/1.1 parser/builder layer:

- `Method` is a `ZtEnum` and is protocol-neutral.
- `Headers<Keys, KVs>` prepends built-in `transfer-encoding` and
  `content-length` matching to caller-selected `Keys`.
- Header names are normalized in place to lowercase before `ZuMatcher`
  matching.
- `Request<Keys, KVs, Max>` parses a request line and selected headers.
- `Response<Keys, KVs, Max>` parses a status line and selected headers.
- `Body<Max>` consumes fixed-length and chunked bodies, including chunk
  trailers.
- `Parser<Header, Body, Context>` owns header/body parse state and caller
  context. Its `process()` method receives the mutable receive stream by
  reference.
- `Builder<Keys, KVs, HasBody, IsChunked, Context>` serializes HTTP/1.1
  messages to caller-owned Tx streams passed to `request()`, `response()`,
  `chunk()`, and `finish()`. Applications write body bytes directly to that
  stream after the header or chunk call; fixed `KVs` are emitted directly and
  variable `Keys` come from callbacks.

These public types remain the HTTP/1.1 implementation substrate and continue
to compile for existing users.

### Current HTTP/3 And QPACK

`zhttp/src/ZhttpHPack.hh` provides HPACK Huffman support for QPACK:

- `H3::HeaderBytes` is a `ZtArray<uint8_t>` used by low-level H3/QPACK code.
- `HPack::enclen`, `encode`, `declen`, and `decode` use caller-owned buffers
  and `ZuBitStream::BE`.

`zhttp/src/ZhttpQPack.hh` provides QPACK:

- `H3Error` and `QPackInstruction` use `ZtEnum`.
- `H3::Header` is `{name, value}` as spans.
- `H3::Headers` is a `ZtArray<Header>`.
- `H3::Params` owns `maxHeaderListSize`, `qpackTableCapacity`,
  `qpackBlockedStreams`, `qpackIndex()`, and `qpackNeverIndex()`.
- `QPack` implements static table lookup, static name lookup, field-section
  prefix handling, literal encode/decode, Huffman string decode, dynamic
  indexed/name-reference encode helpers, and QPACK instruction encode/decode.
- `DynamicTable` and `DynamicState` provide bounded dynamic table storage,
  capacity changes, insertion, duplication, blocked-stream tracking,
  acknowledgements, cancellations, and insert count increments.
- `Params::neverIndex()` treats `authorization`, `cookie`, and `set-cookie`
  as sensitive by default.

`zhttp/src/Zhttp3.hh` provides the current low-level HTTP/3 state layer:

- H3 enums use `ZtEnum`: `H3FrameType`, `H3StreamType`, `MessagePart`,
  `H3Setting`, `FallbackProtocol`, and `FallbackReason`.
- `Settings`, `ErrorCodec`, `Diag`, and `H3FrameCodec` cover settings,
  wire-code mappings, H3 frame parsing/serialization, and diagnostics.
- `FallbackPolicy` selects an H3 or H1.1 outcome from caller-supplied
  availability, ALPN, and connection booleans.
- `RequestParams` and `ResponseParams` encode low-level H3 field sections.
- `DecodedRequest`, `DecodedResponse`, and `DecodedTrailers` decode into
  `H3::Headers` plus `HeaderBytes` storage.
- `MessageStream` validates HEADERS/DATA/trailer order, body byte counts,
  cancellation, stream close, and per-stream H3 errors.
- `Connection` owns H3 SETTINGS/GOAWAY state, QPACK encoder and decoder state,
  peer critical stream state, H3 diagnostics, request/response/trailer
  encode/decode, DATA encode/decode, and QPACK instruction stream handling.
- `H3::Client` and `H3::Server` are thin holders around an implementation
  pointer, transport pointer, and `H3::Connection`.

## Target Public Surface

### Protocol Metadata

Add a small protocol-neutral header, `zhttp/src/ZhttpMsg.hh`, for metadata
shared by H1 and H3 adapters:

```c++
namespace Zhttp {

struct Protocol {
  ZtEnum(Protocol, int8_t, None, HTTP3, HTTP11);
};

struct RequestID {
  uint64_t value = 0;
};

struct RequestMeta {
  Method::T method = -1;
  ZuCSpan scheme = "https";
  ZuCSpan authority;
  ZuCSpan path;
};

struct ResponseMeta {
  unsigned status = 0;
};

struct MessageDiag {
  Protocol::T protocol = Protocol::None;
  uint64_t headerBytesRx = 0;
  uint64_t headerBytesTx = 0;
  uint64_t bodyBytesRx = 0;
  uint64_t bodyBytesTx = 0;
};

struct Error {
  Protocol::T protocol = Protocol::None;
  int code = 0;
  H3::H3Error::T h3Error = H3::H3Error::NoError;
  ZuCSpan text;
};

} // namespace Zhttp
```

### Header Selection

The application header model remains callback-oriented and typelist-driven:

```c++
template <
  typename Keys = ZuStringTL<>,
  typename KVs = ZuStringTL<>,
  typename Context = ZuEmpty>
struct RequestHandler;

template <
  typename Keys = ZuStringTL<>,
  typename KVs = ZuStringTL<>,
  typename Context = ZuEmpty>
struct ResponseHandler;
```

Applications provide callbacks for:

- request operation or response status
- selected variable header keys: `(context, int keyID, ZuCSpan value)`
- selected fixed key/value literals: `(context, int kvID)`
- body data
- selected trailers
- completion and error

HTTP/1.1 uses existing `Headers<Keys, KVs>` matching after in-place lowercase
normalization. HTTP/3 matches QPACK static fields, static names, dynamic
fields, and literal fields into the same callback IDs.

Authority mapping:

- H3 `:authority` is the neutral `RequestMeta::authority`.
- H1.1 `host` is the neutral `RequestMeta::authority`.
- H3 pseudo-headers remain codec metadata.
- Application `Keys` and `KVs` represent regular lowercase field names and
  fixed lowercase `name: value` strings.

### Body Streaming

Add streaming body interfaces to `ZhttpMsg.hh`:

```c++
namespace Zhttp {

struct BodyInfo {
  int64_t contentLength = -1;
  bool trailers = false;
};

using BodyFn = ZmFn<bool(ZuCSpan data, bool final)>;

class BodySource {
public:
  BodyInfo info() const;
  bool pull(BodyFn);
  template <typename KeyFn, typename KVFn>
  bool trailers(KeyFn &&keyFn, KVFn &&kvFn);
};

class BodySink {
public:
  bool push(ZuCSpan data, bool final);
};

} // namespace Zhttp
```

HTTP/1.1 maps `BodyInfo::contentLength` to `content-length`; unknown length
uses chunked transfer when that mode is selected. HTTP/3 maps each pulled body
chunk to a DATA frame on the request or response stream. Trailers reuse the
same `Keys`/`KVs` callback discipline as headers.

## QPACK Typelist Adapter

Add QPACK compile-time lookup helpers beside the current low-level QPACK code.
The adapter maps existing application `Keys`/`KVs` to QPACK static table
indexes and callback IDs:

- `QPackKVs`: fixed lowercase `name: value` strings.
- `QPackKV2ID`: application fixed-KV ID to QPACK static field index.
- `QPackID2KV`: QPACK static field index to application fixed-KV ID.
- `QPackKeys`: lowercase regular field names.
- `QPackKey2ID`: application key ID to QPACK static name index.
- `QPackID2Key`: QPACK static name index to application key ID.

Decode path:

1. Decode the field-section prefix with existing `QPack` helpers.
2. For static indexed fields, dispatch directly from QPACK static index to
   application key/KV callbacks.
3. For static-name and literal-name fields, decode the value/name into
   temporary storage as needed and call the application callback before that
   storage expires.
4. For dynamic fields, resolve through `DynamicState`, then use the same
   callback matching path as static/literal fields.
5. Account decoded name/value bytes against `Params::maxHeaderListSize()`.

Encode path:

1. Encode H3 pseudo-headers from `RequestMeta` or `ResponseMeta`.
2. Encode configured fixed `KVs` as static indexed QPACK fields where static
   table entries exist.
3. Encode variable `Keys` with static name references where possible.
4. Use literal field lines for remaining regular fields.
5. Apply `Params::qpackIndex()` and `Params::neverIndex()` for dynamic table
   insertions and sensitive values.
6. Emit QPACK encoder-stream instructions when dynamic references are enabled.

Low-level H3/QPACK tests may continue to use `H3::Header`, `H3::Headers`, and
`HeaderBytes`. Application adapters deliver selected values through callbacks.

## Protocol Adapters

### `Zhttp::H1::Codec`

Add `zhttp/src/Zhttp1.hh` and `Zhttp1.cc` as a thin adapter over current
HTTP/1.1 parsing and building.

Responsibilities:

- Drive `Request<>`, `Response<>`, `Body<>`, `Parser<>`, and `Builder<>`.
- Convert request method/path/host and response status into neutral metadata.
- Keep HTTP/1.1 framing fields owned by `Zhttp`.
- Preserve lowercase field normalization and `ZuMatcher` matching.
- Stream fixed-length, chunked, and trailer-bearing bodies into the neutral
  body callbacks.
- Keep current public H1 types available for direct users.

### `Zhttp::H3::Codec`

Add `zhttp/src/Zhttp3Codec.hh` and `Zhttp3Codec.cc` as the application-facing
adapter over `H3::Connection`, `MessageStream`, QPACK, and `Zquic::Stream`.

Responsibilities:

- Configure `Zquic::ClientParams` and `Zquic::ServerParams` with ALPN `h3`.
- Use `Zquic::Client::connect()` and `Zquic::Server::listen()` for transport
  lifecycle.
- Send H3 bytes through `Zquic::Stream::txStream()` / `ZiTxStream`.
- Complete and cancel H3 streams through `fin()`, `reset()`, and `stop()`.
- Open one local H3 control stream and local QPACK encoder/decoder streams
  after QUIC establishment.
- Write the QUIC unidirectional stream type prefix before control and QPACK
  bytes.
- Emit SETTINGS as the first control-stream frame using
  `Connection::encodeSettings()`.
- Parse peer unidirectional stream type prefixes and route bytes into
  `Connection::receiveControlFrame()`, `receiveQPackEncoderStream()`, and
  `receiveQPackDecoderStream()`.
- Maintain per-request `MessageStream` state keyed by QUIC stream ID.
- Buffer partial H3 frames per stream and reassemble complete H3 frame
  payloads across arbitrary QUIC STREAM frame splits/coalescing.
- Decode request, response, and trailer field sections into selected header
  callbacks.
- Encode request, response, trailer, and DATA frames from neutral metadata,
  selected headers, and `BodySource`.
- Surface GOAWAY as HTTP/3 graceful-shutdown state.

The `Zquic` source cleanup for this phase promotes `Link`/`Stream` as the
runtime stream surface for higher protocols. Direct STREAM helper use remains
available to low-level diagnostics/tests only while those tests need it.

## Top-Level Client

Add `zhttp/src/ZhttpClient.hh` with:

```c++
namespace Zhttp {

struct ProtocolPolicy {
  ZtEnum(ProtocolPolicy, int8_t, PreferHTTP3, HTTP3Only, HTTP11Only);
};

struct FallbackReason {
  ZtEnum(FallbackReason, int8_t,
    None, HTTP3Disabled, HTTP3Unavailable, ALPNRejected, ConnectFailed,
    RequestReplayDisabled, HTTP11Disabled);
};

struct FallbackDecision {
  Protocol::T protocol = Protocol::None;
  FallbackReason::T reason = FallbackReason::None;
  H3::H3Error::T h3Error = H3::H3Error::NoError;
};

struct ClientParams {
  ProtocolPolicy::T mode = ProtocolPolicy::PreferHTTP3;
  H3::Params h3;
  unsigned maxHeaderBytes = DefltMaxHdr;
  unsigned maxBodyBytes = DefltMaxBody;
  unsigned h3ConnectMS = 250;
  bool http11Fallback = true;
};

template <
  typename App,
  typename Keys = ZuStringTL<>,
  typename KVs = ZuStringTL<>,
  typename Context = ZuEmpty>
class Client;

} // namespace Zhttp
```

Client responsibilities:

- Own protocol selection and fallback decisions.
- Configure QUIC ALPN `h3` when HTTP/3 is enabled.
- Configure TLS ALPN `http/1.1` when HTTP/1.1 fallback is enabled.
- Use `HTTP3Only` and `HTTP11Only` as explicit transport selection modes.
- Start with HTTP/3 in `PreferHTTP3`.
- Use `h3ConnectMS` as the HTTP/3 connect deadline before H1.1 fallback.
- Replay a request after an H3 connect failure when no non-replayable body
  bytes have been sent, or when request options mark the request replayable.
- Send each HTTP/3 request on a client-initiated bidirectional QUIC stream.
- Start HTTP/1.1 fallback with one in-flight request per connection; add
  keep-alive reuse before pipelining.

Application callback shape:

- `zhttpConnected(Protocol::T protocol)`
- `zhttpFallback(const FallbackDecision &decision)`
- `zhttpResponse(RequestID, const ResponseMeta &meta)`
- `zhttpKey(RequestID, int keyID, ZuCSpan value)`
- `zhttpKV(RequestID, int kvID)`
- `zhttpBody(RequestID, ZuCSpan data, bool final)`
- `zhttpTrailers(RequestID, /* selected trailer callbacks */)`
- `zhttpComplete(RequestID)`
- `zhttpError(RequestID, const Error &error)`

The current `H3::FallbackPolicy` remains useful as low-level test scaffolding
while the top-level client grows the replay-aware `FallbackDecision`.

## Top-Level Server

Add `zhttp/src/ZhttpServer.hh` with:

```c++
namespace Zhttp {

template <
  typename App,
  typename Keys = ZuStringTL<>,
  typename KVs = ZuStringTL<>,
  typename Context = ZuEmpty>
class Server;

class RequestContext {
public:
  RequestID id() const;
  Protocol::T protocol() const;
  bool respond(unsigned status, BodySource *body = nullptr);
  bool send(ZuCSpan data, bool final = false);
  bool reject(unsigned status);
  bool cancel(uint64_t appError = 0);
};

} // namespace Zhttp
```

Server callback shape:

- `zhttpRequest(RequestContext &, const RequestMeta &meta)`
- `zhttpKey(RequestContext &, int keyID, ZuCSpan value)`
- `zhttpKV(RequestContext &, int kvID)`
- `zhttpBody(RequestContext &, ZuCSpan data, bool final)`
- `zhttpTrailers(RequestContext &, /* selected trailer callbacks */)`
- `zhttpComplete(RequestContext &)`
- `zhttpError(RequestContext &, const Error &error)`

HTTP/3 server responsibilities:

- Configure `Zquic::ServerParams` with ALPN `h3`.
- Open local control and QPACK streams after QUIC establishment.
- Route client-initiated bidirectional streams into request contexts.
- Enforce H3 request stream ordering through `MessageStream`.
- Send responses through neutral metadata, selected headers, `BodySource`, and
  DATA frames.
- Use GOAWAY for graceful shutdown.

HTTP/1.1 server responsibilities:

- Accept TLS ALPN `http/1.1`.
- Support cleartext only behind explicit configuration.
- Reuse current parser and builder.
- Map `host` to `RequestMeta::authority`.
- Deliver fixed-length, chunked, and trailer-bearing request bodies through the
  same callbacks used by H3.

## File Plan

1. `zhttp/src/ZhttpMsg.hh`: protocol metadata, body source/sink interfaces,
   fallback enums, error type, diagnostics, and shared callback concepts.
2. `zhttp/src/Zhttp1.hh` and `Zhttp1.cc`: HTTP/1.1 adapter over current
   parser/builder/body code.
3. `zhttp/src/Zhttp3Codec.hh` and `Zhttp3Codec.cc`: HTTP/3 adapter over
   `H3::Connection`, `MessageStream`, QPACK, and `Zquic::Stream`.
4. `zhttp/src/ZhttpClient.hh`: protocol-selecting client API.
5. `zhttp/src/ZhttpServer.hh`: unified server API and request context.
6. `zhttp/src/Makefile.am`: install new public headers and compile new `.cc`
   files.
7. `zquic/src/Zquic.hh` and related stream files: converge higher-protocol
   stream runtime on `Link`/`Stream` plus `ZiTxStream`.
8. `zhttp/test`: add adapter/client/server tests while retaining low-level
   H3/QPACK/HPACK tests.

## Implementation Roadmap

### Phase 1: Baseline And Transport Convergence

- Keep current `zhttp/test` and `zquic/test` passing before public API work.
- Keep `ZquicH3Lite` scoped to `zquic/test` interop.
- Promote `Link`/`Stream` as the runtime stream surface used by H3.
- Route production higher-protocol Tx through `Stream::txStream()`.
- Keep direct runtime STREAM helpers only for low-level transport diagnostics
  and tests that exercise packet/frame mechanics.
- Add tests proving H3 adapter code emits stream bytes through `ZiTxStream`.

### Phase 2: Neutral Metadata And H1 Adapter

- Add `ZhttpMsg.hh`.
- Define callback concepts for request/status, keys, fixed KVs, bodies,
  trailers, completion, and errors.
- Add validation helpers for lowercase regular field names, pseudo-header
  ordering, content-length consistency, and trailer legality.
- Wrap current `Request<>`, `Response<>`, `Body<>`, `Parser<>`, and
  `Builder<>` in `H1::Codec`.
- Add tests for host/authority mapping, fixed-length body, chunked body,
  trailers, and selected header callbacks.

### Phase 3: QPACK Callback Adapter

- Add `QPackKeys`/`QPackKVs` static-table mapping helpers.
- Decode static indexed fields to selected header callbacks.
- Decode literal and Huffman strings into temporary storage and invoke
  callbacks before storage expiry.
- Resolve dynamic fields through `DynamicState` when dynamic QPACK is enabled.
- Encode selected fixed and variable headers using static indexed/static-name
  forms where available.
- Preserve current `ZhttpQPackTest` and `ZhttpQPackDynamicTest` coverage.

### Phase 4: H3 Codec Adapter

- Add `H3::Codec`.
- Move control/QPACK stream setup from tests into the codec.
- Add unidirectional stream type prefix parsing and writing.
- Add per-stream H3 frame buffering.
- Drive `H3::Connection` for SETTINGS, GOAWAY, QPACK streams, and
  request/response/trailer encode/decode.
- Deliver decoded headers through `Keys`/`KVs` callbacks.
- Stream DATA frames to/from `BodySource` and body callbacks.
- Add neutral H3 request/response/trailer round-trip tests.

### Phase 5: Client

- Add `Zhttp::Client<App, Keys, KVs, Context>`.
- Add protocol policy and replay-aware fallback decisions.
- Configure `Zquic::ClientParams` and `Ztls::ClientParams` from the top-level
  client.
- Implement HTTP/3-first connection attempts with `h3ConnectMS`.
- Implement HTTP/1.1 fallback.
- Add tests for H3 success, H1.1 fallback, H3-only failure, H1.1-only success,
  and non-replayable request handling.

### Phase 6: Server

- Add `Zhttp::Server<App, Keys, KVs, Context>`.
- Add `RequestContext`.
- Route H3 request streams and H1.1 accepted connections into the same
  callbacks.
- Implement response writers for both protocols.
- Add tests for H3 request/response, H1.1 request/response, trailers,
  cancellation, and graceful shutdown.

### Phase 7: Interop And Examples

- Use top-level `Zhttp::Client` and `Zhttp::Server` in mainstream HTTP/3
  interop tests.
- Keep low-level H3/QPACK/HPACK tests for codec correctness.
- Cover curl HTTP/3 to `Zhttp::Server`.
- Cover `Zhttp::Client` HTTP/3 to Caddy.
- Cover curl HTTP/1.1 fallback to `Zhttp::Server`.
- Cover `Zhttp::Client` HTTP/1.1 fallback to Caddy or a local lightweight
  server.
- Update `zhttp/README.md` with one client example, one server example,
  fallback semantics, replay policy, body streaming, trailers, lowercase field
  names, QPACK defaults, and first-release exclusions.
- Keep `zquic/README.md` transport-oriented.

## Acceptance Criteria

- Existing users of `Zhttp::Request<>`, `Zhttp::Response<>`, `Zhttp::Body<>`,
  `Zhttp::Parser<>`, and `Zhttp::Builder<>` continue to compile.
- Current low-level H3/QPACK/HPACK tests continue to pass.
- Application-facing HTTP APIs expose metadata, selected header callbacks, body
  callbacks, completion, and errors.
- H3 codec internals keep `H3::Headers`, `HeaderBytes`, and `H3FrameCodec`
  inside low-level code, adapter code, and focused codec tests.
- Production H3 stream Tx uses `Zquic::Stream::txStream()` / `ZiTxStream`.
- H3 stream completion and cancellation use `fin()`, `reset()`, and `stop()`.
- HTTP/1.1 and HTTP/3 deliver selected regular fields with the same `Keys` and
  `KVs` IDs.
- H3 pseudo-headers map into neutral metadata before application callbacks.
- QPACK static table entries are used for selected `Keys`/`KVs` where possible.
- Literal and Huffman QPACK decode invokes callbacks while temporary storage is
  still valid.
- Body data streams without hidden full-message buffering in the core
  client/server path.
- Dynamic QPACK remains opt-in through `H3::Params`.
- `Zhttp` owns production HTTP/3; `Zquic` owns QUIC transport and test-only
  H3Lite interop under `zquic/test`.
- From a configured build tree, these commands pass:
  - `make -C zhttp/test test`
  - `make -C zquic/test test`
  - `make -j`
