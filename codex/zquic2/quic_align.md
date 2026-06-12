## Summary

Update: API compatibility is a non-goal. Any older phase text below that says
to keep wrappers, compatibility callbacks, or default-link conveniences is
superseded: delete historical APIs as soon as replacement `CliLink`/`SrvLink`
paths exist, and cascade the break to tests/examples.

The goal is to align `Zquic` with the current `Ztls` public CRTP style while preserving QUIC's transport-specific shape:

- `App`, `Client`, `Server`
- `Link`, `CliLink`, `SrvLink`
- `Cxn`, `CliCxn`, `SrvCxn`
- `Stream`, `CliStream`, `SrvStream`
- `Link` owns zero or more `Stream`s.
- CRTP expectations are documented inline in public headers with `#if 0`, as in `Ztls`.
- Error handling uses configured `ErrorFn`, `error_(ZeException)`, and `ZeEXCEPT(...)`; direct `ZiLOG(Error, ...)` should not remain in `Zquic` runtime or validation error paths.

## Current Implementation Status

Completed in the current alignment pass:

- `Engine` validation/error paths use the configured `ErrorFn` / `error_(ZeException)` style.
- `Client` runtime ownership has moved to `CliLink`; old `Client` connect/send/default-runtime APIs are deleted.
- `Server` owns a `SrvCxn<Server>` UDP listener, accepts new `SrvLink`s through `App::accepted(const InitialInfo &)`, and routes datagrams to active links by DCID on the configured Zquic Rx thread.
- Server logical runtime ownership has moved to `SrvLink`; old server runtime/send wrappers and `App::zquicStream(...)` dispatch are deleted.
- `Stream` is parameterized by its owning link and exposes `link()`.
- `CliStream` and `SrvStream` are thin role-specific stream bases over the link-owned stream type.
- Runtime and H3 tests use `CliLink` / `SrvLink` / `CliStream` / `SrvStream` directly.
- The H3 runtime server response path uses `SrvLink::streamFrame(...)` plus `SrvLink::send(stream, ...)`.
- `Stream::sent_()` / `Stream::fin()` notify the owning `Link`, and `CliLink` / `SrvLink` send paths now enqueue via `stream->txStream()` and flush scheduled streams through `StreamPacketizer`.
- `StreamPacketizer` folds FIN into the final data STREAM frame when the queued data is drained, avoiding the old direct one-shot payload path without introducing an extra FIN-only frame for normal sends.
- The queued-stream packet assembly and scheduled-stream drain loop are factored into common `Link` helpers; `CliLink` and `SrvLink` now only bind role-specific ACK emission and short-packet send callbacks.
- ACK pending/write handling, received packet tracking, sent packet recording, ACK frame processing, and ACK-aware payload building are factored into common `Link` helpers used by both `CliLink` and `SrvLink`.
- CRYPTO flight splitting and CRYPTO packet dispatch are factored into common `Link` helpers; roles only choose Initial key direction, CIDs, and send callbacks.
- Protected Initial/Handshake/short packet construction, protection, send accounting, datagram parsing, deprotection, received packet accounting, and frame dispatch are factored into common `Link` helpers.
- The server listener has a route-token table backed by active `LinkBase` refs and `CIDRouter`; first Initial creates a `SrvLink`, and later Initial/Handshake/short datagrams route to the owning link by DCID.
- Server link close releases its listener route entries, tombstones original Initial DCIDs, retires local CIDs, and treats unknown/stale DCIDs as endpoint diagnostics instead of `ErrorFn` failures.
- Runtime packet reset and TLS advancement are factored into common `Link` helpers; roles still provide bootstrap, transport-parameter validation, packet-send callbacks, and server HANDSHAKE_DONE policy.
- Common logical connection runtime state is owned by `Link`: diagnostics, crypto, transport parameters, connection state, CRYPTO streams, connection IDs, packet number spaces, ACK trackers, sent-packet spaces, pending ACK flags, and handshake/established flags.
- Common `Link` runtime state is encapsulated as private `m_runtime`; `CliLink` and `SrvLink` use protected `Link` operations for reset, TLS, packet construction/protection, receive/deprotection, diagnostics, and state transitions instead of touching runtime fields directly.
- Public datagram/endpoint diagnostic types moved to `ZquicDatagram.hh`; `Zquic.hh` no longer includes `ZquicEndpoint.hh`, and the old `Endpoint` helper is no longer installed as public API.
- `zquic/README.md` documents the final CRTP surface: `Client`/`Server`, `CliLink`/`SrvLink`, `CliStream`/`SrvStream`, server `accepted(const InitialInfo &)`, and the private/test status of `Endpoint`.
- `zquic/example` contains noinst client/server programs using the final `CliLink`/`SrvLink` and stream APIs.

Remaining follow-up work:

- None for the CRTP/API alignment pass. Later QUIC feature work remains covered by the non-goals below.

The pre-alignment `Zquic` runtime was endpoint-centered. `Client` and `Server` duplicated connection state, packet protection, ACK state, CRYPTO stream state, and STREAM dispatch. `Endpoint` owned the physical UDP `ZiConnection`, and STREAM frames were counted and optionally passed directly to `App::zquicStream(...)`.

The target is link-centered. `Client` creates and manages client links, `Server` listens and routes datagrams to server links, and each `CliLink` / `SrvLink` owns one logical QUIC connection runtime plus its streams.

Important decisions from `quic_align.md` are retained:

- Keep two QUIC runtime threads, `rxThread` and `txThread`.
- `Endpoint` is a private/test helper; the aligned public API exposes `Cxn` / `CliCxn` / `SrvCxn`, with public datagram diagnostics in `ZquicDatagram.hh`.
- Packet/protocol failures caused by peers are diagnostics-only unless they reveal a local/internal failure.
- `Stream` is parameterized by `Link`, matching the way `Cxn` is parameterized by owner.
- `Client<App>` can initiate multiple client links that share one app/client context; old one-link convenience methods are deleted as replacement link APIs land.

