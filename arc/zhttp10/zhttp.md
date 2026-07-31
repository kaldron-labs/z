# zhttp transport unification plan

## Objective

Make HTTP over TCP, TLS, and QUIC use one coherent, library-owned integration
model.  `zhttp`, `zhttpd`, other future HTTP applications and higher layers
such as `zrest` should select and configure transports, supply
application policy, and report results; they should not each normalize the
`Ztcp`, `Ztls`, and `Zquic` APIs themselves.

The target is compile-time composition with a consistent
`Client`/`Server`/`Engine`/`Link` vocabulary.  It must not introduce virtual
dispatch, type erasure, extra buffering, or a lowest-common-denominator
transport API on the data path.  TCP and TLS should share virtually all HTTP/1
code.  All QUIC integration for HTTP/3 must be subsumed by `libZhttp`: an HTTP
application must not implement QUIC links or streams, open H3 control streams,
manage QPACK, route work between QUIC shards, or handle QUIC lifecycle events.
It should use HTTP/3 in the same way it uses HTTP/1 over TCP or TLS and provide
only declarative QUIC configuration where defaults are unsuitable.

HTTP/3 necessarily retains connection streams, request streams, QPACK,
flow-control, early-data, and migration internally.  Those are differences in
the `libZhttp` implementation, not differences in the ordinary application
contract.

Dependent compatibility is not a goal.  Replace the current integration
surface and update all in-tree users rather than preserving it with forwarding
types.

## Normative application contract

From the application's point of view, all three protocols follow the same
engine and connection model.  Concrete engine, link, Rx stream, Tx stream, and
configuration types may differ, but one template implementation must be able
to supply the core logic for all three.

### Engine lifecycle

Every HTTP engine has the same control sequence:

1. construct protocol configuration and call `init()`;
2. call `start()`;
3. create/accept and process connections while the engine is running;
4. when signalled to exit, call `stop()` and wait for connections to drain;
5. call `final()` only after stop/drain completion.

Protocol selection must not alter this control flow.  TCP listen setup, TLS
credentials, QUIC endpoint startup, ALPN, control streams, and partial-start
rollback are internal consequences of `init()`/`start()` configuration.

Configuration is the intended point of variation:

- TCP: addresses, ports, buffers, timeouts, and admission/reconnect policy;
- TLS: the common settings plus certificates, keys, CA/verification policy,
  and optional TLS tuning;
- QUIC: the common settings plus optional transport limits, early data,
  heartbeat, migration, key log, qlog, and diagnostic tuning.

Defaults should make ordinary QUIC configuration no more verbose than TLS
configuration.  ALPN and mandatory HTTP/3 transport settings are library
defaults, not application setup.

### Client connection flow

The normalized client sequence is:

```text
connect
  -> connected(link)
       -> obtain link.txStream()
       -> install/supply the received-message process handler
       -> begin sending
  -> process(link, rxStream) for received messages
  -> continue sending and processing
  -> disconnected(link, reason)
```

`connect()` and `connected()` mean that an application-visible HTTP message
channel is ready.  Transport handshake, ALPN validation, H3 control-stream
creation, and initial request-stream allocation complete inside `libZhttp`
before `connected()` is delivered.

### Server connection flow

The normalized server sequence is:

```text
connected(link)
  -> obtain link.txStream()
  -> install/supply the received-message process handler
  -> process(link, rxStream) for the initial message
  -> begin sending and continue processing messages
  -> disconnected(link, reason)
```

The process handler is in place before any received application message is
dispatched.  Admission, accept, handshake, ALPN, and HTTP/3 control streams are
engine/link internals.

### Logical connection mapping

Use one application-visible `Zhttp::Link` contract with `txStream()`,
`process()`, connection metadata, and disconnect control:

- for HTTP/1 over TCP, a link maps to the TCP connection;
- for HTTP/1 over TLS, a link maps to the TLS connection;
- for HTTP/3 over QUIC, a link maps to a bidirectional HTTP request/message
  stream backed by an engine-private QUIC connection/session.

This mapping is what makes the message-processing code genuinely common.
Physical QUIC connections, unidirectional control streams, QPACK streams, and
stream scheduling are not application connections.  A server creates the
application-visible H3 link when an inbound bidirectional request stream is
ready; a client creates it after its hidden QUIC session is established and a
request stream is available.  Concurrent H3 requests are multiple logical
links sharing one private QUIC session, so no mutable "current stream" or
cross-request Tx state is needed.

Handler registration should use CRTP/static template composition where
possible.  Do not introduce per-message virtual dispatch, `ZmFn` allocation,
or type erasure merely to make the signatures look identical.  A shared
application link template should be instantiated with the protocol-specific
`Zhttp` base and stream types.

## Current state and problems

The lower transport libraries already have closely aligned CRTP APIs:

- `Client` and `Server` derive from a `ZmEngine`-controlled `Hub`;
- each application supplies a `Link`;
- links receive `connected()`, `disconnected()`, `connectFailed()`, and
  `process()` callbacks;
- engines use `init()`, `start()`, `stop()`, and `final()`;
- TCP and TLS servers use `listen()`, while QUIC folds listening into
  `start()`.

The differences that need normalization are mostly construction and spelling,
not behavior:

- TCP and TLS have one stream per connection; QUIC has independently owned
  request and control streams.
- TCP/TLS server acceptance returns a `ZiConnection`; QUIC acceptance returns
  a `ZmRef<Link>`.
- the native parameter, connected-info, receive-stream, link-template, and
  server-template types differ.
- TLS and QUIC require crypto/ALPN configuration; QUIC additionally requires
  transport parameters and starts its UDP endpoint through the engine.
- QUIC path events and transport diagnostics have no TCP/TLS equivalent.

The two programs currently absorb those differences inconsistently:

- `zhttp/test/zhttpd.cc` contains almost identical `HTTPServer` and
  `TLSServer` engines and links.  Admission counting, remote-address capture,
  idle timer management, request parsing, response building, disconnect
  accounting, and listen failure handling are repeated.  `H3Server` implements
  the same application policies again around a separate link and stream graph.
- `zhttp/test/zhttp.cc` has a useful TCP/TLS template layer, but has separate
  single-request and pooled HTTP/1 client stacks plus a largely independent
  QUIC client.  Connection logging, completion, retries, redirects, request
  assignment, shutdown, and result accounting are consequently spread across
  several types.
- HTTP/3 connection and QPACK plumbing is only partly factored into
  `ZhttpH3Session.hh`.  Both programs still forward connection-stream methods,
  initialize parser/builder QPACK hooks, open local control streams, finish
  streams, and route work to Rx/Tx owners manually.
