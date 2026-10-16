# Plan: HTTP Clients with Multiple Server-Specific Pools

## Outcome

Replace the current request-centric `Zhttp::Client<TxQ, ResParser>` model with
a client that owns an application-sized array of independent pools.  Each pool
is configured before start with one host+port destination, one effective
request policy, and a fixed number of stable physical-link slots.  Requests
name a pool slot and contain only request-specific data such as method, target,
headers, and body.

The redesign is intentionally breaking.  Update all dependents directly; do
not retain forwarding overloads for the old arbitrary-URL API.

## Refactoring and Slicing Strategy

This is a cross-cutting code refactor.  The numbered implementation sections
below describe logical workstreams and dependency order; they are not promises
that the library builds at the end of every section.  Temporary non-buildable
states are acceptable while public templates, ownership, transport adapters,
and dependents are changing together.

- Do not introduce compatibility overloads, duplicate old/new machinery, or
  temporary forwarding layers merely to keep intermediate phases buildable.
- Before each work tranche, select the smallest coherent dependency slice that
  advances the target ownership model.  A slice may cross several numbered
  sections—for example, a `Link` ownership change may require simultaneous
  edits to pool dispatch, H1/H2/H3 adapters, and one test fixture.
- Prefer structural slices first (configuration types, stable identities,
  ownership, shard boundaries), then behavioral slices (multiplexing,
  saturation/limiting, failure fan-out), then dependent migration and tests.
- Reassess the next slice after each integration attempt.  Group compiler
  failures by missing API propagation or ownership seam, adjust the slice, and
  continue the refactor.  Do not repeatedly rebuild after individual error
  fixes and do not clean to diagnose ordinary refactoring failures.
- A build is an integration checkpoint, not a phase-exit requirement.  Reach
  checkpoints often enough to bound mistakes, but only after enough connected
  pieces have been migrated for compiler feedback to be useful.
- Keep an explicit short-lived checklist while implementing each tranche so
  temporarily broken callers, protocol specializations, and tests are not
  forgotten when the slice closes.

## Target Model and Invariants

- Use one public `Config` type for both client defaults and pool overrides.  It
  contains independent link-count and concurrency limits, transport and
  protocol selection, `H2Policy`, configured per-link concurrency,
  discovery/Alt-Svc controls, request timeout, maximum retries, maximum
  redirects, retained-body limit, and retained-message limit.
- Introduce an owning `Destination` value containing a normalized host and
  non-zero port.  It contains no request path/query data.  Plain versus secure
  HTTP and subsequent protocol selection come from the effective `Config`.
- Pass the pool count separately when initializing `Client`; it is structural
  client state, not request policy and therefore is not part of `Config`.
- Configure pools by application-selected slot before `start()` with
  `client.pool(slot, destination, config)`.  The `config` argument has the same
  `Config` type used to initialize the client and is optional;
  `client.pool(slot, destination)` inherits every client setting.
- Resolve library defaults into the client configuration, then overlay the
  optional pool configuration into one immutable effective configuration when
  configuring each pool.  An unset pool field inherits the corresponding
  client value.  Hot request/link paths read only the resolved configuration
  and do not repeatedly branch on override presence.
- Treat client-level link count and concurrency as independent defaults for
  every pool; a pool may override either.  Link count controls the number of
  persistent `Link` objects and therefore the maximum number of simultaneous
  live connections.  Concurrency controls the maximum number of admitted live
  requests across those links.
- Pre-size the pool array from the separately passed pool count.  `start()`
  fails unless every declared slot was configured exactly once.  Reject an
  out-of-range or duplicate slot, an invalid destination, zero effective
  links, or a destination/effective-policy combination that cannot run.
- Make `Pool` the owner of its transmit queue, active-request table, timers,
  route state, and link slots.  Make `Client` a lifecycle/dispatch coordinator
  over an array of pool references rather than the owner of one global request
  machine and three global protocol pools.