The main correction after scrutinizing the actual APIs is that `ZiMultiplex::udp()` and `ZiConnection` callbacks run on Zi I/O threads, not on the `Zquic::Engine` `rxThread` / `txThread` selected by `ClientParams` and `ServerParams`. Therefore the aligned `Cxn` must do what `Ztls::Link::connected_0()` does: arm transport I/O on Zi threads and explicitly hop QUIC runtime work to the app's QUIC Rx/Tx threads. This is required to make the resolved two-thread QUIC model true in the implementation.

The other important QUIC-specific correction is server ownership. A TLS server has one accepted TCP `Cxn` per `SrvLink`. A QUIC server normally has one UDP socket receiving datagrams for many logical connections. The final `SrvCxn` should be the server/listener UDP adapter, while `SrvLink` is a routed logical connection using that socket for sends. A transitional single-link server can emulate the Ztls shape, but the multi-connection final design must not create one UDP socket per server link.

## Architecture Documentation

### New Or Changed Components

`Zquic::Engine<App>`

- Remains the common base for `Client<App>` and `Server<App>`.
- Keeps separate QUIC runtime threads:
  - `rxThread` for datagram parsing, packet protection, ACK state, CRYPTO input/output production, link state transitions, and stream receive dispatch.
  - `txThread` for packet construction, stream packetization, and UDP send scheduling.
  - optional `asyncThread` remains rejected or unsupported until async signing is implemented robustly.
- Assigns `m_errorFn` before validation, matching `Ztls::Engine::init_()`.
- Adds protected `error_(ZeException)`.
- Uses `error_(ZeEXCEPT(Error, "Zquic", ...))` for local configuration/internal failures.

`Zquic::Cxn<Owner, OwnerRef>`

- Becomes the public UDP socket adapter and derives from `ZiConnection`.
- Reuses the working mechanics in `Endpoint::Cxn_`: Rx packet allocation, datagram delivery, Tx buffer queue, `ZiIOContext::addr` send/receive handling, and abrupt close behavior.
- Calls owner callbacks for physical socket lifecycle and datagrams:
  - `connected_0(Cxn *, ZiIOContext &)` from Zi I/O Rx thread.
  - `disconnected_0(Cxn *, OwnerRef)` from Zi I/O thread.
  - `received_0(Datagram)` from Zi I/O Rx thread, which must hop to QUIC Rx.
  - `sent_0(unsigned)` and `ioError_0()` from Zi I/O Tx/Rx, which must update diagnostics on the correct runtime context or use atomics.
- Does not directly parse QUIC packets on Zi I/O threads.

Proposed alias shape:

```c++
template <typename Link>
using CliCxn = Cxn<Link, Link *>;

// Server owns the physical UDP listener. SrvLink is logical and routed.
template <typename Server>
using SrvCxn = Cxn<Server, Server *>;
```

If a `SrvCxn<Link> = Cxn<Link, ZmRef<Link>>` alias is kept temporarily for single-link tests, document it as transitional. The final server design should route many `SrvLink`s over one listener `SrvCxn`.

`Zquic::Link<App, Impl, Stream, ...>`

- Becomes the shared per-logical-QUIC-connection runtime.
- Owns state currently duplicated in `Client` and `Server`:
  - `Crypto`
  - `TransportParams`
  - `ClientBootstrap` or `ServerBootstrap`
  - `CryptoStream[3]`
  - connection IDs
  - packet numbers
  - receive ACK trackers
  - sent packet spaces
  - pending ACK flags
  - `ConnState`
  - stream table and stream limits
  - runtime diagnostics
  - peer address/path information
- Provides role-neutral helpers for:
  - datagram inspection
  - long and short packet parsing
  - packet protection and deprotection
  - ACK parsing/generation
  - CRYPTO frame reassembly and TLS advancement
  - STREAM/RESET/STOP/MAX_STREAMS dispatch
  - close/drain placeholders
- Calls CRTP callbacks on `Impl`:
  - `connected(const char *alpn, int quicver)`
  - `disconnected()`
  - `streamed(StreamRef)`

`Zquic::CliLink<App, Impl, Stream, ...>`

- Persistent client-side logical QUIC connection.
- Owns a `ZmRef<CliCxn<Impl>>`.
- Holds or accepts server host/IP and port.
- Opens UDP through `ZiMultiplex::udp()` using `ZiCxnOptions::udp(true)`.
- Uses `ClientBootstrap::startRandom(...)`, not hard-coded CIDs.
- Starts ClientHello by scheduling link runtime work on the QUIC Rx thread after the UDP socket is ready.
- Handles stale physical connection callbacks by comparing the current `m_cxn`.
- Provides default `connectFailed(bool)` behavior modeled on `Ztls::CliLink`.

`Zquic::Server<App>`

- Owns the physical server UDP listener `SrvCxn<Server<App>>`.
- Owns the active server-link table for multi-connection mode.
- Parses enough of incoming datagrams to route by DCID.
- Creates `SrvLink` on the first valid Initial.
- Deletes historical app-level stream send helpers as tests/examples move to `SrvLink` stream APIs.

`Zquic::SrvLink<App, Impl, Stream, ...>`

- Represents one server-side logical QUIC connection.
- Does not own a separate UDP socket in final multi-connection mode.
- Stores a non-owning pointer/ref to the server listener `SrvCxn` or calls back through `Server` to send datagrams.
- Uses `ServerBootstrap::acceptInitial(...)` and `transportParams(...)`.
- Emits `HANDSHAKE_DONE` once 1-RTT is established.
- Owns streams and per-connection diagnostics.

`Zquic::Stream<Link, Impl, TxBufAlloc>`

- Evolves the current `Stream<Impl, TxBufAlloc>` by adding an owning link pointer/ref.
- Keeps current receive reassembly, packet-backed queue slices, Tx queue retention, FIN/RESET/STOP state, and `processFrame(...)` contract.
- Adds:
  - `link()`
  - constructor `(Link *, int64_t id)`
  - a Tx notification from `sent_()` to `link()->streamWritable_(id())`
- Derived app stream implements `int process(RxStream &)`.

`Zquic::CliStream<Link, Impl, ...>` and `Zquic::SrvStream<Link, Impl, ...>`

- Thin role-specific bases over `Stream<Link, Impl, ...>`.
- Should not duplicate stream machinery.
- May add role-specific conveniences later only when an application-facing need appears.

### New Or Changed Processes Or Threads

