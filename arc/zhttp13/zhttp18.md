# `Zhttp` Server-Side Cleanup Plan

## Summary

Bring the server side up to the same standard as the cleaned-up client side:
consolidate `ZhttpService.hh` and `ZhttpServer.hh` into one authoritative
`ZhttpServer.hh`; use `Request`, `Response`, and `LiveReq` for the objects which
actually have those roles; give each live request one explicit owner across
Rx, Tx, and completion; introduce bounded request/response admission instead
of posting an unbounded population of scheduler closures; and separate
peer/request failure from service failure.

The most urgent work is not cosmetic.  The current implementation has two
misleading and dangerous boundaries:

- `Service::Session::error()` calls `Service::failed()`, so a malformed peer
  request stops the entire service rather than failing one request or link;
- the workload `complete(...)` callback runs after response construction and
  queueing, not after the transport has sent or acknowledged the response.

Fix those semantics first.  Then perform the naming and ownership changes as
one breaking refactor and propagate them through `zhttpd`, tests, interop, and
`zws`.  Do not preserve the old names with aliases or forwarding wrappers.

## Scope and Current Shape

The server-side implementation is spread across three layers and two
needlessly separate top-level headers:

- `ZhttpServer.hh` is a thin, single-profile transport hub adapter despite
  occupying the obvious public `Server` name;
- `ZhttpService.hh` is the actual public multi-protocol HTTP server and owns
  request parsing, workload dispatch, response construction, admission, and
  lifecycle; and
- `ZhttpMessage.hh` plus the H1/H2/H3 message classes provide the protocol
  parsers and builders used by both client and server.

Consolidate the first two into `ZhttpServer.hh`.  The profile hub adapter and
the multi-protocol coordinator are parts of one server implementation and do
not justify separate public headers.  Delete `ZhttpService.hh` after moving
its implementation; do not retain it as a forwarding include.

`zhttpdapp.cc` is the primary server consumer.  It exposes the awkwardness in
the current application contract: `RequestParser` is the application request,
`ResponseBuilderBase` is the common application response, and three response
types are passed through an `emit(builder, completion)` callback.

The client cleanup provides useful patterns, but the server must not copy it
mechanically.  Client requests are homogeneous, application-generated queue
nodes with retry/archive semantics.  Server responses may have heterogeneous
compile-time body policies and are created in reaction to peer-controlled
input.  The homogeneous server queue object should therefore be the live
request, not an erased or virtualized response builder.

## Priority 1: Correct Failure Scope

### Do not stop the service for a peer error

`Service::Session::error()` currently calls `service->failed()`, which sets
`m_failed` and stops the shared `Runtime`.  A malformed request can therefore
terminate all listeners and unrelated connections.  Response construction
failure and a workload which fails to emit also take the same global path.

Define separate failure scopes:

```text
peer/request error       -> send/classify error; application selects disposition
response Tx error        -> fail that request; reset/disconnect as required
connection error         -> close that connection and release admission
listener/runtime failure -> fail the service and stop the shared runtime
```

Only listener startup failure, unrecoverable hub failure, or an explicitly
fatal application decision should call the service-wide failure transition.
Protocol parse errors need a per-protocol policy: send an error response where
the framing state still permits it and give the application control of the
request disposition.  H2/H3 stream errors must not take down other streams on
the same connection unless the protocol classifies the error as
connection-wide.

Where HTTP defines a standard response status for a standard request error,
use it rather than collapsing all failures to `400 Bad Request` or silently
disconnecting.  The common mapping should include:

| Request failure | HTTP response |
| --- | --- |
| malformed request syntax or invalid framing which still permits a response | `400 Bad Request` |
| authentication credentials required or rejected | `401 Unauthorized`, including the required challenge |
| valid request refused by authorization policy | `403 Forbidden` |
| unsupported method for the selected resource | `405 Method Not Allowed`, including `Allow` |
| request timeout before a complete request is received | `408 Request Timeout` |
| request requires a declared content length under server policy | `411 Length Required` |
| unsupported request content encoding | `415 Unsupported Media Type` |
| unsupported expectation | `417 Expectation Failed` |
| request target too long | `414 URI Too Long` |
| request body exceeds the configured limit | `413 Content Too Large` |
| request header section exceeds the configured limit | `431 Request Header Fields Too Large` |
| request is directed to the wrong origin/server | `421 Misdirected Request` |
| method or transfer coding is not implemented by the server | `501 Not Implemented` |
| unsupported HTTP version | `505 HTTP Version Not Supported` |
| temporary server request-capacity exhaustion | `503 Service Unavailable`, with `Retry-After` when known |