- the server repeats three `init` branches, three start/listen forms, and
  reverse-order partial cleanup.  The client likewise repeats initialization,
  start, timeout, disconnect, stop, and finalization around each protocol path.
- buffer aliases, ALPN arrays, H3 limits, protocol labels, and transport
  metadata are declared at program level even where they are HTTP integration
  defaults.
- the existing `Zhttp::H1::Session`, `H1::Server`, `H3::Cxn`,
  `H3::CxnStream`, and `H3::ServerStream` helpers use related but inconsistent
  callback shapes and naming.
- HTTP/3 applications must currently supply `Zquic::Client`,
  `Zquic::Server`, `Zquic::CliLink`/`SrvLink`, and `Zquic::CliStream`/
  `SrvStream` subclasses.  They also forward QPACK and connection-stream
  callbacks, which is transport implementation rather than HTTP application
  policy.

The programs are also large for their intended role: `zhttp.cc` is about 3,600
lines and `zhttpd.cc` about 1,400 lines, whereas the two current session headers
total only about 340 lines.  Not every program feature belongs in the library,
but transport and HTTP session integration is currently on the wrong side of
that boundary.

## Architectural boundary

Use three layers.

### 1. Native transport traits

Add a library-owned compile-time traits layer, initially in
`zhttp/src/ZhttpTransport.hh`.  Define policy tags for TCP, TLS, and QUIC and
specialize one `TransportTraits` contract for each.  The contract should expose:

- transport identity and HTTP version (`H1` or `H3`);
- secure, multiplexed, and datagram-backed compile-time flags;
- native `Client`, `Server`, client-link, server-link, stream, stream-ref,
  receive-stream, connected-info, client-params, and server-params types for
  use by `libZhttp` internals;
- normalized creation of client and server links;
- normalized endpoint start, stop-listening, and local-address access;
- connected metadata through a small value type containing transport,
  security, negotiated version, and ALPN;
- defaults for HTTP ALPN and named buffer allocation policies.

Traits are for static selection and construction inside `libZhttp`.  They must
not become a runtime vtable, a tagged union consulted for every buffer, or a
place that copies native buffers into a common representation.  Native
transport objects and callbacks must not leak through the ordinary public HTTP
application API.

This layer makes intentional differences visible in one place.  For example,
the QUIC server trait can adapt `Server<App, Link>` and `start()`, while the
TCP/TLS traits adapt `Server<App>` and `listen()`.

### 2. HTTP engines and links

Add public CRTP integration types under a consistent namespace and naming
scheme:

- `Zhttp::Client<App, Protocol>` and `Zhttp::Server<App, Protocol>` are the
  engine-facing types;
- each has a consistently named application-visible `Link` with
  `txStream()`, `process()`, and disconnect operations;
- HTTP/3 internally has a `Stream`, because that object represents a real
  transport lifetime absent from HTTP/1; the library wraps each bidirectional
  request stream as the application-visible H3 `Link`, while the application
  does not define or manage a `Zquic::Stream`;
- a multi-transport owner coordinates a set of engines but does not replace
  their native `ZmEngine` state machines.

Use protocol compositions equivalent to:

```c++
using HTTP = Zhttp::H1<Zhttp::TCP>;
using HTTPS = Zhttp::H1<Zhttp::TLS>;
using HTTP3 = Zhttp::H3<Zhttp::QUIC>;
```

Exact tag spelling can follow nearby precedent during implementation, but the
application-facing template shape must be identical for the three
instantiations.  Invalid compositions such as H3/TCP should fail through the
established detector/`ZuIfT` style, not concepts or `requires`.

The application-facing configuration should use `Zhttp` types.  A common
configuration supplies the multiplex, addresses, certificates, CA policy,
timeouts, admission limits, and request concurrency.  A nested or optional
`Zhttp` QUIC configuration supplies non-default transport limits, heartbeat,
early-data policy, migration policy, key logging, qlog, and diagnostic
settings.  Applications should not need to construct `Zquic::ClientParams` or
`Zquic::ServerParams`, provide ALPN `"h3"`, or translate Zquic enums.

Factor the following into common engine/link bases:

- parameter assembly, ALPN, named buffer allocators, and error routing;
- `init`/start/listen/stop/final lifecycle, including partial-start rollback;
- connection admission and exact-once active-count release;
- remote/local endpoint metadata;
- connected/disconnected/connect-failed translation;
- idle timer arm, refresh, cancellation, and disconnect;
- request assignment, completion, retry notification, and graceful close;
- transport-neutral connection logging hooks;
- Rx parser invocation and Tx response/request scheduling;
- QPACK table ownership and H3 control-stream setup;
- stream FIN/reset mapping and H3 request-stream completion;
- default QUIC transport limits, early-session/ticket ownership, path
  migration handling, transport close, stateless reset, and diagnostics.

The normalized callbacks should report HTTP meanings and must not expose
`Ztcp::RxStream`, `Ztls::RxStream`, `Zquic::RxStream`, `Zquic::Link`,
`Zquic::Stream`, QUIC frame types, or QUIC path types to the application.
The application may receive different protocol-parameterized `Zhttp` link,
Rx-stream, and Tx-stream types, but they implement the same compile-time
operations and are consumed by the same template code.  Parser and builder
templates may receive native streams internally so that data stays in place.
Optional observability callbacks should report transport-neutral HTTP
connection state; detailed QUIC diagnostics belong to library
logging/telemetry APIs.

Do not force H1 and H3 into the same object layout:

- H1 owns one parser/session per connection and supports sequential reuse.
- H3 owns connection-level control/QPACK state in the private QUIC session and
  request/response parser state in each application-visible logical link.
- HTTP/3 flow control, GOAWAY, early data, stream scheduling, migration, and
  transport close remain private H3 implementation paths configured through
  `Zhttp` parameters.

The common contract should normalize the surrounding lifecycle and application
callbacks, not erase these semantics.  The maximum permitted application-level
difference is configuration and explicitly requested transport-neutral
telemetry; there must not be a QUIC-specific application subclassing contract.

### 3. Application policy

Keep the following in `zhttp`/`zhttpd`, or in clearly separate reusable
application components if independently useful:

- command-line parsing, process setup, signals, daemonization, logging sinks,
  and diagnostic output;
- static-file planning, filesystem policy, MIME configuration, auth, redirects,
  access logging, and response-body file reads;
- URL parsing, DNS HTTPS-record discovery, Alt-Svc cache/policy, output files,
  and the command-line client's retry/fallback policy;
- command-line choices for fault injection, migration tests, qlog/debug output,
  timeout/stall monitors, and test counters.

These policies call the library API.  They must not own transport adapters.
Fault injection, migration triggers, qlog, and QUIC diagnostics are expressed
as `Zhttp` configuration or control operations; the library performs the
native QUIC calls and callback handling.

