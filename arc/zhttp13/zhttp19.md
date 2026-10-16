# `Zhttp` Server Repair Implementation Plan

## Objective

Implement every finding and required behavior in `zhttp18.md` as one
coordinated, breaking server refactor.  Treat its example mechanisms as design
candidates, not requirements when a smaller implementation satisfies the same
behavior.  The result is a public `Zhttp::Server` in one
authoritative `ZhttpServer.hh`, structured and application-directed request
error handling, explicit `LiveReq` ownership, bounded and fair response
admission, truthful response milestones, incremental streamed bodies, and one
shared compile-time Tx implementation for client requests and server
responses.

Do not modify `zhttp18.md`.  Its findings and behavioral outcomes are the
requirements for this plan.
Do not add compatibility aliases, forwarding headers, deprecated overloads, or
parallel old/new lifecycles.  Each stage must update all affected dependents
and leave the configured tree buildable before the next stage begins.

## Non-Negotiable Outcomes

The work is complete only when all of the following are true:

- a peer request error cannot stop the server or unrelated connections;
- standard request failures carry the corresponding standard HTTP status;
- the application chooses `Continue` or `Disconnect` for every request-scope
  error whose protocol state permits that choice;
- `Disconnect` acts on the logical HTTP stream and its possible TCP/TLS/QUIC
  consequence is explicit;
- local response construction/queueing and terminal transport completion are
  different, truthfully named events;
- one intrusive `LiveReq` owns each request across Rx, response admission, Tx,
  completion, cancellation, and shutdown;
- active connections, live requests, and retained bytes have bounded
  accounting without a second, redundant response-build quota;
- H1 response order is per connection without globally ordering all traffic;
- H2/H3 stream-local errors and disconnects do not disturb unrelated streams;
- streamed file bodies do no blocking file-I/O loop on the network Tx shard;
- `ZhttpService.hh` is deleted and the complete server is in
  `ZhttpServer.hh`;
- public `Server<Workload>` is a `ZmEngine<Server<Workload>>`; its engine state
  is the sole server start/stop state machine;
- `ResBuilder`, `ReqBuilder`, `RequestParser` as an application type, and
  public `Service`/`ServiceConfig` names are gone;
- `zhttpdmain.cc` contains `main()`, while reusable `Zhttpd::run()` is in
  `zhttpd.cc`; and
- all shutdown paths drain or cancel every live node, byte credit, timer,
  queued body buffer, and callback exactly once.

## Engineering Constraints

- `GUIDELINES.md` compliance is the overriding engineering constraint for this
  entire plan.  Read and apply its relevant sections before each stage; where
  this document's suggested type, API, queue, ownership, allocation, or file
  organization conflicts with `GUIDELINES.md`, change the implementation plan
  detail to comply with `GUIDELINES.md` while preserving the required outcomes
  from `zhttp18.md`.
- Headers contain `ZtEnumStruct`/`ZtFlagsStruct` declarations only.  Every
  corresponding `ZtEnumImpl*`/`ZtFlagsImpl*` definition must be in the owning
  `.cc` file and added to the library build; never place an implementation
  macro in a header or instantiate its name map per template specialization.
- Prefer deleting, merging, or reusing code to adding types, state, callbacks,
  configuration, queues, or validation.  Add a mechanism only when a named
  requirement or demonstrated invariant cannot be met by simplifying an
  existing path; delete the superseded path in the same change.
- Use `ZmEngine` for the public server lifecycle instead of maintaining local
  start/stop states, pending-operation flags, callback queues, blocking
  wrappers, or lifecycle locks.
- Preserve Rx/Tx ownership.  Cross-shard public methods are thin dispatchers;
  owner-only implementations use trailing underscores and debug assertions.
- Keep CRTP, templates, concrete stream types, and compile-time body-policy
  dispatch.  Do not introduce virtual message objects or `std::function`.
- Queue homogeneous `LiveReq` nodes, not heterogeneous application response
  objects.
- Use a named `ZmHeap` for every new long-lived or per-request allocation.
- Move `ZiIOBuf` references across shards for body I/O; do not copy body data
  through temporary contiguous buffers.
- Retain 64-bit body and byte accounting.  Use ordinary `unsigned` for bounded
  local slots and indexes.
- Preserve no-allocation/no-copy fast paths where they are real, but do not
  retain duplicated state merely to avoid a deliberate ownership transfer.
- Update `zhttp`, `zws`, tests, itests, interop, installed headers, and build
  inputs in the same stage as each breaking API change.

## Target Public Contract

The exact template spelling can follow implementation needs, but converge on
the following vocabulary and responsibilities.

### Public server

```c++
template <typename Workload>
class Server : public ZmEngine<Server<Workload>> {
  // ...
};

class ServerConfig;
```

`Server<Workload>` is the multi-protocol coordinator.  The current
single-profile `Server<App, Profile>` becomes the internal
`ProtocolServer<Workload, Profile>` in the same `ZhttpServer.hh`.

Make `ZmEngine<Server<Workload>>` the sole public lifecycle controller.  Expose
its `start()`, `stop()`, `state()`, `running()`, and `stopping()`; implement
`start_()` and `stop_()` as the asynchronous server operations which eventually
call `started(ok)` and `stopped(ok)`.  The default inline `spawn()`/`wake()` are
sufficient unless the implementation audit proves lifecycle initiation must be
moved to a particular control shard.

This inheritance is justified by ownership, not merely by the type being a
server: the public coordinator owns one asynchronous, restartable lifecycle
covering listener admission, the physical hub set, request/response queues,
streamed body producers, terminal callbacks, and shutdown drain barriers.
Those are precisely the transitions and concurrent-caller semantics
`ZmEngine` centralizes.  Do not retain a parallel `running`, `stopping`, failed,
pending-start, or pending-stop state in `Server`; server health remains the
separate `ok()` concern described below.

The engine belongs only at this public coordination boundary.  Do not derive
the per-profile `ProtocolServer<Workload, Profile>` adapters from `ZmEngine` or
give them independent lifecycle state: they are owned start/stop participants
under `Server`, while `Hubs` separately owns the physical hub-set lifecycle.