There are four relevant execution contexts:

- Zi I/O Rx thread: `ZiConnection::connected()`, UDP `recvfrom`, and physical socket disconnect callbacks.
- Zi I/O Tx thread: UDP `sendto` and send completion callbacks.
- Zquic Rx thread: logical QUIC connection state, packet receive, deprotection, ACK state, CRYPTO/TLS handling, stream receive.
- Zquic Tx thread: packet construction, stream scheduling, send queue production.

The aligned design must not confuse Zi I/O threads with Zquic runtime threads. `ZiMultiplex::udp()` internally uses `rxInvoke(...)` on the multiplexer I/O Rx thread. The configured `Zquic::Engine::rxThread()` and `txThread()` are separate scheduler IDs and must be used explicitly through `rxRun` / `rxInvoke` and `txRun` / `txInvoke`.

`Cxn` therefore has two layers of callbacks:

```c++
// Zi I/O Rx thread
void Cxn::connected(ZiIOContext &io) {
  owner()->connected_0(this, io);
}

// Owner connected_0 arms Zi receive immediately, then hops runtime work.
void CliLink::connected_0(Cxn *cxn, ZiIOContext &io) {
  cxn->armRecv_(io);
  app()->rxRun([link = ZmMkRef(impl()), cxn = ZmMkRef(cxn)]() {
    link->connected_(ZuMv(cxn));
  });
}
```

TLS/picotls calls must be serialized per link. The practical owner is the Zquic Rx thread because TLS is driven by received CRYPTO frames and client initial ClientHello production. The Tx thread may consume CRYPTO/STREAM frame bytes already produced by Rx-thread work, but it should not call picotls directly unless marshalled back to Rx.

Async signing is a non-goal for this alignment pass. `Crypto::handleTLSMessage()` currently treats `PTLS_ERROR_ASYNC_OPERATION` as a non-negative output-producing state, but `Zquic` has no `Ztls`-quality async job lifecycle. The aligned runtime should reject/report async configuration as unsupported until that lifecycle exists.

### New Or Changed Interfaces

Public aligned client shape to document inline with `#if 0`:

```c++
struct App : public Zquic::Client<App> {
  struct Link;
  struct Stream;
};

struct App::Stream : public Zquic::CliStream<Link, Stream> {
  // Zquic Rx thread.
  // >0 consumed progress, 0 leave data queued, <0 reset/close per policy.
  int process(Zquic::RxStream &);
};

struct App::Link : public Zquic::CliLink<App, Link, App::Stream> {
  Link(App *, Zquic::Host server, uint16_t port);

  // Zquic Rx thread after QUIC handshake is established.
  void connected(const char *alpn, int quicver);
  void disconnected();
  void connectFailed(bool transient);
  void streamed(ZmRef<Stream>);
};
```

Public aligned server shape to document inline with `#if 0`:

```c++
struct App : public Zquic::Server<App> {
  struct Link;
  struct Stream;

  // Optional: create/reject a logical QUIC connection.
  ZmRef<Link> accepted(const Zquic::InitialInfo &);
};

struct App::Stream : public Zquic::SrvStream<Link, Stream> {
  int process(Zquic::RxStream &);
};

struct App::Link : public Zquic::SrvLink<App, Link, App::Stream> {
  Link(App *);

  void connected(const char *alpn, int quicver);
  void disconnected();
  void streamed(ZmRef<Stream>);
};
```

Historical APIs to delete during migration:

- `Client::connect(localIP, localPort, remoteIP, remotePort)`
- `Server::listen(localIP, localPort)`
- `Client::sendBidi`, `Client::sendUni`, `Client::sendStream`
- `Server::sendBidi`, `Server::sendUni`, `Server::sendStream`
- optional `App::zquicStream(...)`

Current status: deleted. Compatibility is a non-goal.

### New Or Changed Data Flows

Client flow:

1. Application constructs `App::Link`.
2. `CliLink::connect(...)` resolves/records remote address and asks `ZiMultiplex::udp()` to open a connected UDP socket.
3. `ZiMultiplex` invokes `ZiConnectFn`, receives a `CliCxn *`, adds it to the multiplexer, then calls `Cxn::connected()` on Zi I/O Rx.
4. `CliLink::connected_0(...)` arms UDP receive and schedules QUIC runtime startup on Zquic Rx.
5. `CliLink::connected_(...)` initializes bootstrap, Initial keys, local transport params, and TLS, then produces ClientHello CRYPTO data.
6. Packet construction and physical sends happen through Zquic Tx and `Cxn::sendPacket(...)`.
7. Incoming datagrams go Zi I/O Rx -> `Cxn::recvDone_()` -> `CliLink::received_0(...)` -> Zquic Rx `Link::received_(Datagram)`.
8. 1-RTT readiness calls `impl()->connected(alpn, quicver)` and enables streams.

Server flow:

1. `Server::listen(...)` opens one unconnected UDP listener through `SrvCxn<Server>`.
2. The listener arms UDP receive on Zi I/O Rx.
3. Each datagram is scheduled to Zquic Rx.
4. Server parses enough long-header data to route:
  - known DCID -> existing `SrvLink`.
  - first valid Initial -> create `SrvLink`, call `ServerBootstrap::acceptInitial(...)`, add route entries, deliver datagram.
  - invalid/unknown -> diagnostics-only drop or later stateless reset.
5. `SrvLink` owns TLS, packet state, streams, and peer path.
6. Sends from `SrvLink` use the server listener `SrvCxn` with `ZiIOContext::addr` set to the peer address.

Stream flow:

1. Application opens a local stream with `link->stream(StreamType::Bidi/Uni)`.
2. `Link` creates `new Stream(link, id)` and stores it in the link-owned stream hash.
3. Application writes through `stream->txStream()`.
4. `Stream::sent_()` queues data and calls `link()->streamWritable_(id())`.
5. `Link` schedules Tx and uses `StreamScheduler` plus `StreamPacketizer` to emit STREAM frames.
6. Received STREAM frames find or accept a link-owned stream and call `stream->processFrame(...)`.
7. `Stream::processFrame(...)` preserves the current return contract from derived `process(RxStream &)`.

### New Or Changed Event-Driven Or Timer Processing

