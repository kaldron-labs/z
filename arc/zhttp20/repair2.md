# Lifecycle consolidation with ZmEngine

## Objective

Use `ZmEngine` as the canonical start/stop controller for `ZiResolver`,
`zjrpc`, `zmcp`, `zrest`, `ztc`, and `zum`. In `zhttp`, make `PoolImpl` and
`ClientCore` engine completion cover their entire lifecycle, including work
currently performed before or after their transport controllers complete.

This is an implementation plan. Preserve existing working-tree changes and
the current build configuration and artifacts. Follow the milestone-based,
incremental validation policy below.

## Governing requirements

[`docs/GUIDELINES.md`](docs/GUIDELINES.md) is authoritative in full, together with
applicable `AGENTS.md` and module guidelines. Read the relevant rules before
each implementation stage. The requirements below apply them to this repair;
they do not replace or narrow the remaining guidelines. Explicit user
instructions take precedence. The build and validation policy below is an
explicit user-directed override of the guidelines' broader build-matrix and
sanitizer acceptance requirements for this implementation.

For each repair, identify the required behavior, violated invariant, and
owning component before editing. Audit scope is not edit scope: make the
smallest demonstrated change, report unproven concerns, and preserve unrelated
code, diagnostics, tests, standard streams, and platform-normalizing APIs.
Account for allocations, copies, references, lookups, and hot-path work in
each design decision. Do not add validation for conditions already guaranteed
by the caller contract or framework, or broaden the repair merely to clear
an audit flag. Delete obsolete lifecycle code and compatibility paths; retain
deliberately preserved diagnostic probes unless shown to be obsolete.

## Required contract

- `init` establishes configuration and resources with the engine stopped.
- For server daemons governed by Zdb, preserve two distinct control levels.
  Main starts the multiplexer independently of cluster activation. Zdb `up`
  starts network listeners/protocol hubs and `down` stops them; a standby
  process must not listen merely because its process runtime is Running.
  If activation requires asynchronous preparation, continue the `up` operation
  on the DB thread with an active-role/generation check before listener start.
  Shutdown of the process runtime also joins any outstanding role-controlled
  listener transition. Keep database readiness separate from engine state.
- `start` and `stop` use `ZmEngine` transitions, callback handling,
  idempotence, and pending opposite operations. Support start after a
  completed stop without requiring another init.
- `started(true)` means the component's defined startup operation, including
  required dependency startup, has completed. Establish this boundary from
  its contract; do not conflate it with connection, authentication, database
  activation, or other protocol readiness. Failed startup must finish rollback
  before `started(false)` exposes Stopped.
- `stopped(true)` means all component-owned work has drained or been
  cancelled, all relevant shard continuations have completed, and teardown
  can safely proceed. A stopped transport alone is insufficient.
- Stop is idempotent and best effort: attempt every shutdown step and continue
  past individual errors. Completion leaves the engine Stopped even when its
  callback reports failure. Never restart stopped resources to undo shutdown.
  A separately queued start performs a fresh startup after stop completion.
  Drain outstanding callbacks before final releases their owners. Do not map
  a prior request or protocol failure to shutdown failure after cleanup succeeds.
- The application must complete stop before calling `final` on a started
  component. `final` must neither call stop nor wait for shutdown. It may use
  `ZmAssert_` to enforce Stopped and drained-resource preconditions.
- An initialized component that never started, or whose startup failed and
  completed rollback, can be finalized directly. Inspect resource destructors
  as well as explicit final bodies for hidden joins or waits.
- Use assertions for fatal application contract violations. Handle expected
  network, protocol, cancellation, and resource-pressure failures through
  normal completion/error paths.

`ZmEngine` supplies start/stop control, not init/final implementations. Keep
resource initialization and finalization in their owning component.

## Design rules

1. Give each independently controlled component one authoritative engine.
   Prefer direct CRTP inheritance; use a contained controller where an
   existing facade or private implementation requires it. A child transport
   can retain its own engine, but must not determine its parent's state.