Guard `init()` with `ZmEngine::lock(Stopped, ...)`.  `final()` first performs a
blocking engine `stop()`, then finalizes hubs/runtime and clears configuration.
Keep `wait()` as the runtime wait primitive and `ok()` as server health; neither
is an alias for engine state.  `failServer_()` records the health failure and
requests engine stop, but request/link failures do neither.

Do not layer the server engine over the current independent `Hubs::State`
machine.  Refactor the shared `Hubs` coordinator itself to
`ZmEngine<Hubs>`: its `start_()` starts entries in order and rolls back on
failure; its `stop_()` stops admission and entries in reverse order.  Let
`ZmEngine` own repeated callers, stop-during-start, restart-during-stop,
blocking wrappers, and callback retention.  Delete `Hubs::State`, `m_startFns`,
`m_stopFns`, `m_stopPending`, and `m_restartPending`.  The nested engine is the
lifecycle of the physical hub set; the outer server engine additionally owns
request admission, response queues, streamed producers, callbacks, and drain
barriers.  Preserve the existing client-facing `Hubs` start/stop behavior and
tests while changing its implementation.

### Application request and response

```c++
struct Workload {
  using Request = AppRequest;

  Request request();

  // Synchronous, non-retainable response selection on the server Tx shard.
  template <typename Emit>
  void respond(const RequestMeta &, Request &, Emit &&emit);

  // Called only for request-scope errors whose disposition can be honored.
  RequestDisposition::T requestError(
    const RequestMeta &, Request &, const RequestError &);

  void committed(const RequestMeta &, Request &, const BodyCommit &);
  void completed(
    const RequestMeta &, Request &, const ResponseResult &);
};
```

The application `Request` is the inbound parser/sink state.  Each emitted
application `Response` implements the outbound response contract.
`respond(...)` runs synchronously on Tx as a template visitation so
heterogeneous response types retain compile-time body policy.  It must emit
exactly once and cannot retain `emit`.  Move only the parsed `LiveReq` from Rx
to Tx; do not add a separate preparation callback, handle, queue, or lifetime.

`request()`, parser-side `Request` calls, `requestError(...)`, `committed(...)`,
and `completed(...)` run on Rx; `respond(...)` and response production run on
Tx.  These are explicit callback affinities, not permission for concurrent
access to shared mutable workload state.  Partition such state by owner or
post fixed metadata to its owner; do not add locks to the request path.
An emitted `Response` or body continuation must own the state it retains; it
must not retain references to `RequestMeta`, `Request`, or the visitation
callback after `respond(...)` returns.

Remove the generic `Completion` object from the contract.  The application
`Request` survives in `LiveReq` through terminal completion and holds any
application completion state.  This eliminates `ServiceResponseDone`, its
per-response allocation, and a second lifetime graph.

Enforce response emission through the `LiveReq` phase transition: the first
emission changes `Queued` to `Sending`, and a second emission fails the request.
Do not add a separate emitted counter or duplicate Boolean.

### Request errors

Add compact Z enums and a value type.  Keep the declarations in the relevant
header and put every `ZtEnumImpl*` definition in the owning `.cc`:

```c++
ZtEnumStruct(ZhttpAPI, RequestErrorCode, int8_t,
  Malformed, ContentTooLarge, TargetTooLong, HeadersTooLarge,
  NotImplemented, VersionUnsupported);

ZtEnumStruct(ZhttpAPI, RequestErrorScope, int8_t,
  Request, Stream, Connection);

ZtEnumStruct(ZhttpAPI, RequestDisposition, int8_t,
  Continue, Disconnect);

struct RequestError {
  RequestErrorCode::T code;
  RequestErrorScope::T scope;
  bool responsePossible;
};
```

The parser/coordinator audit found concrete producers only for the listed
`RequestErrorCode` values.  Do not add timeout, length-policy, expectation, or
overload catalog entries until the server owns a concrete condition which can
legally emit that response; application policy uses ordinary responses.

Map `code` to its standard default status in one function; do not store a
second status value which can disagree with the code.  Do not add a generic
offset or duplicate a protocol-native error/action merely in case a future
consumer wants it.  Preserve only these distinctions:

- classification and standard default status;
- whether an HTTP response can still be encoded;
- whether protocol correctness permits recovery, mandates a stream terminal,
  or mandates a connection terminal.

`requestError(...)` is invoked only when `scope == Request` and chooses the
application disposition.  The server must not infer disconnection from a
`4xx`/`5xx` status.  A condition which mandates an H2/H3 stream or connection
terminal is classified before application policy and follows its native
protocol path; do not invoke a choice callback whose result cannot be honored.

### Response milestones

Introduce an enum-backed result rather than a trailing Boolean:

```c++
ZtEnumStruct(ZhttpAPI, ResponseOutcome, int8_t,
  Success, BuildFailed, TxFailed, Reset, Cancelled);

struct ResponseResult {
  BodyCommit body;
  ResponseOutcome::T outcome = ResponseOutcome::BuildFailed;
};
```

Do not duplicate `BodyCommit::headers` or `BodyCommit::final` in
`ResponseResult`.  `committed(...)` receives `BodyCommit` directly; only the
terminal `completed(...)` callback needs `ResponseResult`.  The callback name
already identifies the milestone, so do not repeat `committed` or `completed`
as result enum values.

`committed(...)` means the complete response was constructed and accepted by
the local transport queue.  `completed(...)` means the transport's strongest
available terminal local completion event, not peer receipt unless a protocol
actually proves peer receipt.  Document the exact H1/H2/H3 guarantee.

### Protocol message vocabulary

`MessageVersion` is a protocol-traits specialization keyed by the HTTP
version, not a version value or message object.  Rename it to `HttpTraits` and
retain `MessageTraits<Profile>` as the composition of `HttpTraits` with the
profile's transport traits.

Perform these direct breaking renames:

| Current | Target |
| --- | --- |
| `MessageVersion<Version>` | `HttpTraits<Version>` |
| `H1::ReqBuilder` | `H1::Request` |
| `H2::ReqBuilder` | `H2::Request` |
| `H3::ReqBuilder` | `H3::Request` |
| `H1::ResBuilder` | `H1::Response` |
| `H2::ResBuilder` | `H2::Response` |
| `H3::ResBuilder` | `H3::Response` |
| `MessageVersion<Version>::ReqBuilder` | `HttpTraits<Version>::Request` |
| `MessageVersion<Version>::ResBuilder` | `HttpTraits<Version>::Response` |
| `H1ReqBuilder` / `H1ResBuilder` | `H1Request` / `H1Response` |
| `H2ReqBuilder` / `H2ResBuilder` | `H2Request` / `H2Response` |
| `H3ReqBuilder` / `H3ResBuilder` | `H3Request` / `H3Response` |
| `ReqParser` / `ResParser` aliases | `RequestParser` / `ResponseParser` |

Rename the protocol start operation from `request(tx)`/`response(tx)` to one
common phase name selected during implementation, preferably `begin(tx)` if
it accurately covers both start-line and pseudo-header emission.  Retain
`body(tx)` and `finish(tx)`.

## Target Ownership Model

### `LiveReq` and queue node

Define a concrete queue item and intrusive node for each profile in
`ZhttpServer.hh`:

```c++
template <typename Profile>
struct LiveReq_ {
  ZmRef<Link<Profile>> link;
  Request request;
  RequestMeta meta;
  RequestBody requestBody;
  ResponseResult response;
  uint64_t requestID = 0;
  RequestPhase::T phase = RequestPhase::Receiving;
};

template <typename Profile>
using LiveReqQ = ZmList<LiveReq_<Profile>, ...>;
template <typename Profile>
using LiveReq = typename LiveReqQ<Profile>::Node;
```

Use one Tx-owned intrusive FIFO with a named heap for each concrete H1 TCP, H1
TLS, H2 TLS, and H3 QUIC profile.  This preserves concrete link/reference types
and compile-time dispatch; do not erase four link types into one global node.
Add `requestID` only if existing diagnostics consume it; if retained, it is
diagnostic identity only and remains stable from request admission through
completion.  A response queue has no gaps, resend, or archive semantics, so do
not use `ZmPQueue`/`ZmPQTx` for it.

### Lifecycle and owner table

Add `RequestPhase` with centralized transition helpers:

```text
Receiving -> Queued -> Sending -> Committed -> Completing -> Done
```

Failure, reset, and cancellation are response outcomes, not parallel lifecycle
phases.  Record them in `ResponseResult` and transition through `Completing` to
`Done`; do not multiply states for every phase/outcome combination.

At most one owner mutates the node in each phase:

| Phase/state | Owner | Permitted work |
| --- | --- | --- |
| parser and request body | Rx | parse headers/body, update `RequestBody` |
| profile `LiveReqQ`, response selection/build | Tx | dequeue, choose response, construct/commit output |
| streamed file read | file-I/O shard | fill one pooled buffer per turn |
| streamed buffer submission | Tx | enqueue buffer, charge/release credits |
| terminal application callback | Rx | deliver result and release request accounting |
| queue removal/final release | Tx | remove the exact node and release queue ownership |

Cross-shard posts capture the concrete `ZmRef<LiveReq<Profile>>` or pooled
buffer references plus fixed metadata.  Raw pointers are allowed only where an
enclosing intrusive owner is proven to outlive the post.  Add debug
`assertRx_()`, `assertTx_()`, and application/file-shard assertions.

### Body accounting

Replace duplicated flat counters with:

```c++
struct RequestBody {
  uint64_t received = 0;
  uint64_t consumed = 0;
  uint64_t pending = 0;
  uint64_t reset = 0;
  uint64_t discarded = 0;
};
```

Update it directly in the parser adapter.  Use `BodyCommit` for response
produced/committed/reset/discarded values, extending it only if terminal Tx
accounting needs a distinct field.  Every transition which discards unread or
unsent bytes updates the corresponding counter before callbacks run.

### Validation policy

Do not add a second validation layer in `Server`.  Protocol parsers remain the
single authority for message syntax, framing, field semantics, and configured
wire limits; `Server` consumes their structured result without reparsing or
rechecking it.  Application `Request` remains the authority for resource and
policy validation.  Response Tx continues to use the existing body-allowed,
content-length, header-patch, and body-policy checks; factor or move those
checks rather than duplicating them in error-response or queue code.

Add a new validation only when an existing input is genuinely unbounded or a
protocol/configuration invariant is currently unenforced.  Reuse the existing
H2/H3 header-list and stream limits.  Before adding an H1 limit or a new
`ServerConfig` field, identify the storage it bounds and delete any overlapping
limit or check.  Standard HTTP status mapping classifies failures which already
exist; it does not itself justify inventing new validation paths.

### Deliberate reductions from the initial design

This pass rejects several mechanisms considered in `zhttp18.md` or earlier
versions of this plan because simpler existing machinery satisfies the same
requirements:

- no asynchronous application-preparation API; parsing posts the live node and
  Tx performs synchronous response selection;
- no `ZmPQTx`, global type-erased queue, key, acknowledgement, resend, or
  archive path; concrete intrusive FIFOs need none of those semantics;
- no response-build quota/counter separate from the server-wide live-request
  and retained-byte bounds; synchronous Tx already permits only one build per
  shard turn;
- no generic per-connection request limit; the server-wide limit and existing
  H2/H3 stream limits bound the identified resources;
- no duplicated error status, parser offset, transport action, validation
  layer, emission flag, or lifecycle failure states; and
- no server-local lifecycle enum or multi-step persistent shutdown state;
  `ZmEngine` expresses start/stop concurrency, while queue emptiness and owner
  barriers express drain completion.

These are implementation conclusions, not deferred optional features.  Reopen
one only with a measured failure or a protocol invariant that the smaller
model cannot satisfy.

## Implementation Stages

## Stage 0: Baseline and Characterization

Before changing APIs, characterize with existing tests wherever possible:

1. Record the configured compiler/options and build the current `zhttp`,
   `zhttp/util`, `zhttp/test`, `zhttp/itest`, `zhttp/interop`, and `zws`
   targets.
2. Run the current targeted server tests and preserve their TAP output.
3. Extend the nearest existing tests only where callback order, H1 pipelining,
   H2/H3 stream isolation, request-body accounting, idle expiry, or shutdown
   lacks coverage.  Do not add tests whose only purpose is to preserve names or
   structures this plan deletes.

No production behavior changes in this stage.

Gate: all existing tests plus characterization tests pass on the original
implementation.

## Stage 1: Structured Parser Errors

The current H1/H2/H3 parsers expose only a terminal `Error` state.  Add
structured request errors before changing server policy.

1. Define `RequestErrorCode`, `RequestErrorScope`, and `RequestError` in
   `ZhttpTypes.hh`; put both `ZtEnumImpl*` definitions in `ZhttpTypes.cc`.
   Define the server-only `RequestDisposition` later in `ZhttpServer.hh` with
   its implementation in `ZhttpServer.cc`.
2. Give each request parser a latched `error()` accessor.  The first error is
   authoritative; later cleanup must not overwrite it.
3. Classify existing parser failure sites without adding parallel checks:
   - malformed start line, pseudo-header set, header syntax, content length,
     chunk framing, and body-length mismatch;
   - unknown/unimplemented method or transfer coding;
   - unsupported version;
   - request-target, header-section, and body limits; and
   - H2/H3 stream errors versus connection errors mandated by the protocol.
4. Inventory existing H1 line/header storage limits.  Add or expose a limit
   only for storage which is otherwise unbounded; reuse H2/H3 configured
   header-list limits and preserve their native protocol error codes.
5. Centralize the default status mapping in one
   `requestErrorStatus(RequestErrorCode::T)` function.  Do not also store the
   status in `RequestError`.  Reachable parser-detected errors map to `400`,
   `413`, `414`, `431`, `501`, or `505`.  Leave `401`, `403`,
   resource-specific `405`, `408`, `411`, `415`, `417`, `421`, and `503` to
   ordinary application responses unless the protocol/coordinator later owns
   a concrete decision and can supply required headers correctly.
6. Preserve protocol-required behavior: an error which cannot legally produce
   an HTTP response carries `responsePossible = false`.  Keep the native stream
   or connection error/action in the protocol's existing state; do not copy it
   into `RequestError`.
7. Extend parser tests with one case per reachable classification and boundary
   values only for actual configured limits.

Do not change server-wide failure behavior yet; this stage creates the
information required to change it safely.

Gate: parser and protocol tests prove both the standard HTTP classification
and the native H2/H3 terminal scope.

## Stage 2: Uniform Response Commit and Terminal Completion

Audit the native link layers before changing workload callbacks.  Today
`finish()` and terminal Tx notification are not uniform across H1 TCP, TLS/H1,
H2, and H3.

1. Define one internal link contract:
   - `finish()` closes the HTTP message locally;
   - successful return from the existing response-build/`finish()` path means
     local commit; and
   - one internal `txComplete_` transition consumes the strongest terminal
     local Tx event supported by the profile.
2. Reuse existing native sent/stream-completion callbacks to implement the
   terminal hook in `ZhttpLink.hh`, H2 logical links, TLS/H1 logical links, and
   H3 request streams.  Add only the missing final-response fence; do not add a
   second Tx queue or per-buffer tracker.
3. The terminal callback is exact-once on success, Tx error, stream reset,
   disconnect, and shutdown.  Do not synthesize it immediately after
   `finish()` unless the native contract proves those events are equivalent.
4. Add `ResponseOutcome` and the non-redundant `ResponseResult`; put the
   `ResponseOutcome` `ZtEnumImpl*` definition in `ZhttpServer.cc`, and update
   body commit/reset/discarded accounting at the native callback boundary.
5. Rename the workload's old `complete(..., bool sent)` to `committed(...)`
   with the truthful local-queue result; do not reuse `complete` for the
   internal transport hook.
6. Add `completed(...)` only after every enabled profile supplies an exact
   terminal event.  If one profile cannot, extend that transport rather than
   weakening the meaning globally.
7. A build failure calls only `completed(..., BuildFailed)`; it does not call
   `committed(...)` or synthesize a native transport-completion event.
8. Test callback ordering and exact-once behavior for empty, fixed, streaming,
   Tx-failed, reset, peer-disconnected, locally disconnected, and shutdown
   responses.

Gate: no callback or Boolean named `sent`/`complete` overstates the transport
event it represents.

## Stage 3: Repair Failure Scope and Request-Error Policy

1. Define `RequestDisposition` in `ZhttpServer.hh` and its `ZtEnumImpl*` in
   `ZhttpServer.cc`.
2. Replace the overloaded server-wide `failed()` path with explicit
   `failServer_()`, connection-terminal handling, request-terminal handling,
   and response Tx failure.  Factor only repeated transitions; do not create a
   parallel failure hierarchy.
3. Call `failServer_()` only for listener startup, unrecoverable hub/runtime,
   or explicit fatal application failure.  Make server-health diagnostic
   reads race-free under the chosen owner/atomic rule.
4. Add `Workload::requestError(...) -> RequestDisposition::T` for request-scope
   errors whose disposition can be honored, and migrate `zhttpd` and test
   workloads immediately.
5. Add one small internal standard error `Response` which goes through the
   existing response Tx path and therefore reuses header, body-suppression,
   and framing validation.  Application-policy errors continue through
   ordinary application response types.  Audit existing `zhttpd` policy-error
   sites and use `401` with its challenge, `403`, `405` with `Allow`, `415`, or
   `421` only where the application already has the information which proves
   that classification; do not add speculative policy checks to make the
   status catalog appear complete.
6. For a request-scope error, obtain the application disposition and commit the
   standard response when possible.  `Continue` rearms only parser state which
   the protocol parser marked reusable.  `Disconnect` schedules logical
   stream disconnection after local commit; use the Stage 2 terminal/drain hook
   where immediate physical close could discard queued output.