Keep protocol/mechanical classification in the server and application-policy
classification in the workload.  For example, the server can identify an
oversized header block, invalid HTTP syntax, unsupported protocol version, or
admission exhaustion; the application decides whether a valid request is
unauthorized, forbidden, missing, or unsupported for a particular resource.
Centralize construction of library-generated error responses so H1/H2/H3 use
the same status, mandatory headers, body suppression rules,
application-selected disposition handling, and accounting.

### Let the application select request-error disconnection

For a request error, the application must control whether handling the error
also disconnects the affected HTTP stream.  The server supplies the parsed
error classification and standard default status; it must not infer
disconnection merely from that status or unconditionally close after sending
a `4xx`/`5xx` response.

Expose the choice explicitly in the request-error callback/result, for example
as a named disposition with at least `Continue` and `Disconnect`.  Do not use
an unexplained trailing Boolean.  The application must be able to retain a
recoverable H1 connection after an error response, or request disconnection
when its policy does not permit further requests from that peer.

`Disconnect` here means disconnecting the HTTP stream represented by the
server link abstraction.  Its transport consequence depends on the active
profile and stream topology:

- for H1 over TCP or TLS, disconnecting the HTTP stream normally disconnects
  the underlying TCP/TLS connection;
- for an H2 or H3 request stream, it should normally terminate/reset only that
  stream while preserving other streams on the connection; and
- where the logical stream is inseparable from its transport, or the transport
  implementation maps the operation more broadly, it can imply a TCP, TLS, or
  QUIC connection disconnect.

Make that consequence visible to the application through the profile/request
context and document the mapping for each link type.  Apply a requested
disconnect only after the error response has been committed when a response
is possible; an immediate disconnect must not silently discard the selected
error response.

An HTTP error response is possible only after enough of the request has been
decoded to identify a response-capable stream and while outbound framing is
still valid.  Otherwise apply the HTTP-version-specific stream reset or
connection error.  Never send an HTTP status in place of a required H2/H3
protocol error, and never promote a stream-local request error to a shared
connection or service failure merely because an error response cannot be
encoded.

Distinguish such unrecoverable framing/transport failures from request errors
before invoking application policy.  If protocol correctness makes continued
use impossible, report that the stream or connection is already terminal
rather than accepting and then ignoring an application `Continue` decision.

Replace the overloaded `failed()` name with explicit transitions such as
`failService_()`, `failLink_()`, and `failRequest_()`.  Add separate telemetry
for service faults, rejected requests, parse failures, response build
failures, and transport failures.  `ok()` must report only service health.

If service health can be read outside its owning shard, make that diagnostic
read race-free; the current plain `bool m_failed` must not be concurrently
written and read without an ownership rule.

### Give response completion truthful semantics

`sendResponse_()` currently returns after it has constructed headers/body and
submitted buffers to the link.  `Session::request()` then posts back to Rx and
calls the workload's `complete(..., sent)`.  The `sent` value means
"successfully constructed and queued", not "sent to the peer".

Rename and separate the milestones:

- `queued` or `committed`: response construction succeeded and the transport
  accepted the buffers;
- `completed`: the transport has completed the response successfully;
- `failed`: the transport rejected, reset, or failed committed output; and
- `discarded`/`reset`: produced bytes which did not complete, using the same
  explicit accounting vocabulary as the client.

If the service only needs the first milestone initially, expose it as
`committed(...)` and do not call it `complete`.  If a real terminal callback is
provided, connect it to link/stream Tx acknowledgement rather than immediately
posting it after `link.finish()`.

Use a server-side result type rather than a trailing Boolean:

```c++
struct ResponseResult {
  ResponseCode::T code;
  BodyCommit body;
  bool headers;
  bool final;
};
```

Exact fields should follow what the transports can truthfully report.  Do not
claim peer receipt if the available event proves only local transport
completion.

## Priority 2: Normalize the Public Vocabulary

### Rename protocol builders by message role

`ResBuilder` should become `Response`.  Apply the rename consistently rather
than fixing only the service wrapper:

| Current | Recommended |
| --- | --- |
| `H1::ResBuilder` | `H1::Response` |
| `H2::ResBuilder` | `H2::Response` |
| `H3::ResBuilder` | `H3::Response` |
| `MessageVersion::ResBuilder` | `MessageVersion::Response` |
| `H1ResBuilder` | `H1Response` |
| `H2ResBuilder` | `H2Response` |
| `H3ResBuilder` | `H3Response` |