2. Implement `start_()` and `stop_()` as asynchronous operations ending in
   `started(ok)` and `stopped(ok)`. Use the existing owner-shard dispatch
   facilities and engine spawn/wake hooks where necessary. Do not introduce
   a control thread merely to use `ZmEngine`.
3. Remove duplicate lifecycle enums, start/stop waiter queues, and flags such
   as `m_stopDone` and `m_finalOK` once engine completion covers their purpose.
   Retain protocol states, ingress gates, and per-shard drain bookkeeping
   where they represent distinct operational facts.
4. Keep Rx and Tx state on their owning shards. Engine synchronization does
   not authorize cross-shard access. Snapshot fixed metadata and post handles
   for variable-size data using the existing framework patterns.
5. Expose engine APIs with `using` declarations where sufficient. Retain
   wrappers only when they add required dispatch, result translation, or
   checks justified by the contract. Hide bypassing inherited lifecycle APIs
   with private `using` declarations where necessary. Propagate API changes
   to dependents without shims or pure base-forwarding functions.
6. Review callback lifetime and reentrancy before replacing existing queues.
   Preserve the repaired idempotent engine paths that unlock before invoking
   callbacks. Check synchronous hook completion, queued opposite operations,
   and callbacks that finalize an owner; do not promise destruction safety
   while an operation or continuation still requires that owner. Any further
   demonstrated engine defect belongs in `ZmEngine`, not a module workaround.

### Sharding, teardown, and liveness

- Public cross-shard operations are thin dispatchers using `rxRun`/`rxInvoke`
  or `txRun`/`txInvoke` and trailing-underscore owner-shard implementations.
  Use `ZiAssert` for appropriate shard checks. Never access Rx and Tx state
  from the same thread or add locks to permit non-owner access.
- Follow the three-phase I/O teardown: prevent ingress and new work; post an
  Rx continuation to drain Rx; from it post a Tx continuation to drain Tx;
  complete teardown and release ownership after that drainage. Preserve any
  additional demonstrated dependency barriers before publishing completion.
- Cancel each owned timer with `ZmScheduler::del`, prevent its callback from
  initiating further work, and post a drain continuation on the callback's
  own thread before releasing its owner. Schedule timers with absolute times.
- Treat scheduler run/invoke as asynchronous. Consume results in continuations.
  Never block an I/O or worker shard with synchronous lifecycle calls, joins,
  `ZmBlock`, or `ZmSemaphore`. Blocking is confined to the main thread; there
  use the framework primitives rather than polling or sleeps.
- Bound drain loops and container traversals on threads serving mixed work.
  Use workload-appropriate budgets and post continuations for the remainder.
  End iterator lifetimes before callbacks, mutations, or unrelated posts.
- Keep raw back-pointers from short-lived work to its longer-lived owner.
  Do not add owner reference counts in buffers or callback captures to mask
  incomplete teardown. Drain dependents before destruction; destructors assert
  that owned activity is gone. Reclaim resources deterministically without
  periodic garbage-collection scans or monotonic growth across restart cycles.

### Framework and source conventions

- Use GNU C++2b, CRTP, compile-time dispatch, SFINAE/detector traits, and safe
  base defaults. Do not introduce concepts, `requires`, anonymous namespaces
  in `.cc`, `enum class`, or non-specific `[&]`/`[=]` captures. Factor shared
  logic without new virtual interfaces or weak generic abstractions.
- Use Z containers, callbacks (`ZmFn`), formatting, logging, and type mechanics.
  Minimize STL. Keep template-independent declarations outside templates.
  Use sentinel state instead of redundant initialized/null flags.
- Minimize allocation and copies. Use concrete `ZmNHeap` as the first base
  with ADL heap identification for fixed-size allocated types; use `ZmVHeap`
  only for variable-sized allocations. Honor the `ZmSingleton`/`ZmSpecific`
  exceptions. Do not check framework allocation results for null.