7. Define and test the logical disconnect mapping:
   - H1 TCP/TLS closes the underlying connection;
   - H2/H3 terminates only the request stream while preserving siblings; and
   - a profile which necessarily closes TCP/TLS/QUIC exposes that consequence
     in existing request/profile context rather than adding a duplicate flag.
8. For `RequestErrorScope::Stream` or `Connection`, follow the native mandatory
   terminal action and do not invoke the application choice callback.
9. Do not invent a post-admission overload error.  Pre-admission capacity
   exhaustion uses the existing bounded/native refusal path without allocating
   a `LiveReq`; applications which own a temporary-capacity policy can emit an
   ordinary `503` response.
10. Group required diagnostics in one owner-affined stats structure rather
    than adding unrelated atomics: server faults, rejected requests, parse
    failures, response-build failures, and transport failures.
11. Add the final regression at the fix point: after malformed peer traffic,
    the server remains healthy and accepts a later valid request.  Do not first
    add a permanent test which expects the bug.

Gate: request, connection, and server failure scopes are distinct; request
errors never stop unrelated listeners or connections.

## Stage 4: Consolidate and Rename the Server API

Perform this as one mechanical breaking change after the correctness
prerequisites are green.

1. Rename current `Zhttp::Server<App, Profile>` to
   `ProtocolServer<Workload, Profile>` inside `ZhttpServer.hh` and update
   H1/H2/H3 hub derivations.
2. Move the complete contents of `ZhttpService.hh` into `ZhttpServer.hh` and
   rename:
   - `Service` -> public `Server`;
   - `ServiceConfig` -> `ServerConfig`;
   - internal `Builder` -> `ResponseTx`;
   - `BuilderApp_` -> `ResponseOps`;
   - `AppRequestParser` -> `Request`.
   Leave `RequestOps` untouched in this mechanical stage and delete it when
   its fields move into `LiveReq`; do not spend a transition on a temporary
   name.
3. Resolve include ordering so standalone inclusion of `ZhttpServer.hh` and
   inclusion through `Zhttp.hh` both work without recursive incomplete types.
4. Delete `ZhttpService.hh`; remove it from `pkginclude_HEADERS`; replace every
   include with `ZhttpServer.hh`.  Do not leave a forwarding header.
5. Rename application `Workload::RequestParser` to `Workload::Request` and
   `requestParser()` to `request()` in `zhttpd` and tests.
6. Rename `MessageVersion` to `HttpTraits`, then perform the H1/H2/H3
   `Request`/`Response` and parser alias renames listed in the target contract.
   Rename `request(tx)`/`response(tx)` consistently.
7. Rename `zhttpd`'s `ResponseBuilderBase` to `ResponseBase`; retain the
   concrete `FixedResponse`, `StreamResponse`, and `EmptyResponse` names.
8. Update all direct dependents:
   - `zhttp/src` message, hub, and transport headers;
   - `zhttp/test`, `zhttp/itest`, and `zhttp/interop`;
   - `zws/src/ZwsExtended.hh` and `zws/src/ZwsH1.hh`; and
   - public examples/comments and heap/diagnostic names.
9. Ensure `ZhttpServer.cc` contains every server-owned
   `ZtEnumImpl*`/`ZtFlagsImpl*` definition and other appropriate non-template
   result implementations, and is present in `libZhttp_la_SOURCES`.
10. Search the entire repository and require zero old public-name matches,
    excluding historical planning documents.

Keep this stage mechanical unless Stage 10's `ZmEngine` conversion and deletion
of the old lifecycle machinery can land atomically; do not add engine
inheritance while retaining parallel server start/stop state.

Gate: clean full build with no `ZhttpService.hh`, `ServiceConfig`, public
`Zhttp::Service`, `MessageVersion`, `ReqBuilder`, or `ResBuilder` use.

## Stage 5: Introduce `LiveReq` and Remove Split Lifetimes

1. Add the per-profile named-heap intrusive `LiveReqQ<Profile>` and
   `LiveReq<Profile>` types plus `RequestPhase`; put the `RequestPhase`
   `ZtEnumImpl*` definition in `ZhttpServer.cc`.
2. Move `RequestMeta`, application `Request`, `RequestBody`, link ownership,
   IDs, disposition, response result, and lifecycle into the node.
3. Change protocol request parsers to refer to the current node rather than
   inheriting the flat `RequestOps` containing duplicate storage; delete
   `RequestOps` in the same change.
4. Allocate a node at request/stream admission.  For persistent H1, install a
   fresh node before resetting the parser for the next request.
5. Replace `RequestInfo`'s application-specific header members with core
   `RequestMeta`.  Move host, authorization, range, conditional, referer, user
   agent, and other static-file policy storage into `zhttpd`'s `Request`.
6. Replace formatted `EndpointString remote` request identity with the useful
   typed peer address/port subset; format only for logs.
7. Audit target/path/query storage for actual duplication.  Consolidate it only
   if one representation deletes fields or copies without regressing the
   existing in-storage span fast path; otherwise leave the proven fast path
   alone.
8. Delete `ServiceResponseDone`.  Store all completion data in the surviving
   application `Request`/`ResponseResult` and capture one concrete
   `ZmRef<LiveReq<Profile>>` across posts.
9. Replace the stack-reference `emit` lifetime with synchronous documented
   response visitation on Tx.  Enforce exactly one emission in the live-node
   phase transition, not a local Boolean reference.
10. Add transition assertions and exact-once terminal callback/release tests.

Gate: one node and one authoritative state group describe every request from
admission to final release; no per-response completion wrapper remains.

## Stage 6: Add Bounded Tx-Owned Response Admission

1. Reuse existing `maxConnections`, `retainedBodyMax`, and
   `retainedMessageMax`.  Add only the limits not already represented:
   - `maxRequests` server-wide; and
   - `retainedBytesMax` aggregate.
   Do not add `maxResponseBuilds`: response selection/build is synchronous on
   one Tx shard, so the request and byte limits already bound it.  Do not add a
   generic per-connection request limit over existing H2/H3 stream limits
   without a demonstrated H1 resource which the server-wide limit cannot
   bound.