Initial phases should add only the event hooks needed to preserve current behavior:

- Cxn ready hook: Zi I/O Rx callback schedules link startup on Zquic Rx.
- Datagram hook: Zi I/O Rx schedules packet processing on Zquic Rx.
- ACK hook: `noteAck_()` marks pending ACK and schedules a Zquic Tx flush.
- Stream hook: `streamWritable_(id)` adds the stream to `StreamScheduler` and schedules Zquic Tx.
- Close/drain hook: placeholder timer for multi-connection cleanup.
- Client reconnect hook: `reconnFreq()` modeled on `Ztls::CliLink`.
- Server rebind hook: `rebindFreq()` modeled on `Ztls::Server`.

Full recovery, PTO, pacing, PMTUD, and stateless reset timers are not part of the alignment pass unless existing helper tests already cover them.

### New Or Changed Network Programming

Scrutinized Zi API behavior:

- `ZiMultiplex::udp()` requires `ZiCxnOptions::udp(true)`.
- `udp()` schedules `udp_()` on Zi I/O Rx via `rxInvoke`.
- `ZiConnectFn` returns a `ZiConnection *`; `ZiMultiplex::executedConnect()` refs it, adds it, then calls `connected()`.
- `ZiConnection::connected()` calls derived `connected(ZiIOContext &)`.
- For UDP receive, `ZiConnection` uses `recvfrom` / `WSARecvFrom` and fills `ZiIOContext::addr`.
- For UDP send, if `ZiIOContext::addr` is set, `ZiConnection` uses `sendto` / `WSASendTo`.
- UDP `disconnect()` bypasses TCP shutdown and closes through `executedDisconnect()`.

Implications:

- `Cxn` should derive from `ZiConnection`.
- `Cxn` must keep a positive ref while queued sends or scheduled callbacks can refer to it.
- QUIC close frames must be emitted before calling `Cxn::disconnect()` / `close()` if graceful QUIC close behavior is desired.
- Server logical links must not own physical sockets in multi-connection mode.

### New Or Changed Data Stores

Client:

- `Client` holds zero or more client links.
- Each `CliLink` owns its `CliCxn` and runtime state.

Server:

- `Server` owns one listener `SrvCxn`.
- `Server` owns active `SrvLink`s in a hash/ref collection.
- `CIDRouter` is used for CID slots, but its `uintptr_t routeToken` is a token, not ownership.
- Route entries must be retired/tombstoned before a `SrvLink` can be destroyed.
- Unknown/stale DCIDs should increment diagnostics and drop unless a stateless reset feature is explicitly implemented later.

Streams:

- Each `Link` owns a `Streams_<Stream>` hash.
- `StreamScheduler` tracks writable stream IDs.
- Flow limits remain in `Link` and `Stream` helper state.

## Detailed Design and Implementation Plan

### Phase 1: Error Handling, CRTP Docs, And Test Baseline

Area of focus: align diagnostics and public documentation without moving runtime ownership.

Add or modify:

- In `Zquic::Engine::init_()`, assign `m_errorFn = params.errorFn_` and default it before `validate_(params)`.
- Add protected `Engine::error_(ZeException)`, matching `Ztls`.
- Replace all `ZiLOG(Error, Log, ...)` in `Engine::validate_()` with `error_(ZeEXCEPT(Error, "Zquic", ...))`.
- Add Ztls-style pre-validation in `Client::init` and `Server::init`:
  - client cert/key paths must be configured together if mTLS is used.
  - server cert/key paths are required before listen/runtime TLS can succeed.
- Add `#if 0` CRTP documentation blocks for target `Client`/`CliLink`/`CliStream` and `Server`/`SrvLink`/`SrvStream`.
- Identify `sendBidi`, `sendUni`, `sendStream`, and `zquicStream(...)` as historical APIs to delete during cutover.
- Keep protocol/packet peer failures as diagnostics-only.

How it connects:

- This phase makes failure reporting safe before later phases move code across classes.
- It also creates compile-level expectations for the target CRTP API before the implementation is complete.

API scrutiny:

- `Ztls::Engine::init_()` sets `m_errorFn` before validation.
- Current `Zquic::Engine::validate_()` logs globally before `m_errorFn` exists.
- Current `Crypto::initTLS()` will fail without server cert/key, but that failure currently happens late in runtime; moving it to `Server::init` improves parity with `Ztls`.

Tests:

- Add `ZquicErrorTest` or extend `ZquicAPITest` to install a custom `ErrorFn` and assert it receives validation errors.
- Keep `ZquicRuntimeTest`, `ZquicStreamTest`, and `ZquicEndpointTest` unchanged and passing.

### Phase 2: UDP Cxn And Client Link Skeleton

Area of focus: introduce physical UDP `Cxn` and a client `CliLink` path while preserving current client runtime behavior.

Add or modify:

- Implement `Cxn<Owner, OwnerRef>` deriving from `ZiConnection`.
- Move or copy the reusable mechanics from `Endpoint::Cxn_`:
  - Rx buffer allocation and `armRecv_`.
  - Tx buffer plus bounded queued sends.
  - `ZiIOContext::addr` preservation.
  - send completion diagnostics.
  - I/O error diagnostics.
- Add `CliCxn<Link> = Cxn<Link, Link *>`.
- Add minimal `CliLink<App, Impl, Stream, ...>` with:
  - `connect(...)`
  - `connect_()`
  - `disconnect()` / `disconnect_()`
  - `connected_0(Cxn *, ZiIOContext &)`
  - `disconnected_0(Cxn *, Link *)`
  - `received_0(Datagram)`
- `connected_0` and `received_0` must hop to Zquic Rx.
- Keep current `Client` runtime fields initially.
- Delete `Client::connect(...)` and require applications/tests to create `App::Link`.
- Leave `Endpoint` available only as a private/test helper until direct tests no longer need it.

How it connects:

- Establishes the Ztls-like physical connection adapter without requiring a full runtime move.
- Creates a real path where `CliLink` receives socket ready/fail/disconnect callbacks.

API scrutiny:

- `ZiMultiplex::executedConnect()` stores a `ZmRef<ZiConnection>` and calls `connected()` after `cxnAdd`.
- `ZiConnection::connected()` passes a mutable `ZiIOContext &`; receive must be armed there.
- Current `Endpoint::Cxn_` does not hop to Zquic runtime threads; the new `Cxn` owner callbacks must.

Tests:

- Extend `ZquicEndpointTest` or add `ZquicCliLinkTest` to verify UDP open through `CliLink`.
- Assert `connectFailed(bool)` is called on open failure.
- Assert a stale callback after `disconnect()` or reconnect is ignored.

### Phase 3: Move Client Runtime State Into CliLink End-To-End

Area of focus: make the client path link-owned while the server can still be the old runtime.

Add or modify:

- Move client runtime fields from `Client` to `CliLink` or common `Link` storage:
  - `Crypto`
  - `TransportParams`
  - `ClientBootstrap`
  - `CryptoStream[3]`
  - `ConnectionID` values
  - packet numbers
  - ACK trackers
  - packet Tx spaces
  - pending ACK flags
  - runtime diagnostics
  - established/handshake state
- Replace hard-coded `zqinit01` / `zqcli001` CIDs with `ClientBootstrap::startRandom(...)`.
- Derive Initial keys from the actual initial DCID.
- Initialize client transport params from `Engine` limits.
- Start ClientHello on Zquic Rx after UDP ready:
  - `Crypto::initTLS(...)`
  - `Crypto::handleTLSMessage(out, offsets, 0, {})`
  - schedule packet construction/sends through Zquic Tx.
- Validate server transport params with `ClientBootstrap::validateServerTransportParams(...)` when peer params are available.
- Change `Client::runtimeDiag()`, `endpointDiag()`, `crypto()`, `connected()`, `ready()`, and `established()` to delegate to the default link.

How it connects:

- Exercises `CliLink` through the real handshake path.
- Keeps the old server path so the phase has one primary moving side.

API scrutiny:

- `Crypto::initTLS()` owns per-connection TLS state and certificate resources, so placing `Crypto` inside `Link` is correct.
- `Crypto::handleTLSMessage()` returns output length for success, in-progress, and async operation; it returns `-1` for other TLS errors. The link should inspect `tlsResult()` if it needs to distinguish unsupported async from protocol failure.
- Current async lifecycle is not robust; reject/report unsupported async rather than pretending it works.

Tests:

- Existing `ZquicRuntimeTest` must still establish client/server handshake.
- Add a client test creating two `CliLink`s under one `Client<App>` and verifying independent CIDs/diagnostics without requiring both to complete a full interop flow.

### Phase 4: Server Listener And Single SrvLink Runtime

Area of focus: introduce `SrvLink` and move server runtime state into it without yet enabling multiple simultaneous server links.

Add or modify:

- Add `SrvCxn<Server>` as the physical unconnected UDP listener adapter.
- Add `SrvLink<App, Impl, Stream, ...>` as the logical server connection.
- `Server::listen(...)` opens one UDP listener, not one socket per link.
- On first valid Initial datagram:
  - parse long header.
  - create a default `App::Link` / `SrvLink`.
  - call `ServerBootstrap::acceptInitial(...)`.
  - initialize server crypto and transport params.
  - deliver the datagram to that link's receive path.
- Move server runtime fields from `Server` into `SrvLink`.
- Preserve `HANDSHAKE_DONE` emission.
- Delete app-level runtime/send wrappers once `SrvLink` owns logical runtime.

How it connects:

- Converts the server side to the aligned logical-link model while preserving current single-peer runtime tests.
- Sets up the final multi-connection server shape by making the UDP socket server-owned.

API scrutiny:

- `ServerBootstrap::acceptInitial(...)` requires minimum datagram length, supported version, valid Initial, and nonzero route token.
- `CIDRouter` stores `uintptr_t routeToken`; it does not own the link.
- `Crypto::initTLS()` server mode requires cert/key paths, already validated in Phase 1.

Tests:

- Existing `ZquicRuntimeTest` passes through `SrvLink`.
- Add `ZquicSrvLinkTest` for `listening`, `connected`, `disconnected`, and later `streamed`.
- Add invalid Initial test that increments diagnostics and does not invoke `ErrorFn`.

### Phase 5: Common Link Packet, ACK, And CRYPTO Helpers

Area of focus: factor client/server duplicated packet logic after both roles have real link-owned behavior.

Add or modify:

- Move duplicated helpers into common `Link`:
  - `received_(Datagram)`
  - datagram packet loop
  - long/short parse scaffolding
  - ACK tracking and ACK frame emission
  - Tx packet recording
  - payload building
  - CRYPTO flight splitting
  - protected Initial/Handshake/short packet construction and Tx accounting
  - long/short deprotection and Rx accounting
  - frame dispatch, CRYPTO reassembly, ACK processing, and STREAM/RESET/STOP routing
  - common diagnostics updates
- Keep explicit role hooks for:
  - Initial packet key direction.
  - client/server CID selection in long headers.
  - server `HANDSHAKE_DONE`.
  - transport-param validation.
  - default local stream ID parity.
- Keep peer-caused parse/protection errors as counters, not app errors.

How it connects:

- Reduces duplication only after client and server paths can be compared in tests.
- Establishes the receive path into which stream integration will plug.

API scrutiny:

- `InitialPacketProtection::protectLongV` / `unprotectLong` require different client/server secrets; keep role hooks explicit.
- `PacketTxSpace::ack(...)` can mark loss; do not add retransmission behavior in this phase.
- Current `sendRuntimeCryptoFlights(...)` behavior should be preserved until recovery work proves a different flight strategy.

Tests:

- Existing runtime handshake tests pass after the factoring.
- Add packet protection failure counter tests for both roles.
- Verify ACK counters remain stable against current runtime expectations.

Current status:

- Done: CRYPTO flight splitting, CRYPTO packet dispatch, ACK bookkeeping, payload assembly, protected packet send, packet parse/deprotection, Rx packet accounting, and frame dispatch now live in common `Link` helpers.
- Done: runtime reset, TLS advancement, protected packet handling, and runtime state access are centralized in `Link`; roles retain only role-specific bootstrap, transport-parameter validation, send callbacks, and server HANDSHAKE_DONE policy.