- Give every pool link a stable index in `[0, linkCount)`.  Do not compact link
  slots or create one link object per request.  A `Link` persists for the
  lifetime of its pool and owns its current live connection.  Connections may
  come and go, but a `Link` owns at most one live TCP, TLS, or QUIC connection
  at any instant.
- Separate link connection state (`disconnected`, `connecting`, `connected`,
  `stopping`) from the link's collection of assigned operations and its
  two independent admission gates:
  - `saturated` is library-owned protocol/transport capacity state;
  - `limited` is application-owned rate-limit state.
  One request does not necessarily make a connected link busy; a link becomes
  saturated when it cannot accept another operation under its current protocol
  limits.  A link may be saturated, limited, both, or neither.
- Enforce concurrency at pool scope, independently of link count.  For example,
  10 links, pool concurrency 100, and per-link concurrency 10 permit 100 live
  requests, normally distributed by round-robin as 10 operations per link.
- Support protocol-appropriate overlap on each live connection:
  - H1 serializes request bytes and associates pipelined responses with a FIFO
    of live operations up to the configured per-link concurrency;
  - H2 uses the same configured value when negotiating
    `SETTINGS_MAX_CONCURRENT_STREAMS` and as its local per-link admission
    ceiling, further constrained by peer settings, connection/stream flow
    control, and bounded local queue capacity;
  - H3 uses the same configured value for the corresponding H3/QUIC concurrent
    stream negotiation and local per-link admission ceiling, further
    constrained by QUIC stream credit, `MAX_DATA`, per-stream flow control, and
    bounded local queue capacity.
- Completion retires only the operation.  A reusable connection remains owned
  by its persistent link for later operations.  When a connection becomes
  unusable, the `Link` closes that connection, applies failure/replay rules to
  its affected operations, and reconnects lazily without replacing the link.
- Select links with deterministic round-robin scheduling.  Maintain stable
  slot order and advance the pool's selection cursor after each assignment;
  links remain assignable while carrying active operations until they are
  saturated.  Skip saturated, limited, or stopping links and queue at pool
  level when no link currently admits new work.  Do not add load, latency, or
  health scoring yet.
- Expose `Client::limited(unsigned pool, unsigned link, bool value)`.  `pool`
  and `link` are stable application-selected/client-created slots.  Setting the
  flag prevents only future assignment to that link; it does not cancel active
  operations, close the connection, or prevent those operations from making
  progress.  Clearing the flag makes the link assignable again only when it is
  not saturated or stopping.
- A request is identified by `(pool slot, request key)`.  Slot-qualify enqueue,
  cancel, and seal operations.  Add the pool slot to `ClientEvent` and `Result`
  so callbacks remain unambiguous when different pools reuse request keys.
- Remove the absolute URL and `host()` callback from the high-level request
  contract.  The library emits the Host/`:authority` value cached by the pool;
  the request continues to provide `operation()` and optional CONNECT
  `protocol()` plus headers/body.
- Allow relative and same-origin redirects by updating only the request target.
  Reject a redirect that changes scheme, host, or effective port with a
  specific terminal result; a request must never escape its submitted pool.
- Keep pool configuration and pool-array membership immutable after start.
  Rx owns route/link/request-attempt state, Tx owns the pool's transmit queue
  and request construction, and cross-shard posts capture only stable pool/link
  handles plus fixed metadata.

## Implementation Steps

### 1. Define the public configuration and application contract

- Refactor `ClientConfig` into the common layered `Config` type in
  `zhttp/src/ZhttpClient.hh`; do not introduce a separate public pool
  configuration type.  If retaining the `ClientConfig` name would be
  misleading at `client.pool()` call sites, rename it consistently and update
  all dependents.
- Add `Destination` with owning host storage and a port.  Use an identified Z
  string heap, validate and normalize the host once, require a non-zero port,
  and preformat the authority once per pool.