The same pass should rename `ReqBuilder` to `Request`; otherwise the two sides
retain different naming schemes for the same abstraction.  `ReqParser` and
`ResParser` should be spelled `RequestParser` and `ResponseParser` where the
name denotes a parser rather than an application object.

After the type is named for the message, avoid expressions such as
`response.response(tx)`.  Rename the protocol operation to describe the phase,
for example `begin(tx)` or `headers(tx)`, while retaining `body(tx)` and
`finish(tx)`.  Select the name based on the exact framing boundary common to
H1/H2/H3.

This is a repository-wide breaking rename.  Update `zhttp` tests and interop,
the public aliases in `Zhttp.hh`, and dependent `zws` code in the same change.

### Reserve `Request` and `Response` for application messages

Within `Service`, rename the application contract as follows:

| Current | Recommended |
| --- | --- |
| `Workload::RequestParser` | `Workload::Request` |
| `requestParser()` | `request()` |
| `AppRequestParser` | `Request` |
| template `AppBuilder` | template `Response` |
| `BuilderApp_` | `ResponseOps` |
| internal `Builder` | `ResponseTx` |
| `RequestOps` | `RequestState` or fold it into `LiveReq` |
| `ServiceResponseDone` | remove; otherwise `CompletionPost` |

An application `Request` implements the inbound parser/sink contract; an
application `Response` implements the outbound message contract.  "Builder"
describes an implementation technique and should remain only in low-level
generic code where it materially distinguishes construction from parsing.

In `zhttpdapp.cc`, rename `ResponseBuilderBase` to `ResponseBase` and retain
the already good concrete names `FixedResponse`, `StreamResponse`, and
`EmptyResponse`.

### Put the public `Server` name on the public server

`Zhttp::Service` is the user-facing multi-protocol server.  `Zhttp::Server` in
`ZhttpServer.hh` is an internal single-profile hub base.  Prefer:

- `Service` -> `Server`;
- `ServiceConfig` -> `ServerConfig`; and
- current `Server<App, Profile>` -> `ServerHub<App, Profile>` or
  `ProtocolServer<App, Profile>`.

This makes `Zhttp::Client` and `Zhttp::Server` the symmetric public
coordinators and leaves `Hub` for transport/listener machinery.  Both the
public coordinator and its internal profile hub implementation belong in the
consolidated `ZhttpServer.hh`.  Also rename heap IDs, comments, tests, and
diagnostic labels; do not leave a split `Service`/`Server` vocabulary or a
forwarding `ZhttpService.hh`.

## Priority 3: Introduce One Explicit `LiveReq`

The current live request is distributed across `RequestOps`, its embedded
`RequestInfo`, the application parser `sink`, `ServerSession::complete`, a
captured link reference, and the heap-allocated `ServiceResponseDone` used
during the return post.  Consolidate these into one server-owned request node.

A target shape is:

```c++
struct LiveReq : public ZmObject {
  LinkRef link;
  Request request;
  RequestMeta meta;
  RequestBody body;
  uint64_t id = 0;
  uint64_t sequence = 0; // H1 response ordering on a persistent link
  RequestPhase::T phase = RequestPhase::Receiving;
};
```

The exact link handle must follow the existing transport ownership pattern.
The important properties are:

- the node owns all state which must survive parser reset and shard posting;
- the parser refers to the active node rather than being the node by
  inheritance;
- ownership transfers explicitly from Rx parsing to response admission to Tx
  completion;
- H1 can install a fresh node before parsing the next pipelined request; and
- shutdown drains/cancels every node before releasing `Service`, workload, or
  link owners.

Allocate `LiveReq` from a named tunable heap or reuse a bounded pool.  The
current `new ServiceResponseDone` is a heap allocation for every response and
does not use `ZmHeap`; delete that wrapper once the live node itself survives
completion.  Do not move/copy the large metadata strings through successive
scheduler captures.

Use an enum-backed lifecycle with transition helpers, for example:

```text
Receiving -> Ready -> Queued -> Building -> Committed -> Completing -> Done
     |         |        |          |             |
     +---------+--------+----------+-------------+-> Failed
```

Keep transport/body framing state orthogonal to lifecycle, as on the client.
Transitions should assert the owning shard and guarantee exact-once workload
notification and admission release.

## Priority 4: Bounded, Fair Server Admission

### Bound messages, not only connections

