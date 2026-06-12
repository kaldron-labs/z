# Zquic Connection Model Remodel Plan

## Goal

Align the data model with QUIC terminology and current behavior:

- `Endpoint` owns UDP I/O.
- `Link` owns QUIC session state.
- `Cxn` represents one routable QUIC connection identity for a `Link`.
- Every active `Cxn` points directly at its owning `Link`.

This removes the current naming collision where `Cxn` means a UDP `ZiConnection`
adapter while `CxnID` is the QUIC routing identity.

## Target Model

```text
Endpoint
  UDP socket and datagram I/O
  server route index

LinkBase / Link / SrvLink / CliLink
  QUIC crypto, transport, streams, recovery, runtime state
  owns or exposes the Cxn entries used to route packets

Cxn
  one QUIC connection identity / route entry
  ID bytes currently represented by CxnID
  sequence number
  stateless reset token
  state
  direct owning LinkBase pointer
```

The raw QUIC connection ID bytes are small and remain value-stored as
`ZuBArray<CxnIDMax>` or the existing fixed-size alias during migration. The new
`Cxn` is the route/binding object, not the whole QUIC session; the session
remains `Link`.

## Naming Map

```text
current Cxn<Owner, OwnerRef>       -> Endpoint-internal UDP connection adapter
current CliCxn / SrvCxn           -> removed or Endpoint-internal aliases
current CxnID                     -> Cxn identity bytes inside new Cxn
current CxnIDSlot                 -> Cxn
current CxnIDState                -> CxnState
current CxnIDRouter               -> CxnRouter or Endpoint route table
current CxnIDGen                  -> CxnGen or CxnIDGen, depending on final API
```

Prefer `CxnGen` only if it generates complete `Cxn` entries. If it only
generates ID bytes, keep it named around IDs or make it an internal helper.

## Invariants

- `Endpoint` is the only owner of UDP `ZiConnection` I/O.
- `Link` lifetime must dominate every active `Cxn` that points at it.
- Closing a `Link` retires or tombstones all of its `Cxn` entries before the
  `LinkBase *` can become invalid.
- Route lookup returns `LinkBase *` directly; no `uintptr_t` route token and no
  `reinterpret_cast` path is needed.
- Tombstones remain possible so recently closed IDs cannot be immediately
  rebound accidentally.
- Server routing continues to handle both initial DCIDs and local SCIDs.
- Client bootstrap still validates original DCID, retry SCID, and server
  initial SCID against transport parameters.

## Phase 1: Fold UDP `Cxn` Into `Endpoint`

1. Move the current template `Cxn<Owner, OwnerRef>` implementation out of
   `Zquic.hh` and into `ZquicEndpoint.hh` / `ZquicEndpoint.cc` as an
   `Endpoint::Cxn_` private implementation detail.
2. Make `Endpoint::Cxn_` always call back into `Endpoint`:
   - `connected_`
   - `disconnected_`
   - `received_`
   - `sent_`
   - `ioError_`
   - packet allocation helpers
3. Replace server listener storage:
   - `ListenerCxn *m_cxn` becomes `Endpoint m_endpoint` or a direct embedded
     endpoint-like member.
   - `listen()`, `close()`, `sendPacket_()`, `allocTxPacket_()`, and listener
     diagnostics delegate to `Endpoint`.
4. Replace client UDP storage similarly:
   - `CliLink::m_cxn` becomes an endpoint member or reference.
   - client connection setup opens an `Endpoint` in connected mode.
5. Delete `CliCxn` and `SrvCxn` aliases once all users are migrated.

Validation after this phase:

```sh
make -C zquic/src
make -C zquic/test
```

## Phase 2: Introduce `Cxn` Route Entries

1. Rename the current `CxnIDSlot` shape into the new route object:

   ```c++
   struct Cxn {
     CxnID id;
     uint64_t sequence = 0;
     LinkBase *link = nullptr;
     StatelessResetToken resetToken;
     CxnState::T state = CxnState::Active;
   };
   ```