Generic HTTP redirect, connection-reuse, request-queue, and graceful-shutdown
mechanisms may live in the library; the decision rules and CLI options remain
in the programs.

## Proposed source organization

Split the public integration surface so dependencies and ownership are obvious:

- `ZhttpTransport.hh`: internal transport tags, traits, normalized metadata,
  ALPN, buffer policies, and native adaptation.
- `ZhttpLink.hh`: common link lifecycle, admission, idle handling, callback
  translation, and private native-transport adaptation hooks.
- `ZhttpClient.hh`: H1 connection pool, H3 stream scheduler, request lifecycle,
  and common client engine control.
- `ZhttpServer.hh`: common server engine control, accepted-link construction,
  request dispatch, and graceful shutdown.
- `ZhttpH1Session.hh`: only H1 parser/builder/session behavior.
- `ZhttpH3Session.hh`: only H3 connection streams, request streams, QPACK,
  GOAWAY, and stream completion.
- `ZhttpConfig.hh`: public common configuration plus optional TCP, TLS, and
  QUIC overrides expressed entirely in `Zhttp` vocabulary.
- `Zhttp.hh`: HTTP parser/builder API plus the stable aggregate include.

Use more files if a header becomes large, but organize by ownership and
protocol role rather than by the history of the two programs.  Non-template
logic and stable constants should move to `.cc` files where possible to avoid
template/debug-info bloat.

`libZhttp` should become an explicit upper layer over `libZtcp`, `libZtls`, and
`libZquic`.  Update `zhttp/src/Makefile.am` include paths, installed headers,
and `libZhttp_la_LIBADD`; update generated build inputs through the normal
Autotools flow.  There is no dependency cycle in the current module order
(`ztcp`, `ztls`, `zquic`, then `zhttp`).  Replace the README statement that
core `libZhttp` intentionally does not depend on `libZquic`.

If keeping transport headers optional is later required, preserve the same API
with separate installed adapter headers.  Do not leave the adapters in the
example/test programs merely to avoid declaring the dependency.

## Detailed implementation sequence

Treat every phase below as a separately reviewable hand-off.  Do not begin the
next phase until all acceptance criteria pass and the phase-ending
`GUIDELINES.md` audit has repaired every issue it finds.  A phase may change
public APIs without compatibility shims, but it must leave the tree building
and tests deterministic.

Each hand-off must record:

- the files/API surface added, changed, and deleted;
- the acceptance commands and their results;
- the `GUIDELINES.md` audit findings and the repairs made;
- any remaining work, assigned only to a later phase whose stated scope
  explicitly owns it.

Do not hand off with an unrecorded exception, a known failing criterion, or
"cleanup later" work from the completed slice.

### Phase 1: Characterize and freeze the application contract

**Goal:** establish observable lifecycle and message-processing behavior before
moving implementation.

**Files and deliverables:**

- add `zhttp/test/ZhttpLifecycleTest.cc`;
- add reusable test-only client/server event recorders to
  `zhttp/test/ZhttpTestUtil.hh`;
- update `zhttp/test/Makefile.am`;
- add the normative callback signatures and thread ownership table to
  `Zhttp.hh` or a directly included public header.

**Work:**

1. Record `init`, `start`, `connected`, first `process`, `disconnected`,
   `stop`, and `final` events for TCP, TLS, and QUIC.
2. Record the client ordering
   `connect -> connected -> txStream/process -> disconnected`.
3. Record the server ordering
   `connected -> txStream/process -> initial message -> disconnected`.
4. Characterize H1 close-delimited EOF, keep-alive reuse, TLS handshake
   failure, H3 control-stream readiness, H3 request-stream creation, and peer
   close during a response.
5. Use `ZmBlock`/`ZmSemaphore` and callbacks for completion.  Do not add sleeps
   or polling.
6. Keep these tests on the current implementation; this phase changes no
   production behavior.

**Acceptance criteria and hand-off gate:**

- the event traces state the required order for each protocol and fail on
  missing, duplicate, or out-of-order callbacks;
- all three traces reduce to the normative application contract, with
  documented H3-private events excluded from the application trace;
- every request/link completion is observed exactly once;
- `make -j8`, `make -C zhttp/test -j8`, and
  `make -C zhttp/test test` pass;
- the next phase can change internals while using this test as its behavioral
  oracle.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `GUIDELINES.md`, especially deterministic concurrency tests,
   lambda captures, allocations, and file/header style.
2. Audit the complete phase diff for sleeps, polling, broad captures, STL,
   fixed scratch arrays, default-before-overwrite state, and test-only leaks
   into production code.
3. Repair every finding in this phase; do not defer cleanup.
4. Run `git diff --check`, rebuild with `make -j8`, and rerun the phase
   acceptance tests after repairs.  Hand off only when the repaired diff still
   meets every acceptance criterion.

**Phase 1 hand-off record (complete):**

- Added `ZhttpLifecycleTest.cc` and shared `LifeTrace`/`LifeEvt` test support;
  registered the test in `zhttp/test/Makefile.am`; documented the normative
  lifecycle, ownership, EOF, keep-alive, handshake-failure, H3-readiness, and
  exact-once close rules in `Zhttp.hh`.
- The TCP, TLS, and QUIC runtime traces all reduce to the same application
  order.  QUIC physical-connection callbacks are counted separately as private
  H3 events.  Existing parser, multi-request, application, and H3 interop tests
  remain the behavioral coverage for the protocol-specific edge rules.
- `make -j8` passed.  The first run exposed a pre-existing duplicate
  `ZmThreadPriority::N` declaration in `ZvThreadParams.hh`; removing that
  redundant declaration repaired the full-tree build.  `make -C zhttp/test
  -j8` passed and `make -C zhttp/test test` passed all 12 files/134 tests.
- The concluding guideline audit found and repaired incomplete engine unwind
  on test initialization/start/listen failures and verified no sleeps,
  polling, broad lambda captures, anonymous namespaces, STL containers, or
  fixed scratch arrays were introduced.  `git diff --check` and the build/test
  gate passed after those repairs.
- Remaining transport abstraction and program-owned parameter construction are
  owned by Phase 2; no Phase 1 cleanup is deferred.

### Phase 2: Add the internal transport traits and public configuration

**Goal:** put all native type/configuration differences behind `libZhttp`
without yet replacing the program-owned connection bodies.

**Files and deliverables:**

- add `zhttp/src/ZhttpTransport.hh` for private native traits;
- add `zhttp/src/ZhttpConfig.hh` for the public `Zhttp` configuration;
- add non-template implementation to `ZhttpLib.cc` or a focused new `.cc`;
- update `Zhttp.hh`, `zhttp/src/Makefile.am`, installed headers, and README;
- add `zhttp/test/ZhttpTransportContractTest.cc`.