- Give every overridable `Config` field an explicit unset/inherit sentinel,
  distinct from valid values such as a zero timeout or zero retries.  Add an
  internal resolved configuration containing the effective link count,
  concurrency, per-link concurrency, transport/protocol selection, timeout,
  retry, redirect, retained-body, and retained-message settings for one pool.
- Make the initialization shape explicit in the public contract: client init
  receives the pool count and optional common `Config`; pool registration is
  `client.pool(slot, destination)` or
  `client.pool(slot, destination, config)` before `start()`.
- Validate both layers: reject invalid client defaults, invalid explicit pool
  overrides, and effective destination/protocol combinations that cannot run.
- Define the new type relationship explicitly in the header documentation:
  an application pool supplies the unordered `ZmPQTx` queue callbacks and
  response parser, while `Zhttp::Client` instantiates and coordinates those
  pools.  Resolve the existing `ZmPQTx` CRTP cycle by binding `TxQ` to the
  application pool type, not to the client coordinator.
- Expose slot-qualified operations (`enqueue`, `cancel`, and `seal`) on the
  coordinator, with direct delegation to the selected pool.  Provide aggregate
  and per-slot completion/failure/active counters without scanning pools on hot
  paths.
- Add the exact application rate-limit entry point
  `Client::limited(unsigned pool, unsigned link, bool value)`.  Validate both
  slots before dispatch.  Make it a thin cross-shard post carrying the two
  indices and value; mutate the link's `limited` flag only on the link/pool Rx
  owner.  Calls take effect in Rx-queue order and remain safe during shutdown.
- Define aggregate idle semantics: the client application is notified once all
  configured pools are sealed and drained.  A pool becoming idle updates the
  aggregate count exactly once.
- Update the commented Builder/Parser contract and the complete-client example
  in `zhttp/README.md` at the same time as the API so no stale arbitrary-URL
  example remains.

### 2. Extract the request machine into a server-specific pool

- Move `LiveReq`, request/attempt identity, the active hash, free slots,
  request timers, Tx acknowledgement handling, retry/redirect accounting, and
  parser callbacks from the current `Client` template into the new pool
  template.
- Size each pool's admission/free-slot state from effective concurrency, not
  link count.  Track the assigned stable link and protocol stream/FIFO position
  separately in every live operation.
- Read timeout, retry, redirect, and retained-body/message limits from the
  pool's resolved effective configuration.  A pool without overrides therefore
  has exactly the client-level behavior, while an override affects only
  requests submitted to that slot.
- Replace the current `LiveReq::route.url` with:
  - a reference to the immutable pool destination/authority;
  - an owning current request target used for retries and relative redirects;
  - pool-selected endpoint/protocol metadata.
- Cache `operation()` once per wire attempt on Tx for every HTTP version, not
  only H2/H3.  Use the cached method/target for H1/H2/H3 construction and retain
  the target needed by Rx redirect handling without reading Tx-owned builder
  state.
- Change `ClientMessage::ReqOps::host()` to emit the pool's preformatted
  authority.  Remove calls to `Request::host()` and migrate request types in
  the utility and tests.
- Keep request replay/body accounting and typed events intact, adding the pool
  slot to all emitted events/results.  Preserve exact-once completion and TxQ
  acknowledgement for queued, active, cancelled, timed-out, and failed work.

### 3. Replace request-lived links with persistent connection-owning links

- Factor the current protocol-specific `ClientPool`, `TLSClientPool`, and
  logical-link wrappers into link implementations owned by one public pool.
  Rename internal types where necessary so `Pool` consistently means the
  server-specific application pool rather than a protocol-wide collection.
- Allocate all link-slot objects while configuring the pool.  Connection
  establishment remains lazy in this change: the first assigned request calls
  `connect`; a later keepalive feature can call the same operation with no
  assigned request.