2. Expose only counters needed to operate those limits:
   `activeConnections()`, `activeRequests()`, `queuedResponses()`, and
   `retainedBytes()`.  Keep them in one owner-affined stats group.
3. Reserve a live-request credit when a request/stream begins, before
   application-sized allocation.  Return it exactly once on every terminal
   path.  Keep the server-wide request credit on the configured server Rx
   owner; if any profile can admit from another shard, dispatch the admission
   decision to that owner rather than adding a lock.  Keep retained-byte
   credits on the server Tx owner with the profile FIFOs.
4. Use one Tx-owned `ZmList` FIFO per concrete profile.  Post a node after Rx
   parsing completes, schedule at most one drain turn per profile, pop one
   eligible node, invoke response selection/build synchronously, then
   reschedule if work remains.  Directly release queue ownership at the
   terminal transition; there are no keys, acknowledgements, gaps, retries,
   retrieval, or archive state.
5. Preserve H1 wire order with the least state the link already provides.
   Only the head request for an H1 link is eligible for its profile FIFO; keep
   later pipelined requests in existing link/session ownership until the head
   reaches the protocol-defined release point.  Add one per-link pending FIFO
   only if no equivalent ownership already exists.  Do not assign a second
   sequence number merely to restate FIFO order.
6. Enqueue H2/H3 request streams independently.  Per-profile scheduled turns
   prevent one H1 link from globally blocking other links or protocols; do not
   add a cross-profile queue or completion barrier.
7. Charge retained bytes before buffer retention/allocation and release them
   on commit, failure, reset, cancellation, or final release as appropriate.
8. Do not exceed `maxRequests` merely to construct an overload response.  If
   credit is unavailable before admission, use existing input pause or
   protocol-native stream refusal and allocate no `LiveReq`.  If an already
   admitted response exceeds its configured retained-byte budget, report a
   response build failure; that limit does not prove a retryable request
   overload.  Any H2/H3 refusal/reset must preserve unrelated streams.

Gate: stress tests cannot exceed any configured node/byte limit, and one
blocked H1 connection does not block unrelated H1/H2/H3 responses.

## Stage 7: Incremental Non-Blocking Streamed Bodies

The current `zhttpd` file response performs its entire `pread` loop on Tx.
Replace the synchronous whole-body contract for streamed responses.

1. Extend the existing templated body callback with the smallest continuation
   convention that can request another turn or finish/fail.  Reuse an existing
   callback/result convention if one fits; do not introduce a public producer
   hierarchy or enum solely to name three outcomes.  One producer turn emits
   at most one pooled buffer.
2. Keep producer dispatch templated.  Schedule one concrete continuation at a
   time; do not store virtual response objects or `std::function` in
   `LiveReqQ`.
3. Keep the `LiveReq` active while a producer is outstanding.  Cancellation
   invalidates/resolves the next continuation without accessing destroyed
   response or link state.
4. Reuse the transport's existing capacity/window resume signal; do not add a
   parallel threshold.  Do not read another chunk while the link cannot accept
   it.
5. In `zhttpd`, first reuse an existing application/async-I/O shard if it can
   block without delaying network work; add one dedicated file-I/O shard only
   if no such owner exists.  On each turn:
   - allocate a pooled `ZiIOBuf` with correct head/tail room;
   - perform one bounded `pread` into its writable storage;
   - post only the buffer reference, offset, and fixed metadata to Tx;
   - enqueue it on Tx and request the next read only when capacity permits; and
   - terminate exactly at the planned file range.
6. Remove the `ZmScratch(char, FileChunk)` copy loop from
   `StreamResponse::body()`.
7. Keep fixed-body generation synchronous only under `retainedBodyMax`,
   `retainedMessageMax`, and aggregate retained-byte limits.
8. Test one large slow file plus many small responses.  Verify bounded Tx turn
   time and memory under backpressure without adding permanent fairness
   telemetry unless operations already have a consumer for it.
9. Cover file short read, I/O error, disconnect between read and Tx post,
   cancellation, range completion, and shutdown while a read is outstanding.

Gate: no blocking file read runs on the network Tx shard and at most one pooled
buffer is produced per scheduled turn.

## Stage 8: Factor Shared Client/Server Message Tx

Only after server response semantics and accounting are stable, remove the
duplicated Tx algorithms.

1. Identify only identical compile-time operations in `ClientMessage` and
   server `ResponseTx`:
   - body-policy selection;
   - optional body exact-once visitation;
   - fixed body retention and header patching;
   - H1 header-before-body commit ordering;
   - H2 deferred transaction commit;
   - H3 deferred compression;
   - streaming/incremental producer handling; and
   - shared validation/failure/accounting exits.
2. Introduce an internal `MessageTx` template in `ZhttpMessage.hh`, parameterized
   by static request/response operations rather than runtime polymorphism.
   Proceed only if the shared implementation and adapters are smaller than the
   deleted client/server copies and do not add runtime state or allocation.
3. Request operations provide method/target/host/pseudo-header emission and
   client `BodyCommit` callbacks.
4. Response operations provide status/reason emission, body-allowed and
   content-length-forbidden rules, server budget credits, and response result
   callbacks.
5. Centralize only operations whose ordering and semantics are genuinely the
   same.  Keep client retry/redirect and server admission/completion in their
   respective owners; do not force them through a generic state machine.
6. Replace client and server copies in the same change.  Delete the old helper
   paths rather than forwarding them to the new implementation.
7. Re-run the complete client retry/redirect/body accounting suite as well as
   all server tests; this stage must not regress the cleaned-up client.

Gate: duplicated optional/fixed/streaming mechanics are deleted, request and
response differences remain compile-time, and the resulting code/state count
is lower than before the extraction.

## Stage 9: Rename and Rebuild `zhttpd` Translation Units

