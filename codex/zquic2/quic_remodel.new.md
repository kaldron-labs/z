## Summary

The remodel should align the Zquic object model with QUIC terminology without changing the transport behavior more than necessary:

- `Endpoint` owns UDP socket I/O, packet buffer allocation for UDP datagrams, UDP send queuing, and endpoint diagnostics.
- `Link` / `CliLink` / `SrvLink` own QUIC session state through the existing CRTP shape: crypto, transport parameters, streams, recovery, packet number spaces, runtime state, and close state.
- `Cxn<Link_>` becomes one routable QUIC connection identity, not a UDP socket adapter. A `Cxn<Link_>` stores the raw connection ID bytes, sequence number, optional stateless reset token, route state, and the direct `Link_ *` it routes to while active.
- The endpoint/server route table becomes a locked `ZmHash` keyed by `CxnID`, with an explicit Zquic hash/heap ID. This removes the current fixed `CxnIDRouter::Max = 16` capacity limit and matches the rest of the codebase's hash-table patterns.
- The raw QUIC connection ID bytes remain `CxnID = ZuBArray<20>` with `CxnIDMax = 20`. Keep `CxnIDGen` named around IDs because it only generates byte values, not full route entries.

The current tree is already partway through the original Phase 1: `ZquicEndpoint.hh` and `ZquicEndpoint.cc` define `Endpoint::Cxn_` as a private UDP `ZiConnection`, but `Server`, `CliLink`, `SrvLink`, tests, and the route table still use the old templated UDP `Cxn<Owner, OwnerRef>`, `LinkBase` virtual route hooks, and `uintptr_t` route tokens. The revised plan therefore starts by finishing the endpoint migration end-to-end, then replaces token routing with CRTP-typed link pointers in a locked `ZmHash`, then aligns CID ownership with zngtcp2: the link owns connection-local CID state, while the endpoint/server owns the global CID-to-link route index.

Protocol behavior checked against RFC 9000:

- More than one connection ID can be associated with one QUIC connection, so one `Link` must be able to expose multiple active `Cxn` entries.
- Short headers carry only the Destination Connection ID and no explicit length; the receiver or load balancer must know the length or use an encoding scheme. This remodel should align with zngtcp2 by matching server pre-routing against stored local CID associations of known lengths, not by guessing an arbitrary CID length from the packet.
- Initial connection IDs are authenticated through transport parameters: client original DCID, client initial SCID, retry SCID if used, and server initial SCID must remain tracked by bootstrap code.
- NEW_CONNECTION_ID carries sequence, ID bytes, and a 16-byte stateless reset token; RETIRE_CONNECTION_ID invalidates the token. The remodel preserves per-CID token storage and retirement semantics, wires zngtcp2-equivalent stateless reset detection/draining for existing links plus reset emission for unknown server CIDs, and adds a bounded NEW_CONNECTION_ID / RETIRE_CONNECTION_ID slice: frame codec support, link-owned peer/local CID records, route association for locally issued CIDs, and local route dissociation when a peer retires one of our CIDs.

Official protocol reference reviewed: RFC 9000, "QUIC: A UDP-Based Multiplexed and Secure Transport", https://datatracker.ietf.org/doc/rfc9000/.

## Architecture Documentation

### Current Implementation Findings

`Endpoint` already has the right private UDP ownership shape:

```c++
class Endpoint : public ZmPolymorph {
  class Cxn_;
  ...
  bool openUDP(...);
  void closeUDP();
  ZmRef<ZiIOBuf> allocTxPacket();
  bool send(ZmRef<ZiIOBuf>, ZiSockAddr);
};
```

The private `Endpoint::Cxn_` subclasses `ZiConnection`, receives datagrams into `PacketRxBufAlloc<>`, sends queued datagrams with `sendto` through `ZiConnection::send`, updates endpoint diagnostics, and calls the endpoint's datagram callback. This should be reused, not duplicated.

`Endpoint` is currently incomplete for replacing `CliLink` because it has `ReadyFn` and `FailFn`, but no disconnect/down callback. The old `CliLink` observes `Cxn::disconnected()` and calls `impl()->disconnected()` after a requested close. Add an endpoint-level down callback before migrating the client path.

`ZquicEndpoint.hh` is currently a `noinst_HEADERS` entry. If `CliLink` or `Server` store an `Endpoint` member in the public `Zquic.hh` template definitions, `ZquicEndpoint.hh` must be included from `Zquic.hh` and installed as a public header, or `Endpoint` must be hidden behind a complete private wrapper. The pragmatic choice is to promote `ZquicEndpoint.hh` to `pkginclude_HEADERS` because tests already include it directly and the type is part of the target model.

`ZiMultiplex::udp()` behavior was checked locally:

- It fails immediately if the multiplexer is not running or `ZiCxnOptions::udp()` is not set.
- It schedules socket setup on the Rx thread through `rxInvoke`.
- It creates/binds/connects a UDP socket, calls the supplied `ZiConnectFn` to obtain a `ZiConnection`, adds it to the multiplexer connection table, and then calls `ZiConnection::connected()`.
- `ZiConnection::connected()` invokes the derived `connected(ZiIOContext &)`. The derived type must initialize the receive context; `Endpoint::Cxn_::connected()` delegates to `Endpoint::connected_()`, which calls `armRecv_()`.
- `ZiConnection::close()` is asynchronous across Tx/Rx scheduling and eventually calls the derived `disconnected()`. Route cleanup must not wait for that callback if a link pointer is about to become invalid.

`Packet::parseShort()` requires the caller to supply a CID length. This is not a Zquic-only limitation: zngtcp2's short-header decoder also takes an explicit `dcidlen`, and connection-local receive decodes short headers with the active connection's known CID length. zngtcp2 avoids guessing packet-local CID length because short headers do not carry it. Its server/application routing works by associating every locally issued SCID returned by `ngtcp2_conn_get_scid2()` in an external Destination-CID lookup table, then dissociating those CIDs through the `remove_connection_id` callback and connection teardown. `Server::routeShort_()` should therefore use the endpoint route table to match stored known CID bytes/lengths before handing the datagram to the link, where normal short-header decoding can use the link's active local CID length. If stored CIDs share prefixes, the router should choose the longest matching stored CID so a shorter route does not shadow a longer issued CID.

Current Zquic has only partial stateless reset support compared with zngtcp2:

- `StatelessResetToken` storage/generation exists in `ZquicConn.hh` / `ZquicConn.cc`.
- `StatelessReset::writeForUnknownCID()` can build a reset packet.
- Server route entries can store reset tokens for local CIDs.
- There is no incoming stateless-reset decode/verify path analogous to zngtcp2's `conn_on_stateless_reset()`.
- A validated reset token does not transition a link to `Draining`.
- `TransportParams` does not carry the server `stateless_reset_token` transport parameter.
- Failed decrypt and failed packet association paths do not compare the trailing 16 bytes against remembered peer reset tokens.
- Server routing does not use `StatelessReset::writeForUnknownCID()` when a packet routes to no live link but matches a known retained local CID token.

The zngtcp2 reference split is important for route storage:

- `ngtcp2_conn` owns connection-local CID state: source CIDs issued to the peer, destination CIDs received from the peer, sequence numbers, retired/bound state, and reset tokens.
- The example server/application owns the global lookup table from packet Destination CID to handler/link; it associates CIDs returned by `ngtcp2_conn_get_scid2()` and dissociates them on `remove_connection_id` and connection removal.
- Zquic should mirror that split: `Link` owns the CID records needed by the QUIC session, but `Server` / `Endpoint` owns the `ZmHash` route index that maps local CID bytes to the concrete CRTP server link type.
- zngtcp2's full `NEW_CONNECTION_ID` / `RETIRE_CONNECTION_ID` support is broader than this remodel because it also covers path binding, migration policy, active CID limit enforcement across unused/bound sets, and proactive issuance decisions. Zquic should implement the route-critical subset now: received `NEW_CONNECTION_ID` updates link-owned peer destination-CID state and reset tokens; received `RETIRE_CONNECTION_ID` marks link-owned local source CIDs retired and triggers endpoint route dissociation; endpoint/server routing remains only a global Destination-CID-to-link association table.