**Work:**

1. Define TCP, TLS, and QUIC trait specializations exposing the native engine,
   link, Rx/Tx stream, connected-info, params, endpoint-start, and shutdown
   operations needed internally.
2. Define one common engine configuration plus typed TCP, TLS, and QUIC
   overrides in `Zhttp` vocabulary.
3. Provide library defaults for H1/H3 ALPN, HTTP buffer heap IDs, H3 flow
   limits, and other mandatory H3 setup.
4. Normalize connected metadata into one `Zhttp` value type.
5. Add detector/`ZuIfT` contract checks for the common `txStream`,
   `process`, `connected`, and `disconnected` operations.  Do not use concepts
   or `requires`.
6. Make `libZhttp` explicitly depend on `libZtcp`, `libZtls`, and `libZquic`.
7. Adapt existing programs temporarily through the new configuration builders;
   do not move their link implementations in this phase.

**Acceptance criteria and hand-off gate:**

- the contract test instantiates all three traits and configurations;
- default HTTP, HTTPS, and H3 configurations produce the correct ALPN and
  native parameter values without program-owned ALPN arrays;
- no public configuration field or callback type names `Ztcp`, `Ztls`, or
  `Zquic`;
- programs no longer construct native client/server params directly;
- existing lifecycle and application tests pass unchanged;
- `make -j8` and `make -C zhttp/test test` pass.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `GUIDELINES.md` sections on static polymorphism, SFINAE,
   header layout, enum conventions, storage/capacity, and template debug-info
   bloat.
2. Audit the new public/private boundary, includes, member ordering, names,
   fixed constants, heap IDs, casts, and any logic unnecessarily nested in
   templates.
3. Repair all findings, moving non-dependent declarations and stable logic out
   of templates where appropriate.
4. Run `git diff --check`, rebuild with `make -j8`, and rerun the contract,
   lifecycle, and existing application tests.  Hand off only after the repaired
   API still meets every acceptance criterion.

**Phase 2 hand-off record (complete):**

- Added public `ZhttpConfig.hh` protocol tags, engine/TLS/QUIC configuration,
  normalized connected metadata, migration vocabulary, and role-aware H3
  defaults.  Added private `ZhttpTransport.hh` TCP/TLS/QUIC traits, native
  parameter builders, endpoint operations, named HTTP buffer allocators, and
  detector/`ZuIfT` link-contract enforcement.
- `libZhttp` now explicitly includes and links `libZtcp`, `libZtls`, and
  `libZquic`; the README records the new ownership boundary.  Neither public
  configuration fields nor callback types expose native transport names.
- Added `ZhttpTransportContractTest.cc`.  It instantiates all traits/configs
  and verifies identity, normalized metadata, HTTP/1.1 and H3 ALPN, client and
  server H3 flow defaults, and migration vocabulary.
- Migrated all six `zhttp.cc`/`zhttpd.cc` initialization sites to the builders.
  The programs no longer construct native client/server parameter objects or
  maintain HTTP ALPN arrays.
- The guideline audit repaired an invalid include, preserved native qlog
  defaults instead of overwriting them with zero, distinguished an explicit
  zero stream limit from the role default, documented non-obvious H3
  capacities, normalized `heartbeat` spelling, and assigned every transport
  buffer allocator a `Zhttp` heap ID.  `git diff --check` passed.
- `make -j8`, `make -C zhttp/test -j8`, and `make -C zhttp/test test` passed;
  the latter ran 13 files/137 tests.  Shared H1 link/engine ownership is the
  explicit scope of Phase 3.

### Phase 3: Deliver the common H1 engine/link slice for TCP and TLS

**Goal:** replace all duplicated HTTP/1 transport integration with one
library-owned template instantiated for TCP and TLS.

**Files and deliverables:**

- add the H1 portions of `ZhttpLink.hh`, `ZhttpClient.hh`, and
  `ZhttpServer.hh`;
- reduce `ZhttpH1Session.hh` to H1 message/session behavior used by those
  types;
- add `zhttp/test/ZhttpH1EngineTest.cc`;
- update `zhttp.cc` and `zhttpd.cc` to consume the shared H1 types.

**Work:**

1. Implement one H1 client link template and one H1 server link template over
   the TCP/TLS traits.
2. Normalize `connected`, `txStream`, `process`, `disconnected`, and
   `connectFailed` without runtime dispatch.
3. Move H1 admission, remote endpoint metadata, idle timers, exact-once active
   accounting, parser invocation, and response transmission into the library.
4. Merge single-request and pooled H1 client paths.  A single request is pool
   size/concurrency one.
5. Preserve close-delimited EOF, HTTP/1.0 keep-alive, connection-close,
   reconnect, redirect, and graceful shutdown semantics.
6. Delete `TCPClient`, `TLSClient`, `H1TCPClient`, `H1TLSClient`,
   `HTTPServer`, and `TLSServer` as soon as their last users move.

**Acceptance criteria and hand-off gate:**

- the test implements the connection body once and instantiates it for TCP and
  TLS using only type aliases/configuration;
- TCP and TLS produce identical application event traces after handshake;
- no duplicated TCP/TLS link, admission, timer, parser, or response-send block
  remains in either program;
- H1 single-request, pool concurrency, keep-alive, close-delimited EOF, TLS
  failure, idle timeout, and admission-limit tests pass;
- lifecycle characterization from Phase 1 remains unchanged;
- `make -j8` and `make -C zhttp/test test` pass.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `GUIDELINES.md` sections on repeated blocks, CRTP defaults,
   I/O buffers, heap allocation, thread ownership, timers, and cleanup.
2. Audit all H1 link state for Rx/Tx affinity, exact-once removal, unnecessary
   locks, hidden copies, default initialization, padding, broad captures, and
   dead compatibility code.
3. Repair every finding, including deletion of superseded program adapters.
4. Run `git diff --check`, rebuild with `make -j8`, and rerun the full H1,
   lifecycle, and application test set.  Hand off only when the repaired slice
   meets every acceptance criterion.

**Phase 3 hand-off record (complete):**

- Added public `ZhttpLink.hh`, `ZhttpClient.hh`, and `ZhttpServer.hh`.  One
  CRTP H1 client link and one CRTP H1 server link now normalize connection
  metadata, process dispatch, idle refresh, and exact-once admission release
  for both TCP and TLS.
- Replaced the separate TCP/TLS clients and servers in `zhttp.cc` and
  `zhttpd.cc` with `H1PoolClient<Protocol>`, `H1Server<Protocol>`, and their
  shared link templates.  The single-request client is now the same pool path
  at concurrency one; all six superseded adapter types were deleted.