### Phase 6: Runtime Stream Receive Integration

Area of focus: route received STREAM/RESET/STOP frames to link-owned streams instead of directly to `App::zquicStream(...)`.

Add or modify:

- Change stream template to `Stream<Link, Impl, TxBufAlloc>`.
- Add `link()` accessor and `(Link *, int64_t id)` constructor.
- Add `CliStream` and `SrvStream` thin bases.
- Update `Link::newStream_()` to construct streams with a link pointer.
- On STREAM receive:
  - find or accept stream.
  - call `stream->processFrame(frame, packet, &diag)`.
  - honor return values:
    - positive: progress, continue.
    - zero: leave queued data.
    - negative: reset/close according to policy.
- Delete `App::zquicStream(...)`; route received stream data through link-owned streams or explicit link CRTP hooks while stream processing is being completed.

How it connects:

- Uses existing tested `Stream` mechanics and connects them to real runtime packet receive.
- Keeps H3 tests alive while the API migrates.

API scrutiny:

- Current `Stream::processFrame(...)` already calls `impl()->process(m_rx)`.
- Current stream receive code queues packet-backed slices and must keep packet buffers alive.
- Current `Link` already has stream table and limits; this phase attaches it to runtime rather than rewriting it.

Tests:

- Port `ZquicStreamTest`, `ZquicLoopTest`, `ZquicFlowTest`, and `ZquicAPITest` to the new constructor and aliases.
- Add runtime stream test where `App::Stream::process()` receives data.
- Port `ZquicH3InteropTest` to link/stream APIs and optional link `streamFrame(...)` hooks.

### Phase 7: Stream Tx Scheduling And Send Wrappers

Area of focus: make application stream Tx use real stream APIs and delete old one-shot send helpers.

Add or modify:

- Add `Link::streamWritable_(uint64_t id)`.
- Add `StreamScheduler` / `TxScheduler` to link runtime state.
- `Stream::sent_()` calls `link()->streamWritable_(id())`.
- Zquic Tx thread flush:
  - picks next stream ID.
  - finds stream.
  - uses `StreamPacketizer::writeNext(...)`.
  - builds/protects a short packet.
  - requeues stream if data remains.
- Implement link stream Tx APIs over `stream->txStream()` and `fin()`.
- Delete `Client::sendBidi`, `sendUni`, `sendStream` and server equivalents.

How it connects:

- Completes application-level stream Tx end-to-end.
- Removes the old send helper surface once link-owned stream Tx is available.

API scrutiny:

- `StreamScheduler::next()` returns `uint64_t(-1)` when empty; guard before lookup.
- `StreamPacketizer::writeNext()` returns positive bytes written, zero for no stream data, and negative for invalid/error.
- `PacketAssembly` allows at most one stream frame per packet; keep that limitation documented for this alignment pass.

Tests:

- Runtime send checks use link-owned streams directly.
- Add two-stream scheduling test: both streams eventually emit over separate packets.
- Add FIN-only packetization test through runtime link.

### Phase 8: Public CRTP API Cutover

Area of focus: make `CliLink`, `SrvLink`, `CliStream`, and `SrvStream` the documented public path.

Add or modify:

- Finalize template signatures with short local names and avoid alias shadowing.
- Update `#if 0` docs to match exact compilable signatures.
- Add public compile/runtime tests that copy the documented CRTP shape.
- Add examples:
  - `zquic/example/ZquicClient.cc`
  - `zquic/example/ZquicServer.cc`
- Update `zquic/README.md` or create it if absent.
- Remove old wrapper APIs from documentation and tests.

How it connects:

- This phase should be mostly surface/API work because runtime behavior already exists.

API scrutiny:

- Do not require applications to allocate physical `Cxn`s manually.
- Client link owns/open its UDP connection, mirroring Ztls client links.
- Server creates logical links through `accepted(const InitialInfo &)`.

Tests:

- Add `ZquicCRTPDocTest` or extend `ZquicAPITest` with exact `#if 0` examples.
- Delete old wrapper tests as replacement `CliLink` / `SrvLink` coverage lands.

### Phase 9: Server Multi-Connection Routing

Area of focus: route multiple logical server links over one UDP listener.

Add or modify:

- Add server-owned active link store.
- Add server routing table:
  - DCID -> route token -> `SrvLink`.
  - optional path tuple for Initial/candidate routing.
- On first valid Initial:
  - parse long header without full decrypt.
  - create `SrvLink`.
  - call `ServerBootstrap::acceptInitial(...)`.
  - add local and initial DCID routes.
  - deliver datagram to the new link.
- On subsequent datagrams:
  - parse DCID.
  - find route.
  - schedule delivery to target `SrvLink`.
  - unknown/stale DCID increments diagnostics or later stateless reset.
- Retire/tombstone route entries before link destruction.
- Closing one `SrvLink` must not close the listener or other links.

How it connects:

- Builds directly on the single-link server model and common packet receive path.
- Converts `Server` from old single-logical-link ownership into an actual multi-link manager.

API scrutiny:

- `CIDRouter::find()` returns `uintptr_t`, not a ref.
- Route tokens must never outlive the server-owned `SrvLink`.
- `Endpoint`'s old single `m_cxn` model is not enough for final routing and must be private/removed.

Tests:

- Add `ZquicMultiServerTest`: one server, two clients, independent links/streams/diagnostics.
- Verify closing one client leaves the listener and other client alive.
- Verify unknown/stale DCID datagrams do not crash and do not call `ErrorFn`.

Current status:

- Done: `Server` keeps an active `LinkBase` ref table, routes by `CIDRouter` route tokens, calls `accepted(const InitialInfo &)` for first Initial, registers each accepted link's original and local Initial CIDs, releases route entries on `SrvLink::close()`, drops unknown/stale DCIDs as endpoint diagnostics without calling `ErrorFn`, and has runtime coverage for two clients sharing one UDP listener plus closing one server link while the other remains live.
- Remaining: stateless reset emission can be added later if diagnostics-only stale DCID handling is not sufficient.

### Phase 10: Cleanup, Compatibility Decision, And Documentation

Area of focus: remove duplication, delete old API compatibility, and document the aligned surface.

Add or modify:

- Remove old duplicated client/server runtime fields.
- Keep `Endpoint` private to `Zquic` tests only.
- Remove `sendBidi` / `sendUni` / `sendStream` and port tests/examples to pure link/stream APIs.
- Update module docs with:
  - thread context table.
  - CRTP expectation table.
  - error handling policy.
  - peer-failure diagnostics policy.

Tests:

- Build all `zquic/test` binaries.
- Run all local non-external tests.
- Run H3 interop tests where dependencies are installed.

## Code References to Impacted Code

- `zquic/src/Zquic.hh:50` - `ErrorFn` and `defaultErrorFn`; keep this public shape.
- `zquic/src/Zquic.hh:422` - `Engine<App>`; set error callback before validation, add protected `error_()`, preserve runtime thread helpers.
- `zquic/src/Zquic.hh:512` - `Engine::validate_()`; replace direct `ZiLOG(Error, ...)` with `error_(ZeEXCEPT(...))`.
- `zquic/src/Zquic.hh` - aligned `Client<App>` manager; logical client state is owned by `CliLink`.
- `zquic/src/Zquic.hh` - aligned `Server<App>` listener/router; logical server state is owned by routed `SrvLink`s.
- `zquic/src/Zquic.hh` - aligned `Link` owns streams, common runtime state, packet helpers, ACK helpers, TLS advancement, and runtime diagnostics.
- `zquic/src/Zquic.hh` - aligned `Stream<Link, Impl, TxBufAlloc>` exposes `link()` and notifies its owning link when Tx becomes writable.
- `zquic/src/Zquic.hh` - aligned `Cxn<Owner, OwnerRef>` derives from `ZiConnection` and hops owner callbacks to QUIC runtime threads.
- `zquic/src/ZquicDatagram.hh:22` - public `Datagram` and `EndpointDiag` types shared by `Cxn`, `Client`, `Server`, and tests.
- `zquic/src/ZquicEndpoint.hh:23` - private/test `Endpoint` helper; no longer included by `Zquic.hh` or installed as public aligned API.
- `zquic/src/ZquicEndpoint.cc:17` - `Endpoint::Cxn_`; implementation source for new `Cxn`, but add runtime thread hops.
- `zquic/src/ZquicCrypto.hh:213` - `CryptoConfig`; keep as per-link TLS config input.
- `zquic/src/ZquicCrypto.cc:737` - `Crypto::initTLS`; confirms per-link TLS ownership.
- `zquic/src/ZquicCrypto.cc:980` - `Crypto::handleTLSMessage`; caller must serialize per link and handle async result deliberately.
- `zquic/src/ZquicConn.hh:100` - `CIDRouter`; route-token table, not ownership.
- `zquic/src/ZquicConn.hh:194` - `ClientBootstrap`; use for random client CIDs and server transport-param validation.
- `zquic/src/ZquicConn.hh:262` - `ServerBootstrap`; use for first Initial acceptance and server transport params.
- `zquic/src/ZquicConn.hh:288` - `ConnState`; use in link runtime for connection lifecycle.
- `zquic/src/ZquicSched.hh:125` - `StreamPacketizer`; reuse for stream Tx, one stream frame per packet initially.
- `zquic/src/ZquicSched.hh:228` - `StreamScheduler`; reuse for link stream scheduling.
- `zquic/src/ZquicRecovery.hh:22` - `AckTracker`; keep for ACK generation.
- `zquic/src/ZquicRecovery.hh:430` - `PacketTxSpace`; keep for sent packet tracking.
- `zquic/src/ZquicStream.hh:21` - `StreamID`; keep for role/parity logic.
- `zquic/src/ZquicStream.hh:88` - `ReceiveFlow`; integrate into link/stream receive policy as needed.
- `zquic/src/ZquicStream.hh:154` - `StreamLimit`; keep in `Link`.
- `zi/src/ZiIOContext.hh:24` - `ZiIOContext`; `addr` carries UDP peer address for recv/send.
- `zi/src/ZiMultiplex.hh:448` - `ZiConnection`; `Cxn` should derive from this.
- `zi/src/ZiMultiplex.hh:859` - `ZiMultiplex::udp`; use this UDP open path.
- `zi/src/ZiMultiplex.cc:256` - `udp()` validation and `rxInvoke` behavior.
- `zi/src/ZiMultiplex.cc:731` - `executedConnect()` refs and connects returned `ZiConnection *`.
- `zi/src/ZiMultiplex.cc:1281` - UDP `recvfrom` fills `ZiIOContext::addr`.
- `zi/src/ZiMultiplex.cc:1481` - UDP `sendto` uses `ZiIOContext::addr`.
- `zi/src/ZiMultiplex.cc:1782` - UDP disconnect closes directly without TCP shutdown.
- `ztls/src/Ztls.hh:160` - reference `Cxn<Link, LinkRef>` pattern.
- `ztls/src/Ztls.hh:258` - reference I/O callback to runtime-thread hop in `connected_0`.
- `ztls/src/Ztls.hh:298` - reference consolidated `handshake_()` error handling style.
- `ztls/src/Ztls.hh:516` - reference disconnect hop and stale connection handling.
- `ztls/src/Ztls.hh:873` - reference `reset_tls_()` state reset pattern.
- `ztls/src/Ztls.hh:970` - reference `CliLink` persistent ownership and connect flow.
- `ztls/src/Ztls.hh:1096` - reference `SrvLink` class shape, with documented QUIC ownership deviation.
- `ztls/src/Ztls.hh:1146` - reference `Engine` error handling and init sequencing.
- `ztls/src/Ztls.hh:1562` - reference client CRTP documentation style.
- `ztls/src/Ztls.hh:1661` - reference server CRTP documentation style.
- `zquic/test/ZquicAPITest.cc:16` - current compile-level stream/link API test; port to new CRTP shape.
- `zquic/test/ZquicRuntimeTest.cc:24` - current endpoint-centered runtime harness; port incrementally through wrappers.
- `zquic/test/ZquicEndpointTest.cc:18` - current endpoint ownership/diag test; keep until `Endpoint` becomes private or remove/replace.
- `zquic/test/ZquicStreamTest.cc:16` - current stream unit harness; update for link-owned stream constructor.
- `zquic/test/ZquicH3InteropTest.cc` - H3 interop coverage on the final link/stream API.