- Preserve intrusive nodes and appropriate `ZuRef`/`ZmRef` ownership. Keep
  I/O buffers pooled; pass handles plus fixed metadata across shards and
  retain variable-sized data in destination-owned buffers/queues. Use
  workload-aware scratch/container storage without arbitrary capacities.
- Preserve cache-line separation of shard-owned data. Avoid needless atomics,
  locks, telemetry snapshots, zero initialization, and temporary conversions.
- Follow the header skeleton and include direct dependencies, including
  `ZmEngine.hh` where used. Keep headers acyclic and preserve the hybrid
  header/source organization.
- Use hard tabs, tab stops of 8, logical indentation of 2, and the 98-column
  source limit. Follow local layout and member alignment. Names are camelCase
  with a 28-byte maximum and established abbreviations. Struct data is public,
  unprefixed, and first; class data is private, `m_`-prefixed, and last.
  Friendship does not authorize access to another owner's private state.
- Apply the guidelines' assertion distinctions: `ZuAssert` for compile-time
  assertions, `ZmAssert_` for always-fatal integrity/precondition violations,
  and debug-only `ZmAssert` only where debug abortion is intended. Use normal
  error handling for expected failures; use the documented resilient
  `ZiAssert`/two-argument `ZmAssert` forms only where appropriate. Never turn
  routine malformed input or resource pressure into an assertion failure.

## Implementation order and scope

### 1. ZiResolver

Files: `zi/src/ZiResolver.hh`, `zi/src/ZiResolver.cc`.

- Make resolver `Main` use an engine for its complete scheduler, event-loop,
  and resolver-channel lifecycle. The scheduler's engine is only a child.
- Replace the private Stopped/Starting/Running/StopPending/Stopping lifecycle
  and associated start/stop callback queues with engine control. Review the
  existing Finalizing state against the application-owned final precondition.
- Start the scheduler and loop in order; finish failure rollback before
  completing startup. Stop query admission, timers, and outstanding resolver
  requests; drain the loop and scheduler before completing stop.
- Separate engine boolean completion from resolver diagnostic payloads.
  Adapt or change the public result API deliberately and update all callers;
  do not recreate an independent lifecycle queue to preserve the old API.
- Address the current final path's scheduler destruction/control-thread join:
  establish a completion boundary and ownership arrangement that permits
  final to release already-quiescent resources without waiting.
- Ensure a subsequent start can recreate the necessary runtime resources
  from retained configuration.

### 2. zhttp

Files: `zhttp/src/ZhttpClientImpl.hh`, `zhttp/src/ZhttpClient.hh`,
`zhttp/src/ZhttpClient.cc`; inspect `ZhttpTransport.hh/.cc` as dependencies.

**PoolImpl**

- Establish complete-pool engine control. Reuse or extend an existing owning
  controller if it can cover the complete operation; otherwise add the pool
  engine and keep `m_hubs` only for its distinct child transport aggregate.
  Do not duplicate engines for the same lifecycle boundary.
- Complete startup only after transports, Rx state, Tx state, and request
  queue admission are ready.
- Move ingress shutdown, timer/discovery cancellation, transport shutdown,
  Tx drainage, and the final Rx continuation into the engine stop operation.
  Preserve the existing queue boundaries that retire already-dequeued work.
- Complete stop only when request slots, links, discovery posts, and queued
  owner callbacks are quiescent. Assert that state in final.
- Reset operational state within startup so repeated start/stop cycles work.

**ClientCore**

- Give the complete client an engine boundary that includes resolver
  startup/shutdown and the pool aggregate currently held in `m_ctl`. Evaluate
  extending that controller before introducing another; retain nested
  controllers only for independently owned lifecycle boundaries.
- Start the owned resolver before pools. Roll back dependencies started by
  this attempt if a later stage fails. Preserve externally owned resolver
  semantics; do not stop a dependency the client does not own.
