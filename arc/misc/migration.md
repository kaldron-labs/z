# Full QUIC Network Path Migration Implementation Plan

## Target

Implement QUIC v1 network path migration in `zquic` as a single-active-path
feature.  This is not multipath: at any instant normal STREAM/control traffic
uses one active path, and at most one candidate path may be under validation.

Ship the work as one coordinated change series across:

- `zquic`: transport API, path state, endpoint rebinding, CID lifecycle,
  diagnostics, qlog, tests.
- `Zhttp`: HTTP/3 policy and callbacks over the extended transport API.
- `zhttpd` and `zhttp`: CLI flags, runtime wiring, diagnostics, integration
  tests.

Do not add compatibility shims for old callback names or vague policy flags.
Rename or replace dependent code when the new API is cleaner.

## Required Semantics

Implement these behaviors exactly:

- Passive peer address change: when a valid packet arrives from a new remote
  tuple, start path validation for that tuple and keep the old path active until
  validation succeeds.
- NAT rebinding: treat same-CID remote tuple changes as passive migration.  Do
  not reject them because local `activeMigration(false)` was configured.
- Application active migration: allow an application to request migration to a
  new local socket, remote tuple, or both.  Reject the request if the peer sent
  `disable_active_migration`.
- Promotion: promote only after a matching `PATH_RESPONSE` for the active
  candidate challenge.  Do not promote just because packets arrive from the new
  tuple.
- Fallback: if candidate validation times out, abandon the candidate and keep
  the previous active path.  If local rebinding already made fallback
  impossible, close the connection with an explicit migration-failure reason.
- CID use: use a fresh peer CID for active migration when one is available.
  Reuse the active peer CID only for zero-length CID connections or passive NAT
  rebinding where the peer has not supplied a spare CID.
- Packet routing: send validation/probe packets for the candidate path to the
  candidate tuple; send ordinary application traffic on the active path until
  promotion.
- Path scoping: anti-amplification, ECN state, PMTUD state, bytes Rx/Tx, and
  qlog path events are updated on the path that actually handled the datagram.

## Implementation Slices

Implement in these slices.  A later slice may start only after the previous
slice's acceptance criteria are true.  Each slice must leave the tree building
and must not carry hidden broken behavior for a later slice to repair.

### Slice 0: Dependency And Baseline Audit

Scope:

- Confirm the current `zquic`, `zhttp/src`, and `zhttp/test` build inputs.
- Confirm `ZiMultiplex::udp()` / `udp_()`, `ZiConnection`, `ZiIOContext`,
  `Sock::initUDP()`, `Path`, `PathState`, `CxnRouter`, `ZtArray<LinkCID>`,
  `ZmScheduler::Timer`, and `ZquicLOG` are the primitives used by later
  slices.
- Record any build-system change that would be required before writing code.

Files:

- `zquic/src/Zquic_.hh`
- `zquic/src/ZquicLink.hh`
- `zquic/src/ZquicPath.hh`
- `zquic/src/ZquicSock.*`
- `zhttp/src/Makefile.am`
- `zhttp/test/Makefile.am`

Do not:

- change runtime behavior;
- add dependencies;
- add new abstractions.

Acceptance criteria:

- The implementer has verified that no external dependency is needed.
- The plan for HTTP keeps core `libZhttp.la` independent of `libZquic.la`, or
  a deliberate build-system dependency change is explicitly written before
  implementation starts.
- Baseline commands pass before behavior changes:

```sh
make -C zquic/src -j8
make -C zquic/test -j8
make -C zquic/test test
```

Next slice may depend on:

- the existing socket, path, CID, timer, and qlog primitives being sufficient
  for migration implementation.

### Slice 1: Public Zquic Vocabulary And Params

Scope:

- Add `PathRole`, `MigrationState`, `MigrationReason`, `MigrationMode`,
  `PathInfo`, `MigrationParams`, and `MigrationResult`.
- Add `EngineParams` migration members, fluent setters, and getters.
- Wire local transport parameter generation so `MigrationMode::Active` maps to
  `disable_active_migration = false`; `Passive` and `Disabled` map to `true`.
- Clamp `migrationCIDReserve()` to `LocalActiveCxnIDLimit - 1`.
- Update transport parameter encode/decode/log tests only where defaults or
  public formatting require it.

Files:

- `zquic/src/ZquicTypes.hh`
- `zquic/src/Zquic.hh`
- `zquic/src/ZquicLink.hh`
- `zquic/src/ZquicLog.*`
- `zquic/test/ZquicCodecTest.cc`
- `zquic/test/ZquicLogTest.cc`

Do not:

- change path validation behavior;
- expose `migrate()` yet;
- alter endpoint socket ownership.

Acceptance criteria:

- `Zquic::EngineParams{}.migrationMode()` returns `MigrationMode::Passive`.
- Default local transport params still advertise
  `disable_active_migration = true`.
- `activeMigration(true)` advertises `disable_active_migration = false`.
- `migrationCIDReserve()` cannot exceed `LocalActiveCxnIDLimit - 1`.
- `make -C zquic/src -j8` passes.
- `make -C zquic/test -j8 && make -C zquic/test test` passes.

Next slice may depend on:

- migration vocabulary and policy accessors being stable and available from
  `ZquicLink.hh`, tests, and dependent modules.

### Slice 2: Candidate State Refactor With No Behavior Change

Scope:

- Extend existing `PathState` with migration state/reason fields.
- Rename `m_validatingPath` to `m_migration`.
- Update all existing path validation call sites to use `m_migration`.
- Preserve current peer-address observation, `PATH_CHALLENGE`,
  `PATH_RESPONSE`, path promotion, and timeout behavior.
- Add internal helpers for snapshots and result construction:
  `migrationActive_()`, `activePathInfo_()`, `candidatePathInfo_()`, and
  `migrationResult_()`.

Files:

- `zquic/src/ZquicLink.hh`
- `zquic/test/ZquicAPITest.cc`

Do not:

- add public migration methods;
- change CID selection policy;
- change endpoint sockets.

Acceptance criteria:

- Existing path validation API tests still pass.
- Existing tests that inspect `validatingPath_()`, `validatingRemote_()`, and
  `validatingChallenge_()` are updated only for internal naming and still
  assert the same behavior.
- A new or updated `ZquicAPITest` case proves that passive path validation
  still promotes only after a matching `PATH_RESPONSE`.
- `make -C zquic/src -j8` passes.
- `make -C zquic/test -j8 && make -C zquic/test test` passes.

Next slice may depend on:

- candidate path state being represented by `m_migration` without changing
  externally observable behavior.

### Slice 3: Passive Migration API, Snapshots, And Callbacks

Scope:

- Add public `pathInfo(L &&)` and `migrationState(L &&)` to `CliLink` and
  `SrvLink`.
- Add CRTP base defaults for `migrationStarted()`, `migrationPromoted()`,
  `migrationFailed()`, and `pathUpdate(const PathInfo &)`.
- Replace the old `pathUpdate(const ZiSockAddr &, ...)` and
  `migrationFailure(const ZiSockAddr &, ...)` callback signatures in tests and
  local dependent code.
- Route existing passive path validation through the new callback result
  helpers.

Files:

- `zquic/src/ZquicLink.hh`
- `zquic/src/ZquicCliLink.hh`
- `zquic/src/ZquicSrvLink.hh`
- `zquic/test/ZquicAPITest.cc`

Do not:

- add active `migrate()` entry points;
- change endpoint sockets;
- change CID policy beyond what existing passive validation already does.

Acceptance criteria:

- `pathInfo()` returns an active path snapshot on Tx and through cross-thread
  callback posting.
- During passive validation, `pathInfo()` can report both active and candidate
  path snapshots.
- Callback order for passive success is:
  `migrationStarted()`, `pathUpdate()`, `migrationPromoted()`.
- Callback order for passive timeout is:
  `migrationStarted()`, `migrationFailed()`.
- `make -C zquic/src -j8` passes.
- `make -C zquic/test -j8 && make -C zquic/test test` passes.

Next slice may depend on:

- stable public snapshots and callbacks for path changes, without active local
  migration support.

### Slice 4: CID Policy And Route Refresh

Scope:

- Replace `selectPathCID_()` use with `migrationPeerCID_(PathState &, bool)`.
- Enforce spare-peer-CID requirements for active migration candidates.
- Preserve CID reuse for passive peer/NAT migration where allowed.
- Bind promoted peer CID and clear old associations deterministically.
- Add server-owned Rx route refresh helper for promoted server links.
- Ensure retired local CIDs become route tombstones when late packets can
  arrive.

Files:

- `zquic/src/ZquicLink.hh`
- `zquic/src/ZquicSrvLink.hh`
- `zquic/src/Zquic.hh`
- `zquic/src/Zquic_.hh`
- `zquic/test/ZquicCIDTest.cc`
- `zquic/test/ZquicAPITest.cc`

Do not:

- add endpoint rebinding;
- add active public migration;
- add a second route table.

Acceptance criteria:

- Spare peer CID is selected when active migration requires a new CID.
- Current peer CID is reused only when `requireNewPeerCID == false` or active
  CID length is zero.
- No spare peer CID produces `MigrationReason::NoPeerCID` before packet
  generation.
- Promoted CID becomes the only associated peer CID.
- Server route refresh installs active local CID routes from the server Rx
  thread.
- Retired local CID route becomes tombstone.
- `make -C zquic/src -j8` passes.
- `make -C zquic/test -j8 && make -C zquic/test test` passes.

Next slice may depend on:

- deterministic CID selection/binding and server route ownership for any future
  active migration.

### Slice 5: Endpoint UDP Rebind Primitive

Scope:

- Add `EndpointRebindFn` and `Endpoint_::rebindUDP()`.
- Implement rebind using existing `ZiMultiplex::udp()` / `udp_()`,
  `ZiCxnOptions::udp(true)`, `ZiConnectFn`, `ZiFailFn`, `SockConfig`, and
  `Sock::initUDP()`.
- Preserve old endpoint until new UDP socket is connected.
- Ignore stale completions with generation checks.
- Keep old Tx queue on the old socket; do not migrate queued Tx nodes.

Files:

- `zquic/src/Zquic_.hh`
- `zquic/src/ZquicSock.*`
- `zquic/test/ZquicSockTest.cc` or a new endpoint-focused test file

Do not:

- send `PATH_CHALLENGE` from the rebind primitive;
- mutate link migration state from `Endpoint_`;
- introduce direct system socket calls outside existing `ZiMultiplex` and
  `ZquicSock` code.

Acceptance criteria:

- Rebind success refreshes `Endpoint::local()` and increments endpoint
  generation.
- Rebind failure leaves the old endpoint connected and usable.
- Stale completion from the old generation cannot replace the active endpoint.
- Old queued Tx nodes are not moved to the new socket.
- `make -C zquic/src -j8` passes.
- `make -C zquic/test -j8 && make -C zquic/test test` passes.

Next slice may depend on:

- a reliable local UDP rebind primitive that does not itself know QUIC
  migration protocol state.

### Slice 6: Client Active Migration

Scope:

- Add `CliLink::migrate()`, `migrateLocal()`, and `migrateRemote()`.
- Implement `startMigration_()` and `startMigrationTx_()` for app-requested
  client migration.
- Enforce runtime-established, no-active-candidate, peer-policy, remote tuple,
  and CID checks in the documented order.
- For local rebind, call `Endpoint::rebindUDP()` before queuing
  `PATH_CHALLENGE`.
- Keep ordinary traffic on old active path until promotion.
- Promote only on matching `PATH_RESPONSE`.

Files:

- `zquic/src/ZquicLink.hh`
- `zquic/src/ZquicCliLink.hh`
- `zquic/test/ZquicAPITest.cc`
- `zquic/test/ZquicLoopTest.cc`

Do not:

- add server accepted-link local rebinding;
- reconnect as a substitute for migration;
- discard stream, crypto, ACK, packet number, or TLS state.

Acceptance criteria:

- `CliLink::migrateLocal()` changes the UDP source port on loopback.
- Existing stream data continues across promotion on the same QUIC connection.
- App-requested migration is rejected when peer
  `disable_active_migration == true`.
- No-spare-CID rejection happens before packet generation.
- New app migration request while candidate is active is rejected and does not
  replace the active candidate.
- `make -C zquic/src -j8` passes.
- `make -C zquic/test -j8 && make -C zquic/test test` passes.

Next slice may depend on:

- working client active migration with validated promotion and policy
  rejection.

### Slice 7: Passive Peer/NAT Migration Hardening

Scope:

- Apply `MigrationMode::Disabled`, `Passive`, and `Active` policy to peer tuple
  observations.
- Classify `NATRebind` when matched CID data is available; otherwise preserve
  `PeerAddrChange` and leave a test-covered extension point.
- Ensure passive migration does not require a fresh peer CID.
- Ensure anti-amplification and path byte accounting apply to the observed
  candidate path.
- Verify server route refresh and remote printable address updates after
  promotion.

Files:

- `zquic/src/ZquicLink.hh`
- `zquic/src/ZquicSrvLink.hh`
- `zquic/src/Zquic.hh`
- `zquic/test/ZquicAPITest.cc`
- `zquic/test/ZquicLoopTest.cc`

Do not:

- promote on observation alone;
- reject NAT rebinding because active migration is disabled locally;
- add locks around route or path state.

Acceptance criteria:

- `MigrationMode::Disabled` ignores new peer tuples and records a reject
  counter.
- `MigrationMode::Passive` validates and promotes peer tuple changes.
- Passive peer tuple migration works without a spare peer CID.
- Server `peer()` / printable remote state updates after promotion.
- Candidate path anti-amplification counters reflect candidate traffic.
- `make -C zquic/src -j8` passes.
- `make -C zquic/test -j8 && make -C zquic/test test` passes.

Next slice may depend on:

- passive migration and NAT-rebind behavior being correct for HTTP/3 server
  use.

### Slice 8: Failure, PMTUD, ECN, Diagnostics, And qlog

Scope:

- Implement `abandonMigration_()` and timeout behavior.
- Reset promoted-path PMTUD to `MinUDPPayload` and let existing DPLPMTUD
  rediscover size.
- Scope ECN validation state to the promoted path.
- Reset congestion state for the promoted path.
- Add `MigrationDiag` and update runtime diagnostic snapshots.
- Add qlog migration/path/CID events using enum reasons.
- Make qlog a full event source for migration: the qlog stream must be enough
  to reconstruct each migration attempt, candidate path, validation result,
  CID decision, endpoint rebind result, promotion, abandonment, or close.

Files:

- `zquic/src/ZquicLink.hh`
- `zquic/src/ZquicPath.hh`
- `zquic/src/ZquicRecovery.hh`
- `zquic/src/ZquicLog.*`
- `zquic/src/Zquic.hh`
- `zquic/test/ZquicPMTUDTest.cc`
- `zquic/test/ZquicLogTest.cc`
- `zquic/test/ZquicLoopTest.cc`

Do not:

- format qlog JSON on Rx/Tx threads;
- capture path objects, `this`, stream objects, payload, or key material in
  qlog lambdas;
- create duplicate PMTUD or ECN state outside `Path`.

Acceptance criteria:

- Timeout abandons the candidate and leaves old active path usable.
- Local-rebind failure behavior follows `closeOnFailure`.
- Promoted path starts PMTUD from `MinUDPPayload`.
- ECN state and counters are path-scoped after promotion.
- Runtime diagnostics expose migration requested/started/promoted/failed
  counters.
- qlog records every migration state-machine transition listed in
  `qlog Event Sourcing Contract`.
- qlog records enough fields to correlate one migration attempt across path,
  CID, endpoint rebind, PMTUD, ECN, and close events.
- `ZquicLogTest` parses JSON-SEQ and verifies ordered migration success,
  timeout, policy reject, no-CID reject, local-rebind failure, and passive
  peer-address migration event streams.
- `make -C zquic/src -j8` passes.
- `make -C zquic/test -j8 && make -C zquic/test test` passes.

Next slice may depend on:

- transport migration being observable, debuggable, and failure-complete.

### Slice 9: Zhttp Transport-Neutral Hooks And QUIC Policy Wiring

Scope:

- Add transport-neutral hook templates to `Zhttp::H3::Cxn`.
- Keep `libZhttp.la` independent of `libZquic.la`.
- Add optional `ZhttpH3Quic.hh` adapter only if shared policy helpers are used.
- Wire QUIC migration policy directly in `zhttp/test/zhttp.cc` and
  `zhttp/test/zhttpd.cc`, or through the optional adapter.
- Implement H3 link callbacks that forward migration/path events to H3 session
  state without changing QPACK/request state.

Files:

- `zhttp/src/ZhttpH3Session.hh`
- optional `zhttp/src/ZhttpH3Quic.hh`
- optional `zhttp/src/Makefile.am` header list only
- `zhttp/test/zhttp.cc`
- `zhttp/test/zhttpd.cc`
- `zhttp/test/Zhttpd.hh`

Do not:

- include `Zquic.hh` from core `Zhttp` headers;
- link `libZhttp.la` against `libZquic.la`;
- reset H3 control, QPACK, request, or GOAWAY state on path promotion.

Acceptance criteria:

- `make -C zhttp/src -j8` passes without adding `libZquic.la` to
  `libZhttp_la_LIBADD`.
- `zhttp` and `zhttpd` accept `--quic-migration`,
  `--quic-migration-cid-reserve`, and
  `--quic-migration-close-on-failure`.
- H3 callbacks compile and do not alter QPACK tables or active streams.
- `make -C zhttp/test -j8 && make -C zhttp/test test` passes.

Next slice may depend on:

- HTTP/3 code being able to observe and configure QUIC migration without adding
  a core library dependency.

### Slice 10: HTTP/3 Migration Tests, Matrix, And Docs

Scope:

- Add deterministic H3 migration tests.
- Add synthetic/faked/simulated migration tests at the transport and H3 layers
  before relying on external interop.
- Add `zhttpmatrix` migration flag pass-through and matrix rows with packet
  drop.
- Add curl/caddy interop migration scenarios where the existing
  `haveCurlH3()` / `haveCaddy()` checks say the tools are available.
- Update `zquic/README.md` and `zhttp/README.md`.
- Run full debug and release validation.

Files:

- `zhttp/test/ZhttpdTest.cc` or `zhttp/test/Zhttp3InteropTest.cc`
- `zhttp/test/zhttpmatrix.cc`
- `zquic/README.md`
- `zhttp/README.md`

Do not:

- add new transport behavior in this slice;
- weaken earlier transport tests to make H3 pass.

Acceptance criteria:

- Static H3 response survives client local-port migration.
- Large H3 response migrates mid-body with exactly one request completion.
- Server diagnostics remote address updates after migration promotion.
- Peer-disabled active migration is rejected without corrupting H3 state.
- `zhttpmatrix` can run zhttp-to-zhttpd migration scenarios with QUIC packet
  drop enabled.
- `zhttpmatrix` can run zhttp-to-caddy H3 client-migration scenarios when
  `haveCaddy()` is true.
- `zhttpmatrix` can run curl-to-zhttpd passive peer/NAT migration scenarios
  only when curl exposes a usable migration/rebinding capability; otherwise
  the matrix must skip these cases explicitly and keep the zhttp-driven
  migration cases mandatory.
- `make -C zhttp/test -j8` passes.
- `make -C zhttp/test test` passes.
- Full validation passes:

```sh
make -j8
make test
```

- Release validation after `./z.config` and `make clean` passes with:

```sh
make -j8
```

Next slice may depend on:

- no later slice is required; this is the merge-ready acceptance point.

## Dependency Audit

Validate these dependencies before coding and keep the implementation inside
existing Z framework boundaries:

- `zquic` already depends on `zi`, `zt`, `zm`, and `zu`; use those libraries
  directly.  Do not introduce STL containers, `std::function`, `std::string`,
  standalone socket wrappers, or a new event loop.
- UDP socket creation/recreation must use `ZiMultiplex::udp()` /
  `ZiMultiplex::udp_()` with `ZiCxnOptions::udp(true)`, `ZiConnectFn`, and
  `ZiFailFn`.  Do not call `socket()`, `bind()`, or `connect()` directly from
  `zquic` migration code; those are already centralized in `ZiMultiplex`.
- UDP send/receive must continue through `ZiConnection::send()`,
  `ZiConnection::recv()`, and `ZiIOContext`.  Do not add a parallel send queue
  outside `Zquic_::Endpoint_::Cxn_`.
- Endpoint callbacks must use `ZmFn` with module-specific heap IDs, following
  `EndpointDiscFn`, `ZiConnectFn`, and `ZiFailFn`.
- Connection lifetime must continue using `ZmRef<ZiConnection>`/`ZmRef<Cxn_>`
  and the existing endpoint generation checks.  Do not add raw owning pointers
  or shared ownership wrappers.
- Timers must remain `ZmScheduler::Timer` instances owned by `ZquicLink`.  Do
  not add polling, sleeps, or timer scans.
- Path mechanics must stay in the existing `Path` type in
  `zquic/src/ZquicPath.hh`.  Use its anti-amplification, PMTUD, ECN, and
  diagnostics methods instead of duplicating path counters.
- CID storage must continue using the existing `ZtArray<LinkCID,
  ZtArrayHeapID<...>>` local and peer arrays plus `CxnRouter`.  Do not add a
  second route table or hash just for migration.
- Packet buffers must remain `ZiIOBuf` objects allocated by `PktRxBufAlloc<>`
  and `PktTxBufAlloc<>`.  Do not add staging buffers for migration packets.
- qlog work must use existing `ZquicLOG`, `ZquicLog.hh`, `ZquicLog.cc`,
  `ZtEnumMap`, `ZtStruct`, and `ZtJSON` patterns.  Do not format JSON on Rx/Tx
  threads.
- `Zhttp` core currently does not link `libZquic.la` and its library Makefile
  does not include `zquic/src`.  Therefore `zhttp/src/Zhttp.hh`,
  `ZhttpH3.hh`, `ZhttpH3Session.hh`, and `ZhttpQPack.hh` must not include
  `<zlib/Zquic.hh>` or name `Zquic::*` types.  Put QUIC-specific policy wiring
  in `zhttp`/`zhttpd` integration code or in a separate optional adapter header
  that explicitly includes `Zquic.hh`.
- If a future change intentionally makes `libZhttp` depend on `libZquic`, add
  `-I$(top_srcdir)/zquic/src` and `$(top_builddir)/zquic/src/libZquic.la` in
  `zhttp/src/Makefile.am` in the same commit and document why the core library
  dependency is now required.
- No new external packages are required.  Do not edit `configure.ac`, `m4/`, or
  top-level dependency checks for migration unless implementation discovery
  proves a missing platform socket capability that is not already wrapped by
  `ZiMultiplex` or `ZquicSock`.

## Public Zquic Types

Add the following to `zquic/src/ZquicTypes.hh` near the existing path/protocol
vocabulary.  Use `ZtEnum`/`ZtEnumMap` style, not `enum class`.

```c++
struct PathRole {
  ZtEnum(PathRole, int8_t, Active, Candidate, Previous);
  ZtEnumMap(PathRole, JSON,
    "active", "candidate", "previous", "unknown");
};

struct MigrationState {
  ZtEnum(MigrationState, int8_t,
    Idle, Requested, Validating, Promoted, Failed);
  ZtEnumMap(MigrationState, JSON,
    "idle", "requested", "validating", "promoted", "failed", "unknown");
};

struct MigrationReason {
  ZtEnum(MigrationReason, int8_t,
    None, Passive, NATRebind, Active, Disabled, PeerDisabled, NoPeerCID,
    Endpoint, Validation, Timeout, Abandoned, Closed);
  ZtEnumMap(MigrationReason, JSON,
    "none", "passive", "nat_rebind", "active", "disabled",
    "peer_disabled", "no_peer_cid", "endpoint", "validation", "timeout",
    "abandoned", "closed", "unknown");
};

struct MigrationMode {
  ZtEnum(MigrationMode, int8_t, Disabled, Passive, Active);
  ZtEnumMap(MigrationMode, JSON,
    "disabled", "passive", "active", "unknown");
};
```