- Add a round-robin selection cursor and a pending-request path.  For the
  initial implementation, scan at most the fixed link count starting at the
  cursor, assign to the next non-stopping, non-limited, non-saturated link, and
  advance the cursor to the slot after it.  A disconnected link accepts an initial
  reservation and establishes its one connection; connecting and connected
  links accept further reservations only within their applicable pre-negotiated
  or negotiated capacity.  If all links are saturated or pool concurrency is
  exhausted, leave the request in the pool TxQ.
- Track reserved, emitted, and completed operation counts separately per link
  so asynchronous Tx/Rx posts cannot over-admit past capacity.  Recompute the
  link's saturated state only on its owning shard and notify the pool when a
  capacity transition can resume queued admission.
- Implement protocol-specific capacity checks:
  - H1 compares reserved/live FIFO depth with the effective configured H1
    pipeline limit, which is the common per-link concurrency value;
  - H2 caps active/reserved streams by the common per-link concurrency value
    used for `SETTINGS_MAX_CONCURRENT_STREAMS`, then applies peer stream limits,
    flow-control credit, and bounded frame/operation queues;
  - H3 caps active/reserved streams by the same common per-link concurrency
    value used in H3/QUIC negotiation, then applies peer stream credit,
    connection `MAX_DATA`, stream flow-control credit, and bounded
    frame/operation queues.
- Treat flow-control exhaustion as temporary saturation, not failure.  H2
  WINDOW_UPDATE and settings changes, and QUIC MAX_DATA/MAX_STREAM_DATA/
  MAX_STREAMS updates, must re-evaluate capacity and wake round-robin dispatch.
- Keep `limited` independent of capacity recomputation: protocol events may set
  or clear `saturated` but never alter `limited`.  `limited(true)` removes the
  link from future round-robin selection immediately in Rx order;
  `limited(false)` wakes queued admission if the link is otherwise eligible.
  If every link is limited and/or saturated, requests remain queued without
  polling or timer-driven rescans.
- Give each link an intrusive/identified collection of live operations plus the
  protocol-specific correlation state:
  - H1 maintains transmit order and a response FIFO because pipelined responses
    arrive in request order;
  - H2/H3 map native stream identity to the corresponding live operation.
- Give each live connection a generation under its persistent `Link`, and tag
  assigned/emitted operations with that generation so delayed callbacks from a
  closed connection cannot affect its replacement.
- Serialize H1 request construction onto the link's Tx stream without waiting
  for earlier responses.  Preserve body-commit ordering and do not interleave
  bytes from different H1 requests.
- On successful message completion, reset/destroy only that operation's
  parser/builder/stream state, decrement pool concurrency, acknowledge its TxQ
  node, and leave the link's live connection and other operations intact.
- When a reusable connection has capacity after completion, immediately admit
  queued work through the pool's round-robin cursor.  Never use completion
  timing as an implicit priority signal.
- On EOF, GOAWAY, reset, transport failure, a non-keepalive H1 response, or
  cancellation requiring connection teardown, detach or fail every affected
  operation exactly once according to protocol ordering, close only the
  connection, and leave the persistent link eligible for a lazy reconnect.
- Define H1 cancellation precisely: an unsent queued operation can be removed
  independently; cancelling an already emitted pipelined operation closes the
  connection when isolation is impossible and classifies/retries all affected
  later operations using their body-commit state.
- Cancel an emitted H2/H3 operation with its stream reset while preserving the
  connection and sibling streams.  Handle H2/H3 GOAWAY using stream identity:
  drain operations the peer accepted, retry only known-unprocessed operations,
  stop opening new streams on that connection, then reconnect the same `Link`.
- Ensure stop first rejects new submissions, cancels queued work, marks active
  work terminal, closes every native connection, drains Rx/Tx callbacks, and
  only then permits pool/client finalization.

### 4. Make native connection ownership honor pool and link identity

- Remove or bypass the current native TLS and QUIC hub behavior that keys one
  physical connection only by host/port.  Otherwise two configured link slots,
  or two distinct application pools for the same origin, can collapse onto one
  connection.