### New or Changed Components

`Endpoint`

- Continue to be the only owner of UDP `ZiConnection` I/O.
- Add a disconnect/down callback:

```c++
using DownFn = ZmFn<void(Endpoint *)>;

bool openUDP(
  ZiMultiplex *mx,
  PathMode::T mode,
  ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  DatagramFn datagramFn = {},
  ReadyFn readyFn = {},
  FailFn failFn = {},
  DownFn downFn = {});
```

- Call `DownFn` from `Endpoint::disconnected_()` after clearing `m_cxn` and `m_listening`.
- Keep `inject()` for endpoint tests.

`StatelessResetToken` / `StatelessReset`

- Move `StatelessResetToken` out of `ZquicConn.hh` into a lower-level header that `ZquicTransportParams.hh`, `ZquicConn.hh`, and packet/link code can include without cycles. `ZquicTypes.hh` is acceptable if keeping the type small and dependency-free; a dedicated `ZquicReset.hh` is also acceptable if the module wants separation.
- Keep `StatelessReset::writeForUnknownCID()` as the packet construction helper for endpoint/router-owned reset emission.
- Add decode/verify helpers mirroring the useful zngtcp2 split:

```c++
struct StatelessReset {
  static constexpr unsigned TokenLength = StatelessResetToken::Length;
  static constexpr unsigned MinLength = 21;

  static int decode(StatelessResetToken &, ZuCSpan datagram);
  static bool verify(ZuCSpan datagram, const StatelessResetToken &);
  static int writeForUnknownCID(
    uint8_t *, unsigned, ZuCSpan receivedPacket,
    const StatelessResetToken &);
};
```

`TransportParams`

- Add server stateless reset token support:

```c++
struct TransportParams {
  ...
  StatelessResetToken	statelessResetToken;
  bool			statelessResetTokenPresent = false;
};
```

- Encode `stateless_reset_token` only when present.
- Decode it only when the value length is exactly 16 bytes.
- Validate role-specific use in the caller: clients consume the server token; servers do not accept a client-sent stateless reset token.

`Cxn<Link_>`

- New route-entry type replacing `CxnIDSlot` after the old UDP `Cxn` template is removed:

```c++
template <typename Link_>
struct Cxn {
  CxnID			id;
  uint64_t		sequence = 0;
  Link_			*link = nullptr;
  StatelessResetToken	resetToken;
  CxnState::T		state = CxnState::Active;
};
```

- `Cxn<Link_>` is the hash node stored in the endpoint/server route index. It is not the owner of the QUIC session and should not become another connection state object.
- `Link_` is the concrete CRTP server link type for `Server<App_, Link_>`, not a virtual base. This keeps route dispatch static: `Server` routes to `Link_ *` and invokes the link's non-virtual CRTP methods directly.

`CxnState`

- Rename `CxnIDState` to `CxnState`.
- Keep values `Active`, `Retired`, and `Tombstone`.

`CxnRouter`

- Rename `CxnIDRouter` to `CxnRouter<Link_>`.
- Replace the fixed array with a `ZmHash` keyed by `Cxn::id`.
- Use a named hash/heap ID so the table appears under Zquic-specific hash telemetry and can be configured through the existing `ZmHashMgr` path. `ZmHashHeapID` sets both the heap ID and hash ID when the hash ID is otherwise default.
- Use striped `ZmPLock` locking through `ZmHashLock<ZmPLock, ...>`. This matches existing locked hash usage in the tree and is sufficient for route lookup, install, retire, and tombstone operations without a separate coarse route-table lock.
- Start with conservative route hash parameters, for example `ZmHashParams().bits(5).loadFactor(1).cBits(3)`, then allow the ID-backed `ZmHashMgr` configuration to tune it. This replaces `Max = 16`; there should be no route-capacity constant in the final router.
- Return `Link_ *` directly for active entries:

```c++
template <typename Link_>
inline const CxnID &Cxn_IDAxor(const Cxn<Link_> &cxn) { return cxn.id; }

template <typename Link_>
ZuDerive(CxnRoutes_,
  (ZmHash<Cxn<Link_>,
    ZmHashNode<Cxn<Link_>,
      ZmHashKey<Cxn_IDAxor<Link_>,
	ZmHashLock<ZmPLock,
	  ZmHashHeapID<"Zquic.Endpoint.CxnRouter">>>>>));

template <typename Link_>
class CxnRouter {
public:
  bool add(const CxnID &, uint64_t sequence, Link_ *);
  bool add(
    const CxnID &, uint64_t sequence, Link_ *,
    const StatelessResetToken &);
  Link_ *find(const CxnID &) const;
  bool resetToken(const CxnID &, StatelessResetToken &) const;
  bool retire(const CxnID &);
  bool tombstone(const CxnID &);
  CxnState::T state(const CxnID &) const;

  template <typename Fn> void all(Fn) const;

private:
  ZmRef<CxnRoutes_<Link_>>	m_routes;
};
```

- If `ZuHash<CxnID>` is not already valid for `ZuBArray<20>`, add a small explicit hash/comparator NTP for `CxnID` instead of falling back to string conversion or byte-by-byte ad hoc code at lookup sites.
- Do not call into `Link_` while holding any `ZmHash` bucket/stripe lock. Route lookup should copy out the `Link_ *` under the hash operation, release the hash lock, then call `receivedRouted_()`.
- `Server<App_, Link_>` must continue to own or otherwise retain the target `Link_` while routes point to it. The route hash is the CID-to-link index, not the link lifetime owner.

`ServerBootstrap`

- Keep responsibility for QUIC Initial validation and transport parameter values.
- Remove responsibility for owning route tables in the final shape.
- During the transition, it can still expose route entries, but the end state should be that `SrvLink` / `Link` stores only connection-local CID state and `ServerBootstrap` stores only:
  - original DCID from the client's first Initial
  - client initial SCID
  - local initial SCID
  - local stateless reset token
  - accepted flag

`Server<App_, Link_>` / `Link`

- Remove `LinkBase` from the target model. `Link` already uses CRTP through its `Impl` type, so route dispatch should be static and typed.
- Change `Server` to know its concrete server link type:

```c++
template <typename App_, typename Link_>
class Server : public Engine<App_> {
public:
  using App = App_;
  using Link = Link_;
  using LinkRef = ZmRef<Link>;

private:
  CxnRouter<Link>	m_routes;
  // active link lifetime registry, not the route index
};
```

- Because `Server<App>` is instantiated before a nested `App::Link` declaration is visible, do not depend on `typename App::Link` inside `Server<App>`. Application examples should forward-declare the concrete link externally and pass it as the second CRTP parameter, while optionally aliasing it back to `Link` inside the app:

```c++
struct App;
struct AppStream;
struct AppLink;

struct App : public Zquic::Server<App, AppLink> {
  using Link = AppLink;
  ZmRef<Link> accepted(const Zquic::InitialInfo &);
};
```

- `Link` itself should derive from `ZmPolymorph`, not from `LinkBase`, so existing `ZmRef<Link>` ownership still works without virtual route hooks.
- Expose non-virtual route lifecycle operations on the concrete `SrvLink` / CRTP `Link` path:

```c++
void receivedRouted_(Datagram);
void installRoutes_(CxnRouter<Impl> &) const;
void retireRoutes_(CxnRouter<Impl> &);
bool checkStatelessReset_(ZuCSpan datagram);
```