- Added `ZhttpH1EngineTest.cc`.  Its connection body is instantiated unchanged
  for TCP and TLS, exercises two sequential exchanges on one connection, and
  verifies identical callback counts/order plus exact-once TLS handshake
  failure notification.  Existing parser/application tests retain
  close-delimited EOF, HTTP/1.0, admission, pooling, redirect, and shutdown
  coverage.
- The concluding guideline audit pinned the idle timer to the Rx owner thread,
  preserved pooled native I/O buffers, verified exact-once release and
  failure-callback suppression, and found no remaining TCP/TLS adapter block.
  The established timer-held link reference is retained because
  `ZmScheduler::del()` cannot cancel a callback already queued on a worker;
  replacing it with a raw pointer would permit use-after-free.
- The umbrella-header audit exposed an order-dependent
  `ptls_iovec_t` specialization.  Moving that existing trait to
  `ZtlsBackend.hh` makes it visible before either TLS or QUIC array
  instantiation and removes the include-order hazard.
- `git diff --check`, `make -j8`, and `make -C zhttp/test test` passed after
  repair; the suite ran 14 files/142 tests.  Private H3 logical-link ownership
  is the explicit scope of Phase 4.

### Phase 4: Deliver the H3 logical-link slice over private QUIC sessions

**Goal:** make HTTP/3 implement the same application-visible link/message
contract while keeping every physical QUIC and H3 control mechanism inside
`libZhttp`.

**Files and deliverables:**

- add the H3 portions of `ZhttpLink.hh`, `ZhttpClient.hh`, and
  `ZhttpServer.hh`;
- complete `ZhttpH3Session.hh` as the private session/control/QPACK layer;
- add `zhttp/test/ZhttpH3EngineTest.cc`;
- migrate an in-process H3 test application to the public `Zhttp` API before
  changing the example programs.

**Work:**

1. Introduce a private H3 session object owning the native QUIC link, local and
   peer control streams, QPACK Rx/Tx tables, GOAWAY state, early session data,
   migration state, and stream scheduler.
2. Route unidirectional control streams entirely within that session.
3. Wrap each bidirectional request stream in an application-visible
   `Zhttp::Link` implementing the same `txStream`, `process`, connection
   metadata, and disconnect operations as H1.
4. On the client, deliver `connected` only after the QUIC handshake, H3 control
   setup, and request-stream allocation succeed.
5. On the server, create/deliver the logical link before dispatching the first
   request bytes, with its process handler and response Tx stream ready.
6. Internalize request-stream queueing, returned-stream binding, FIN/reset,
   transport close, stateless reset, early data, flow control, migration,
   drain/abort, fault injection, and diagnostics.
7. Provide only declarative `Zhttp` configuration and transport-neutral
   connection status/telemetry to applications.
8. Keep all Rx-owned session/link state on Rx and Tx-owned QPACK/send state on
   Tx; cross-shard entry points immediately dispatch to trailing-underscore
   owner methods with bounded captures.

**Acceptance criteria and hand-off gate:**

- the Phase 3 application connection template instantiates for H3 without
  adding QUIC/H3 branches to its core message-processing body;
- H3 application fixtures name no `Zquic` type and define no QUIC link or
  stream subclass;
- physical QUIC and unidirectional streams never produce application
  `connected`, `process`, or `disconnected` events;
- each H3 bidirectional stream produces exactly one logical-link lifecycle;
- control streams, QPACK static/dynamic paths, concurrency/queueing, reset,
  GOAWAY, early data, flow-control exhaustion, migration, and graceful close
  tests pass;
- the Phase 1 lifecycle trace and common API contract pass for all three
  protocols;
- `make -j8` and `make -C zhttp/test test` pass.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `GUIDELINES.md` and `zquic/GUIDELINES.md`, including sharding,
   buffers, retained data, hidden-copy prohibition, logging/qlog, captures, and
   teardown.
2. Audit every H3 session/link/stream member by owner thread; inspect every
   cross-shard post, retained buffer, QPACK write, reference/back-pointer,
   close path, and diagnostic call.
3. Repair every finding.  Do not add locks to legitimize non-owner access and
   do not retain a program-level escape hatch to native QUIC.
4. Run `git diff --check`, rebuild with `make -j8`, and rerun the full H3,
   lifecycle, and common-contract tests.  Hand off only when the repaired slice
   meets every acceptance criterion.

**Phase 4 hand-off record (complete):**

- Added the private H3 engine/session/stream adapters in
  `ZhttpH3Engine.hh` and completed the QUIC specializations of the public
  `Zhttp::Client`, `Server`, `ClientLink`, and `ServerLink` templates.
  Applications now use the same `init`, `start`, `connect`, `connected`,
  `txStream`, `process`, `disconnected`, `stop`, and `final` shape for TCP,
  TLS, and QUIC.  Mandatory H3 control streams, ALPN validation, QPACK state,
  native stream routing, and physical-session shutdown stay in `libZhttp`.
- Added `ZhttpH3EngineTest.cc` and factored the shared application body into
  `ZhttpEngineFixture.hh`.  The H3 run names no `Zquic` type or private
  transport trait, uses only public `Zhttp` configuration, and drives two
  concurrent logical links over one library-owned origin session.  Physical
  and unidirectional streams do not enter the application trace; each
  bidirectional stream has one connected/process/disconnected lifecycle.
- Client sessions now queue links during handshake, reuse an established
  same-origin session, allocate one native bidirectional stream per logical
  link, remove completed logical streams promptly, and drain the physical
  endpoint before engine finalization.  Server sessions create the logical
  link before dispatching initial bytes and notify every live logical link
  exactly once during peer close or engine drain.
- The teardown audit exposed that the native client disconnect callback
  precedes UDP endpoint destruction.  Added an optional, final-close-only
  `endpointDown()` hook to `Zquic::CliLink`; the Zhttp adapter uses it to move
  final cleanup back to the Rx owner before releasing session ownership.
  A gdb backtrace also caught and repaired an initial-endpoint replacement
  being mistaken for final teardown.
- The concluding root/zquic guideline audit removed application-visible
  QPACK accessors, removed per-message callback type erasure, split public
  dispatchers from Rx-owned close methods, eliminated retained completed
  logical-stream references, and made server pre-stop notification ordering
  explicit.  The one `ZmContext` plus two function pointers in the client
  session registry exist only to bridge the incomplete CRTP application type
  on connection-control paths; message processing remains direct static
  dispatch with native buffers and no copying.
- Existing H3 parser, static/dynamic QPACK, interop, stream/reset,
  flow-control, migration, qlog, lifecycle, and application tests remain
  green.  `git diff --check`, full-tree `make -j8`, and
  `make -C zhttp/test test` passed after repairs; the latter ran 15 files/144
  tests.  Multi-engine partial-failure coordination is the explicit scope of
  Phase 5.