1. Rename current main-only `zhttpd.cc` to `zhttpdmain.cc`.
2. Rename reusable `zhttpdapp.cc` to `zhttpd.cc`.
3. Retain `zhttpdutil.cc` for out-of-line enum/utility definitions.
4. Update `zhttp/util/Makefile.am`:
   - `libzhttputil_la_SOURCES = zhttpd.cc zhttpdutil.cc`;
   - `zhttpd_SOURCES = zhttpdmain.cc`; and
   - `zhttpd` continues linking `libzhttputil.la`.
5. Keep `Zhttpd::run()` declared in `zhttpd.hh` for itest and interop users.
6. Verify `zhttpqirimpl.cc` and every `libzhttputil.la` consumer link without a
   duplicate `main()` and can still run the server in-process.

This stage may be combined with Stage 4 if file history is easier to preserve,
but the resulting build boundary must be identical.

Gate: executable and in-process server consumers link and run with source names
matching their roles.

## Stage 10: `ZmEngine` Lifecycle, Shutdown, and Final Cleanup

1. Convert shared `Hubs` to `ZmEngine<Hubs>`.  Its `start_()` retains only the
   current entry/index needed to start in order and roll back a partial start;
   its `stop_()` stops admission and started entries in reverse order, then
   calls `stopped(ok)`.  Delete its local lifecycle enum, pending flags,
   callback arrays, blocking wrappers, and associated state-based tests.  Use
   the inherited engine state in the remaining lifecycle tests and rerun the
   client suite because `Client` shares this component.  Guard entry mutation
   in `init()` with the engine's `Stopped` lock.  A start failure rolls back
   started entries but leaves initialized entries for `final()`; do not retain
   a separate permanent `Failed` state or finalize implicitly from `start_()`.
2. Derive public `Server<Workload>` from `ZmEngine<Server<Workload>>`, add the
   required friendship and `Engine` alias, and expose the inherited lifecycle
   API.  Delete the server's manual sync/async `start`/`stop` wrappers and do
   not add `ServerState`.
3. Guard `init()` with `Engine::lock(ZmEngineState::Stopped, ...)`.  Implement
   `start_()` by enabling internal admission/Tx machinery before starting the
   hub set and call `started(ok)` only when both are usable.  Roll back partial
   startup before reporting failure.
4. Implement `stop_()` as one asynchronous drain.  Stop listener admission
   first, then prevent new request credits; do not call `stopped(ok)` merely
   because stop was requested.
5. Resolve partially received Rx nodes with request/body reset accounting and
   cancel or drain file-I/O handles.
6. Stop the Tx FIFOs only after queued nodes are committed or cancelled under
   the selected stop policy.
7. Wait for native response completion callbacks or explicitly reset committed
   streams; deliver one terminal workload callback for each node.
8. Remove all queue nodes and return connection, request, and byte credits,
   then stop the physical hub set.
9. Retain the existing final Rx drain required by native hub teardown and add
   only the Tx/file-shard barriers required by outstanding work.  Call
   `stopped(ok)` exactly once after all of those barriers complete.
10. Make `final()` perform a blocking engine `stop()` before finalizing hubs and
    runtime.  Keep `Runtime::wait()` and server-health `ok()` separate from
    engine state.
11. Assert all counters, queues, timers, body producers, and live-node heaps are
    empty at `final()` in debug builds.
12. Remove temporary transition functions, stale comments, dead fields, old
    heap IDs, and historical compatibility code.

Gate: repeated/concurrent start and stop, stop during start, restart during
stop, stop under load, and forced error tests are clean under leak detection;
callbacks complete exactly once and never enter a destroyed workload.

## File-by-File Work Map

### Core server and shared message code

- `zhttp/src/ZhttpServer.hh`
  - absorb `ZhttpService.hh`;
  - define public server/config, internal protocol server, workload contract,
    `LiveReq`, queue, `ZmEngine` hooks, admission, error policy, and shutdown.
- `zhttp/src/ZhttpService.hh`
  - delete after Stage 4; no forwarding include.
- `zhttp/src/ZhttpServer.cc` (new, required)
  - all server-owned `ZtEnumImpl*`/`ZtFlagsImpl*` definitions and non-template
    result formatting.
- `zhttp/src/ZhttpMessage.hh`
  - rename `MessageVersion` to `HttpTraits` and rename its message aliases;
  - structured server-session error propagation;
  - incremental producer/common `MessageTx` implementation.
- `zhttp/src/ZhttpLink.hh`
  - logical disconnect semantics and uniform commit/completion hooks.
- `zhttp/src/ZhttpHubs.hh`
  - replace its duplicate lifecycle state machine with `ZmEngine<Hubs>`;
    retain only ordered start, rollback, reverse stop, and entry finalization.
- `zhttp/src/Zhttp.hh`
  - renamed public aliases and one consolidated server include.
- `zhttp/src/ZhttpTypes.hh` / `ZhttpTypes.cc`
  - `RequestErrorCode`, `RequestErrorScope`, `RequestError`, and the single
    default-status mapping; keep disposition and response results server-only.
- `zhttp/src/ZhttpConfig.hh` / `ZhttpConfig.cc`
  - shared protocol limits where appropriate; keep server admission limits in
    `ServerConfig`.

### Protocol implementations

- `ZhttpH1.hh` / `ZhttpH1.cc`
  - structured parse errors, exposure of existing H1 limits (adding one only
    for proven unbounded storage), Request/Response rename, reusable parser
    reset, and persistent-connection ordering hooks.
- `ZhttpH2Message.hh` / `.cc`
  - structured message errors, Request/Response rename, standard status versus
    native stream-error classification.
- `ZhttpH3.hh` / `.cc`
  - same, preserving QPACK/H3 native error scope.
- `ZhttpH2Hub.hh`, `ZhttpH3Hub.hh`, `ZhttpTLSHub.hh`
  - renamed protocol server base, exact logical disconnect mapping, uniform
    commit/completion, and stream-local teardown.
- `ZhttpHubs.hh`, `ZhttpRuntime.hh`
  - staged stop/drain integration where required.

### Utility application