2. Rename `CxnIDState` to `CxnState`.
3. Change route insert/update APIs to accept `LinkBase *` directly instead of a
   `uintptr_t routeToken`.
4. Update route lookup:

   ```c++
   LinkBase *find(const CxnID &id) const;
   ```

5. Remove server-side `link_(uintptr_t)` once lookup returns `LinkBase *`.
6. Keep ID equality and packet parsing based on the raw ID bytes, not on the
   owning link pointer.

Validation after this phase:

```sh
rg -n "routeToken|uintptr_t|reinterpret_cast<LinkBase|CxnIDSlot|CxnIDState" zquic/src zquic/test
make -C zquic/src
make -C zquic/test
```

## Phase 3: Move Route Ownership Toward `Link`

1. Add route-entry storage or accessors to `LinkBase` / `Link` so each link can
   enumerate its active `Cxn` entries.
2. Move `ServerBootstrap::m_localCIDs` and `m_initialDCIDs` toward the owning
   `SrvLink`, or make `ServerBootstrap` a short-lived initializer that fills
   the link-owned route entries.
3. Replace `serverRoutes_(CxnIDRouter &)` and `serverRoutesClosed_(...)` with
   clearer link-owned operations:
   - `installRoutes(EndpointRoutes &)`
   - `retireRoutes(EndpointRoutes &)`
   - or direct `Endpoint` calls from server accept/close paths.
4. On close:
   - tombstone initial DCIDs used for client Initial routing.
   - retire active local SCIDs that carried stateless reset tokens.
   - clear every back-pointer before releasing the final `Link` reference.

Validation after this phase:

```sh
rg -n "serverRoutes_|serverRoutesClosed_|initialDCIDs|localCIDs" zquic/src zquic/test
make -C zquic/src
make -C zquic/test
```

## Phase 4: Resolve Public Naming

After the structural change is stable, perform the public rename in one
mechanical pass:

1. Rename the route table:
   - `CxnIDRouter` -> `CxnRouter` or `EndpointRoutes`.
2. Rename generators and tests:
   - `ZquicCIDTest` -> `ZquicCxnTest` if the tested unit is now route entries.
   - keep a dedicated ID-byte test name if it only tests byte generation.
3. Decide whether the raw byte alias remains public:
   - if public, use a precise name such as `CxnID`.
   - if internal, hide it behind `Cxn::id`.
4. Update comments and examples so “connection” means `Link` only when referring
   to the full QUIC session, and `Cxn` when referring to route identities.

Validation after this phase:

```sh
rg -n "CID|CxnID|CliCxn|SrvCxn|class Cxn : public ZiConnection" zquic/src zquic/test zquic/example
make -C zquic/src
make -C zquic/test
```

## Phase 5: Clean Up API Friction

1. Remove compatibility aliases unless external API stability requires them.
2. Collapse any remaining route helper methods that only forward through
   bootstrap objects.
3. Re-check object layout:
   - no unnecessary atomics in route telemetry.
   - no heap allocations for fixed-size ID storage.
   - no `reinterpret_cast` route tokens.
4. Revisit `Endpoint` diagnostics after UDP I/O ownership is centralized.

## Risk Areas

- Lifetime bugs if `Endpoint` route tables outlive closed links.
- Retry and transport-parameter validation regressions if original DCID,
  initial SCID, or retry SCID ownership moves too early.
- Short-header routing currently assumes `CxnIDGen::InitialLength`; if variable
  ID lengths are later supported, the route table needs a parser strategy.
- Public source compatibility will break if `CxnID` is fully renamed instead of
  retained as a byte alias.

## Suggested Commit Slices

1. Fold UDP `Cxn` into private `Endpoint::Cxn_`.
2. Add new `Cxn` route-entry type and pointer-based route lookup.
3. Move server bootstrap route state into `SrvLink` / `Link`.
4. Rename router/generator/tests and remove old aliases.
5. Final cleanup and documentation pass.