Add concrete value structs in the same header after `PMTUDState` and
`PathECNState` are declared.  Keep them trivially movable and avoid heap-owned
strings.

```c++
struct PathInfo {
  ZiSockAddr		local;
  ZiSockAddr		remote;
  PathRole::T		role = PathRole::None;
  MigrationState::T	migration = MigrationState::Idle;
  MigrationReason::T	reason = MigrationReason::None;
  PathECNState::T	ecnState = PathECNState::Disabled;
  PMTUDState::T		pmtudState = PMTUDState::Base;
  uint64_t		antiAmplification = 0;
  uint64_t		peerCIDSeq = uint64_t(-1);
  unsigned		activeMaxUDP = MinUDPPayload;
  bool			validated = false;
};

struct MigrationParams {
  ZiSockAddr		local;
  ZiSockAddr		remote;
  MigrationReason::T	reason = MigrationReason::Active;
  bool			rebindLocal = false;
  bool			requireNewPeerCID = true;
  bool			closeOnFailure = false;
};

struct MigrationResult {
  ZiSockAddr		local;
  ZiSockAddr		remote;
  MigrationState::T	state = MigrationState::Idle;
  MigrationReason::T	reason = MigrationReason::None;
  uint64_t		peerCIDSeq = uint64_t(-1);
  unsigned		activeMaxUDP = MinUDPPayload;
  bool			validated = false;
};
```

Use the existing `PMTUDState::Base` initial value.  Do not invent a second
PMTUD enum for path info.

## Public Zquic Params

Modify `EngineParams` in `zquic/src/Zquic.hh` and transport-param setup in
`ZquicLink.hh`.

Add private members using existing `EngineParams` naming:

```c++
MigrationMode::T	m_migrationMode = MigrationMode::Passive;
unsigned		m_migrationCIDReserve = 1;
bool			m_migrationCloseOnFailure = false;
```

Add fluent setters and getters:

```c++
EngineParams &&migration(MigrationMode::T);
EngineParams &&activeMigration(bool b = true); // Active when true, Passive when false
EngineParams &&migrationCIDReserve(unsigned);
EngineParams &&migrationCloseOnFailure(bool b = true);
MigrationMode::T migrationMode() const;
unsigned migrationCIDReserve() const;
bool migrationCloseOnFailure() const;
```

Mapping rules:

- `MigrationMode::Disabled`: advertise `disable_active_migration = true`;
  do not start passive validation for peer address changes; packets from a new
  peer tuple are ignored except stateless reset handling.
- `MigrationMode::Passive`: advertise `disable_active_migration = true`;
  accept and validate peer/NAT address changes, but reject local
  application-requested migration.
- `MigrationMode::Active`: advertise `disable_active_migration = false`;
  allow application-requested migration unless the peer disabled it.

In `configureLocalTransportParams_()` set:

```c++
m_transportParams.disableActiveMigration =
  app->migrationMode() != MigrationMode::Active;
m_transportParams.activeCxnIDLimit = LocalActiveCxnIDLimit;
```

Clamp `migrationCIDReserve()` to `LocalActiveCxnIDLimit - 1` in the setter.
Use it as a low-water mark for issuing spare local CIDs, not as an increase to
the advertised limit.  `LocalActiveCxnIDLimit` is already the local advertised
policy cap and the existing `ZtArray<LinkCID, ...>` storage grows up to that
limit.

## Internal State

In `zquic/src/ZquicLink.hh`, extend the existing `PathState` rather than
adding a parallel container.  Keep the type name `PathState` unless every
existing reference is renamed in the same patch.  Rename the member
`m_validatingPath` to `m_migration` and update all existing references in one
pass.

```c++
struct PathState {
  Path			path;
  Path			prev;
  CxnID			peerCID;
  uint64_t		peerSeq = uint64_t(-1);
  ZuTime		deadline;
  PathChallenge		challenge;
  MigrationState::T	state = MigrationState::Idle;
  MigrationReason::T	reason = MigrationReason::None;
  bool			active = false;
  bool			localRebind = false;
  bool			closeOnFailure = false;
};
```

Rules:

- `m_path` remains the active path.
- `m_migration.path` is the candidate path.
- `prev` is a value snapshot of the previous active path.
- Only Tx code mutates `m_path` or `m_migration`.
- Rx code may observe datagram tuples and post compact tuple snapshots to Tx.
- Clear the candidate by assigning `{}` only after callbacks have received a
  value `MigrationResult`.

Add helpers in `ZquicLink.hh`:

```c++
bool migrationActive_() const;
PathInfo activePathInfo_() const;
PathInfo candidatePathInfo_() const;
MigrationResult migrationResult_(MigrationState::T, MigrationReason::T) const;
bool startMigration_(const MigrationParams &);
bool startMigrationTx_(MigrationParams);
void abandonMigration_(MigrationReason::T);
void promoteMigration_();
bool migrationPeerCID_(PathState &, bool requireNew);
void notifyMigrationStarted_(const MigrationResult &);
void notifyMigrationPromoted_(const MigrationResult &);
void notifyMigrationFailed_(const MigrationResult &);
```

`startMigration_()` is the public-thread wrapper and must post to Tx when the
caller is not already on Tx.  `startMigrationTx_()` contains all protocol state
mutation and must assert `txInvoked_()`.

## State Machine

Implement exactly this transition table:

| Current | Event | New State | Action |
| --- | --- | --- | --- |
| `Idle` | app request accepted | `Requested` | build candidate and perform local rebind when requested |
| `Idle` | peer tuple observed | `Requested` | build candidate without local rebind |
| `Requested` | challenge queued | `Validating` | arm path timer, callback started |
| `Requested` | policy/CID/socket failure | `Failed` | callback failed, clear candidate |
| `Validating` | matching `PATH_RESPONSE` | `Promoted` | promote, bind CID, callback promoted |
| `Validating` | timeout | `Failed` | keep old active path, callback failed with reason `Timeout` |
| `Validating` | connection close | `Failed` | clear candidate, no path promotion |
| any non-idle | new app request | unchanged | reject with `MigrationReason::Validation` |
| any non-idle | same candidate observed | unchanged | count duplicate observation |

After `Promoted`, immediately clear `m_migration` to `Idle` after callbacks and
route updates complete.  After `Failed`, clear it after callbacks and fallback
handling complete.

## Passive Migration Refactor

Rewrite `observePathRxTx_()` so it does not directly own path-validation logic.
It must:

1. Assert Tx.
2. Drop null remotes and same-remotes as today.
3. If migration mode is `Disabled`, increment a new reject counter and return.
4. If the tuple matches `m_migration.path.remote()`, increment the existing
   active-validation counter and return.
5. Build:

```c++
MigrationParams params;
params.local = ZuMv(local);
params.remote = ZuMv(remote);
params.reason = MigrationReason::Passive;
params.rebindLocal = false;
params.requireNewPeerCID = false;
params.closeOnFailure = false;
```

6. Call `startMigrationTx_(ZuMv(params))`.

Classify `NATRebind` instead of `Passive` only when the packet uses the
currently active DCID and only the UDP source tuple changed.  If that
distinction is not locally available at first, implement `Passive` first and
add `NATRebind` only when route code passes the matched CID into link
receive.

## Active Migration API

Extend `zquic/src/ZquicCliLink.hh`:

```c++
bool migrate(const MigrationParams &params) {
  if (Base::disconnecting_()) return false;
  return Base::startMigration_(params);
}

bool migrateLocal(ZiIP ip, uint16_t port = 0) {
  MigrationParams params;
  params.local.init(ip, port);
  params.remote = Base::activePathRemote_();
  params.reason = MigrationReason::Active;
  params.rebindLocal = true;
  return migrate(params);
}

bool migrateRemote(ZiSockAddr remote) {
  MigrationParams params;
  params.local = Endpoint::local();
  params.remote = ZuMv(remote);
  params.reason = MigrationReason::Active;
  return migrate(params);
}
```

Extend `zquic/src/ZquicSrvLink.hh` with `migrateRemote()` and `pathInfo()`.
Do not add `migrateLocal()` for server accepted links in the first pass unless
`Endpoint_` supports per-link connected UDP sockets.  Server-side local rebinding
of accepted links is explicitly phase 2.

