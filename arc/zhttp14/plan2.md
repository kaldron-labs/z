# Repair Plan: Minimal Transport Delta and HTTP Pool Completion

## Outcome

Complete the server-specific HTTP pool refactor while preserving established,
integration-tested QUIC behavior.

The completed change will provide:

- one application-configured destination and effective request policy per pool;
- persistent pool-owned HTTP `Link` objects, each owning zero or one current
  TCP, TLS, H2, or QUIC connection;
- independent pool concurrency, link count, and per-link concurrency;
- deterministic round-robin admission with independent `saturated` and
  `limited` gates;
- generation-safe H1 operation, resolver, timer, and connection callbacks;
- correct overlapping H1 requests and multiplexed H2/H3 streams;
- deterministic client tests and orderly asynchronous teardown;
- a minimal lower-transport diff that preserves hardened QUIC behavior while
  repairing the four concrete ownership defects in stateless-reset
  teardown, peer-CID migration, STOP_SENDING completion, and stream-credit
  return, plus placement-only corrections for QUIC peer-address,
  Rx-advertised stream-limit, and timer members.

This remains a breaking HTTP client refactor.  Do not restore the old
per-request arbitrary-destination API and do not add compatibility forwarders.

## Scope Correction

`findings2.md` reviews the current QUIC implementation.  Its findings authorize
only the four ownership repairs specified in Phase 1.2, the member-placement
corrections in Phase 1.3, the minimal H3 hook in Phase 1.4, and their
deterministic tests.  The current worktree is the authoritative implementation
to repair; do not restore files or selectively revert existing hunks.  In
particular:

- preserve the established single endpoint connection reference and
  generation;
- preserve the established CID inventory, Initial-key, path, packet-reset,
  stream-terminal, and stream-reaping behavior except for the four explicitly
  scoped ownership repairs;
- preserve existing `Endpoint::connected()` send guards and disconnect return
  semantics;
- leave unrelated atomics, locks, snapshots, wrappers, and tests unchanged;
- accept other existing, integration-tested QUIC cross-shard snapshots and
  formal ownership compromises as out of scope for this project;
- do not use the HTTP pool refactor as authority to redesign unrelated QUIC
  internals.

The four audited defects are in scope even though they pre-date this project:

1. Rx stateless-reset processing clears Tx-owned packet and stream queues;
2. Rx peer-CID updates can race Tx migration selection and binding;
3. Rx STOP_SENDING processing mutates Tx completion state and invokes its
   callback;
4. Rx and Tx can both return the same peer-stream credit.

Other QUIC ownership work requires a reproduced correctness defect and should
be recorded as separate follow-on work.  Formal ownership purity alone is not
sufficient reason to replace hardened QUIC behavior in this change.

## Target Public Contract

- Use the existing public `Zhttp::Config` type for both client defaults and
  optional pool overrides.  Do not introduce `PoolConfig`, a compatibility
  wrapper, or a second policy hierarchy.
- Resolve library defaults into the client configuration once.  When a pool is
  configured, overlay only explicitly set fields from its optional `Config`
  and store one immutable effective configuration.  Transport/security and
  H2/H3 policy, discovery/Alt-Svc controls, request timeout, retries,
  redirects, retained-body/message limits, link count, pool concurrency, and
  per-link concurrency all follow this rule.
- `Destination` owns and normalizes only the pool host and non-zero port; it
  contains no request path/query.  The effective configuration determines
  plain H1/TCP versus secure H1/TLS, H2/TLS, or H3/QUIC policy.
- Client initialization receives the fixed pool count and client-level
  defaults.  Before `start()`, the application configures each selected slot
  with `client.pool(slot, destination)` or
  `client.pool(slot, destination, config)`.  The latter argument is the common
  `Config` type and is optional.
- `Client` owns a fixed, non-compacting array of pool references indexed by the
  application-selected slots.  `Pool` is a distinct server-specific request
  machine, not a metadata view into one client-global request machine.
- `pool()` rejects duplicate or out-of-range slots, invalid destinations, and
  invalid effective configurations.  `start()` rejects any still-missing slot.
  Pool membership, destination, and effective configuration remain immutable
  until `final()`.
- Enqueue, cancel, and seal are slot-qualified.  Requests contain only the
  operation/target, headers, and body; they do not select host, port, or
  protocol.  Request, event, and result identity includes the pool slot so
  equal keys in different pools remain independent.
- Relative and same-origin redirects remain in the originating pool.  A
  redirect that changes scheme, host, or effective port terminates with a
  specific cross-origin result; it must never silently retarget the pool.
- `Client::limited(unsigned pool, unsigned link, bool value)` validates stable
  configuration bounds without reading an Rx-owned live container, then posts
  only the slots and value to the pool/link Rx owner.

## Non-Negotiable Invariants for New and Retained Changes

- A pool `Link` persists for the lifetime of its pool.  A native connection is
  replaceable state owned by that link and never substitutes for the link.
- Each persistent link owns at most one live native connection at a time.
- Link count limits live connections.  Pool concurrency limits live requests.
  Per-link concurrency limits H1 pipelining and H2/H3 concurrent streams.
- The common per-link value is passed to H2
  `SETTINGS_MAX_CONCURRENT_STREAMS` and QUIC/H3 `maxStreamsDuplex`.  Existing
  QUIC configuration already supports this; do not redesign it.