- The concrete link should own the CID records it learns or issues before exposing route associations to the endpoint route table.
- Do not put the endpoint/server `ZmHash` inside `Link`. Align with zngtcp2: the connection object owns CID state, while the server/application owns the global Destination-CID lookup table.
- Closing a link must retire/tombstone endpoint routes before the final `ZmRef<Link>` is released.
- `checkStatelessReset_()` should decode the trailing token, compare it against peer-issued reset tokens remembered by the link, transition runtime close state to draining on success, and optionally notify the application.

### New or Changed Processes or Threads

- UDP socket setup remains on the `ZiMultiplex` Rx thread because `ZiMultiplex::udp()` schedules `udp_()` with `rxInvoke`.
- Endpoint ready/down/datagram callbacks may originate on the multiplexer Rx path. `CliLink` and `Server` should continue to bounce work onto their application Rx thread with `app()->rxRun(...)` or `this->rxRun(...)`, matching current `connected_0()` and `received_0()` patterns.
- `CxnRouter<Link>` owns its own striped `ZmHash` locks. Server route lookup, route install, and route retirement should still be serialized through the app Rx thread where practical, and no hash lock should be held while invoking a concrete link callback.
- Datagram transmission remains through `ZiConnection::send()`, which schedules Tx work. `Endpoint::send()` should remain a thin wrapper over the private UDP connection.

### New or Changed Interfaces

- `CliLink` should no longer be parameterized by UDP `Cxn_` / `CxnRef_`; it should use `Endpoint` internally.
- `SrvLink` should no longer default to `SrvCxn<App>` because server links do not own UDP sockets.
- `Server` should expose endpoint state through existing methods where possible:
  - `listening()` delegates to `Endpoint::listening()`.
  - `connected()` delegates to `Endpoint::connected()`.
  - `local()` delegates to `Endpoint::local()`.
  - `endpointDiag()` delegates to `Endpoint::diag()`.
- `CliLink::cxn()` should be removed or replaced by `endpoint()` only if a public endpoint accessor is desired. Source compatibility with the old UDP connection adapter is a non-goal.
- `CliLink::cxnDiag()` can remain as a compatibility-shaped method returning `m_endpoint.diag()`, but the underlying member should be named around endpoint diagnostics.
- `CxnID` remains the public raw byte type; `CxnIDGen` remains the generator for raw ID bytes.

### New or Changed Data Flows

Client UDP flow:

```text
CliLink::connect_()
  -> m_endpoint.openUDP(ClientConnected, remote)
  -> Endpoint::ReadyFn schedules CliLink::endpointReady_()
  -> startHandshake_()
  -> sendProtected*Packet_ allocates through Endpoint and sends through Endpoint

Endpoint::DatagramFn
  -> app()->rxRun(CliLink::received_)
  -> Link receive/decrypt/frame processing

Endpoint::DownFn
  -> app()->rxRun(CliLink::endpointDown_)
  -> m_udpReady = 0, optional Impl::disconnected()
```

Server UDP flow:

```text
Server::listen()
  -> m_endpoint.openUDP(ServerUnconnected, local)
  -> Endpoint::ReadyFn calls App::listening() if present

Endpoint::DatagramFn
  -> Server::received_()
  -> routeLong_() / routeShort_()
  -> CxnRouter<Link>::find(dcid) looks up the locked ZmHash and returns Link *
  -> Link::receivedRouted_()
  -> link installs any newly activated routes
```

Server accept flow:

```text
Server::routeLong_(Initial)
  -> App::accepted(InitialInfo) returns ZmRef<Link>
  -> Server stores ZmRef<Link> in the active link registry
  -> SrvLink::receivedRouted_()
  -> SrvLink::initRuntimeCrypto_()
  -> ServerBootstrap validates Initial
  -> SrvLink records initial/local CID state
  -> Server installs those CID route associations in endpoint CxnRouter
```

Stateless reset receive flow for an existing link:

```text
Endpoint::DatagramFn
  -> Server::route_() or CliLink::received_()
  -> Link packet decrypt/association fails for the first packet in the datagram
  -> Link::checkStatelessReset_(full UDP datagram bytes)
  -> compare trailing 16 bytes with peer reset tokens remembered by the link
  -> on match, move LinkState/CloseState to Draining and suppress further sends
  -> optional application callback/log event
```

Stateless reset emission flow for an unknown server CID:

```text
Server::routeShort_() or Server::routeLong_()
  -> no active Link * found
  -> CxnRouter finds an explicitly retained local route entry with a valid reset token
  -> Server builds response with StatelessReset::writeForUnknownCID()
  -> Endpoint sends the reset to the datagram source address
```

Do not emit a stateless reset for packets that are too small, for packets that look like long-header packets before a token is available to the peer, or for IDs whose tokens were retired/cleared.

### New or Changed Event-Driven or Timer Processing

- No new timers are required for the remodel itself.
- Close processing must maintain the current event-driven model:
  - Application request calls `CliLink::disconnect()` or `SrvLink::close()`.
  - Link runtime close state is updated on the app Rx thread.
  - Endpoint UDP close is asynchronous.
  - Route entries are retired/tombstoned synchronously from the link close path before link references can be released.

### New or Changed Network Programming

- Reuse `ZiMultiplex::udp()` and `Endpoint::Cxn_`.
- Keep `ZiCxnOptions options; options.udp(true);` inside `Endpoint::openUDP()`.
- Do not add another direct `mx->udp()` caller in `Server` or `CliLink`.
- Continue to use connected UDP sockets for clients (`PathMode::ClientConnected`) and unconnected UDP sockets for servers (`PathMode::ServerUnconnected`).
- Preserve current version negotiation behavior: unknown long-header versions cause a Version Negotiation response, with DCID/SCID swapped through `VersionNegotiation::write()`.
- Stateless Reset emission remains endpoint/router-owned because it is needed when no live `Link` can process the packet. Existing-link stateless reset detection remains link-owned because it depends on peer-issued tokens and close/draining state.

### New or Changed Data Stores

- `Server<App_, Link_>` keeps the endpoint/server route index as `CxnRouter<Link> m_routes`; internally this is a locked `ZmHash` of `Cxn<Link>` nodes keyed by `CxnID`.
- `Server<App_, Link_>` keeps active link lifetime separately from the route index. During the transition it can keep an `m_links` registry, but the final registry must not be sized from `CxnRouter::Max`; either use an existing dynamic container/hash keyed by `Link *` or make route cleanup fully serialized with the owning `ZmRef<Link>` lifetime.
- `SrvLink` / `Link` owns connection-local CID state: local SCIDs issued to the peer, peer DCIDs received from the peer, sequence numbers, reset tokens, retired/bound state, and bootstrap CIDs.
- `SrvLink` / `Link` exposes those CID records as associations to install or remove in `Server::m_routes`; it does not own the endpoint/server `ZmHash`.
- Route entries are heap-managed hash nodes owned by `ZmHash`, not fixed-size arrays. Remove `CxnRouter::Max`.

## Detailed Design and Implementation Plan

### Phase 1: Promote and Complete `Endpoint`, Then Migrate Client UDP

Area of focus: one complete external workflow, client UDP connect/reconnect/disconnect, should stop using the old UDP `Cxn` template.

Add or modify:

- Promote `ZquicEndpoint.hh` from `noinst_HEADERS` to `pkginclude_HEADERS` in `zquic/src/Makefile.am`.
- Include `ZquicEndpoint.hh` from `Zquic.hh` so public template definitions can store and call `Endpoint`.
- Add `Endpoint::DownFn` and store `m_downFn`.
- Extend `Endpoint::openUDP()` to accept the down callback.
- Call `m_downFn(this)` from `Endpoint::disconnected_()`.
- Change `CliLink` to own `Endpoint m_endpoint`.
- Remove `CliLink` template parameters `Cxn_` and `CxnRef_`, plus the `m_cxn`, `m_closingCxn`, and `m_disconnectRef` members.
- Replace `CliLink::connect_()` direct `app()->mx()->udp(...)` call with `m_endpoint.openUDP(...)`.
- Replace `connected_(ZmRef<Cxn>)` with `endpointReady_(Endpoint *)`.
- Replace `disconnected_(Cxn *)` with `endpointDown_(Endpoint *)`.
- Replace packet allocation/send lambdas in client send paths with `m_endpoint.allocTxPacket()` and `m_endpoint.send(...)`.
- Make `cxnDiag()` return `m_endpoint.diag()` or rename to `endpointDiag()` and adjust tests.

Key implementation shape:

```c++
bool connectEndpoint_(ZiIP ip) {
  return m_endpoint.openUDP(
    app()->mx(),
    PathMode::ClientConnected,
    ZiIP{}, 0,
    ip, m_port,
    Endpoint::DatagramFn{[link = ZmMkRef(impl())](Datagram d) mutable {
      link->app()->rxRun([link = ZuMv(link), d = ZuMv(d)]() mutable {
        link->received_(ZuMv(d));
      });
    }},
    Endpoint::ReadyFn{[link = ZmMkRef(impl())](Endpoint *ep) mutable {
      link->app()->rxRun([link = ZuMv(link), ep]() mutable {
        link->endpointReady_(ep);
      });
    }},
    Endpoint::FailFn{[link = ZmMkRef(impl())](bool transient) mutable {
      link->connectFailed_0(transient);
    }},
    Endpoint::DownFn{[link = ZmMkRef(impl())](Endpoint *ep) mutable {
      link->app()->rxRun([link = ZuMv(link), ep]() mutable {
        link->endpointDown_(ep);
      });
    }});
}
```

The exact lambda capture style should follow existing `ZmMkRef(impl())` patterns. Avoid capturing raw `this` into asynchronous endpoint callbacks unless the link lifetime is already held by a `ZmRef`.

Dependencies on previous work:

- Uses existing `Endpoint::Cxn_`; no route remodel required.
- Requires no changes to server routing.

Complexity and feasibility:

- Moderate complexity due to asynchronous close callback preservation.
- Feasible with current `ZiMultiplex` behavior.
- Main risk is double notification on reconnect. Keep the current `m_udpReadyCount` behavior and reset `m_udpReady` before opening a new endpoint.

Validation after this phase:

```sh
rg -n "CliCxn|newCxn_|connected_0\\(|disconnected_0\\(|m_cxn|m_cxnDiag|m_closingCxn" zquic/src/Zquic.hh zquic/test
make -C zquic/src
make -C zquic/test ZquicEndpointTest ZquicAPITest
./zquic/test/ZquicEndpointTest
./zquic/test/ZquicAPITest
```

Expected result: no client-side direct use of `ZiMultiplex::udp()` remains outside `Endpoint::openUDP()`.

### Phase 2: Migrate Server Listener UDP Onto `Endpoint`

Area of focus: the server listener should use the same endpoint abstraction while preserving routing behavior.

Add or modify:

- Remove `Server::ListenerCxn`, `newCxn_()`, `connected_0()`, `disconnected_0()`, `received_0()`, `sent_0()`, `ioError_0()`, `allocRxPacket_()`, and `m_cxn` from `Server`.
- Add `Endpoint m_endpoint` to `Server`.
- Replace `Server::listen()` direct `mx()->udp(...)` setup with `m_endpoint.openUDP(...)`.
- Make endpoint datagram callback schedule `Server::received_(Datagram)` on the app Rx thread, matching current `received_0()`.
- Make endpoint ready callback update `m_local` from `m_endpoint.local()` and call `app()->listening()` if present.
- Make endpoint failure callback call the existing `failed_0(bool)` logic or fold that logic into `listenFailed_(bool)`.
- Make `sendVersionNegotiation_()` and all server packet send paths allocate and send through `m_endpoint`.
- Keep `Server::m_routes` and `Server::m_links` unchanged until Phase 3.

Key implementation shape:

```c++
bool listen() {
  if (!this->mx()) return false;
  close();

  ZiIP localIP = this->app()->localIP();
  uint16_t localPort = this->app()->localPort();

  return m_endpoint.openUDP(
    this->mx(),
    PathMode::ServerUnconnected,
    localIP, localPort,
    ZiIP{}, 0,
    Endpoint::DatagramFn{this, [](Server *self, Datagram d) {
      self->rxRun([self, d = ZuMv(d)]() mutable { self->received_(ZuMv(d)); });
    }},
    Endpoint::ReadyFn{this, [](Server *self, Endpoint *ep) {
      self->m_local = ep->local();
      if constexpr (requires(App *app_) { app_->listening(); })
        self->app()->listening();
    }},
    Endpoint::FailFn{this, [](Server *self, bool transient) {
      self->failed_0(transient);
    }});
}
```

Use the codebase's valid `ZmFn` construction style; the snippet is design intent, not final syntax.

Dependencies on previous work:

- Depends on completed/promoted `Endpoint`.
- Does not depend on route-entry renaming.

Complexity and feasibility:

- Moderate. The old server listener `Cxn` and `Endpoint::Cxn_` have nearly identical send/receive queues, so this is mostly deleting duplicate code and preserving callback threading.
- Low protocol risk because route parsing and link runtime state remain untouched.

Validation after this phase:

```sh
rg -n "class Cxn : public ZiConnection|template <typename Owner_, typename OwnerRef_>|CliCxn|SrvCxn|ListenerCxn|connected_0|disconnected_0|received_0|sent_0|ioError_0" zquic/src/Zquic.hh zquic/test
rg -n "mx\\(\\)->udp|app\\(\\)->mx\\(\\)->udp|this->mx\\(\\)->udp" zquic/src/Zquic.hh zquic/src
make -C zquic/src
make -C zquic/test ZquicEndpointTest ZquicAPITest ZquicLoopTest ZquicHandshakeTest
./zquic/test/ZquicEndpointTest
./zquic/test/ZquicAPITest
./zquic/test/ZquicLoopTest
./zquic/test/ZquicHandshakeTest
```

Expected result: the only `ZiConnection` subclass used for QUIC UDP is `Endpoint::Cxn_`.

### Phase 3: Replace `uintptr_t` Route Tokens and `LinkBase` With Hash-Backed CRTP Routes

Area of focus: route lookup should return the concrete CRTP server link directly from a locked `ZmHash`, with no integer token, `reinterpret_cast`, virtual `LinkBase`, or fixed route-array capacity.

Add or modify:

- Rename `CxnIDState` to `CxnState` in `ZquicTypes.hh`.
- Rename `CxnIDSlot` to `Cxn<Link_>` in `ZquicConn.hh`.
- Rename field `cid` to `id` unless the surrounding code reads clearer with `cid`; prefer `id` because `Cxn::id` reads as "connection identity bytes".
- Replace `uintptr_t routeToken` with `Link_ *link`.
- Rename `CxnIDRouter` to `CxnRouter<Link_>`.
- Replace the current `CxnIDRouter::Max` array with a `ZmHash<Cxn<Link_>, ...>` route index:
  - use `Cxn_IDAxor(const Cxn<Link_> &) -> const CxnID &` as the key accessor
  - use `ZmHashNode<Cxn<Link_>, ZmHashKey<Cxn_IDAxor<Link_>, ...>>`
  - use `ZmHashLock<ZmPLock, ZmHashHeapID<"Zquic.Endpoint.CxnRouter">>` so the route index is internally locked and visible under a Zquic hash ID
  - initialize with `ZmHashParams().bits(5).loadFactor(1).cBits(3)` unless the implementation adds a Zquic/server parameter that feeds `init()`
  - verify `ZuHash<CxnID>` support; if `ZuBArray<20>` does not hash correctly by default, add an explicit hash/comparator NTP for `CxnID`