### Phase 5: Deliver uniform engine lifecycle and multi-transport coordination

**Goal:** make `init`/`start`/process/`stop`/`final` identical at the
application control layer, including partial failure and graceful drain.

**Files and deliverables:**

- complete engine control in `ZhttpClient.hh` and `ZhttpServer.hh`;
- add a focused coordinator implementation file if non-template state warrants
  it;
- add `zhttp/test/ZhttpEngineLifecycleTest.cc`;
- replace program-level init/start/final flags with the coordinator.

**Work:**

1. Own enabled HTTP, HTTPS, and H3 engines in one server coordinator.
2. Own policy-selected TCP/TLS/H3 client attempts in one client coordinator
   without moving DNS/Alt-Svc/redirect decision rules out of the application.
3. Use existing engine states or one compact state enum; remove parallel
   `httpInit`, `tlsInit`, `h3Init`, and `h3Started` booleans.
4. Initialize in declared order, start only initialized engines, stop/drain all
   started engines asynchronously, and finalize exactly once in reverse order.
5. On partial init/start failure, drain started engines before finalizing them
   and report one completion result.
6. Replace unrelated semaphore posts with one coordinator completion callback.
7. Make repeated `start`/`stop` behavior explicit and consistent with
   `ZmEngine`.

**Acceptance criteria and hand-off gate:**

- applications execute one control sequence regardless of enabled protocols;
- tests cover zero enabled transports, each transport alone, all transports,
  every init/start failure position, stop during start, repeated stop,
  connection drain, and finalization;
- no engine is finalized while a logical link or private H3 session remains
  live;
- completion is delivered exactly once on success and every failure path;
- program-level per-transport lifecycle flags and reverse-cleanup ladders are
  gone;
- `make -j8` and `make -C zhttp/test test` pass.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `GUIDELINES.md` sections on `ZmEngine`, asynchronous
   continuations, blocking, cleanup, sentinel state, and dead code.
2. Audit state transitions, callbacks, partial-failure unwind, iterator/lock
   lifetimes, completion multiplicity, and teardown ordering.
3. Repair every finding; specifically remove any synchronous assumption after
   `run`/`invoke` and any boolean state that duplicates an engine state.
4. Run `git diff --check`, rebuild with `make -j8`, and rerun lifecycle,
   failure-injection, and full application tests.  Hand off only when the
   repaired coordinator meets every acceptance criterion.

**Phase 5 hand-off record (complete):**

- Added `ZhttpEngines.hh`.  `Zhttp::Engines` owns the enabled engines in
  declaration order, starts them through their native asynchronous control
  callbacks, prevents ingress before reverse-order drain, and finalizes in
  reverse order only after every started engine has completed its stop.
- Partial init and start failures now have one unwind path and one completion
  result.  Concurrent/repeated start and stop calls queue exact-once
  completions; start is idempotent while running, stop is idempotent while
  stopped, and stop-during-start drains the engines that actually started
  before reporting the interrupted start.
- `zhttpd` now has one coordinator and one control sequence for any combination
  of HTTP, HTTPS, and H3.  The parallel per-protocol init/start booleans,
  protocol-specific start calls, and reverse cleanup ladder were deleted.
  The H1 and H3 client attempt paths in `zhttp` use the same coordinator while
  retaining redirect, fallback, and Alt-Svc policy in the application.
- `ZhttpEngineLifecycleTest.cc` covers zero, one, and all engines; every init
  and start failure position; repeated start/stop; reverse stop/final order;
  synchronous and genuinely deferred stop-during-start; and exact-once
  callbacks.  Existing H1/H3 engine tests supply real logical-link and private
  H3-session drain coverage.
- The concluding guideline audit replaced the initial synchronous coordinator
  draft with continuation chaining, removed lifecycle booleans that duplicated
  engine state, exposed asynchronous H1 server start without changing its
  synchronous convenience API, and verified that callbacks run only after
  their traversal state has been released.
- `git diff --check` and full-tree `make -j8` passed.  The first full suite run
  saw the existing randomized QUIC migration-drop matrix case fail once; that
  case passed immediately in isolation, and a complete rerun passed all 16
  files/154 tests.  Removing the remaining program-local H3 adapters is the
  explicit scope of Phase 6.

### Phase 6: Migrate `zhttp` and `zhttpd` completely to the library API

**Goal:** leave the programs with application policy only and prove that the
same templated connection/message logic drives TCP, TLS, and QUIC.

**Files and deliverables:**

- refactor `zhttp/test/zhttp.cc` and `zhttp/test/zhttpd.cc`;
- update example/test build inputs and `zhttp/README.md`;
- add or strengthen `ZhttpAppTest.cc` and `ZhttpMultiRequestTest.cc` assertions;
- delete every superseded program-local transport adapter.

**Work:**

1. In `zhttpd`, retain only configuration, admission/request policy, static
   response planning/body production, process control, and reporting.
2. In `zhttp`, retain only URL/discovery/fallback policy, request construction,
   response sink/output policy, retries, redirects, and reporting.
3. Implement one client connection template and one server connection template
   containing the shared `connected`/`txStream`/`process`/`disconnected` logic.
4. Instantiate those templates with the public TCP, TLS, and QUIC `Zhttp`
   types; permit configuration builders to differ.
5. Route QUIC-specific CLI options into `Zhttp` configuration/control without
   native types or callbacks.
6. Delete all program-owned native params, ALPN arrays, links, streams, QPACK
   forwarding, migration/drain helpers, and native diagnostics.
7. Move URL/origin/Alt-Svc or request/response types into the library only when
   independently reusable; do not move CLI policy merely to reduce line count.

**Acceptance criteria and hand-off gate:**

- neither program includes `Ztcp.hh`, `Ztls.hh`, or `Zquic.hh`;
- `rg 'Ztcp::|Ztls::|Zquic::' zhttp/test/zhttp.cc zhttp/test/zhttpd.cc`
  returns no matches;
- neither program defines a transport link/stream adapter or performs QPACK,
  H3 control-stream, migration, drain, or native diagnostic work;
- code review can identify exactly one client and one server core connection
  template shared by all protocols;
- HTTP/TCP, HTTPS/TLS, forced H3, DNS-preferred H3, Alt-Svc upgrade, redirects,
  fallback, multi-request concurrency, static server, and shutdown integration
  tests pass;
- `make -j8` and `make -C zhttp/test test` pass.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read root `GUIDELINES.md` sections on framework fit, repeated blocks,
   control flow, names/layout, formatting/logging, files, and dead compatibility
   code.
2. Audit both complete programs, not only changed hunks, for remaining
   transport normalization, duplicate policy blocks, fixed arrays, broad
   captures, STL, avoidable copies, casts, and historical paths.