Add async snapshots to both link classes:

```c++
template <typename L>
void pathInfo(L &&l) const;

template <typename L>
void migrationState(L &&l) const;
```

These must follow the existing `runtimeDiag()` pattern: if already on Tx,
invoke immediately with value snapshots; otherwise `txRun()` and invoke the
callback on Tx.

Add base defaults in `ZquicLink.hh`:

```c++
void migrationStarted(const MigrationResult &) { ++m_txDiag.unhandledAppEvents; }
void migrationPromoted(const MigrationResult &) { ++m_txDiag.unhandledAppEvents; }
void migrationFailed(const MigrationResult &) { ++m_txDiag.unhandledAppEvents; }
void pathUpdate(const PathInfo &) { ++m_txDiag.unhandledAppEvents; }
```

Replace the old `pathUpdate(const ZiSockAddr &, ...)` and
`migrationFailure(const ZiSockAddr &, ...)` callback signatures.  Update tests
and HTTP code in the same commit series.

## Endpoint Rebinding

Modify `Zquic_::Endpoint_` in `zquic/src/Zquic_.hh`.

Add:

```c++
using EndpointRebindFn =
  ZmFn<void(bool, ZiSockAddr), ZmFnHeapID<"Zquic.Endpoint.RebindFn">>;

bool rebindUDP(
  PathMode::T mode, ZiSockAddr local, ZiSockAddr remote, EndpointRebindFn);
```

Implementation rules:

- `rebindUDP()` is an `Endpoint_` orchestration helper, not a new socket
  subsystem.  It must call `m_mx->udp()` or `m_mx->udp_()` with
  `ZiCxnOptions options; options.udp(true);`, just as `openUDP()` does.
- `rebindUDP()` may be called from any thread.  If not already on the Rx
  thread, it posts to Rx before mutating endpoint state.
- Do not mutate `m_local`, `m_remote`, `m_sockConfig`, or `m_generation` until
  the new socket has connected successfully.
- Keep the old `CxnRef m_cxn` alive until the new socket is connected.  Mark it
  closing only after the new socket is ready, using existing `Cxn_::disconnect`
  / close paths.
- If open fails, keep the old socket active and invoke callback with
  `(false, {})`.
- If open succeeds, increment `m_generation`, swap in the new `Cxn_`, close the
  old one, refresh `m_local` with `getsockname()`, and invoke callback with
  `(true, m_local)`.
- Stale completions must be ignored by checking the captured generation.
- Tx queue policy: do not move old queued Tx nodes to the new socket.  Migration
  validation packets are generated after the new socket is ready.  Normal
  traffic stays on the old active path until promotion.
- Use `Sock::initUDP()` and `SockConfig` for the new socket.  Do not duplicate
  socket-option setup from `ZquicSock.cc`.

For client active local migration, `CliLink::startMigrationTx_()` must call
`Endpoint::rebindUDP()` first and continue candidate setup in the rebind
callback.  Do not queue `PATH_CHALLENGE` before the new socket is ready.

## Candidate Setup

`startMigrationTx_()` must perform checks in this order:

1. `runtimeEstablished_()` must be true.
2. `m_migration.active` must be false.
3. `params.remote` must be non-null and not equal to the active remote unless
   `params.rebindLocal` is true.
4. If `params.reason` is app/local/remote/socket request and peer
   `disable_active_migration` is true, fail with
   `MigrationReason::PeerDisabled`.
5. Select peer CID with `migrationPeerCID_()` using the existing peer CID
   `ZtArray`; do not allocate a temporary CID container.
6. Build candidate path:

```c++
m_migration = {};
m_migration.prev = m_path;
m_migration.path = m_isServer ?
  Path::server(params.local, params.remote) :
  Path::client(params.local, params.remote);
initPathECN_(m_migration.path);
m_migration.path.configuredMaxUDP(m_path.configuredMaxUDP());
m_migration.path.peerMaxUDP(m_path.peerMaxUDP());
m_migration.reason = params.reason;
m_migration.localRebind = params.rebindLocal;
m_migration.closeOnFailure = params.closeOnFailure;
m_migration.state = MigrationState::Requested;
m_migration.active = true;
```

7. Generate challenge.  If generation fails, clear and fail with
   `MigrationReason::Validation`.
8. Set deadline from `pathValidDeadline_()`.
9. Queue one `PATH_CHALLENGE`.
10. Schedule path timer.
11. Set state to `Validating`.
12. Call `migrationStarted()`.
13. Flush to `m_migration.path.remote()`.

Do not copy stream data, crypto data, ACK state, packet number state, or QPACK
state during candidate setup.

## PATH_RESPONSE Handling

Update `onPathResponse_()`:

- If no active migration candidate exists, increment `pathResponseUnknown` and
  return false.
- If challenge mismatches, increment `pathResponseUnknown`, log qlog
  `ResponseMismatch`, and return false.
- If challenge matches, call `promoteMigration_()` and return true.

`promoteMigration_()` must:

1. Assert Tx.
2. Copy candidate `Path` to `m_path`.
3. Mark `m_path.validated()`.
4. Bind the selected peer CID with `bindPromotedCID_()`.
5. Reset path PMTUD to `MinUDPPayload` and let the existing DPLPMTUD probe
   path rediscover the usable payload size.
6. Reset or reinitialize congestion state for the new path.  Use the existing
   `NewReno{app()->maxUDP()}` initialization and update congestion diagnostics.
7. Cancel the path timer.
8. Call `impl()->pathPromoted_()`.
9. For server links, post an Rx-thread route refresh to the owning `Server`
   that calls the existing `installRoutes_(m_routes)` path before old CID
   tombstones are relied on.
10. Call `impl()->pathUpdate(activePathInfo_())`.
11. Call `impl()->migrationPromoted(result)`.
12. Clear `m_migration`.
13. Flush normal Tx on the new active path.

Do not discard TLS keys, stream state, or packet number spaces.

## Failure Handling

`abandonMigration_(reason)` must:

- Assert Tx.
- Return immediately if no migration is active.
- Cancel the path timer.
- If `m_migration.localRebind` and the old endpoint is unavailable:
  - if `m_migration.closeOnFailure`, close the connection;
  - otherwise close the connection with a local migration failure because
    transparent fallback is impossible.
- If the old endpoint is still active, leave `m_path` unchanged.
- Call `impl()->migrationFailed(result)`.
- Clear `m_migration`.
- Flush pending active-path Tx if any was blocked by validation.

`pathExpired_()` must call `abandonMigration_(MigrationReason::Timeout)`.

## CID Lifecycle

Modify `selectPathCID_()` into a stricter helper used by migration:

```c++
bool migrationPeerCID_(PathState &m, bool requireNew);
```

Rules:

- Prefer an active, unassociated peer CID.
- If `requireNew` is false, allow current `m_peerCID`.
- If no CID is available and the active peer CID length is zero, allow reuse.
- If no CID is available and a new CID is required, fail migration with
  `NoPeerCID`.
- On promotion, mark all peer CIDs unassociated, then mark the promoted peer
  CID associated and update `m_peerCID`.
- After promotion, send `RETIRE_CONNECTION_ID` for old peer CIDs that are
  retired by `retire_prior_to` or local migration policy.  Do not retire the
  active promoted CID.

Server route handling:

- `SrvLink::pathPromoted_()` must update `m_peerAddr` from
  `Base::activePathRemote_()`.
- After promotion, add a server-owned Rx helper:

```c++
void refreshLinkRoutes_(Link *link) {
  if (this->state() == ZmEngineState::Running && link)
    link->installRoutes_(m_routes);
}
```

  `SrvLink::pathPromoted_()` must request this helper through the owning
  server's Rx thread.  Do not let `ZquicLink` access `Server::m_routes`
  directly from Tx.
- Retired local CIDs must become tombstones, not simple deletes, when late
  packets may still arrive.

## Diagnostics

Add `MigrationDiag` to `zquic/src/Zquic.hh`:

```c++
struct MigrationDiag {
  uint64_t	requested = 0;
  uint64_t	started = 0;
  uint64_t	promoted = 0;
  uint64_t	abandoned = 0;
  uint64_t	failed = 0;
  uint64_t	policyReject = 0;
  uint64_t	noPeerCID = 0;
  uint64_t	endpointFailure = 0;
  uint64_t	timeouts = 0;
  uint64_t	peerObserved = 0;
  uint64_t	natRebind = 0;
  uint64_t	localRebindOK = 0;
  uint64_t	localRebindFail = 0;
};
```

Add it as `RuntimeTxDiag::migration` or a top-level `RuntimeDiag::migration`.
Prefer Tx diag because migration state is Tx-owned.  Update diagnostic printing
in `zhttp/test/zhttp.cc` and `zhttp/test/zhttpd.cc`.

Keep existing counters such as `pathValidationStarted` for local debugging.
New tests must assert the migration counters.

## qlog Event Sourcing Contract

Migration qlog is not just diagnostic decoration.  It must be a complete event
source for the migration state machine.  Given one qlog JSON-SEQ file, a test
or operator must be able to replay every migration attempt and answer:

- why the attempt started;
- which active path was current before the attempt;
- which candidate tuple was selected;
- whether a local endpoint rebind was requested and whether it succeeded;
- which peer CID was selected or why CID selection failed;
- which `PATH_CHALLENGE` was sent and whether a matching response arrived;
- whether promotion, timeout, abandonment, policy rejection, no-CID rejection,
  endpoint failure, or connection close ended the attempt;
- which path became active after promotion;
- which PMTUD, ECN, route, and CID updates were caused by the attempt.

### qlog Types

Extend qlog enums in `zquic/src/ZquicLog.hh` and writers in
`zquic/src/ZquicLog.cc`.  Reuse existing `PathEvent` and `CIDEvent` for path
and CID details, but add one migration-specific event type instead of trying to
encode all state-machine transitions as generic path updates.

Add:

```c++
struct MigrationAction {
  ZtEnum(MigrationAction, int8_t,
    Requested, Rejected, Started, RebindStart, RebindOK, RebindFail,
    CIDSelected, CIDUnavailable, ChallengeQueued, ResponseMatched,
    ResponseMismatch, Promoted, Abandoned, Failed, Closed);
  ZtEnumMap(MigrationAction, JSON,
    "requested", "rejected", "started", "rebind_start", "rebind_ok",
    "rebind_fail", "cid_selected", "cid_unavailable", "challenge_queued",
    "response_matched", "response_mismatch", "promoted", "abandoned",
    "failed", "closed", "unknown");
};

struct MigrationEvent {
  LinkInfo		linkInfo;
  ZiSockAddr		activeLocal;
  ZiSockAddr		activeRemote;
  ZiSockAddr		candidateLocal;
  ZiSockAddr		candidateRemote;
  uint64_t		attemptID = 0;
  uint64_t		peerCIDSeq = uint64_t(-1);
  uint64_t		deadlineUS = 0;
  uint32_t		mtu = 0;
  MigrationAction::T	action = MigrationAction::Requested;
  MigrationReason::T	reason = MigrationReason::None;
  MigrationState::T	state = MigrationState::Idle;
  PathRole::T		pathRole = PathRole::None;
  bool			localRebind = false;
  bool			validated = false;
  bool			closeOnFailure = false;
};
```

Add a qlog event name such as `MigrationUpd` in `EventName` and map it to a
stable JSON event name under the existing zquic qlog namespace, for example
`"zquic:migration_updated"`.  Do not use arbitrary string names at call sites.
Extend existing qlog enums as needed:

- add `PathReason::NATRebind`;
- add path reasons for `Active`, `Endpoint`, `PeerDisabled`, `NoPeerCID`, and
  `Validation` when those
  reasons are emitted on path events;
- add `CIDReason::Migration` or more specific CID reasons when a CID event is
  migration-caused but not simply `PathPromoted`;
- keep enum names aligned with `MigrationReason` so tests can compare qlog
  strings deterministically.

The qlog writer must serialize `MigrationReason`, `MigrationState`, and
`PathRole` through `ZtEnumMap`.  If those public enums are not directly usable
from `ZquicLog_`, add qlog-local enum maps with explicit conversion helpers on
the logger thread.  Do not capture string versions of reasons or states.

### Attempt Identity

Add a Tx-owned monotonically increasing `m_migrationAttemptID` to `ZquicLink`.
Increment it exactly once for each accepted migration attempt before any qlog
`MigrationAction::Started` event is emitted.  Use the same `attemptID` in:

- all migration qlog events for that attempt;
- path validation qlog events emitted for the candidate path;
- CID binding/retirement qlog events caused by that attempt;
- PMTUD/ECN reset qlog events caused by promotion;
- close/failure qlog events caused by migration failure.

Do not use wall-clock time or tuple fields as the correlation key.

### Required Migration Event Stream

Emit the following qlog events for every path migration path.  "Migration event"
means `MigrationEvent`.  "Path event" means existing `PathEvent`.  "CID event"
means existing `CIDEvent`.

Accepted app request:

1. Migration event: `Requested`, reason `Active`.
2. Migration event: `Started`, state `Requested`, with previous active and
   candidate tuples.
3. If local rebind is requested: `RebindStart`, then `RebindOK` or
   `RebindFail`.
4. Migration event: `CIDSelected` or `CIDUnavailable`.
5. Migration event: `ChallengeQueued`.
6. Path event: `PathValid` `ChallengeTx` with the same `attemptID`.

Passive peer tuple observation:

1. Path event: `Path` `Observed`, reason `PeerAddrChange` or `NATRebind`.
2. Migration event: `Started`, reason `Passive` or `NATRebind`.
3. Migration event: `CIDSelected` when a peer CID is selected or reused.
4. Migration event: `ChallengeQueued`.
5. Path event: `PathValid` `ChallengeTx`.

Policy rejection:

1. Migration event: `Requested`.
2. Migration event: `Rejected`, reason `PeerDisabled`.
3. No path challenge, no candidate promotion, no CID bind event.

No-peer-CID rejection:

1. Migration event: `Requested`.
2. Migration event: `Started`.
3. Migration event: `CIDUnavailable`, reason `NoPeerCID`.
4. Migration event: `Failed`, reason `NoPeerCID`.
5. No path challenge.

Local endpoint rebind failure:

1. Migration event: `Requested`.
2. Migration event: `Started`.
3. Migration event: `RebindStart`.
4. Migration event: `RebindFail`, reason `Endpoint`.
5. Migration event: `Failed` or `Closed`, depending on fallback policy.

Successful validation and promotion:

1. Path event: `PathValid` `ResponseRx`, reason `Matched`.
2. Migration event: `ResponseMatched`, state `Validating`.
3. CID event: `RouteBound`, reason `PathPromoted`, with the same `attemptID`.
4. Path event: `PathValid` `Validated`, reason `Response`.
5. Path event: `Path` `Updated`, reason `Promoted`.
6. Migration event: `Promoted`, state `Promoted`, with active tuple equal to
   the promoted candidate tuple.
7. PMTUD path event showing MTU reset/restart for the promoted path.
8. ECN event showing path ECN state reinitialization when ECN is enabled.

Response mismatch:

1. Path event: `PathValid` `ResponseUnk`, reason `Mismatch`.
2. Migration event: `ResponseMismatch`, reason `Validation`.
3. The candidate remains active until a later matched response or timeout.

Timeout / abandonment:

1. Path event: `PathValid` `Failed`, reason `Timeout`.
2. Migration event: `Abandoned`, reason `Timeout`, with active tuple still set
   to the previous active path and candidate tuple set to the failed candidate.
3. Migration event: `Failed`, reason `Timeout`, with the same `attempt_id`.
4. No promoted path event.

Connection close caused by migration:

1. Migration event: `Closed`, reason `Endpoint`, `Timeout`, or `Closed`.
2. Existing close qlog event with a migration reason or trigger field that can
   be correlated by `attemptID`.

### Required Fields

Every migration qlog event must include:

- `linkInfo`;
- `attempt_id`;
- `action`;
- `state`;
- `reason`;
- active local and remote tuple when available;
- candidate local and remote tuple when available;
- selected `peer_cid_sequence` when available;
- `local_rebind`;
- `validated`;
- `close_on_failure`.