- Key any retained hub lookup by stable pool identity plus link slot, or have
  each stable link own its native connection directly.  The latter is
  preferable if it simplifies deterministic teardown and future per-link RTT
  state.
- Store at most one native transport connection handle in each persistent
  `Link`; native disconnect clears that handle but does not destroy or replace
  the `Link` or renumber its slot.
- Build connection-local protocol parameters from the pool's resolved common
  per-link concurrency: set the H2 `SETTINGS_MAX_CONCURRENT_STREAMS` input and
  the corresponding H3/QUIC concurrent-stream input (including
  `maxStreamsDuplex`) from that same value.  Do not mutate shared client hub
  configuration when a pool override differs; carry the resolved value into
  each native link/connection construction.
- For TLS, negotiate H1/H2 once for a link connection and bind the selected
  implementation to that stable slot.  H1 uses the link's ordered operation
  FIFO; H2 opens concurrent streams for operations assigned to the link.
  Reconnect the slot using the pool's cached selected protocol policy rather
  than creating an H1/H2 pair per request.
- For H3, retain the physical QUIC connection and H3 control/QPACK state across
  overlapping request streams.  Create and destroy only each operation stream;
  closing a stream must not retire the pool link or disturb sibling streams.
- Enforce both independent limits in diagnostics and tests: live native
  connections never exceed link count, while live operations never exceed
  concurrency and may be greater than link count.

### 5. Move discovery and protocol selection to pool scope

- Replace the client-wide multi-origin Alt-Svc cache and per-request discovery
  with one route-resolution state machine per pool:
  `unresolved -> resolving -> resolved`, plus a generation for deliberate
  protocol transitions.
- Resolve the configured host once per pool.  Queue admitted requests while
  resolution is pending, then let all link slots reuse the selected IP, port,
  and protocol (`H1/TCP`, `H1/TLS`, `H2/TLS`, or `H3/QUIC`).
- When the effective configuration selects plain HTTP, use H1/TCP directly.
  When it selects secure HTTP, apply its forced, disabled, or preferred H3
  policy and effective H2 policy once, including HTTPS DNS discovery and ALPN.
  These values come from the client defaults unless that pool explicitly
  overrides them.
- Cache a successful selection at pool scope.  Retries use that selection and
  do not independently resolve per request or per link.
- Make fallback or an accepted Alt-Svc upgrade a pool-wide route-generation
  transition: stop assigning work to the old generation, drain/close its links,
  update the cached selection once, and retry eligible requests on the new
  generation.  This prevents a pool from silently maintaining unrelated
  prevailing protocols at the same time.
- Preserve replay-safety rules during a pool-wide transition.  Requests that
  have emitted non-replayable bytes complete as indeterminate/replay-unsafe
  rather than moving to another protocol.
- Deterministically cancel a pending resolver operation during pool/client
  shutdown and release its cached endpoint/Alt-Svc storage during `final()`.

### 6. Turn `Client` into the pool-array lifecycle coordinator

- Pre-size an array of intrusive pool references using the pool-count argument
  supplied during client initialization.  Pool slots are never renumbered or
  compacted.
- Initialize shared transport infrastructure once, then initialize each pool's
  immutable destination, effective configuration, route cache, queue, timers,
  and stable links.
- Start configured pools as one engine operation.  Roll back already-started
  pools on a partial start failure.
- Dispatch public operations by slot with bounds/configuration checks outside
  hot protocol paths.  Do not let a request or redirect select a pool based on
  host.
- Route `limited(pool, link, value)` to the addressed persistent link without
  scanning pools or links.  Preserve the flag across connection loss and
  reconnect because it belongs to the persistent link, not its current native
  connection; reset it only during pool finalization/reinitialization.
- Stop all pools asynchronously and complete the client stop continuation only
  after every pool and shared hub has drained.  `final()` asserts empty queues,
  no active requests, no native links, no resolver work, and no retained-body
  accounting.