- Complete stop after pools and any owned resolver have fully stopped.
- Keep the typed client/pool facade delegation, while ensuring public
  completion reports this complete operation rather than an inner engine.

### 3. zjrpc

Files: `zjrpc/src/ZjrpcStdio.hh`, `ZjrpcClient.hh`, `ZjrpcServer.hh`.

- Adopt engine control in the shared `IOEndpoint` used by `IOClient` and
  `IOServer`; avoid adding another engine to each facade for the same work.
- Adopt complete-endpoint engines for `HTTPClient`, `HTTPServer`, `WSClient`,
  and `WSServer`. Include RPC cancellation/drain and connection cleanup in
  stop completion, beyond HTTP/WebSocket transport completion.
- Preserve application protocol errors separately from lifecycle results.
- Resolve stdio restart explicitly: retain configuration and establish handle
  ownership so runtime links can be reconstructed after stop. Never restart
  using a closed handle or silently take ownership of an application handle.

### 4. zmcp

Files: `zmcp/src/ZmcpClient.hh`, `ZmcpServer.hh`.

- Adopt engine control for `HTTPClient_::Client` (public `HTTPClient`),
  `IOClient`, `WSClient`, `HTTPServer`, `IOServer`, and `WSServer`.
- Include MCP peer/session cleanup, RPC/batch cancellation, SSE producers,
  request contexts, and outstanding owner-shard work in stop completion.
- Keep MCP handshake/session protocol state separate from lifecycle state;
  define which readiness condition startup promises using the existing API.
- Share lifecycle implementation where endpoint bases genuinely own the same
  operation. Do not put engines on parser/dispatcher classes merely because
  they have an init method.
- Apply the stdio ownership/reconstruction solution consistently with zjrpc.

### 5. zrest

Files: `zrest/src/ZrestClient.hh`, `ZrestServer.hh`.

- Give Client and Server engines covering HTTP plus REST-owned work.
- Include calls, responses, SSE work, and completion callbacks in the drain
  boundary; remove parallel completion flags when redundant.
- Make synchronous and asynchronous startup failure produce the same stopped
  state after rollback. Reset ingress/drain state on subsequent startup.
- Preserve resilient malformed-request handling and existing response policy.

### 6. zum

Files: `zum/src/ZumService.hh/.cc`, `zumd_daemon.hh/.cc`, and lifecycle
dependencies discovered through these controllers.

- Adopt complete-component engine control for `Zum::Service` and
  `Zum::Daemon`, including their private state implementations as appropriate.
- Model startup/rollback and shutdown of HTTP, database, publication,
  maintenance, and outstanding service work in dependency order.
- Keep authentication, service readiness, and maintenance protocol state
  separate from engine lifecycle state.
- Audit the rest of the module for implementations of the same four-method
  contract. Do not infer a lifecycle from unrelated request start methods.

### 7. ztc

Files: `ztc/src/ZtcApp.hh/.cc`, `ztcagent_daemon.hh/.cc`,
`ztchub_daemon.hh/.cc`, plus dependent application entry points.

- Adopt engines for `Ztc::App`, `Ztc::Agent`, and `Ztc::Hubd`.
- Replace their independent lifecycle orchestration with engine hooks while
  preserving registration, publication, connection, and telemetry state.
- Complete startup only after the dependencies promised by each public API
  are ready. Roll back partial startup in reverse dependency order.
- Drain timers, requests, subscriptions, sessions, telemetry, and transports
  before reporting stopped. Final releases already-stopped resources.
- Audit remaining module controllers and update dependent main paths to
  complete explicit stop on every successful-start exit path.

Implement zum before ztc where ztc controllers consume `Zum::Service`.
Within every stage, update dependent code when the owning API changes.

## Dependent code and documentation

- Inspect all source, test, integration, example, utility, benchmark, and
  interoperability directories in the affected modules, plus external module
  callers found by symbol searches.