- `limited` belongs to the persistent HTTP link and survives native reconnect.
  `saturated` belongs to current protocol/transport capacity.  Neither gate
  mutates the other.
- H1 Tx error handling is link/connection-generation scoped, never installed
  as a raw per-operation callback on a longer-lived connection.
- Every mixed-shard type touched by this work declares stable/shared members
  first, then one cache-line-aligned Rx section, then one cache-line-aligned Tx
  section.
- Shared members newly introduced by this project are limited to stable
  lifetime values, unclean telemetry primitives, and genuinely necessary
  atomic arbitration.
- Preserve the accepted single, rarely mutated `m_cxn` design in `ztcp` and
  `ztls`.  Do not reintroduce `m_rxCxn`/`m_txCxn` there.
- Short-lived HTTP operations and callbacks use raw back-pointers to stable
  owners where appropriate.  Owners stop ingress and drain callbacks before
  destruction.
- Initial link scheduling is strictly round-robin.  Do not add load, latency,
  health, or adaptive scoring.
- Pool discovery and selected protocol are cached once per route generation
  and shared by every link in that pool; requests and individual links do not
  independently resolve or select a protocol.

## Refactoring and Build Strategy

This is still a cross-cutting refactor.  The phases below identify coherent
integration tranches, not a requirement that every intermediate phase build.

- Confirm the current configuration is clang debug.  If it already is, do not
  reconfigure and do not clean.  Otherwise run `./z.config -d -L /usr`, clean
  exactly once because the build type changed, and establish one baseline.
- Do not rebuild after individual edits or compiler diagnostics.  Complete a
  connected ownership/API slice, inspect it statically, then build the affected
  library and its direct dependents once.
- Temporary non-buildable states are acceptable while the persistent HTTP link
  seam and its H1/H2/H3 adapters are changed together.
- Do not add shims or duplicate old/new machinery merely to keep an
  intermediate checkpoint buildable.
- Use an outer `timeout` only as a test-process watchdog.  Tests must synchronize
  concurrent work with observable events, semaphores, or continuations.
- Preserve unrelated pending work.  Do not reopen `zi` or `ZiMxLoopTest` absent
  a newly demonstrated direct dependency.

## Phase 1: Audit and Narrowly Repair QUIC

Perform this before further HTTP implementation so later work does not depend
on the speculative QUIC shard split.

### 1.1 Bound the QUIC repair

Audit the current implementation in these files against the requirements in
sections 1.2-1.5.  Do not use historical revisions to decide what current
implementation to remove, and do not restore or selectively revert it:

- `zquic/src/Zquic.hh`
- `zquic/src/Zquic_.hh`
- `zquic/src/ZquicLink.hh`
- `zquic/src/ZquicCliLink.hh`
- `zquic/src/ZquicSrvLink.hh`
- `zquic/src/ZquicStream.hh`
- `zquic/test/ZquicAPITest.cc`
- `zquic/test/ZquicSockTest.cc`
- `zquic/test/ZquicStreamTest.cc`
- `zquic/test/ZquicTimerTest.cc`

Limit edits to the four concrete repairs, the agreed placement corrections,
the minimal H3 capacity seam, and tests that directly prove them.  Record other
findings as follow-on work.  Do not alter CID representation, Initial-key
derivation, connection references, endpoint locking, terminal/reaping state,
disconnect contracts, or foundational `ZuRef`/`ZmRef` semantics unless one of
the named repairs cannot be implemented correctly without that exact change.

### 1.2 Repair the four concrete ownership defects

Apply these repairs as four independently reviewable slices.  Preserve the
existing wire behavior, error handling, migration policy, reference semantics,
and callback ordering except where ordering is the defect being repaired.

#### 1.2.1 Order stateless-reset teardown across Rx and Tx

- Keep datagram verification, Rx ACK cleanup, Rx diagnostics, and receipt of
  the stateless-reset event on Rx.
- Move the `m_streamQueue.clean()` and `m_txPkts` clears performed specifically
  by stateless-reset handling into one Tx-only helper with a `txInvoked_()`
  assertion; do not consolidate unrelated normal Tx reset/loss cleanup.
- Hold the link alive through an explicit Rx -> Tx cleanup -> Rx notification
  chain.  Tx first observes the existing terminal/disconnecting state, stops
  packetization, and
  clears its queues; only the final Rx continuation invokes the external
  stateless-reset/disconnect notification and permits finalization.
- Make the transition idempotent so concurrent local disconnect, peer close,
  and duplicate reset detection cannot clean or notify twice.
- Claim an Rx-owned one-shot reset transition together with the existing
  disconnect arbitration before posting.  The mandatory Tx cleanup must not be
  discarded merely because the generic disconnecting flag is now set.
- Keep this as the narrow continuation required by stateless reset rather than
  generalizing packet reset into a new Rx/Tx decomposition.

#### 1.2.2 Give peer-CID inventory one canonical owner

- Keep `m_peerCIDs`, retirement state, reservation/association flags, and the
  authoritative active sequence canonical on Rx, where
  `NEW_CONNECTION_ID` and retirement are processed.