Every path/CID/PMTUD/ECN qlog event caused by migration must include the same
`attempt_id`.  If adding `attemptID` to the existing event structs would bloat
non-migration hot paths, add it as an optional zero/default field and only
populate it inside `ZquicLOG` captures for migration call sites.

### qlog Call-Site Rules

- Emit qlog from the exact state transition function that mutates the state.
  Do not log inferred transitions later from callbacks.
- Keep all qlog-only work inside `ZquicLOG(...)`.
- Capture only scalar IDs, enum values, addresses, CID sequence numbers, MTU,
  anti-amplification credit, and booleans.
- Do not capture `this`, `Path`, `PathState`, `LinkCID`, stream objects,
  packet buffers, payload, TLS secrets, tokens, or references.
- Do not format JSON, reason strings, CID strings, or address strings on Tx/Rx.
  The logger thread owns enum-to-string and JSON serialization.
- Do not make qlog required for protocol behavior.  Release builds with qlog
  compiled out must execute the same migration state machine.

### Automated qlog Tests

Add automated tests in `zquic/test/ZquicLogTest.cc` and runtime qlog tests in
`zquic/test/ZquicLoopTest.cc`.

`ZquicLogTest.cc` must directly exercise logger serialization:

- `migration qlog serializes all actions`;
- `migration qlog emits enum reasons and states`;
- `migration qlog includes attempt_id on migration, path, CID, PMTUD, and ECN
  events`;
- `migration qlog JSON-SEQ parses after every new event`;
- `migration qlog does not emit empty action/state/reason strings`.

Loopback/runtime qlog tests must enable qlog to a temporary `.sqlog`, perform
real migration flows, parse JSON-SEQ with the existing test helpers, and assert
ordered event streams for:

- successful client `migrateLocal()`;
- passive peer-address migration;
- policy rejection from peer `disable_active_migration`;
- no-peer-CID rejection;
- local endpoint rebind failure;
- path validation timeout;
- response mismatch followed by later success or timeout.

Each runtime qlog test must assert:

- the event stream contains exactly one `attempt_id` per migration attempt;
- all migration-caused path/CID/PMTUD/ECN events carry that `attempt_id`;
- success streams contain `Requested` or `Started`, `ChallengeQueued`,
  `ResponseMatched`, and `Promoted` in order;
- failed streams contain the expected terminal event and do not contain
  `Promoted`;
- `parseJSONSeq_()` succeeds and `ZquicLogDiag::writerFailures == 0`.

Required event coverage:

- migration requested;
- migration started;
- path challenge sent for candidate;
- path response matched;
- migration promoted;
- migration failed or abandoned;
- local endpoint rebind success/failure;
- CID selected/bound/retired because of migration.

This shorter coverage list is not sufficient by itself; it is a checklist over
the full event-sourcing contract above.

## Zhttp Library Changes

Keep the core `Zhttp` library transport-neutral.  Do not include
`<zlib/Zquic.hh>` from `Zhttp.hh`, `ZhttpH3.hh`, `ZhttpH3Session.hh`, or
`ZhttpQPack.hh`, because `libZhttp.la` currently does not depend on
`libZquic.la`.

Add transport-neutral optional hooks to `Zhttp::H3::Cxn` in
`zhttp/src/ZhttpH3Session.hh`:

```c++
template <typename PathInfo>
void pathUpdate(const PathInfo &) { }
template <typename MigrationResult>
void migrationStarted(const MigrationResult &) { }
template <typename MigrationResult>
void migrationPromoted(const MigrationResult &) { }
template <typename MigrationResult>
void migrationFailed(const MigrationResult &) { }
```

Do not make QPACK tables, H3 parser state, request state, or GOAWAY state
depend on migration.  Migration is transport continuity; H3 streams continue
unchanged unless QUIC disconnects.

Add an optional adapter header only if shared QUIC/H3 policy helpers are needed:

- file: `zhttp/src/ZhttpH3Quic.hh`;
- include order: `#include <zlib/Zquic.hh>` then `#include <zlib/Zhttp.hh>`;
- add it to `pkginclude_HEADERS`;
- do not add a `.cc` file or link `libZhttp.la` to `libZquic.la` for this
  header-only adapter.

The adapter may define:

```c++
struct QuicMigrationPolicy {
  Zquic::MigrationMode::T mode = Zquic::MigrationMode::Passive;
  unsigned cidReserve = 1;
  bool closeOnFailure = false;
};

inline void applyQuicMigration(
  Zquic::EngineParams &params, const QuicMigrationPolicy &policy);
```

If no shared helper is needed, keep policy parsing and `EngineParams` mutation
inside `zhttp/test/zhttp.cc` and `zhttp/test/zhttpd.cc`.

## zhttpd Changes

Update `zhttp/test/Zhttpd.hh` options:

- `--quic-migration=disable|passive|active`, default `passive`.
- `--quic-migration-cid-reserve=N`, default `1`.
- `--quic-migration-close-on-failure`, default false.
- `--quic-migration-local=ADDR[:PORT]` only if server local rebinding is
  implemented; otherwise do not expose it.

Parse the mode into `Zquic::MigrationMode::T` with a small switch, not string
comparisons at call sites.

In `zhttp/test/zhttpd.cc`:

- Set the new params when constructing `Zquic::ServerParams`.
- Implement in `H3ServerLink`:

```c++
void pathUpdate(const Zquic::PathInfo &info);
void migrationStarted(const Zquic::MigrationResult &result);
void migrationPromoted(const Zquic::MigrationResult &result);
void migrationFailed(const Zquic::MigrationResult &result);
```

`migrationPromoted()` must update the printable `remote` field from
`result.remote`, call `touch()`, and log local/remote addresses.  It must not
close active request streams.

Extend `printDiag()` to include migration counters and active path address/MTU.

## zhttp Client Changes

Update `zhttp/test/zhttp.cc` options:

- `--quic-migration=disable|passive|active`, default `passive`.
- `--quic-migration-cid-reserve=N`, default `1`.
- `--quic-migration-close-on-failure`, default false.
- `--quic-migration-local=ADDR[:PORT]` for explicit client local rebind.
- `--quic-migrate-after-headers` for deterministic tests.
- `--quic-migrate-after-bytes=N` for large-body tests.

In `QUICClient::Link` implement the same four callbacks as server.  The client
must not call `failH3Link()` on migration failure unless the QUIC connection
also disconnects or `closeOnFailure` was requested.

Trigger test migrations from the request/response flow:

- after H3 control and QPACK streams open;
- after response headers are parsed;
- after N response body bytes.

Use `link->migrateLocal()` for local-port rebinding.  Do not fake migration by
disconnecting and reconnecting.

## zhttpmatrix

Extend `zhttp/test/zhttpmatrix.cc`:

- parse `--quic-migration`, `--quic-migration-cid-reserve`, and
  `--quic-migrate-after-bytes`;
- parse `--quic-migrate-after-headers`;
- pass the flags to both `zhttp` and `zhttpd`;
- add at least one matrix row with HTTP/3, client migration after response
  headers, and QUIC packet drop enabled.
- add zhttp-to-caddy H3 migration cases by passing migration flags to the
  zhttp client while caddy remains the server;
- keep curl-to-caddy as non-migration control unless curl exposes a migration
  mechanism;
- add curl-to-zhttpd passive migration cases only when the installed curl stack
  can simulate or request QUIC migration/rebinding; otherwise print a skipped
  case reason and keep zhttp-to-zhttpd migration mandatory.

## Test Strategy

Use layered automated coverage.  Do not rely only on full loopback or external
interop tests; those are too coarse to isolate state-machine errors.

Mocked/faked/synthetic tests:

- Use `ZquicAPITest` test links to synthesize `observePathRxTx_()`,
  `startMigrationTx_()`, `onPathResponse_()`, `pathExpired_()`, callback order,
  and policy rejection without real sockets.
- Use synthetic `PathChallenge` payloads to test response match/mismatch.
- Use fake local/remote `ZiSockAddr` values to test active/candidate snapshots
  and path role transitions.
- Use synthetic `NEW_CONNECTION_ID` / `RETIRE_CONNECTION_ID` frames in
  `ZquicCIDTest` to test CID selection, association, reuse, retirement, and
  route tombstones without packet I/O.