- Replace `CxnRouter::find()` return type with `Link_ *`.
- Change `Server` to be parameterized by the concrete server link type, e.g. `Server<App_, Link_>`.
- Change `App::accepted(const InitialInfo &)` to return `ZmRef<Link_>` for that server instantiation.
- Change `ServerBootstrap::acceptInitial()` to accept `Link_ *link` instead of `uintptr_t routeToken`, or remove the link argument once bootstrap no longer mutates routes.
- Replace `SrvLink::initRuntimeCrypto_()` call from `uintptr_t(static_cast<LinkBase *>(this))` to the concrete `static_cast<Impl *>(this)` or remove the argument once bootstrap no longer mutates routes.
- Remove `Server::link_(uintptr_t)`.
- Remove `LinkBase` and its virtual `receivedRouted_()`, `serverRoutes_()`, and `serverRoutesClosed_()` hooks from the target model. `Link` should inherit `ZmPolymorph` directly, and `Server<App_, Link_>` should call the concrete link's non-virtual CRTP methods.
- Keep or replace `Server::m_links` as the owning reference registry; route entries do not own links. The final registry must not depend on `CxnRouter::Max`, because that constant is removed.

Important route semantics:

- `add(id, sequence, nullptr)` must fail for active routes.
- `tombstone(id)` must leave no active link pointer and must clear the reset token.
- `retire(id)` must leave no active link pointer and should clear the reset token because RFC 9000 invalidates the token on retirement.
- `find(id)` returns only active entries.
- Existing `state(id)` behavior can continue to return `Tombstone` for unknown IDs, but tests should document that this is local route-table behavior, not a protocol statement.
- Hash operations must not call into the concrete link while a `ZmHash` lock is held. `Server::route_()` should get the pointer from `find()`, then invoke the link after the hash operation returns.

Dependencies on previous work:

- Depends on Phase 2 removing the old UDP `Cxn` template, so the name `Cxn` is free for the route-entry type.
- Keeps server route ownership where it is to reduce blast radius.

Complexity and feasibility:

- Low to moderate. The data shape is small, but the rename touches many tests.
- The main risks are the existing nested `App::Link` examples and dangling link pointers. Mitigation: require an externally forward-declared concrete link type for `Server<App_, Link_>` and keep an active link lifetime registry with route lookup/removal serialized on the app Rx thread while the hash lock protects the route index structure.

Validation after this phase:

```sh
rg -n "LinkBase|virtual void receivedRouted_|routeToken|uintptr_t|reinterpret_cast<.*Link|CxnIDSlot|CxnIDState|CxnIDRouter|CxnRouter::Max" zquic/src zquic/test zquic/example
rg -n "CxnRoutes|ZmHashHeapID<\"Zquic.Endpoint.CxnRouter\"|ZmHashLock<ZmPLock" zquic/src
make -C zquic/src
make -C zquic/test ZquicCIDTest ZquicAPITest ZquicHandshakeTest ZquicRuntimeTest
./zquic/test/ZquicCIDTest
./zquic/test/ZquicAPITest
./zquic/test/ZquicHandshakeTest
./zquic/test/ZquicRuntimeTest
```

Expected result: route lookup is typed as the concrete CRTP server link pointer, the endpoint route table is a locked `ZmHash` with a Zquic hash ID, and no `LinkBase`, `uintptr_t` route token, or fixed `CxnRouter::Max` route capacity remains in zquic.

### Phase 4: Align Link CID State With zngtcp2 and Make Route Lifecycle Explicit

Area of focus: `Link` should own connection-local CID state, `Server` / `Endpoint` should own the global CID-to-link `ZmHash`, and close must retire/tombstone the installed route associations before link lifetime ends.

Add or modify:

- Add CID storage/accessors to `Link` or `SrvLink`; do not add a virtual `LinkBase` layer.
- Prefer placing common CID storage in `Link` so client and server can eventually share connection-ID lifecycle code:

```c++
protected:
  bool addLocalCID_(const CxnID &, uint64_t sequence, const StatelessResetToken & = {});
  bool addPeerCID_(const CxnID &, uint64_t sequence, const StatelessResetToken & = {});
  void installRoutes_(CxnRouter<Impl> &) const;
  void retireRoutes_(CxnRouter<Impl> &);

private:
  LinkCIDs m_localCIDs;
  LinkCIDs m_peerCIDs;
```

- If keeping CID storage in `SrvLink` is substantially smaller, use that first, but keep the interface non-virtual and CRTP-typed.
- Do not store `CxnRouter` inside `Link`; that is the endpoint/server route index. This matches zngtcp2, where `ngtcp2_conn` owns source/destination CID state and the server/application `handlers_` table maps Destination CID to handler.
- Refactor `ServerBootstrap` so it validates and records initial CID values but does not own `CxnRouter m_localCIDs` or `m_initialDCIDs` in the end state.
- In `SrvLink::initRuntimeCrypto_()`:
  - call `m_bootstrap.acceptInitial(h, datagramLen)` without a link argument
  - set runtime CIDs from bootstrap values
  - record the original/client Initial DCID as a bootstrap routing association that tombstones on close
  - record the local initial SCID with sequence `0` and the generated stateless reset token
- In `Server::received_()`, after `link->receivedRouted_()`, call `link->installRoutes_(m_routes)`.
- In `Server::releaseLink_()`, call `link->retireRoutes_(m_routes)` before removing the `ZmRef<Link>`.
- In `Server::clearLinks_()`, clear `m_routes` before clearing `m_links`.
- Add the route-critical `NEW_CONNECTION_ID` / `RETIRE_CONNECTION_ID` subset using the zngtcp2 split: `Link` records connection-local peer DCIDs and locally issued SCIDs, including sequence numbers, reset tokens, `retire_prior_to`, uniqueness checks, active CID limits, and retired/active state; `Server` / `Endpoint` only associates or dissociates locally issued SCIDs in `m_routes`.

Route close policy:

- Initial DCIDs used only for routing client Initial packets become tombstones on close, preventing immediate accidental rebinding.
- Local SCIDs that carried stateless reset tokens are retired on close and have reset tokens cleared.
- Every active route entry installed in `Server::m_routes` must have `link == this`.
- Link-owned CID records should mark their endpoint association removed after endpoint routes are retired/tombstoned, but the endpoint hash nodes remain owned by `CxnRouter`.

Dependencies on previous work:

- Depends on typed `Cxn` route entries from Phase 3.
- Includes variable-length short-header routing for stored known local CID lengths; it does not require any packet-local CID-length inference.

Complexity and feasibility:

- Moderate to high because it touches server accept, route install, close, and tests.
- Feasible with the Phase 3 `ZmHash` route table.
- The main risk is ordering: `Server::releaseLink_()` must update endpoint routes before it drops the last `ZmRef<Link>`.

Validation after this phase:

```sh
rg -n "serverRoutes_|serverRoutesClosed_|initialDCIDs|localCIDs|m_bootstrap\\.initial|m_bootstrap\\.local" zquic/src zquic/test
make -C zquic/src
make -C zquic/test ZquicCIDTest ZquicHandshakeTest ZquicLoopTest ZquicRuntimeTest
./zquic/test/ZquicCIDTest
./zquic/test/ZquicHandshakeTest
./zquic/test/ZquicLoopTest
./zquic/test/ZquicRuntimeTest
```

Expected result: `ServerBootstrap` no longer owns route tables; `SrvLink` or `Link` exposes CID associations, and close behavior retires/tombstones the corresponding endpoint hash routes explicitly.

### Phase 5: Restore zngtcp2 Stateless Reset Parity

Area of focus: wire the stateless reset support that Zquic already partially carries into the receive, route, transport-parameter, and close-state paths.