- Keep only the fixed active peer CID/sequence and candidate CID/sequence
  needed for packet construction and path validation on Tx.  These are
  immutable publication values for a selected path, not a second mutable CID
  inventory.
- Publish the initial active pair and every later committed active-pair change
  to Tx in order.  Routine additions of unused peer CIDs remain Rx-only and do
  not trigger publication or Initial-key derivation.
- Replace Tx iteration or mutation of `m_peerCIDs` with an ordered asynchronous
  selection command: Tx supplies a migration generation and requirements; Rx
  selects/reserves an eligible CID and posts the fixed result back; Tx resumes
  path validation only if that generation is still current.  Do not introduce
  a synchronous Rx/Tx invoke cycle.
- Promotion uses a second generation-tagged command to Rx.  Rx commits the
  reserved sequence only if it remains active, publishes the authoritative
  active CID/sequence back to Tx, and refreshes routes; Tx completes promotion
  only after that acknowledgement.  Retirement or supersession before commit
  follows the existing no-CID/migration-failure path.
- Preserve retirement, active-CID-limit, reset-token, route-refresh, and
  migration-failure semantics.  A CID retired before selection or binding
  completes must cause the existing no-CID/failure path, not use stale state.
- Do not introduce `m_txCIDs`, full inventory snapshots, repeated Initial-key
  derivation, or routine full-inventory publication on every CID update.

#### 1.2.3 Complete STOP_SENDING on Tx

- Keep validation and `m_stopReceived` receipt state on Rx.  Capture the peer
  error code by value; do not touch `m_txCompleted` or invoke Tx completion
  from `Stream::receiveStop()`.
- Post a retained `StreamRef` plus that fixed error metadata to a Tx-only helper.
  That helper alone tests/sets `m_txCompleted` and calls `txComplete_(false)`.
- Preserve exactly-once completion when STOP_SENDING overlaps FIN ACK, reset,
  disconnect, or stream reaping.  A late continuation must safely observe an
  already-completed or detached stream and do nothing.
- Do not add atomics to terminal flags or introduce a general two-phase
  terminal/reaping state machine.
- Preserve the existing externally visible aggregate stream-error semantics in
  this slice.  The separate medium-priority arbitration between peer Rx errors
  and local Tx reset/stop errors is not silently solved with atomics; record it
  as follow-on unless a focused completion test demonstrates it is inseparable.

#### 1.2.4 Return peer-stream credit on Rx exactly once

- Make advertised local stream limits and `creditReturned` Rx-owned.  Move
  `m_localLimit` into the Rx member section.  Do not conflate it with
  peer-granted local-open limits (`m_peerLimit`).
- Replace Tx calls to `returnStreamCredit_()` with a transition notification
  carrying retained link/stream references and the fixed fact that its sending
  half is closed for credit purposes.  Rx records that fact in Rx-owned stream
  state and performs the existing eligibility check; shutdown drains these
  continuations before link finalization.
- Only Rx may mark credit returned, increment `m_localLimit`, and request the
  `MAX_STREAMS` flow update.  Continue using the existing control-frame post to
  perform the actual Tx queue mutation.
- Make duplicate and reordered notifications harmless.  Each peer-initiated
  stream advances the advertised limit at most once, including simultaneous
  receive FIN/RESET and local FIN/RESET completion.
- Keep stream object removal on its established owner and avoid broad reaping
  changes unless a focused test proves they are required for this credit
  handoff.

### 1.3 Put QUIC members under their actual owners

These are layout corrections, not new state machines.  Before moving a member,
enumerate its non-telemetry reads and writes and confirm they already occur on
the destination shard, or repair the isolated initialization/finalization
access noted below.  Maintain the class order: stable/shared members, first Rx
member cache-line aligned, then first Tx member cache-line aligned.

#### 1.3.1 Move `SrvLink::m_peerAddr` to Tx

- Treat `m_peerAddr` as Tx operational state: all packet send, PTO,
  retransmission, heartbeat, close, flush, and migration-promotion consumers
  are Tx-side.
- Move `m_peerAddr` into `SrvLink`'s Tx-exclusive member section.  Add the
  cache-line alignment only to the first member of that section; retain shared
  members first and the existing aligned Rx section before it.
- Replace direct Rx initialization and clearing of `m_peerAddr` with ordered
  publication of a by-value `ZiSockAddr` to Tx before send activation and with
  Tx-side clearing during teardown.  Rx route refresh must use captured Rx
  input or the established path/CID publication path rather than reading the
  Tx member.
- Keep migration promotion's assignment on Tx.  Do not add an address lock or
  a second long-lived peer-address copy.  Make `peer()` explicitly Tx-only and
  update its test/callers to copy the value while invoked on Tx.

#### 1.3.2 Correct `m_localLimit` placement

- Move `m_localLimit` from the Tx section to the Rx section as part of Phase
  1.2.4.  It governs peer-opened streams and the locally advertised receive
  limit; after the credit handoff repair, only Rx mutates it.
- Make public limit getters owner-thread APIs or provide an explicit by-value
  owner-shard query.  They affect protocol correctness and are not eligible for
  the unclean telemetry exception.  A layout move must not conceal a remaining
  operational cross-shard access.