- Remove reliance on final initiating shutdown. Ensure stop is executed even
  after connection/request failure or short-circuited application conditions.
- Update lifecycle documentation and examples to state completion ownership,
  restart behavior, and the stopped precondition of final.
- Preserve the existing `ZdbusClient` name. This plan does not migrate
  `zdbus`, `zgrpc`, data-store implementations, or terminal components.

## Validation and completion criteria

Perform source/diff review throughout implementation, batching source changes
into large, coherent batches. Rebuild and retest infrequently, only at major
milestones; do not build or test after individual edits or each component.
Record deferred checks explicitly rather than claiming runtime validation.

Retain the current compiler, configuration, release/debug flags, platform, and
build artifacts. Rebuild incrementally with `make -j8`, reusing existing
artifacts. Do not reconfigure, clean, or introduce another build directory or
configuration. Other compilers, release/debug flags, and platforms are out of
scope; ASan, LSan, gcc, and MinGW validation are explicitly out of scope.

Use these major milestones:

1. Complete the ZiResolver and zhttp lifecycle changes as one foundational
   batch. If a build is needed at this milestone, rebuild only the changed
   library targets incrementally; defer dependent builds and tests.
2. Complete the remaining module migrations and all dependent source/API
   updates as one implementation batch. Defer dependent rebuilding until
   this implementation milestone is complete.
3. At implementation completion, incrementally rebuild affected libraries and
   their dependents in dependency order, then run the relevant tests. The
   Makefile hierarchy does not rebuild cross-directory dependencies
   automatically; refresh changed library layers before dependent targets.
   Build the applicable default `all` targets before `make test`. Require
   warning-free builds. Batch any resulting repairs before another validation
   pass, except where a targeted check is necessary to diagnose a failure.

Use a top-level incremental build at the final milestone if needed to refresh
all affected dependents; retain and reuse current artifacts throughout.

Use `ZuTestUtil`/`ZuTest` and TAP, with `ZuTestUtil::parse` and `log` diagnostics.
Place isolated cases in `test`, complete-program/network/environment cases in
`itest`, and third-party conformance fixtures in `interop`. Wire repository
tests into their directory's `make test` target and document new entry points.
Use CamelCase module-prefixed unit-test basenames and lowercase integration
program basenames. Synchronize deterministically with continuations and
main-thread `ZmBlock`/`ZmSemaphore`, never sleeps or completion polling.

Exercise meaningful lifecycle cases through existing tests, extending
coverage for agreed invariants or reproduced failures where missing:

- init followed by final without start;
- successful start, repeated start, stop, repeated stop, then final;
- restart after completed stop;
- startup failure at each dependency stage, completed rollback, then final;
- stop during startup and start during shutdown;
- multiple completion callbacks and lifecycle reentrancy;
- expected request/protocol failure followed by successful shutdown;
- outstanding requests, timers, discovery, SSE, and shard work at shutdown;
- final after stop completion, including supported callback-driven teardown;
- fatal final-before-stop misuse in an isolated process.

Use Valgrind sparingly: for targeted memory-corruption debugging when needed,
and as the final memory-leak and corruption gate over relevant lifecycle
tests, including repeated start/stop cycles. Do not run it routinely between
source batches. Use `libtool exec` for source-tree debugging tools; never run
`.libs` binaries directly. Acceptance is limited to the current configuration:
passing relevant tests and the final Valgrind gate. Record deferred checks
explicitly; source review alone does not satisfy runtime acceptance. Do not
add sanitizer, alternate-compiler, or cross-platform gates.

For each component, review that the owning engine remains in the appropriate
transition state (including StopPending/StartPending for opposite requests)
until the last dependency/shard step completes, and that no duplicate
lifecycle state machine or redundant completion flag survives. Confirm that
final neither initiates shutdown nor waits, and that protocol hot paths retain
their existing allocation and shard-ownership properties.