Files likely to be created:

- `zquic/test/ZquicErrorTest.cc`
- `zquic/test/ZquicCliLinkTest.cc`
- `zquic/test/ZquicSrvLinkTest.cc`
- `zquic/test/ZquicRuntimeStreamTest.cc`
- `zquic/test/ZquicMultiServerTest.cc`
- `zquic/example/ZquicClient.cc`
- `zquic/example/ZquicServer.cc`
- `zquic/README.md` if no module README exists.

## Detailed Test Plan

Baseline tests after Phase 1:

- Build `zquic/test` targets.
- Run `./zquic/test/ZquicAPITest`.
- Run `./zquic/test/ZquicEndpointTest`.
- Run `./zquic/test/ZquicStreamTest`.
- Run `./zquic/test/ZquicRuntimeTest`.

Error handling tests:

- `ZquicErrorTest`
- Custom `ErrorFn` receives null multiplexer, invalid Rx thread, invalid Tx thread, same Rx/Tx thread, invalid async thread, and missing server cert/key failures.
- Packet parse/protection failures increment diagnostics without invoking `ErrorFn`.

Client link tests:

- `ZquicCliLinkTest`
- App defines `Client`, `CliLink`, and `CliStream` per docs.
- Link receives connect failure for UDP open failure.
- Link ignores stale physical connection callbacks.
- Two client links under one app have independent state.

Server link tests:

- `ZquicSrvLinkTest`
- App defines `Server`, `SrvLink`, and `SrvStream` per docs.
- Server listener opens one UDP socket.
- First Initial creates one `SrvLink`.
- Link receives connected/disconnected callbacks.
- Invalid Initial is diagnostics-only.

Runtime regression tests:

- `ZquicRuntimeTest`
- Existing handshake, ALPN, packet protection counters, ACK counters, HANDSHAKE_DONE, and link-owned stream send behavior continue to pass after each phase.

Stream tests:

- `ZquicStreamTest`
- `ZquicFlowTest`
- `ZquicLoopTest`
- `ZquicRuntimeStreamTest`
- Verify link-owned stream constructor, stream receive, Tx retention, packetization, FIN, RESET, STOP, flow/stream limits, scheduler behavior, and runtime delivery to `App::Stream::process`.

Multi-connection server tests:

- `ZquicMultiServerTest`
- One server accepts two simultaneous clients.
- Each client gets an independent `SrvLink`.
- Closing one link does not close the listener or the other link.
- Unknown/stale DCID datagrams increment diagnostics only.

Interop tests:

- Keep `ZquicH3InteropTest` on the final link/stream API while preserving external curl/Caddy coverage where available.
- Run H3 tests only when external dependencies are available.

Broader regression after packet/runtime movement:

- `./zquic/test/ZquicHandshakeTest`
- `./zquic/test/ZquicPacketProtectionTest`
- `./zquic/test/ZquicCloseTest`
- `./zquic/test/ZquicFlowTest`
- `./zquic/test/ZquicRecoveryTest`
- `./zquic/test/ZquicPMTUDTest`
- `./zquic/test/ZquicCIDTest`
- `./zquic/test/ZquicVersionTest`
- `./zquic/test/ZquicRuntimeTest`
- `./zquic/test/ZquicStreamTest`

## Acceptance Criteria

- `Zquic` exposes documented CRTP classes:
  - `Client`
  - `Server`
  - `Link`
  - `CliLink`
  - `SrvLink`
  - `Cxn`
  - `CliCxn`
  - `SrvCxn`
  - `Stream`
  - `CliStream`
  - `SrvStream`
- `#if 0` CRTP docs compile when copied into tests.
- `Link` owns zero or more `Stream`s.
- Client runtime state is owned by `CliLink`, not `Client`.
- Server logical connection runtime state is owned by `SrvLink`, not `Server`.
- Server physical UDP listener is server-owned and can route multiple `SrvLink`s.
- `Client<App>` can initiate multiple `CliLink`s sharing one app/client context.
- Error reporting follows `Ztls` style:
  - configured `ErrorFn`
  - `error_(ZeException)`
  - `ZeEXCEPT(Error, "Zquic", ...)`
  - no direct `ZiLOG(Error, ...)` in runtime/validation error paths.
- Peer packet/protocol failures are diagnostics-only unless they indicate a local/internal failure.
- Cxn callbacks from Zi I/O threads hop to Zquic Rx/Tx runtime threads before touching link runtime state.
- Existing runtime tests are ported to link/stream APIs as each side is aligned.
- New link/stream tests prove the aligned API works without compatibility wrappers.
- Multi-server routing supports at least two simultaneous clients over one UDP listener.

## Non-goals

- Full QUIC recovery, PTO, loss retransmission, and pacing beyond preserving existing helpers.
- Full PMTUD integration beyond preserving existing `Path` / `Pacer` helpers.
- Async certificate signing support in `Zquic`.
- Stateless reset implementation unless already covered by existing tested helpers.
- Preserving historical wrapper APIs.
- Rewriting low-level packet/frame/crypto helpers that already have tests.
- Forcing Ztls TCP ownership semantics onto QUIC server UDP sockets.
- Supporting stream migration between links.

## Options and Open Questions

No blocking open questions remain from `quic_align.md`. The resolved decisions are:

- Thread model: preserve two QUIC runtime threads, `rxThread` and `txThread`, and explicitly hop from Zi I/O callbacks to those threads.
- Endpoint: keep it private to tests; public datagram types live in `ZquicDatagram.hh`, and public UDP ownership is through `Cxn` / `CliCxn` / `SrvCxn`.
- Packet/protocol failures: diagnostics-only unless local/internal.
- Stream parameterization: parameterize by `Link`.
- Client ownership: align with `Ztls`; multiple client links may share one `Client<App>`.
- Server ownership: align names and CRTP style with `Ztls`, but keep one server UDP listener routing many logical `SrvLink`s.

Implementation options retained for future feature work:

- Stateless reset emission can be added later on top of the current diagnostics-only unknown/stale DCID path.
- Recovery, PTO, pacing, PMTUD integration, and async signing remain separate feature work after alignment.