`ServiceConfig::maxConnections()` and `m_active` limit admitted links or
connections.  They do not provide a service-wide bound on concurrent H2/H3
streams, H1 pipelined requests, queued response work, retained response bytes,
or scheduler closures.  The existing `retainedBodyMax` and
`retainedMessageMax` are per response, so many concurrent responses can still
retain an unbounded aggregate.

Add explicit limits and telemetry for:

- active connections;
- live requests across all transports;
- queued/active response builds;
- aggregate retained response bytes; and
- optionally per-connection live requests and queued response bytes.

Rename `active()` to `activeConnections()` and add names such as
`activeRequests()` and `queuedResponses()`; do not overload one counter with
multiple meanings.

Admission must occur when a new request/stream begins, not only when a socket
is accepted.  When capacity is exhausted, apply a protocol-appropriate
mechanism: pause input or produce the standard overload error, then apply the
application's request-error disposition instead of unconditionally closing.
For H2/H3, any required refusal/reset must not disturb unrelated streams.
Ensure the policy cannot allocate a full response object merely to report that
capacity is exhausted.

### Queue homogeneous `LiveReq` nodes

The client demonstrates the useful `ZmPQTx` properties: intrusive lifetime,
Tx ownership, exact acknowledgement, resumable backpressure, and scheduled
turns which do not monopolize a shard.  Use those properties on the server at
the homogeneous `LiveReq` layer.

Do not attempt to put arbitrary application `Response` types directly into
one queue.  Their fixed/streaming/empty types are intentionally heterogeneous
so body policy remains compile-time.  Type erasure or virtual response nodes
would add allocation and indirect dispatch merely to satisfy the queue.

A service-owned response-admission `ZmPQTx` can queue `LiveReq` nodes keyed by
a service request ID.  Its `send_()` begins application response selection and
protocol transmission when both response-build and aggregate-byte credits are
available.  Terminal completion acknowledges exactly that node and returns
credits.  If `ZmPQTx` resend/archive semantics add no value after the design is
worked through, use a smaller intrusive queue with the same ownership and
scheduled-turn discipline; do not use `ZmPQTx` by analogy alone.

Global response completion is unordered across links and multiplexed streams.
H1 response order is nevertheless per connection.  Preserve it with either:

- a small ordered queue/sequencer per H1 link feeding the global unordered Tx
  admission queue; or
- a per-link next-response sequence gate which returns transient backpressure
  until preceding responses commit.

Do not create one globally ordered queue: one slow connection must not block
responses for every other connection.  H2/H3 streams may complete out of
order subject to their own protocol dependencies.

### Make body production fair

The application body callback executes synchronously on the Tx shard.  In
`zhttpd`, `StreamResponse::body()` performs a complete `pread` loop there, so
one large file can monopolize the shared Tx thread.  A response queue alone
does not fix this because one queue turn still produces the entire body.

Add incremental/asynchronous body production with bounded chunks and explicit
resume on transport capacity.  File I/O must not block the network Tx shard;
use the existing async I/O facilities or a dedicated application/file-I/O
shard, then move pooled `ZiIOBuf` references to Tx.  Preserve the current
pooled-buffer model and avoid copying file chunks through contiguous scratch
storage where direct buffer reads are available.

Fixed-body generation may remain synchronous only within a configured small
bound.  Charge retained header and body buffers against both per-message and
aggregate budgets before allocation/retention, and return credits on every
failure and cancellation path.

## Priority 5: Clarify Shard and Callback Contracts

The current workload object is invoked from several contexts:

- request parsing and `response(...)` selection occur on Rx;
- application response header/body methods run on Tx after the response object
  is moved into a Tx closure;
- the current `complete(...)` callback is posted back to Rx; and
- listener/connection callbacks depend on hub callback context.

Make this contract explicit in `ZhttpServer.hh` and enforce it with debug
shard assertions.  Prefer trailing-underscore methods for owner-only entry
points and thin public dispatchers for cross-shard calls.

The `emit` callback passed to `Workload::response()` is synchronous in the
current implementation: it captures stack references to `link`, `request`,
and `emitted`.  State that constraint in the contract or, preferably, remove
the misleading callback-shaped API.  Viable replacements are:

- a synchronous `respond(liveReq, emit)` whose name and documentation make
  the synchronous visitation explicit; or
- an asynchronous API which transfers a `LiveReqRef`/completion handle by
  value and permits response submission later without dangling captures.