`m_peerLimit` is not a placement-only correction.  Rx establishment
sets it, H3 control-stream setup opens local streams from Rx, and Tx applies
peer `MAX_STREAMS` updates and services queued local opens.  Do not move it
under a different section comment while those accesses remain.  If a later
required H3 slice naturally makes all local stream allocation and
peer-granted-limit mutation Tx-owned, then move `m_peerLimit`,
`m_nextBidiOrdinal`, `m_nextUniOrdinal`, `m_queued`, and
`m_openQueuedPending` together and add focused owner-thread assertions and
tests.  Otherwise record that ownership correction as follow-on work rather
than expanding this phase.

#### 1.3.3 Move Tx-operated timers to the Tx section

- Move the connection and active-path timer objects whose schedule, cancel,
  delete, and callbacks are Tx-only from the Rx section to the Tx section:
  ACK-delay, loss, PTO, idle, heartbeat, close, key-discard, PMTUD, and path
  validation timers.
- Move the audited Tx-only metadata with them:
  `m_lossTimerOut`, `m_ptoTimerOut`, `m_idleTimeout`, `m_idleTimerOut`,
  `m_idleBase`, `m_heartBeatTimerOut`, `m_heartBeatBase`, `m_closeTimerOut`,
  `m_closeNextResponse`, `m_ptoTimerLevel`, and
  `m_idleAckElicitingSent`.
- Leave genuinely Rx packet-space and ACK-production state in the Rx section.
  Do not move `m_rxSpaceDiscarded`, ACK trackers, or similarly named state
  solely because a Tx timer consumes a posted snapshot of it.
- Retain existing `txInvoked_()` assertions on timer helpers and add missing
  assertions only at direct internal Tx entry points.  No timer lock, atomic,
  duplicate timer, or new scheduling layer is required for a placement-only
  correction.
- If `m_drainPTOs` is assigned but never read in the current implementation,
  delete it rather than relocating dead state merely to satisfy the section
  layout.

### 1.4 Retain only minimal H3 capacity integration

The existing QUIC API already exposes and consumes `maxStreamsDuplex`.  The
HTTP coordinator only needs to pass the pool's effective per-link concurrency
to it.

Retain or add only the smallest capacity-notification seam required to wake a
saturated HTTP H3 link:

- continue using the existing QUIC `flowBlocked` CRTP callback for local Tx
  blocking;
- if no existing callback reports restored credit, add side-effect-safe base
  defaults and one direct callback after the existing Tx-owned
  `txApplyMaxData_`, `txApplyMaxStreamData_`, and `txApplyMaxStreams_` updates;
- carry only frame/stream identifiers needed by the H3 adapter;
- other than the explicit `m_localLimit` repair in Phase 1.2.4, do not move the
  underlying credit, stream-limit, or frame-processing state to another shard;
- keep the H3 adapter's saturation model scoped correctly: connection data and
  stream-count exhaustion can gate new operations; one existing stream's
  individual flow-control exhaustion must not automatically prohibit opening
  unrelated streams unless the protocol's initial stream credit makes that
  necessary.

Keep stable pool/link identity and connection generations in `zhttp`.  Do not
push the HTTP persistent-link abstraction into QUIC core.

### 1.5 Baseline verification

After the narrow repairs, member moves, and minimal H3 hook form one coherent
slice:

1. build `zquic/src` once;
2. build `zquic/test` once;
3. run the complete existing `zquic/test` suite;
4. run focused tests proving:
   - stateless reset during queued/in-flight Tx drains once without touching Tx
     containers on Rx;
   - concurrent `NEW_CONNECTION_ID`/retirement and migration selection or
     promotion cannot invalidate iteration, select a retired CID, or revert the
     promoted CID;
   - STOP_SENDING racing FIN ACK/reset produces exactly one Tx completion;
   - every peer stream returns exactly one credit under all Rx/Tx close
     orderings;
   - server initial-path publication and path promotion send to the intended
     peer while only Tx accesses `m_peerAddr`;
   - `m_localLimit` operations and timer operations execute on the member
     section's owning shard, with timer cancellation/finalization leaving no
     armed callbacks;
5. confirm every QUIC hunk introduced while executing this plan maps to one of
   those repairs, a placement correction, its focused test, or the H3 capacity
   callback.

Keep these tests deterministic with shard barriers or continuations.  Repeat
the race-focused cases for hundreds of bounded iterations and diagnose the
first failure rather than accepting a later pass.

## Phase 2: Make the HTTP Test Harness Deterministic

Repair the new loopback client tests so later HTTP failures exit promptly.

- Replace raw blocking listener ownership in
  `zhttp/test/ZhttpClientCancelTest.cc` with an owning fixture that tracks the
  listening and accepted descriptors.
- Give the fixture an idempotent stop path that wakes `accept` and `recv` with
  `shutdown`/close before joining the server thread.
- Ensure every early return, failed start, failed semaphore wait, and client
  stop path terminates the fixture before join.
- Coordinate accept, request receipt, response release, and server exit with
  semaphores/events rather than elapsed sleeps.
- Replace fixed request buffers and distribution arrays with suitable
  `ZtScratch`, `ZtBuiltin`, or `ZtArray` storage.  Replace small integral
  `static constexpr` test values with enums and name any genuine capacity with
  a maintenance rationale.