- Use a fake or test-only `Endpoint_` hook to force rebind success/failure and
  stale generation completion without depending on OS timing.
- Use direct `ZquicLogger` calls in `ZquicLogTest` to serialize every migration
  qlog action independent of runtime scheduling.

Simulated loopback tests:

- Use `ZquicLoopTest` / runtime tests with real UDP sockets on loopback to
  validate local-port rebinding, passive tuple changes, packet loss, PMTUD
  restart, ECN state, and stream continuity.
- Simulate peer/NAT migration by injecting datagrams from a changed source tuple
  through existing endpoint receive paths rather than reconnecting.
- Simulate timeout by driving the existing path timer expiration path.
- Simulate response mismatch by delivering an incorrect `PATH_RESPONSE` before
  the correct response or timeout.

HTTP and interop tests:

- Use `zhttp`/`zhttpd` tests for deterministic H3 request/response continuity
  across migration.
- Use `zhttpmatrix` for zhttp-to-zhttpd migration with packet drop.
- Use existing caddy support for zhttp-to-caddy H3 active client migration when
  `haveCaddy()` is true.
- Use existing curl support for curl-to-zhttpd passive peer/NAT migration only
  if the installed curl/ngtcp2 stack exposes a controllable migration or
  rebinding behavior.  If not available, the test must be skipped explicitly
  and must not weaken mandatory zhttp-driven migration coverage.

## Tests To Add

Add or extend these tests with deterministic assertions.

`zquic/test/ZquicAPITest.cc`:

- `migration API rejects before established`;
- `migration API rejects when peer disable_active_migration is set`;
- `migration callback order is started then promoted`;
- `migration failure callback reports timeout and candidate tuple`;
- `pathInfo reports active and candidate snapshots`.
- synthetic peer-address observation creates a candidate without real socket
  I/O;
- synthetic `PATH_RESPONSE` mismatch does not promote the candidate;
- synthetic timeout abandons the candidate and preserves active path snapshot;
- fake endpoint rebind callback failure reports `Endpoint`.

`zquic/test/ZquicCIDTest.cc`:

- spare peer CID selected for active migration;
- current peer CID reused only when `requireNewPeerCID` is false;
- no spare peer CID fails with `NoPeerCID`;
- promoted CID becomes associated and old associated flag is cleared;
- retired local CID route becomes tombstone.
- synthetic `NEW_CONNECTION_ID` frames populate the peer CID reserve used by
  active migration;
- synthetic `RETIRE_CONNECTION_ID` frames cannot retire the promoted active CID
  accidentally.

`zquic/test/ZquicSockTest.cc` or a new endpoint test:

- `Endpoint_::rebindUDP()` success refreshes local address and generation;
- failed rebind leaves old endpoint connected;
- stale completion from old generation is ignored;
- queued Tx on old endpoint does not migrate to new socket.
- fake rebind failure path invokes the callback exactly once;
- fake rebind success followed by old-generation disconnect does not clear the
  new endpoint.

`zquic/test/ZquicLoopTest.cc`:

- client local-port migration after handshake keeps stream data flowing;
- passive peer-address change validates and promotes;
- migration under configured packet drop eventually succeeds or times out with
  the expected counter;
- PMTUD restarts on promoted path.
- response mismatch followed by correct response promotes only after the correct
  response;
- simulated peer/NAT source tuple change validates and promotes without active
  client migration;
- path validation timeout leaves old stream traffic usable.

`zquic/test/ZquicLogTest.cc`:

- direct serializer test covers every `MigrationAction` value;
- migration qlog events include non-empty enum `action`, `state`, and `reason`;
- migration qlog events include `attempt_id`, active tuple, candidate tuple,
  local-rebind flag, validation flag, and close-on-failure flag;
- path, CID, PMTUD, ECN, and close qlog events caused by migration include the
  same `attempt_id`;
- JSON-SEQ parsing succeeds for migration success, rejection, timeout, rebind
  failure, and close event streams;
- qlog path events use enum reasons, including `peer_address_change` and
  `nat_rebind`;
- qlog CID binding/retirement events identify migration cause and attempt.

`zquic/test/ZquicLoopTest.cc` qlog-enabled runtime cases:

- successful client `migrateLocal()` emits `Started`, `ChallengeQueued`,
  `ResponseMatched`, and `Promoted` in order;
- passive peer-address migration emits path observation, migration start,
  challenge, response, path promotion, and CID bind events with one
  `attempt_id`;
- peer-policy rejection emits `Requested` and `Rejected` and does not emit
  `ChallengeQueued`;
- no-peer-CID rejection emits `CIDUnavailable` and does not emit a path
  challenge;
- local endpoint rebind failure emits `RebindStart`, `RebindFail`, and the
  expected terminal failure/close event;
- timeout emits `Abandoned`, then `Failed`, with reason `Timeout` and no
  `Promoted`;
- response mismatch emits `ResponseMismatch` with reason `Validation` and keeps
  the attempt open until later success or timeout.

`zhttp/test/ZhttpdTest.cc` or `Zhttp3InteropTest.cc`:

- static H3 response survives client local-port migration;
- large H3 response migrates mid-body with one completion;
- server diagnostics remote address updates after promotion;
- peer-disabled active migration is rejected without corrupting H3 state.
- request after H3 control/QPACK streams open but before first response
  survives client local migration;
- upload/request-body migration is covered when upload tests exist;
- qlog-enabled H3 migration test confirms HTTP request completion and transport
  migration `attempt_id` are both present.

`zhttp/test/zhttpmatrix.cc`:

- zhttp-to-zhttpd H3 case with `--quic-migrate-after-headers`;
- zhttp-to-zhttpd H3 large-body case with `--quic-migrate-after-bytes=N`;
- zhttp-to-zhttpd H3 case with migration plus `--quic-rx-drop` /
  `--quic-tx-drop`;
- zhttp-to-caddy H3 client active migration case gated by `haveCaddy()`;
- curl-to-zhttpd passive migration/rebinding case gated by `haveCurlH3()` and
  by detection of a curl/ngtcp2 migration capability; skip explicitly when the
  capability is absent.

## Required Build Runs

After each transport phase:

```sh
make -C zquic/src -j8
make -C zquic/test -j8
make -C zquic/test test
```

After HTTP integration:

```sh
make -C zhttp/src -j8
make -C zhttp/test -j8
make -C zhttp/test test
```

Before merge:

```sh
make -j8
make test
```

For release/qlog validation, reconfigure through `./z.config`, then run:

```sh
make clean
make -j8
```

## Documentation Cleanup

When tests pass:

- update `zquic/README.md`: remove "full active migration" from exclusions and
  add a feature note saying migration is single-active-path, not multipath;
- update `zhttp/README.md`: remove "no active migration" from HTTP/3
  exclusions and document the new CLI flags;
- document that `MigrationMode::Passive` is the default for servers and clients
  because it supports NAT rebinding without allowing local active migration.

## Acceptance Criteria

The work is complete only when all of the following are true:

- `CliLink::migrateLocal()` changes the UDP source port on loopback and the
  existing QUIC connection continues with the same streams.
- Passive peer tuple changes validate and promote without application action.
- Application active migration is rejected when peer
  `disable_active_migration` is set.
- Candidate failure leaves the previous active path usable unless local rebind
  made fallback impossible.
- CID association and server route tombstones are deterministic and tested.
- PMTUD and ECN state are reset or scoped to the promoted path.
- `Zhttp` H3 control streams, QPACK streams, request streams, DATA, trailers,
  and GOAWAY state survive migration.
- `zhttpd` and `zhttp` expose migration policy flags and print migration
  diagnostics.
- `zhttpmatrix` covers zhttp-to-zhttpd migration with packet drop.
- `zhttpmatrix` covers zhttp-to-caddy H3 client migration when caddy is
  available.
- curl-to-zhttpd passive migration/rebinding is covered when the installed curl
  stack exposes the needed capability; otherwise the skip is explicit and
  zhttp-driven migration coverage remains mandatory.
- qlog can explain every migration start, success, and failure without relying
  on text logs.
- `make -C zhttp/test test` passes.