Do not leave an API which looks retainable but becomes undefined if retained.
Whichever model is selected must enforce at-most-one response without a stack
Boolean and must define cancellation when the peer disconnects while
application work is outstanding.

Similarly, state whether a workload `Request` remains Rx-owned after parsing
or is moved to an application worker/Tx owner.  Do not permit concurrent
access by adding locks around what should be shard-affined state.

## Priority 6: Simplify Request Metadata and Accounting

`RequestInfo` contains storage for a fixed list of application headers
(`authorization`, `range`, `referer`, and others) while the parser is already
parameterized by `Workload::Request::Headers` and forwards those same headers
to the application request.  This couples the generic service to `zhttpd`-like
policy and enlarges every live request even when an application does not use
those fields.

Keep only transport-neutral core metadata in the server:

- method and owned request target;
- peer endpoint/connection identity;
- transport, HTTP version, and security/framing facts; and
- body/result accounting required by the framework contract.

Let the application `Request` retain application-specific headers.  Move the
static-file headers currently copied by `RequestInfo` into `zhttpd`'s
`Request`.  Prefer retaining a typed peer endpoint or `ZiCxnInfo` subset over
formatting only `remoteIP` into `EndpointString`; preserve the remote port and
avoid formatting until diagnostics or access logging needs text.

Replace the flat body counters with a named group:

```c++
struct RequestBody {
  uint64_t received = 0;
  uint64_t consumed = 0;
  uint64_t pending = 0;
  uint64_t reset = 0;
  uint64_t discarded = 0;
};
```

Update this group directly.  The current `RequestOps` duplicates the counters
and repeatedly snapshots four of them into `RequestInfo`.  Preserve the
distinction between reset and discarded bytes even where terminal paths
currently increment both.

Use one owned request-target representation rather than the current
`target`/offset/path/query plus fallback-storage collection if a local storage
type can preserve the same no-extra-copy fast path.  Do not introduce a
temporary contiguous conversion solely to simplify the type.

## Priority 7: Share Client and Server Tx Machinery

`Service::sendStreamingResponse_()`, `sendFixedResponse_()`, and
`sendFixedResponseH2_()` substantially duplicate the request transmit
algorithm in `ClientMessage`: body-policy dispatch, optional-body visitation,
duplicate-emission checks, header provisioning/patching, H1 retained-header
ordering, H2 deferred commit, H3 deferred compression, and retained budgets.
This is already difficult to audit and will drift when one side gains a fix.

Factor the common algorithm into compile-time message Tx machinery
parameterized by the small semantic differences:

- request versus response start-line/pseudo-header emission;
- body-allowed and content-length-forbidden response rules;
- commit/result accounting callbacks; and
- link finish/failure policy.

Keep CRTP/static dispatch and concrete body stream types.  Do not introduce a
virtual message interface or `std::function`.  The factor should make one
implementation responsible for optional/fixed/streaming body rules across
H1/H2/H3 while allowing request and response adapters to supply their protocol
operations.

Within the service, flatten `sendResponse_()` after factoring.  Repeated empty
response construction and repeated `link.finish()`/validation blocks should
be centralized so every exit has explicit commit, failure, and credit-release
behavior.

## Priority 8: Consolidate the Server Header

Merge `ZhttpService.hh` into `ZhttpServer.hh` and make that file the single
authoritative server entry point.  It should contain:

- `ServerConfig`;
- the public multi-protocol `Server<Workload>` coordinator;
- the internal single-profile server hub adapter currently named `Server`;
- the server `Hub`, `Link`, `Session`, request-parser, `LiveReq`, and response
  Tx adapters which must remain templated; and
- the server application contract and ownership/shard documentation.

Rename the current thin `Server<App, Profile>` before introducing the public
`Server<Workload>` name, then move the coordinator from `ZhttpService.hh`.
Keep the profile adapter in the same header under a clear internal name or
implementation namespace.  Do not create `ZhttpServerHub.hh`; the purpose of
this cleanup is to consolidate the complete server implementation rather than
move the existing split to a differently named file.

Delete `ZhttpService.hh` and update all includes in `zhttpd`, tests, interop,
installed-header lists, and dependent modules to include `ZhttpServer.hh`
directly.  Do not leave a compatibility forwarding header.  `Zhttp.hh` should
include only the consolidated header when full server support is enabled.

Shared request/response Tx algorithms still belong in `ZhttpMessage.hh` or a
narrowly named internal message-Tx header because they are used by both client
and server.  Non-template enums, result formatting, and other suitable code
may move to `ZhttpServer.cc` to limit template/debug symbol bloat; this does not
split the public header contract.