- Add focused deterministic cases for:
  - two pipelined H1 requests followed by a connection Tx error;
  - synchronous resolver success/failure/fallback/cancellation;
  - stop while resolving, connected-idle, and pipelined-active.

Checkpoint by building `zhttp/src` before `zhttp/test`, then run
`ZhttpClientCancelTest` and `ZhttpClientPoolTest` under an outer watchdog.  At
this stage new behavioral assertions may expose unfixed HTTP defects, but the
test processes must terminate deterministically.

## Phase 3: Finish the Narrow TCP/TLS Ownership Work

Do not generalize these fixes into a lower-transport redesign.

### TCP

- Keep public connect entry points thin: capture server/port by value and assign
  Rx-owned destination fields inside the existing Rx invocation.
- Preserve the single `m_cxn` and existing disconnect/race guards.
- Keep `ZiTxErrorFn` configured before use and stable for the connection/link
  lifetime.  Fix per-operation misuse in the HTTP H1 adapter, not by adding TCP
  callback machinery.

### TLS

- Assign server, port, and selected remote IP inside the Rx invocation.
- Preserve the single `m_cxn` and established connection/disconnection flow.
- For application-thread Tx buffer construction, reserve the stable worst-case
  `TxMaxOverhead`.  Keep negotiated `m_headroom` Rx-owned and
  `m_txHeadroom` Tx-owned; publish only through the existing Tx installation
  command.
- Replace diagnostic/statistics atomics with plain telemetry primitives where
  exactness is not required.  Retain only correctness-related generation and
  disconnect arbitration atomics.
- Keep data members private and organize stable/shared, aligned Rx, then aligned
  Tx sections without exposing `m_` members through protocol adapters.
- Cover TLS 1.2/1.3 headroom, buffers created across key-generation changes,
  and reconnect.

Checkpoint `ztcp/src`, `ztls/src`, their direct tests, and the HTTP H1/TLS and
H2/TLS message tests.  Do not run a top-level build for this slice.

## Phase 4: Rebuild HTTP Around Persistent Pool Links

This is the central structural tranche.  Do not preserve the current
metadata-only `LinkSlot` and patch around it.

### 4.1 Finish and propagate the common pool contract

- Preserve one public `Config` type.  Verify its explicit-field mask can
  distinguish inheritance from valid zero-valued policies, and resolve every
  client/pool field listed in the Target Public Contract exactly once at pool
  configuration.
- Keep `Destination` owning host+port and preformat the Host/`:authority` value
  once per pool.  Remove the high-level request `host()`/absolute-destination
  contract and retain only the per-request target/path.
- Make the pool count structural client state rather than a `Config` field.
  Pre-size the client pool array, configure each slot exactly once before
  start, and make start/rollback/stop/final aggregate over the configured pool
  objects.
- Move the live-request table, Tx queue, request timers, retry/redirect
  accounting, route generation, parser callbacks, and stable links into the
  corresponding `Pool`.  `Client` remains the array/lifecycle dispatcher and
  shared-transport coordinator.
- Route enqueue, cancel, seal, counters, events, and idle completion by pool
  slot.  Aggregate idle fires once only after every sealed pool is drained.
- Implement `limited(pool, link, value)` using immutable configured bounds for
  validation followed by an Rx post; do not inspect the live Rx link container
  before dispatch.
- Apply timeout, retry, redirect, retained-body, and retained-message behavior
  from the pool's immutable effective configuration.  Same-origin redirects
  change only the target; reject cross-origin redirects without changing pool
  destination or route generation.
- Propagate the breaking API through all `Zhttp::Client` dependents, tests,
  examples, `zhttp/README.md`, and `zhttp/util/zhttp.cc`.  The utility creates
  one pool at slot 0, constructs one host+port `Destination`, and keeps its
  request objects path-only.  Preserve `-j` as pool concurrency and expose
  distinct link-count and per-link-concurrency options so all three independent
  limits remain configurable.  Map `http` to plain H1/TCP and `https` to the
  configured secure H1/H2/H3 selection policy.

### 4.2 Stable link ownership

- Replace `Pool::LinkSlot` with one persistent link object per configured slot,
  constructed once and retained until pool finalization.
- Give the link immutable pool/slot identity, Rx-owned connection state and
  generation, independent `limited`/`saturated` gates, operation counts, and
  protocol correlation state.
- Store the one current native transport/protocol connection handle in the
  persistent link.  Native disconnect clears that handle and increments the
  generation without deleting or renumbering the link.
- Keep the lifecycle/native handle on Rx.  Tx transport work receives only a
  retained native adapter reference plus the fixed connection generation
  through the existing send path; it must not read the Rx-owned handle to
  discover the current connection.
- Carry `(pool slot, link slot, connection generation)` through delayed native
  callbacks and ignore stale generations.
- Preserve `limited` across disconnect/reconnect.  Recompute only `saturated`
  from H1/H2/H3 capacity events.
- Delete write-only `LinkSlot` counters/flags or replace them with state that is
  consumed by admission and teardown.
- Use identified Z heaps for stable links, operations, pool nodes, and remaining
  hot-path allocations.

### 4.3 Native HTTP adapters

- Make the H1/TCP, H1/TLS, H2/TLS, and H3/QUIC adapters children of a persistent
  pool link.  A protocol-native object may represent the current connection but
  must not become the stable application-visible link identity.