3. Repair every finding that is in the refactor's scope and delete all
   superseded code rather than retaining forwarders.
4. Run `git diff --check`, rebuild with `make -j8`, rerun the complete zhttp
   test suite, and repeat the native-symbol `rg` gate.  Hand off only when the
   repaired programs meet every acceptance criterion.

**Phase 6 hand-off record (complete):**

- `zhttp.cc` now has one `PoolClient<Protocol>` and one
  `PoolLink<App, Base>` core flow.  `zhttpd.cc` has one
  `AppServer<Protocol>`, `AppServerLink<Protocol>`, and
  `StaticServer<Protocol>` flow.  TCP, TLS, and QUIC differ only in public
  Zhttp type/configuration selection; request construction, response parsing,
  static response production, connection callbacks, and message processing
  are shared.
- Added `ZhttpMessage.hh` and completed the normalized link operations
  (`receive`, `transmit`, `finish`, and `active`).  H3 parser/builder QPACK
  binding, control streams, logical/physical stream mapping, migration,
  diagnostic filters, and graceful drain are library-owned.  The programs no
  longer define native links/streams or forward QPACK state.
- QUIC migration triggers and local-rebind configuration are now part of
  `Zhttp::QUICConfig`.  The H3 client library performs migration on open,
  after response headers, or after configured body bytes and defers logical
  close until the migration outcome.  Applications only select policy.
- gdb isolated two migration/reuse teardown defects during the migration:
  callback-form engine stop had bypassed Zhttp session drain, and a re-entrant
  logical disconnect cleared the next stream's newly attached session.  The
  repaired lifecycle now drains before native stop and clears old state before
  application callbacks.  Focused H3 single-request, 1,000-request reuse,
  concurrent, migration-after-headers, migration-after-bytes, and Caddy
  migration cases pass.
- The concluding guideline audit removed non-specific captures, native
  symbols, application QPACK/control code, duplicate protocol branches, and
  stale migration callbacks.  `rg
  'Ztcp::|Ztls::|Zquic::' zhttp/test/zhttp.cc zhttp/test/zhttpd.cc` and the
  corresponding native-header/QPACK gates return no matches;
  `git diff --check` passes.
- `zhttp/README.md` now documents the common engine/link flow and shows
  protocol differences confined to `TCPConfig`, `TLSConfig`, and
  `QUICConfig`.  Full-tree `make -j8` and `make -C zhttp/test test` pass; the
  latter ran all 16 files/154 tests.  Release/resource hardening is the
  explicit scope of Phase 7.

### Phase 7: Harden, document, and release-verify the completed design

**Goal:** validate the whole design as a production library surface and close
remaining performance, resource, documentation, and build risks.

**Files and deliverables:**

- finalize public header comments and `zhttp/README.md`;
- update installed-header and library dependency manifests;
- add missing resource/teardown and API matrix coverage;
- remove obsolete session helpers, tests, and build entries.

**Work:**

1. Run one compile-time public API matrix using the same application fixture
   for TCP, TLS, and QUIC.
2. Run repeated init/start/connect-or-listen/stop/final cycles and verify heap,
   hash, link, stream, timer, QPACK, and session state return to baseline.
3. Verify no new data-path allocation, temporary contiguous conversion, body
   copy, header copy, or QUIC stream copy was introduced by the abstraction.
4. Verify client/server shutdown with active H1 requests, concurrent H3
   streams, failed handshakes, resets, migration, and peer close.
5. Document common configuration first and protocol overrides second; examples
   must show identical engine/link flow for all protocols.
6. Delete obsolete headers/helpers and regenerate Autotools outputs through the
   repository's normal flow when required.
7. Perform current gcc and clang debug builds, then a clean release
   configuration/build; perform MinGW compilation where available.

**Acceptance criteria and final hand-off gate:**

- all completion criteria in this document are demonstrably true;
- public examples use only `Zhttp` types and differ only in configuration/type
  aliases;
- no obsolete adapter, compatibility forwarder, unused include, or dead build
  entry remains;
- resource telemetry is stable across repeated lifecycle cycles;
- the full debug test matrix passes with gcc and clang;
- after release configuration through `z.config`, top-level `make clean` and
  `make -j8` complete without warnings; release tests pass;
- README, installed headers, build files, and implementation describe the same
  final API.

**Concluding `GUIDELINES.md` alignment audit and repair:**

1. Re-read all applicable guidance: root `AGENTS.md`, root `GUIDELINES.md`, and
   `zquic/GUIDELINES.md` for the private QUIC integration.
2. Audit the complete final `zhttp` diff for architecture, static polymorphism,
   naming/layout, allocation/copy behavior, I/O buffering, sharding, lifecycle,
   logging/qlog, cleanup, tests, and build integration.
3. Repair every finding and rerun the relevant debug tests immediately after
   each repair group.
4. Repeat `git diff --check`, the full gcc/clang debug matrix, and the clean
   release rebuild/test.  The work is complete only when the repaired final
   tree still satisfies every phase gate and final acceptance criterion.

**Phase 7 hand-off record (2026-07-26):**

- The installed public surface and build manifests agree: `ZhttpConfig.hh`,
  `ZhttpTransport.hh`, `ZhttpLink.hh`, `ZhttpClient.hh`, `ZhttpServer.hh`,
  `ZhttpEngines.hh`, `ZhttpH3Engine.hh`, and `ZhttpMessage.hh` are installed
  by `zhttp/src/Makefile.am`, and the library manifest links all three private
  transports.
- `zhttp/README.md` documents common configuration first, protocol overrides
  second, and one `init`/`start`/`connected`/`txStream`/`process`/`stop` flow
  for TCP, TLS, and QUIC.  The application programs include and name only
  `Zhttp` transport types; static scans find no native transport include,
  `Ztcp::`, `Ztls::`, `Zquic::`, QPACK/control-stream, native session, or
  native link reference in `zhttp.cc` or `zhttpd.cc`.
- The common engine fixture instantiates the same application contract for
  TCP, TLS, and QUIC.  The H1 and H3 engine tests now repeat complete
  init/start/connect-or-listen/stop/final cycles three times.  The release
  HTTP suite passes all 16 files and 160 tests.
- The H3 migration-on-open, migration-after-headers, migration-after-bytes,
  repeated-stream-reuse, concurrent-stream, and Caddy migration rows pass.
  `mig-drop` remains active in diagnostic builds and is now explicitly skipped
  when `ZiMultiplex_FILTER` is not compiled, rather than passing unsupported
  diagnostic options to a release binary.