The resulting layout gives a reader one place for the complete server
lifecycle while keeping genuinely cross-client/server message machinery in
its shared layer.

### Make the `zhttpd` source names reflect their roles

The split between `zhttpd.cc` and `zhttpdapp.cc` is intentional, but the names
are misleading.  `zhttpd.cc` contains only `main()`, while `zhttpdapp.cc`
contains `Zhttpd::run()` and the actual reusable server application.  The
latter is compiled into `libzhttputil.la` so integration and interoperability
programs such as `zhttpqirimpl.cc` can run `zhttpd` in-process without linking
another `main()`.

Preserve that useful linkage boundary while renaming the files:

| Current | Recommended role and name |
| --- | --- |
| `zhttpd.cc` | executable entry point: `zhttpdmain.cc` |
| `zhttpdapp.cc` | reusable server application: `zhttpd.cc` |
| `zhttpdutil.cc` | retain for out-of-line utility/enum definitions |

Update `zhttp/util/Makefile.am` so `libzhttputil.la` contains `zhttpd.cc` and
`zhttpdutil.cc`, while the `zhttpd` executable compiles `zhttpdmain.cc` and
links that library.  Continue to expose `Zhttpd::run()` through `zhttpd.hh` for
the in-process test/interop consumers.  Do not fold `main()` into the reusable
translation unit, which would reintroduce symbol conflicts for those users.

## Shutdown and Draining

Define shutdown in terms of the new ownership model:

1. stop listener admission;
2. stop accepting new request/stream admissions;
3. cancel or drain Rx-owned partially received requests;
4. drain/cancel queued response work and return all credits;
5. complete or reset committed Tx work according to the stop policy;
6. deliver each workload terminal callback exactly once;
7. release links and connection admission; and
8. finalize hubs/runtime only after the Rx and Tx owner queues are empty.

The existing extra Rx drain after native hub teardown is important and should
remain, but it is not sufficient once application work and response queues can
outlive parsing.  `LiveReq` ownership must make the drain condition observable
rather than relying on captured references buried in scheduler closures.

## Tests Required by the Refactor

Add focused TAP tests for the behavior which is currently implicit or wrong:

- a malformed request fails only its request/link and the server continues to
  serve a subsequent valid request;
- a recoverable request error with `Continue` preserves the H1 connection,
  while `Disconnect` commits the error response and then closes it;
- H2/H3 request-error disconnection terminates the selected logical stream
  without closing unrelated streams, except where the tested profile
  explicitly maps the logical disconnect to the underlying transport;
- response construction failure does not stop unrelated listeners or links;
- service/listener failure still wakes `wait()` and makes `ok()` false;
- the committed callback and true terminal callback, if both exposed, occur at
  their documented transport milestones;
- H1 pipelined responses commit in request order even when application work
  completes out of order;
- H2/H3 responses on independent streams can complete out of order;
- global and per-connection request limits reject or pause without exceeding
  configured live-node and byte budgets;
- one large/slow response does not starve small responses on other links;
- disconnect during application work, queued response work, fixed-body
  retention, and streamed output releases every node and credit exactly once;
- stop drains both Rx and Tx ownership without callbacks into a destroyed
  workload; and
- response-body produced, committed, reset, and discarded accounting is
  correct for failure before headers, after headers, and during body output.

Propagate the naming changes through compile-time contract tests so old
`ResBuilder`, `RequestParser`, and `Service` vocabulary cannot silently remain
in less frequently built interop or `zws` targets.

## Recommended Implementation Order

1. Split service-wide failure from request/link failure and correct callback
   naming/semantics without changing queue architecture.
2. Perform the repository-wide `Request`/`Response`/`Server` vocabulary change
   and consolidate `ZhttpService.hh` into `ZhttpServer.hh`.
3. Introduce pooled `LiveReq` ownership and remove `ServiceResponseDone` plus
   stack-reference `emit` lifetime assumptions.
4. Add request/response/byte admission limits and the Tx-owned live-request
   queue with per-H1 sequencing.
5. Make streamed body production incremental and non-blocking on the Tx shard.
6. Factor the common client/server body-policy and retained-message Tx paths.
7. Complete shutdown/drain integration, telemetry, and the concurrency/failure
   test matrix.

Each stage should leave one authoritative path.  Delete superseded state,
names, and compatibility code as it lands rather than operating parallel old
and new server lifecycles.