- Key any unavoidable hub lookup by pool identity plus stable link slot so
  same-origin pools and multiple links cannot collapse onto one connection.
- Ensure each stable link has at most one live protocol choice/connection at a
  time.  Pool-wide fallback drains the old route generation and reconnects the
  same stable slots.
- Pass the effective common per-link concurrency to H1, H2, and the existing
  QUIC `maxStreamsDuplex` configuration without mutating shared hub defaults.
- Successful operation completion destroys only its parser/builder/native
  stream state and releases its reservation; the persistent link and reusable
  connection remain available for later requests.
- H2/H3 cancellation resets only the affected stream and preserves siblings.
  EOF, GOAWAY, transport failure, or non-reusable H1 response applies
  generation-aware replay/failure rules, closes only the native connection,
  and leaves the stable link available for lazy reconnect.

### 4.4 Round-robin admission

- Run pool concurrency accounting, the selection cursor, reservations, and
  admission decisions on Rx with the stable links.  Tx protocol code publishes
  generation-tagged capacity transitions to Rx rather than exposing Tx flow
  state to the selector.
- Iterate stable slots from the pool cursor, skipping limited, saturated, or
  stopping links.
- Enforce pool concurrency before link selection.  A disconnected link may
  accept an initial reservation and connect lazily; connecting or connected
  links accept more reservations only within their configured and negotiated
  capacity.
- A link remains eligible while it has active requests until its configured or
  negotiated protocol capacity is exhausted.
- Track reserved, emitted, and completed counts explicitly so asynchronous
  Rx/Tx posts cannot over-admit.  Recompute `saturated` on its owner and wake
  the pool only on a capacity transition.
- H1 saturates at the common configured per-link pipeline depth.  H2 saturates
  at that local ceiling further constrained by peer stream settings,
  connection flow control, and bounded queues.  H3 saturates at the same local
  ceiling further constrained by peer stream credit, connection `MAX_DATA`,
  and bounded queues.  Exhaustion of one existing H2/H3 stream's individual
  credit blocks that operation, not unrelated stream admission.
- Advance the cursor after every assignment.  If no link admits work, leave the
  request queued without polling.
- Capacity-restoring H2/H3 events and `limited(false)` wake admission.  Protocol
  events never alter the application `limited` flag.

Checkpoint only after the persistent link, all four adapters, connection
generation, and admission cursor form one coherent slice.

## Phase 5: Repair H1 Operation Lifetime and Pipelining

- Install one Tx error callback per persistent H1 link/connection generation,
  never a callback capturing an `Operation *`.
- On Tx error, post a fixed link-generation failure to Rx.  Rx classifies and
  retires affected operations exactly once using body-commit/replay state.
- Keep H1 response correlation in an Rx-owned intrusive FIFO.  Tx must never
  iterate the Rx operation container.
- Replace linear find/splice completion with O(1) intrusive unlinking.
- Keep request-byte serialization in Tx-owned state.  Cross-shard posts carry a
  typed live-slot/generation token, not `void *` operation state.
- Define cancellation precisely:
  - queued/unemitted work is removed independently;
  - cancelling emitted pipelined bytes closes the native connection when
    isolation is impossible;
  - later affected operations retry or fail from commit/replay state exactly
    once;
  - the persistent link remains available for lazy reconnect.
- Enforce the configured per-link H1 pipeline ceiling independently of pool
  concurrency and link count.

Checkpoint `zhttp/src` and `zhttp/test`, then run the H1/TCP, H1/TLS,
`ZhttpClientCancelTest`, and `ZhttpClientPoolTest` binaries.

## Phase 6: Repair Resolver, Timer, and Teardown Lifetimes

### Resolver publication

- Keep one route-discovery state machine and cache per pool, shared by all of
  its stable links.  Introduce a route-discovery generation and explicit
  `Starting/Resolving/Resolved` state.
- Publish the generation/state before invoking a resolver that may complete
  synchronously.
- Install the returned handle only if the same generation is still active.  If
  synchronous completion or fallback advanced the generation, discard/cancel
  the obsolete handle without overwriting the newer one.
- Make stop take the active generation/handle once; late callbacks become
  no-ops.
- Plain effective policy selects H1/TCP directly.  Secure policy performs DNS,
  HTTPS/Alt-Svc discovery, forced/preferred/disabled H3 selection, and H2/ALPN
  selection once per pool route generation.
- Resolve the configured hostname once per pool generation (or use its literal
  address directly), then share the resulting endpoint set with every link.
- Cache the selected IP, port, and protocol at pool scope.  Retries and newly
  connecting links reuse that selection rather than resolving independently.
- Treat accepted Alt-Svc upgrade or protocol fallback as a pool-wide generation
  transition: stop admission to the old generation, drain/close its native
  connections, publish the new selection once, then retry only replay-safe
  operations.  Never let links in one pool silently settle on unrelated
  prevailing protocols.

### Request timers

- Replace separately allocated/ref-counted request timers with timers contained
  by stable pool-owned live-request slots or another stable owner.
- Tag each arm with the live-request generation.  Cancel on completion and make
  stale-generation callbacks no-ops.