### 7. Migrate applications and documentation

- Update `zhttp/util/zhttp.cc` to:
  - parse the CLI URL once;
  - configure client-level protocol, timeout, retry, redirect, and retained
    limits from the CLI/defaults;
  - keep `-j`/`--jobs` as pool concurrency and add a distinct link-count option
    (default one) plus a per-link concurrency option so the utility can
    configure all three limits explicitly;
  - initialize the client with a pool count of one and common defaults for both
    link count and concurrency;
  - construct a host+port `Destination` from the URL and configure slot 0 with
    `client.pool(0, destination)` unless a pool-specific override is needed;
  - keep only the path/query in each request;
  - submit/cancel/seal through slot 0;
  - retain its bounded request-generation behavior and aggregate completion
    reporting.
- Update `zhttp/test/ZhttpClientCancelTest.cc` and
  `zhttp/itest/zhttpclientfallbacktest.cc` to the new pool/application model.
  Remove per-request URLs and configure the destination once per test pool.
- Update `zhttp/README.md`, `ZhttpClient.hh` contract comments, and the boundary
  fixture so the documented high-level API and layering checks describe
  server-specific pools.
- Search the complete tree for `Zhttp::Client<`, old `ClientConfig::concurrency`,
  old `client.pool` signatures, unqualified `enqueue/cancel/seal`, and
  high-level request `host()`/absolute URL members; propagate every breaking
  change rather than adding adapters.

### 8. Add focused multi-pool and reuse coverage

- Add a client-pool test binary under `zhttp/test` or `zhttp/itest` according to
  whether it launches complete loopback servers, and register it in the
  corresponding `Makefile.am`.
- Cover at least:
  - two slots targeting different servers with the same request paths and
    overlapping request keys; each server receives the correct authority;
  - two slots targeting the same origin but with independent policy/link
    configuration and no native-connection sharing;
  - inheritance of every client-level request policy by a pool with no
    overrides, plus isolated per-pool overrides for link count, concurrency,
    per-link concurrency, protocol selection, timeout, retries, redirects,
    retained-body limit, and retained-message limit;
  - invalid/missing/duplicate pool slot configuration;
  - per-pool enqueue, cancel, seal, idle, counters, and aggregate completion;
  - independent link/concurrency bounds, including 10 links carrying 100 live
    operations without ever owning more than 10 live native connections;
  - deterministic round-robin assignment across stable link slots, including
    wraparound, equal distribution of overlapping operations, skipping a
    saturated link, and restoring that link to rotation when capacity returns;
  - `limited(true)` skipping the addressed link while its active operations and
    connection continue, all links limited leaving requests queued, and
    `limited(false)` immediately resuming round-robin admission;
  - all four combinations of `saturated` and `limited`, proving that clearing
    either gate does not admit work while the other remains set;
  - invalid pool/link slots, repeated idempotent limit changes, limit changes
    racing connection loss, and limit calls during orderly shutdown;
  - multiple overlapping pipelined operations over one accepted H1/TCP
    connection and one accepted H1/TLS connection, with response FIFO and
    exact enforcement of configured per-link concurrency plus
    cancellation/failure ordering;
  - multiple simultaneous H2 and H3 streams per persistent physical connection
    plus continued reuse across later operations, with saturation at negotiated
    stream/flow-control limits;
  - one configured per-link concurrency value producing the asserted H1
    pipeline cap, H2 `SETTINGS_MAX_CONCURRENT_STREAMS`, and H3/QUIC concurrent
    stream limit, including a pool override that differs from the client
    default;
  - H2 WINDOW_UPDATE/SETTINGS and H3 MAX_DATA/MAX_STREAM_DATA/MAX_STREAMS credit
    updates clearing saturation and resuming queued round-robin dispatch;
  - repeated disconnect/reconnect cycles proving each persistent `Link` owns
    zero or one live native connection and retains the same stable slot;
  - one resolver query/selection per pool reused by multiple requests and
    links;
  - forced H3, disabled H3, H3-to-TLS fallback, and pool-wide selection
    generation changes;
  - same-origin relative redirects and rejection of cross-origin redirects;
  - stop/final during queued, resolving, connecting, active, and reusable-idle
    states, with exact-once completion and no leaked links or resolver work.