Add or modify:

- Move `StatelessResetToken` to a lower-level header usable by `ZquicTransportParams.hh`.
- Add `TransportParams::statelessResetToken` and `TransportParams::statelessResetTokenPresent`.
- Update `TransportParams::encodedLength()`, `encode()`, `decode()`, and `validate()`:
  - encode the token only when present
  - reject decoded token values that are not exactly 16 bytes
  - reject client-sent stateless reset token transport parameters in server validation
- In server transport parameter setup, publish the local initial SCID reset token generated during server bootstrap.
- In client transport parameter validation, if the server token is present and the active destination CID sequence is `0`, remember it as the peer reset token for that destination CID, matching zngtcp2's `conn->dcid.current.seq == 0` behavior.
- Add link-owned peer reset token storage separate from local route reset tokens. Local route tokens are for resets this endpoint sends; peer tokens are for resets this endpoint receives.
- Add `StatelessReset::decode()` and `StatelessReset::verify()` helpers:
  - decode treats the final 16 bytes of a sufficiently long UDP datagram as the token
  - verify compares tokens in constant time or as close to constant-time as the existing crypto utility layer reasonably supports
- In failed decrypt paths for the first packet in a datagram, call `checkStatelessReset_()` with the full UDP datagram:
  - `receiveProtectedShortPacket_()` when `PacketProtection::unprotectShort()` fails
  - protected long-packet receive paths when packet processing/decryption fails after a connection is associated
  - buffered packet retry paths if the code keeps buffered protected packets
- On a valid incoming stateless reset:
  - set `m_runtimeCloseState = CloseState::Draining`
  - set `m_linkState = LinkState::Draining`
  - clear writable/send scheduling so no further packets are emitted
  - increment diagnostics
  - optionally invoke an application callback such as `statelessReset()` if present
- In server routing, when no active route is found:
  - parse the CID as currently supported
  - look up a reset token for a non-active local route entry if one is intentionally retained for stateless reset
  - call `StatelessReset::writeForUnknownCID()`
  - send the result through `Endpoint`
- Keep retired tokens unavailable for reset detection/emission unless the route state explicitly represents "lost local state but token retained for reset." RFC 9000 says retired tokens must not be checked; model this state explicitly if needed rather than overloading `Retired`.

Dependencies on previous work:

- Depends on Phase 4 link-owned route entries so local reset tokens have a clear owner and close policy.
- Depends on `Endpoint` send ownership from Phases 1 and 2 for unknown-CID reset emission.
- Does not depend on full migration policy, but it can share the bounded NEW_CONNECTION_ID / RETIRE_CONNECTION_ID codec and link-owned CID state added by Phase 4.

Complexity and feasibility:

- Moderate. The packet writer and token generation already exist, but receive-side semantics, transport parameter encoding, and close-state transitions must be added deliberately.
- The main complexity is token ownership: do not compare peer reset tokens against local route tokens, and do not keep checking retired peer tokens.
- Feasible as a bounded zngtcp2 parity slice because current Zquic already has `CloseState::Draining`, `LinkState::Draining`, `StatelessResetToken`, and `StatelessReset::writeForUnknownCID()`.

Validation after this phase:

```sh
rg -n "statelessResetToken|statelessResetTokenPresent|checkStatelessReset_|StatelessReset::decode|StatelessReset::verify|CloseState::Draining|LinkState::Draining" zquic/src zquic/test
make -C zquic/src
make -C zquic/test ZquicCIDTest ZquicVersionTest ZquicHandshakeTest ZquicPacketProtectionTest
./zquic/test/ZquicCIDTest
./zquic/test/ZquicVersionTest
./zquic/test/ZquicHandshakeTest
./zquic/test/ZquicPacketProtectionTest
```

Expected result: current Zquic has zngtcp2-equivalent stateless reset detection for associated links, transitions to draining on a valid token, encodes/decodes server stateless reset transport parameters, and can emit a reset for unknown server CIDs when a retained local reset token exists.

### Phase 6: Public Naming Cleanup and Test Split

Area of focus: make names match the target model after behavior is stable.

Add or modify:

- Remove old UDP adapter public aliases `CliCxn` and `SrvCxn`.
- Remove `Cxn_` / `CxnRef_` template parameters from `Link`, `CliLink`, and `SrvLink` if they only exist to support the deleted UDP adapter.
- Keep `CxnID` public for raw byte storage.
- Keep `CxnIDGen` because it generates raw ID bytes. Do not rename to `CxnGen` unless it starts constructing full `Cxn` route entries.
- Split tests if useful:
  - `ZquicCIDTest` keeps byte generation, packet CID constraints, retry/transport parameter CID validation.
  - `ZquicCxnTest` or `ZquicRouteTest` covers `Cxn`, `CxnState`, and `CxnRouter`.
- Update comments and example code so:
  - "connection" means a QUIC session only when referring to `Link`.
  - `Cxn` means route identity.
  - `Endpoint` means UDP socket/datagram I/O.
- Update `zquic/example/ZquicClient.cc` and `zquic/example/ZquicServer.cc` if they refer to old `CliCxn` / `SrvCxn` shape.

Dependencies on previous work:

- Depends on Phases 1 through 5.

Complexity and feasibility:

- Low to moderate, mostly mechanical but public API breaking.
- Source compatibility with code naming `CliCxn`, `SrvCxn`, or `CliLink::cxn()` is intentionally not preserved.

Validation after this phase:

```sh
rg -n "CID|CxnID|CliCxn|SrvCxn|class Cxn : public ZiConnection|CxnRef|typename Cxn_" zquic/src zquic/test zquic/example
make -C zquic/src
make -C zquic/test
./zquic/test/ZquicCIDTest
test -x ./zquic/test/ZquicRouteTest && ./zquic/test/ZquicRouteTest || true
```

Expected result: remaining `CID` names refer to raw ID bytes or RFC terminology, not route objects.

### Phase 7: Final Cleanup and Documentation Pass

Area of focus: remove compatibility friction and document the final invariants.

Add or modify:

- Remove leftover forwarding helpers that only existed to bridge old UDP `Cxn` ownership.
- Re-check object layout:
  - no route-entry heap allocations
  - no unnecessary atomics in route state
  - no integer route tokens
  - no duplicate endpoint diagnostics outside `Endpoint`
- Re-check that `Endpoint` remains the only direct `ZiMultiplex::udp()` user in Zquic.
- Add a short section to `zquic/README.md` or nearby module docs describing:
- `Endpoint`
- `Link`
- `Cxn`
- fixed short-header CID length limitation
- stateless reset token ownership, detection, draining, and unknown-CID emission

Validation after this phase:

```sh
rg -n "mx\\(\\)->udp|app\\(\\)->mx\\(\\)->udp|this->mx\\(\\)->udp|LinkBase|routeToken|reinterpret_cast<.*Link|class Cxn : public ZiConnection" zquic/src zquic/test zquic/example
make -C zquic/src
make -C zquic/test
```

## Code References to Impacted Code