- During stop: prohibit further work, cancel timers on their callback shard,
  post a same-shard drain continuation, then proceed with cross-shard teardown.

### Pool/client teardown

- Reject ingress and cancel resolver/timers on Rx.
- Drain Rx, then detach/drain Tx queues and native connection work, then return
  to Rx for final removal/completion.
- Finish queue/container traversal before posting follow-on cross-shard work;
  do not retain iterators across callbacks.
- Count actual visited children rather than snapshotting a container count used
  as completion state.
- Finalization asserts no resolver handle, armed timer, queued node, active
  operation, or native connection remains.

## Phase 7: Audit New HTTP/TLS Code Without Expanding QUIC Scope

Apply `GUIDELINES.md` flags to the code introduced or materially changed for
this project.  Do not convert pre-existing hardened QUIC compromises into a new
refactoring program.

- Reorganize `ClientMessage`, HTTP `Pool`, HTTP `Client`, persistent link, and
  touched TLS classes into stable/shared, aligned Rx, aligned Tx sections.
- Move nondependent declarations out of `Pool<>` and `Client<>` templates.
- Make touched types with `m_` data proper classes with private members, or true
  public records with unprefixed fields.
- Replace per-attempt endpoint-array copies with pool-owned route generations
  referenced by generation/index and retired deterministically.
- Batch potentially unbounded HTTP dispatch, failure, and stop loops on I/O
  threads.
- Cache genuinely invariant loop bounds and configuration values.
- Use uninitialized/direct construction for link, request, and route arrays
  where default construction is immediately overwritten.
- Remove unnecessary span casts, CRTP casts, and the type-erased operation
  boundary.
- Give real capacities named enum constants and maintenance rationale; replace
  fixed C arrays with workload-appropriate Z storage.
- For every amber flag, either make the lower-cost correction or document why
  the current storage/control-flow choice is measured and intentional.  Amber
  does not mandate unrelated architectural change.
- Preserve already clean categories: no concepts/`requires`, `enum class`, STL
  substitution, broad lambda captures, hand-rolled hashes, direct
  `FILE`/`printf`, unsafe qlog captures, or sleep polling.

## Phase 8: Focused Behavioral Coverage

### HTTP pool/link tests

- Missing, duplicate, and out-of-range pool slots; invalid destinations and
  invalid effective policy; immutable pool registration after start.
- Inheritance of every client-level `Config` field and isolated pool overrides
  for transport/security and H2/H3 policy, discovery/Alt-Svc controls, timeout,
  retries, redirects, retained-body/message limits, link count, pool
  concurrency, and per-link concurrency.
- Two destination slots with overlapping request keys and paths reach the
  correct host/port and emit unambiguous pool-qualified events.
- Multiple pipelined H1 operations on one connection, including Tx error and
  cancellation at different FIFO positions with exact-once results.
- Independent link count, pool concurrency, and per-link concurrency, including
  10 stable links carrying 100 operations at 10 operations per link.
- Round-robin wraparound, saturation skip/re-entry, all four
  saturated/limited combinations, and limiting across reconnect.
- The common per-link value controls H1, H2 settings, and QUIC/H3
  `maxStreamsDuplex`, including a pool override differing from client defaults.
- H2/H3 multiple streams per persistent native connection, saturation and
  credit wakeup, operation completion without connection destruction, and
  repeated connection-generation reuse.
- Successive completed waves over H1, H2, and H3 reuse the same native
  connection until a protocol event makes it non-reusable.
- Same-origin pools retain independent link/connection identity.
- Resolver selection is reused per pool and synchronous completion/fallback
  cannot overwrite its active handle.
- Forced H3, disabled H3, H3-to-TLS fallback, and accepted Alt-Svc transitions
  change one pool route generation without allowing its links to diverge.
- Relative and same-origin redirects retain the pool destination; cross-origin
  redirects fail without creating or selecting another pool.
- Exercise timeout, retry, redirect-count, retained-body, and retained-message
  behavior using different effective policies in two pools, proving that the
  override changes behavior only in its own slot rather than merely changing a
  reported getter.
- Stop/final from queued, resolving, connecting, active, limited, and reusable
  idle states drains exactly once and returns identified heaps to baseline.

### Lower-transport tests

- Run all existing QUIC integration/unit coverage after the scoped
  implementation audit and narrow ownership repairs.
- Add the focused deterministic QUIC tests specified in Phase 1.5 for
  stateless-reset ordering, CID update/migration overlap, STOP_SENDING
  completion, exactly-once stream credit, peer-address publication, and timer
  teardown.
- If a capacity callback is retained, prove blocked and restored-credit
  notification without otherwise changing CID/path/stream semantics.
- Retain focused TLS tests for application/negotiated headroom and reconnect.

Repeat concurrency-sensitive HTTP tests for hundreds of bounded iterations.
Record the first failing iteration and diagnostics; do not rerun until green
without diagnosis.

## Phase 9: Verification and Acceptance

Use sparse integration checkpoints in dependency order:

1. audited and narrowly repaired QUIC core plus existing and focused QUIC
   tests;
2. TCP/TLS slice and direct tests;
3. common HTTP configuration/pool API plus persistent-link/H1 structural slice
   and HTTP unit tests;