- `zhttp/util/zhttpd.hh`
  - new server include/vocabulary; application header storage in `Request`;
    typed remote endpoint use; incremental file producer declarations.
- `zhttp/util/zhttpd.cc` (renamed from `zhttpdapp.cc`)
  - `Zhttpd::run()`, `ResponseBase`, `Request`, error disposition, response
    callbacks, file-I/O shard integration, and incremental file bodies.
- `zhttp/util/zhttpdmain.cc` (renamed from old `zhttpd.cc`)
  - `main()` only.
- `zhttp/util/zhttpdutil.cc`
  - retain enum implementations.
- `zhttp/util/Makefile.am`
  - renamed sources and unchanged reusable-library linkage boundary.

### Dependents and tests

- all `zhttp/test` message/hub/parser/server tests;
- all `zhttp/itest` binaries linked with `libzhttputil.la`;
- all `zhttp/interop` direct low-level builder users and QIR server reuse;
- `zws/src/ZwsExtended.hh` and `zws/src/ZwsH1.hh`; and
- `zhttp/src/Makefile.am` installed headers and required `ZhttpServer.cc`
  source.

## Test Matrix

Add or extend standalone TAP binaries rather than hiding all coverage in one
large integration test.

### Error classification and disposition

- tests for every reachable server-owned standard status mapping and its
  mandatory headers; application-owned statuses remain application tests;
- H1 request-scope error + `Continue` + subsequent request on same connection;
- H1 error + `Disconnect`, proving response commit precedes TCP/TLS close;
- H2/H3 error + `Disconnect`, proving sibling streams survive;
- native H2/H3 connection error, proving application `Continue` is not
  falsely offered;
- malformed peer traffic followed by a healthy request on another connection;
- response-build failure without server failure; and
- listener/runtime failure still setting server health false and waking wait.

### Lifecycle, queue, and ordering

- H1 pipelined order with a slow streamed first response and a ready second
  response;
- no global head-of-line blocking across two H1 links;
- independent H2/H3 stream completion order;
- exact final queue release after out-of-order terminal completion;
- max connection, server-wide request, retained-body/message, and aggregate
  byte boundaries;
- pre-admission saturation pauses/refuses without allocating an error node;
- application-owned `503` behavior where an application has a concrete
  temporary-capacity policy; and
- counter/credit restoration after every rejection and cancellation point.

### Response milestones and accounting

- empty, fixed, optional-empty, optional-present, and streamed responses;
- build failure before headers;
- failure after headers and during body;
- local reset, peer reset/disconnect, and shutdown cancellation;
- produced/committed/reset/discarded byte totals; and
- exact ordering/count of `committed` and `completed` callbacks.

### Fair streaming and shutdown

- large file plus many small responses on shared Tx;
- backpressured client with bounded buffers and byte credits;
- short read, read error, range boundary, and disconnect during file read;
- stop during Rx parse, Tx queueing, retained fixed body, file read, body Tx,
  and terminal callback;
- repeated/concurrent engine start/stop callbacks, stop during start, restart
  during stop, partial-start rollback, and `init()` rejection while running;
- server-health failure remains distinct from normal engine stop;
- repeated init/start/stop/final cycles; and
- no leaked node, buffer, timer, callback, or workload reference.

### Build/platform coverage

At minimum run:

1. the normal configured gcc build and `make test`;
2. direct `zhttp/test` binaries for parser, transport contract, H2/H3 hubs,
   server lifecycle, and new error/queue tests;
3. `zhttp/itest` including `zhttpdtest`, lifecycle, app, and multi-request;
4. `zhttp/interop` including QIR reuse and fallback tests;
5. the `zws` build and its tests;
6. a clang debug build;
7. leak/UB diagnostics on the lifecycle and error suites using `libtool exec`;
8. Linux x64 plus available ARM coverage; and
9. MinGW compile coverage for the new file-I/O and scheduler paths.

Do not run source-tree `.libs` binaries directly.

## Per-Stage Review Checklist

Before declaring any stage complete:

- search for stale names and deleted includes;
- run `git diff --check`;
- list every new type, field, queue, limit, callback, validation, and
  allocation; delete it unless it replaces more code or enforces a named,
  tested invariant that existing machinery cannot enforce;
- verify moved validation was deleted from its old owner and that error/status
  classification did not create a second parser or response check;
- inspect all new cross-shard captures for ownership and variable-size copies;
- inspect every allocation for a named/tunable heap;
- verify every credit has one acquisition and exact-once release;
- verify error status, application disposition, and native protocol action are
  three distinct decisions;
- verify `Disconnect` happens after error response commit when possible;
- verify H2/H3 stream actions do not accidentally call physical connection
  teardown;
- verify callback names match the event actually observed;
- verify shutdown covers the new phase and queue state; and
- run the stage's focused tests before the full suite.

## Final Removal Audit

The final repository search must show no live code references to:

- `ZhttpService.hh`;
- public `Zhttp::Service` or `ServiceConfig`;
- `ServiceResponseDone`;
- protocol `ReqBuilder` or `ResBuilder`;
- `MessageVersion`;
- application `RequestParser` or `requestParser()`;
- `ResponseBuilderBase`;
- `zhttpdapp.cc`;
- the old main-only role for `zhttpd.cc`;
- workload `complete(..., bool sent)`;
- `Hubs::State`, `ServerState`, and custom start/stop callback queues or pending
  flags duplicating `ZmEngine`;
- server-wide `failed()` for request/link failures; or
- the synchronous whole-file `pread` loop on Tx.

Historical planning documents are excluded from the name search.  No shim or
forwarder may be added merely to make this audit pass incrementally.

## Completion Definition

The refactor is complete when the non-negotiable outcomes and final removal
audit pass, all stage gates are green, full gcc and clang builds pass, server
and client tests pass under normal and diagnostic builds, `zws` compiles
against the new API, QIR can reuse `Zhttpd::run()` without `main()` conflicts,
`Server` and `Hubs` use `ZmEngine` without parallel lifecycle state, and
shutdown under load leaves all counters, queues, heaps, and callback owners
drained.