- `zquic/src/Makefile.am:13` - `ZquicEndpoint.hh` is currently `noinst_HEADERS`; promote it if public templates store `Endpoint`.
- `zquic/src/ZquicEndpoint.hh:23` - `Endpoint` public/private API; add `DownFn`, update `openUDP()`, keep private `Cxn_`.
- `zquic/src/ZquicEndpoint.cc:18` - private `Endpoint::Cxn_` UDP adapter; reuse as the sole QUIC UDP `ZiConnection` subclass.
- `zquic/src/ZquicEndpoint.cc:144` - `Endpoint::openUDP()` validates multiplexer, configures UDP, and calls `mx->udp()`.
- `zquic/src/ZquicEndpoint.cc:187` - `Endpoint::closeUDP()` clears the private UDP connection and calls asynchronous `ZiConnection::close()`.
- `zquic/src/ZquicEndpoint.cc:220` - `Endpoint::disconnected_()` currently only clears state; add `DownFn` notification here.
- `zquic/src/ZquicConn.hh:92` - `CxnIDSlot` should become route-entry `Cxn<Link_>`.
- `zquic/src/ZquicConn.hh:100` - `CxnIDRouter` should become `CxnRouter<Link_>`, backed by a locked `ZmHash` keyed by `CxnID`.
- `zquic/src/ZquicConn.hh:134` - `find()` currently returns `uintptr_t`; change it to `Link_ *`.
- `zquic/src/ZquicConn.hh:199` - `CxnIDGen` should remain ID-byte-specific.
- `zquic/src/ZquicConn.hh:276` - `ServerBootstrap` currently owns route tables; reduce it to bootstrap CID and transport-parameter state after route associations move to `Link` CID state plus `Server::m_routes`.
- `zquic/src/ZquicConn.hh:54` - `StatelessResetToken` currently lives too high for transport parameters; move it to a lower-level header.
- `zquic/src/ZquicConn.hh:83` - `StatelessReset` currently only exposes packet writing; add decode/verify helpers for incoming resets.
- `zquic/src/ZquicConn.hh:140` - route reset-token lookup currently only covers active entries; add explicit retained-token semantics for unknown-CID reset emission without violating retired-token invalidation.
- `zquic/src/ZquicConn.cc:85` - `CxnIDGen::random()` enforces `[MinCIDLength, CxnIDMax]`; this supports fixed-length route assumptions.
- `zquic/src/ZquicConn.cc:122` - `ServerBootstrap::acceptInitial()` currently requires a nonzero `uintptr_t` route token; change to typed link or remove route mutation from bootstrap.
- `zquic/src/ZquicConn.cc:62` - `StatelessReset::writeForUnknownCID()` exists; wire it into server no-route handling after reset-token lookup.
- `zquic/src/ZquicTransportParams.hh:18` - `TransportParams` lacks stateless reset token fields; add server token storage and presence flag.
- `zquic/src/ZquicTransportParams.cc` - update transport parameter encode/decode/validation for `stateless_reset_token`.
- `zquic/src/ZquicPacket.cc:235` - `Packet::parseShort()` requires the caller to know CID length; server pre-routing should therefore match stored route CIDs by known length before connection-local short-header parsing.
- `zquic/src/Zquic.hh:59` - `LinkBase` currently exposes route-copy virtuals; remove it from the target model and put non-virtual route lifecycle operations on the concrete CRTP link.
- `zquic/src/Zquic.hh:1623` - `closeRuntime_()` moves to closing; add a draining transition for validated stateless reset.
- `zquic/src/Zquic.hh:2146` - `receiveProtectedShortPacket_()` returns false on decrypt/parse failure; call stateless reset verification for the first packet/datagram failure path.
- `zquic/src/Zquic.hh:672` - `Server` currently owns listener UDP directly through `ListenerCxn`; replace with `Endpoint`.
- `zquic/src/Zquic.hh:808` - `Server::received_()` routes datagrams and then copies link routes; update to typed route lifecycle.
- `zquic/src/Zquic.hh:833` - long-header route lookup currently returns a token; return concrete `Link *` and invoke unknown-CID reset emission when appropriate.
- `zquic/src/Zquic.hh:839` - short-header route parsing uses fixed `CxnIDGen::InitialLength`; keep for now, document, and use it for unknown-CID reset token lookup.
- `zquic/src/Zquic.hh:887` - `Server::link_(uintptr_t)` and `reinterpret_cast` should be deleted.
- `zquic/src/Zquic.hh:1324` - old templated UDP `Cxn` should be removed after client/server endpoint migration.
- `zquic/src/Zquic.hh:2360` - `CliLink` currently parameterizes and owns UDP `Cxn`; migrate to `Endpoint`.
- `zquic/src/Zquic.hh:2490` - `CliLink::connect_()` directly calls `app()->mx()->udp()`; replace with `Endpoint::openUDP()`.
- `zquic/src/Zquic.hh:2718` - client protected packet send paths depend on `m_cxn`; change to endpoint allocation/send.
- `zquic/src/Zquic.hh:2887` - `SrvLink` defaults to `SrvCxn<App>` even though server links should not own UDP.
- `zquic/src/Zquic.hh:2991` - `SrvLink::initRuntimeCrypto_()` passes `uintptr_t(static_cast<LinkBase *>(this))`; remove token casting and use the concrete CRTP link pointer only while bootstrap still needs a link argument.
- `zquic/src/Zquic.hh:3243` - `SrvLink::serverRoutes_()` copies bootstrap route tables; replace with link-owned CID association install into the endpoint route hash.
- `zquic/src/Zquic.hh:3248` - `SrvLink::serverRoutesClosed_()` closes bootstrap route tables; replace with explicit endpoint route retirement/removal from the hash.
- `zquic/test/ZquicEndpointTest.cc:14` - existing endpoint test only checks injected datagram ownership; expand for ready/down and send diagnostics where practical.
- `zquic/test/ZquicCIDTest.cc:14` - route tests currently use integer route tokens; update to concrete `Link *` and/or split route tests from CID byte tests.
- `zquic/test/ZquicAPITest.cc:184` - test currently asserts old `CliCxn` / `SrvCxn` public shape; update to endpoint/link shape.
- `zquic/test/ZquicAPITest.cc:220` - client UDP connect/reconnect test should remain the main Phase 1 regression test, but assertions should read endpoint diagnostics.
- `zm/src/ZmHash.hh:210` - `ZmHashLock<ZmPLock, ...>`, `ZmHashID`, and `ZmHashHeapID` are the local NTPs to use for route hash locking and telemetry identity.
- `zquic/src/Zquic.hh:1318` - existing `ZmHash` pattern for stream objects; copy the `ZuDerive` / key accessor style.
- `zdb/src/ZdbMemStore.hh:1340` - existing locked `ZmHash` pattern using `ZmHashLock<ZmPLock, ZmHashHeapID<...>>`.

## Detailed Test Plan

Endpoint tests:

- Add `testEndpointReadyDownCallbacks` to `ZquicEndpointTest` using a real `ZiMultiplex`, `Endpoint::openUDP()`, and `closeUDP()`:
  - verify ready callback fires
  - verify local ephemeral port is populated
  - verify down callback fires after close
  - verify diagnostics remain stable
- Keep `testDatagramOwnership`.
- Add a direct send queue unit only if it can be deterministic without relying on timing. Otherwise cover send through existing client/server tests.

Client migration tests:

- Update `ZquicAPITest::testCliLinkUDPConnect`:
  - connect to endpoint sink
  - verify `udpReady()`, `udpReadyCount()`, `local().port()`, `remote().port()`
  - verify `cxnDiag()` or `endpointDiag()` counters are sourced from `Endpoint`
  - reconnect and verify old endpoint closes without extra failure
  - disconnect and verify `impl()->disconnected()` if the shape test tracks it
- Update `testAlignedSurfaceShape` to stop naming `CliCxn`.
- Run handshake/client tests to catch send path regressions.

Server endpoint tests:

- Add or update a server listen test:
  - start `Server` with `localPort() == 0`
  - verify `listening()`
  - verify `local().port()` is populated from `Endpoint`
  - close server and verify route table/link table clear
- Existing loop/handshake tests should exercise server receive routing after listener migration.

Route tests:

- Update route table tests to use typed link pointers.
- Verify `CxnRouter<Link>` is backed by the `CxnRoutes_<Link>` `ZmHash` and has no `Max` capacity path.
- Verify the route hash is constructed with `ZmHashHeapID<"Zquic.Endpoint.CxnRouter">` / the matching hash ID and uses `ZmHashLock<ZmPLock, ...>`.
- Test active add/find, retire, re-add after retire, tombstone, failure to reactivate tombstone, reset-token lookup, reset-token clearing on retire/tombstone.
- Test route install/iteration copies or installs only active entries from link CID state.
- Test null active link add fails.
- Test `ServerBootstrap::acceptInitial()` no longer accepts a null link if it still takes a link pointer in the transitional phase.

Link CID / route lifecycle tests:

- Add a server bootstrap route lifecycle test:
  - accept an Initial
  - verify `SrvLink` records the original DCID and local SCID as CID state
  - verify original DCID and local SCID route to the `SrvLink`
  - close link
  - verify original DCID is tombstoned
  - verify local SCID is retired and has no reset token
  - verify `find()` returns null after close
- Add a route lifetime test that removes the `ZmRef<Link>` only after routes are retired.
- Add a callback-order test or code-level assertion that `CxnRouter::find()` / install / retire do not call into the concrete link while holding the hash lock.

Stateless reset parity tests:

- Add transport parameter tests:
  - encode/decode a 16-byte server `stateless_reset_token`
  - reject malformed token lengths
  - reject client-sent token use in server-side validation
- Add packet helper tests:
  - decode the final 16 bytes of a valid-sized datagram as a reset token
  - reject too-small datagrams
  - verify constant-token equality behavior through `StatelessReset::verify()`
- Add existing-link receive tests modeled after zngtcp2:
  - set a remembered peer reset token on a client/server link
  - feed a datagram ending in that token through the failed-decrypt path
  - verify `LinkState::Draining` and `CloseState::Draining`
  - verify no further sends are emitted
  - verify a token mismatch does not drain the link
- Add server unknown-CID tests:
  - install a retained local route token with no active link
  - send an unknown-CID short-header-like datagram
  - verify `StatelessReset::writeForUnknownCID()` output is sent through `Endpoint`
  - verify no reset is sent when the route token was retired/cleared

Regression tests:

```sh
make -C zquic/src
make -C zquic/test
./zquic/test/ZquicEndpointTest
./zquic/test/ZquicAPITest
./zquic/test/ZquicCIDTest
./zquic/test/ZquicHandshakeTest
./zquic/test/ZquicLoopTest
./zquic/test/ZquicRuntimeTest
./zquic/test/ZquicPacketProtectionTest
```

Search-based tests:

```sh
rg -n "class Cxn : public ZiConnection|template <typename Owner_, typename OwnerRef_>|CliCxn|SrvCxn" zquic/src zquic/test zquic/example
rg -n "LinkBase|routeToken|uintptr_t|reinterpret_cast<.*Link|CxnIDSlot|CxnIDState|CxnIDRouter|CxnRouter::Max" zquic/src zquic/test zquic/example
rg -n "CxnRoutes|ZmHashHeapID<\"Zquic.Endpoint.CxnRouter\"|ZmHashLock<ZmPLock" zquic/src
rg -n "mx\\(\\)->udp|app\\(\\)->mx\\(\\)->udp|this->mx\\(\\)->udp" zquic/src zquic/test zquic/example
```

## Acceptance Criteria

- `Endpoint` is the only Zquic component that directly subclasses `ZiConnection` for UDP or directly calls `ZiMultiplex::udp()`.
- `CliLink` and `Server` both use `Endpoint` for UDP open, close, datagram receive, send, and diagnostics.
- The old templated UDP `Cxn<Owner, OwnerRef>` and aliases `CliCxn` / `SrvCxn` are gone.
- `Cxn<Link_>` means route identity and contains raw ID bytes, sequence number, reset token, state, and `Link_ *`.
- `CxnID` remains the raw byte alias.
- `CxnIDGen` remains ID-byte-specific and is not renamed to `CxnGen`.
- Route lookup returns the concrete CRTP server `Link *` directly.
- No `LinkBase`, virtual route hook, `routeToken`, `uintptr_t` route handle, or `reinterpret_cast<...Link...>` route path remains.
- Closing a server link retires/tombstones all of its endpoint route entries before the server releases the final `ZmRef<Link>`.
- Tombstones still prevent immediate accidental rebinding of recently closed Initial DCIDs.
- Client bootstrap validation still checks original DCID, retry SCID when present, and server initial SCID against transport parameters.
- Server bootstrap still rejects non-Initial packets, unsupported versions, short CIDs, undersized Initial datagrams, and repeated accepts.
- Server transport parameters can encode the 16-byte stateless reset token, and client transport parameter handling remembers it for the sequence-0 peer destination CID.
- Existing-link receive paths check for stateless reset when the first packet in a datagram cannot be decrypted or processed, and a valid token transitions the link to draining.
- Token mismatches and unavailable/retired tokens do not drain the link.
- Server unknown-CID routing can emit a Stateless Reset through `Endpoint` when a valid retained local reset token exists.
- Unknown-CID reset emission does not use tokens that have been retired/cleared.
- Short-header server pre-routing matches known stored CID lengths and chooses the longest matching route.
- zquic builds and the focused tests listed above pass.

## Non-goals

- Preserving source compatibility for code that names `CliCxn`, `SrvCxn`, the old UDP `Cxn` template, `CxnIDSlot`, `CxnIDRouter`, or `CliLink::cxn()`.
- Preserving the old one-parameter `Server<App>` shape when the server route table needs the concrete link type. Use `Server<App_, Link_>` with an externally forward-declared link type instead.
- Implementing automatic path migration policy, path validation, or peer CID selection across multiple paths.
- Implementing proactive additional-CID issuance policy beyond the route/state/codec hooks needed to support NEW_CONNECTION_ID frames.
- Implementing complete active CID accounting across unused/bound/path-bound sets beyond the bounded link-owned state used for current route association and retirement.
- Changing QUIC packet protection, TLS, stream, recovery, congestion, PMTUD, HTTP/3, or transport parameter semantics except where route ownership requires preserving existing CID validation.
- Preserving the old fixed route-table capacity behavior.

## Options and Open Questions

No blocking open questions remain for this remodel. The following decisions resolve the ambiguity in the original plan:

- Use `Endpoint` as a real public Zquic component by installing `ZquicEndpoint.hh`, because public link/server templates need the complete type and endpoint tests already include it.
- Add `Endpoint::DownFn` instead of preserving the old UDP `Cxn` adapter just for disconnect notification.
- Name the route object `Cxn` only after deleting the old UDP `Cxn` template.
- Rename `CxnIDRouter` to `CxnRouter<Link_>`.
- Remove `LinkBase`; route dispatch uses `Server<App_, Link_>` plus non-virtual CRTP link methods.
- Use an externally forward-declared concrete server link type instead of `typename App::Link` inside `Server<App>`, because the nested type is not visible when the CRTP base is instantiated.
- Keep `CxnID` and `CxnIDGen` for raw byte IDs; do not use `CxnGen` unless future code generates full route entries.
- Use known-length short-header pre-routing from the endpoint route table; do not infer a packet-local CID length from the wire image.
- Keep `Server::m_links` as a `ZmRef<Link>` lifetime registry while route entries store non-owning concrete `Link *`.

Remaining non-blocking future work:

- Variable short-header CID lengths: the route-critical zngtcp2-style behavior is now in scope through stored-length route matching. Future work is limited to issuance policy for non-default CID lengths and tests that exercise live handshakes with multiple issued lengths.
- Additional CID issuance and migration: the bounded zngtcp2-style split is now in scope through link-owned CID state, frame codec support, route association, and retirement hooks. Remaining work is path migration policy: choosing peer DCIDs for new paths, emitting additional NEW_CONNECTION_ID frames proactively, sending RETIRE_CONNECTION_ID frames for peer CIDs retired by `retire_prior_to`, path validation, and complete unused/bound/path-bound active CID accounting.