4. resolver/timer/teardown slice;
5. all dependent applications, HTTP utilities, documentation examples, and
   integration tests.

At final verification:

- run `git diff --check`;
- verify that QUIC edits made while executing this plan are limited to the four
  ownership repairs, member-placement corrections, the required H3 capacity
  integration point, and their focused tests; do not treat unrelated existing
  implementation as a restoration target;
- re-audit the pending HTTP/TLS diff against all `GUIDELINES.md` flags;
- run one incremental top-level `make -j8` in the existing clang debug build;
- run `make -C zquic/test test`, `make -C ztls/test test`,
  `make -C zhttp/test test`, and `make -C zhttp/itest test`;
- run the complete top-level module test matrix, with only `ZiMxLoopTest`
  excluded because its unrelated lower-level flakiness is already follow-on
  work and this plan must not reopen `zi`;
- run targeted HTTP loop suites and the new QUIC ownership/interleaving suites
  with ASAN/LSAN and valgrind memcheck/leak checking through `libtool exec`.

Changing build type for sanitizer or release acceptance requires one deliberate
reconfiguration through `z.config` and one clean rebuild.  Never clean during
ordinary incremental repair work or mix objects from different build modes.

## Explicitly Out of Scope

- Proactive connection establishment or keepalive traffic before the first
  request.
- RTT learning, slow-link classification/replacement, and adaptive scheduling;
  round-robin is the complete initial policy.
- Application rate calculation, token buckets, or automatic mutation of
  `limited`; the application supplies the boolean gate.
- Dynamic pool insertion, removal, resizing, destination replacement, or
  policy mutation after start.
- Compatibility overloads for arbitrary per-request host/port or absolute URL
  submission.
- Broad QUIC endpoint, close-state, stream-terminal, or reaping redesign beyond
  the four repairs and member placements explicitly authorized in Phase 1.
- Redefining aggregate QUIC stream-error precedence when local reset/stop and
  peer reset/STOP_SENDING race; Phase 1 fixes completion ownership and leaves
  that separate medium-priority semantic question unchanged.
- `m_peerLimit` ownership redesign unless required H3 work first makes every
  local-stream allocation and peer-limit mutation Tx-owned.

## Finding Disposition

### `findings.md`

- Blockers 1, 2, and 4: persistent HTTP link and H1 repair, Phases 4-5.
- Blocker 3: resolver publication, Phase 6.
- Blocker 5: new HTTP layout and ownership, Phases 4 and 7.
- Blocker 6: narrow TLS headroom repair, Phase 3.
- Blocker 7: preserve the four audited ownership repairs and placement-only
  member corrections in Phase 1 without restoring or selectively reverting
  unrelated QUIC implementation.  `m_appClose` remains a documented,
  infrequent close-path compromise rather than a blocker for this project
  unless a reproducible failure promotes it to separate work.
- Blocker 8: timer/teardown repair, Phase 6.
- Blocker 9: deterministic HTTP fixture, Phase 2.
- Amber findings: address only where introduced or materially changed by this
  project, primarily Phases 4-7.

### `findings2.md`

- CID representation, atomics/locks, connection guards, disconnect behavior,
  Initial-key derivation, and stream terminal/reaping machinery are not broad
  cleanup targets in this plan; change them only where a named repair directly
  requires it.
- The stateless-reset, canonical CID, STOP_SENDING completion, and
  stream-credit ownership defects are then repaired narrowly; `m_peerAddr`,
  `m_localLimit`, and Tx-operated timers are moved to their correct member
  sections.  `m_peerLimit` is not misrepresented as a placement-only repair;
  its remaining Rx/Tx ownership is deferred unless the required H3 work makes
  all of its accesses Tx-owned.
- The existing single endpoint connection reference and other hardened
  cross-shard compromises remain out of scope for this project.
- Existing QUIC implementation is retained unless an explicitly scoped Phase
  1 repair or the minimal H3 capacity hook requires a change.

## Completion Criteria

The repair is complete when current code and tests prove that:

- one common `Config` supplies client defaults and optional pool overrides;
  each immutable host+port pool receives the correct effective protocol,
  timeout, retry, redirect, retained-body/message, link, pool-concurrency, and
  per-link-concurrency policy;
- `Client` owns and dispatches over its configured pool slots, while each
  disentangled `Pool` owns its request machine, discovery generation, stable
  links, and destination; requests cannot select or escape to another origin;
- execution has not changed unrelated QUIC CID, endpoint, migration,
  packet-reset, terminal-state, or stream-reaping design, and the four targeted
  ownership repairs pass their interleaving tests;
- `m_peerAddr`, `m_localLimit`, and Tx-operated timers reside in their actual
  owner sections with no remaining operational non-owner writes;
- each HTTP pool link persists across native disconnect/reconnect and owns at
  most one live connection;
- H1 callbacks cannot dereference retired operations and Rx/Tx containers are
  accessed only by their intended HTTP owner;
- pool/link/per-link limits, saturation, and application limiting compose
  correctly for H1, H2, and H3;
- synchronous resolver callbacks, request timers, and shutdown cannot publish
  stale handles or callbacks;
- deterministic client tests, existing and targeted QUIC tests, targeted
  TLS/H2/H3 tests, sanitizer/leak checks, and the complete test suite pass.