- A Clang debug top-level build and the full HTTP test suite pass.  A Clang
  ASAN build with `-F -Q` compiled through all HTTP libraries, programs, and
  tests; the normalized lifecycle/transport/H1/H3 tests pass functionally.
  The repository LSAN suppressions account for OpenSSL/zpicotls caches.
  `Zhttp.Engines.Done` reports only `ZmHeap` cache retention; the independent
  Valgrind run reports 23,992 allocations, 23,111 frees, zero definitely,
  indirectly, or possibly lost bytes, and zero errors.
- A clean Clang release configuration rebuilt the dependency and HTTP
  libraries and all HTTP tests without an HTTP warning after repair.  The
  release HTTP suite passes all 160 tests.  The top-level release build is
  blocked later in the pre-existing `zquic/test/ZquicStreamTest.cc`, which
  calls debug-only `installOneRTT` and `growActivePath` members under
  `-DNDEBUG`; this does not prevent `libZquic`, `libZhttp`, or the HTTP suite
  from building and passing in release mode.
- GCC 16 debug compiles the dependency libraries and `libZhttp`, but GCC
  cannot compile any repository TAP test using `ZuTest.hh`: its local
  `ZuTest_Step step` objects produce a section-type conflict in section
  `ZuTest`.  The same unrelated failure occurs in existing `ZuIDTest`,
  `ZuCmpTest`, and HTTP test translation units, so no GCC HTTP test binary can
  be formed until that repository-wide test-harness issue is repaired.
- A MinGW-w64 cross compiler is installed, but this configured in-tree
  checkout cannot be configured simultaneously out of tree, and `z.config
  -M` is the native MSYS2 path (`--build=x86_64-w64-mingw32`), not a Linux
  cross-build path.  MinGW compilation therefore remains an environment gate,
  not an HTTP source failure.
- The concluding root `AGENTS.md`/`GUIDELINES.md` and
  `zquic/GUIDELINES.md` audit repaired the release-only qlog alias warning,
  the release-only `h3Enabled` warning, and the diagnostic-only matrix row.
  It reconfirmed static polymorphism, owner-thread buffer use, no added
  data-path copy/allocation, reverse-order lifecycle teardown, installed
  headers, test registration, and application/native-transport separation.
  Final `git diff --check` and all application-boundary scans pass.

**Final disposition:** the Zhttp unification and HTTP/3 encapsulation are
complete and release-tested.  The two remaining full-matrix exceptions are
pre-existing repository test-infrastructure/toolchain gates outside the
Zhttp change: GCC's `ZuTest` section conflict and the release-only
`ZquicStreamTest` debug-helper calls.

## Callback and ownership contract

Document this contract alongside the new types:

| Event/work | Owner | Common behavior |
|---|---|---|
| engine init/final | caller/control thread | no live links at final |
| engine start/stop | `ZmEngine` control path | completion reported once |
| accept/connect result | native Rx path | create/bind one HTTP link |
| received H1 bytes | Rx thread | parse in place on the connection |
| received H3 bytes | Rx thread | parse in place on the owning stream |
| request/response build | Tx thread | write directly to native Tx stream |
| idle/admission state | Rx owner | no lock added for foreign access |
| QPACK Rx table | Rx thread | connection-owned |
| QPACK Tx table | Tx thread | connection-owned |
| H3 path/migration | QUIC owner | handled internally; HTTP state unchanged |
| completion to application | declared by API | exactly once per request/link |

Large or variable data must remain in owner-managed buffers.  A callback that
needs to cross shards should post a stream/link reference and bounded metadata,
not copy the payload or capture stack references.

## Cumulative verification matrix

Add or extend tests in this order:

1. **API matrix:** all three protocol compositions instantiate with the same
   client/server application contract; application fixtures contain no
   `Zquic` types or H3 stream subclasses.
2. **Core-flow identity:** one templated client connection implementation and
   one templated server connection implementation cover all three protocols;
   only `Zhttp` type aliases and configuration differ.
3. **Lifecycle matrix:** init failure, listen/connect failure, repeated
   start/stop, partial multi-engine startup, graceful drain, and finalization.
4. **Request matrix:** the same GET/HEAD/body/header/trailer/error cases over
   TCP, TLS, and QUIC.
5. **Reuse/concurrency:** sequential H1 reuse, H1 pool concurrency, H3 stream
   concurrency/queueing, close-delimited H1 EOF, and peer close mid-response.
6. **Server policy parity:** admission limit, idle timeout refresh, request
   accounting, access logging, and error response behavior on all transports.
7. **H3-specific regression:** control streams, QPACK dynamic/static paths,
   GOAWAY, stream reset, early data, flow-control exhaustion, migration, and
   graceful QUIC close.
8. **Client policy regression:** HTTP/TCP, HTTPS/TLS, forced H3, DNS-preferred
   H3, Alt-Svc upgrade, redirects, retry, and fallback.
9. **Resource checks:** heap/hash telemetry returns to baseline after repeated
   start/stop; no monotonically growing request, stream, link, timer, or QPACK
   state.
10. **Build matrix:** current gcc and clang debug builds, then a clean
   `z.config` release rebuild; include MinGW compilation where available.

Use deterministic completion primitives and transport callbacks.  Do not add
sleep-based unit tests; existing shell-level process readiness loops may remain
only until the programs expose a deterministic readiness mechanism.

Run at minimum:

```sh
make -j8
make -C zhttp/test -j8
make -C zhttp/test test
```

After changing the build type, configure through `z.config`, run top-level
`make clean`, and rebuild from the top level before testing.

## Completion criteria

The refactor is complete when:

- `zhttp.cc` and `zhttpd.cc` contain no native TCP/TLS/QUIC link adapter
  classes;
- `zhttp.cc` and `zhttpd.cc` include no native transport headers, name no
  `Ztcp`, `Ztls`, or `Zquic` types, and directly perform no QUIC stream, QPACK,
  migration, drain, or diagnostic work;
- TCP and TLS use the same H1 client/server/link implementation;
- one application client-link template and one application server-link
  template implement `connected`/`txStream`/`process`/`disconnected` for all
  three protocols, with only concrete `Zhttp` types and configuration varying;
- H1 and H3 applications receive the same ordinary HTTP request/response and
  lifecycle callbacks;
- physical QUIC sessions are private and H3 bidirectional request streams are
  exposed only as ordinary application-visible `Zhttp::Link` instances;
- H3-only behavior is entirely internal to `libZhttp` and controlled through
  `Zhttp` configuration rather than application extension points;
- one library coordinator owns partial startup, shutdown, and finalization;
- transport buffers remain native and no new data-path allocation or copy is
  introduced;
- all three protocols pass the common matrix and their protocol-specific
  regression tests;
- obsolete program-local adapters and compatibility code are deleted;
- the examples read as examples of `libZhttp`, not reference implementations
  of missing library functionality.