- Prefer semaphores/continuations and observable accept/request counters over
  time-based sleeps.  Extend existing diagnostics with pool slot and stable
  link slot where needed to make failures attributable.

## Verification

Use a clang debug build throughout.  Before making code changes, inspect the
existing configured compiler and build mode:

- If the tree is already configured as clang debug, do not reconfigure and do
  not clean it.
- If it is not clang debug (or is unconfigured), configure with
  `./z.config -d -L /usr`.  Only in this case, run one top-level `make clean`
  after configuration and one `make -j8` to establish a consistent baseline.
- Do not clean again during implementation.  In particular, compilation
  errors, template/header changes, and test failures are not reasons to clean.

Do not require a build at the end of each numbered implementation section and
do not rebuild after every edit.  Some cross-cutting slices will intentionally
leave `zhttp` uncompilable until the next related slice is complete.  Batch
work into coherent integration tranches, iteratively revise their boundaries
as dependencies become clear, then build only the affected layer and its
immediate dependents in dependency order:

1. Once a useful library integration checkpoint spans the necessary public
   API, pool, link, and transport pieces, run `make -C zhttp/src -j8` once.  If
   it exposes another missing refactor slice, complete that slice before the
   next build attempt rather than rebuilding after every compiler diagnostic.
2. Once the new API is stable enough to migrate a coherent unit-test group,
   run `make -C zhttp/test -j8`, then run the specific affected test binaries
   (using `libtool exec` where required).
3. Once the utility-facing slice is coherent, migrate `zhttp.cc` and run
   `make -C zhttp/util -j8` once.
4. Once the integration fixtures and behavior form a coherent slice, run
   `make -C zhttp/itest -j8`, then run the specific affected integration test
   binaries.

At final verification, run one incremental top-level `make -j8`, followed by
`make -C zhttp/test test`, `make -C zhttp/itest test`, and finally `make test`.
These commands must reuse the existing clang debug objects; do not precede
them with `make clean`.

Also run the new reuse/multi-pool tests repeatedly under the repository's
`libtool exec` workflow with debug assertions and leak checking.  Verify that
simultaneous live connection counts remain bounded by link count, live request
counts remain bounded by the independently configured concurrency, repeated
requests do not increase retained link/container counts, and shutdown returns
all client/pool/link heaps to their baseline.

## Explicitly Out of Scope

- Proactive connection establishment or keepalive traffic before the first
  application request.
- RTT mean/standard-deviation tracking, slow-link classification, or automatic
  replacement of outlier links.
- Any adaptive link scheduler; round-robin is the complete selection policy for
  this implementation.
- Application rate calculation, token buckets, timers, or automatic changes to
  `limited`; the library exposes and enforces the gate, while the application
  decides when to change it.
- Dynamic pool insertion, deletion, resizing, or destination reconfiguration
  after `start()`.
- Compatibility overloads accepting arbitrary per-request absolute URLs.

## Completed Follow-on Work

- Investigated the intermittent `zi/test/ZiMxLoopTest` hang and reproduced it
  during repeated execution.  `ZiMultiplex::stop__()` could clear the Rx wake
  callback and close the epoll wake descriptors before `ZmScheduler::stop__()`
  marked worker queues EOF.  If Rx consumed the preceding wake first, it could
  re-enter `epoll_wait` with no remaining scheduler wake path.  Teardown now
  leaves the I/O platform active until scheduler workers have exited; the test
  subsequently passed 1,000 consecutive timed runs.

The stable pool and link identities, separated connection/request lifecycles,
and pool-level route cache created here are the required extension points for
the deferred keepalive and RTT work.
