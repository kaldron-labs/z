# Implementation Plan: Reduce `Zhttp::Server` to Parser/Builder

## Outcome

After this change, `Zhttp::Server` is a protocol and transport framework with
one application boundary in each direction:

```text
configured Rx thread                    configured Tx thread

decoder -> application Parser           application Response -> Builder -> encoder
                         \               /
                          Link::send(...)
```

The server calls an application Parser synchronously on Rx.  The application
submits an owning Response with `Link::send()`.  The server queues that
Response on the originating link and calls its Builder callbacks on Tx.

The server does not copy or retain an application request, dispatch request
work, choose a response, correlate requests and responses, or enforce H1
response order.  An application must call `Link::send()` in request order for
H1.  `zhttpd` satisfies that rule with its own FIFO and one worker.

Complete `plan_2.md` first.  This plan assumes the client API has already been
renamed to `Client::send(slot, request)` and `Pool::send(request)`.

## 1. Fix the Application Contract

Parameterize `Zhttp::Server` by an application type, not by a workload type:

```cpp
template <typename App_>
class Server : public ZmEngine<Server<App_>> {
public:
  using App = App_;
  using AppParser = typename App::Parser;
  using ReqHeaders = typename AppParser::Headers;
  using ResponseQ = typename App::ResponseQ;
  using Response = typename ResponseQ::Node;
  // ...
};
```

Rename `Workload_`, `Workload`, and `m_workload` to `App_`, `App`, and `m_app`
throughout the server.  Do not retain aliases under the old names.

Require the application to provide exactly these request/response facilities:

```cpp
struct App {
  using Parser = /* application Parser */;
  using ResponseQ = /* intrusive ZmList whose Node fulfils Builder */;

  Parser parser();
};
```

Existing connection/listener lifecycle callbacks may remain application hooks;
they are transport lifecycle, not request dispatch.  Remove the request-specific
`request()`, `respond()`, `requestError()`, `committed()`, and `completed()`
hooks.

`App::parser()` is the sole request-construction hook.  Call it once per
request, synchronously on the configured Rx thread, before the first Parser
callback.  Destroy or reset the returned Parser immediately after its
`complete()` callback returns.  Do not require Parser to derive from a server
type or place it in a server-owned intrusive queue.

## 2. Change and Propagate the Parser Contract

Change the terminal Parser callback to exactly:

```cpp
template <typename LinkRef>
void complete(LinkRef &&link, bool ok);
```

Apply the signature wherever `Zhttp.hh` Parser facades terminate a Parser,
including H1, H2, H3, WebSocket, REST, utilities, and tests.  Delete the old
`complete(bool)` form; do not add a compatibility overload.

`LinkRef` is an rvalue handle backed by an owning `ZmRef` to the concrete
server link/request stream.  Forward the existing owning handle into
`complete()`; do not manufacture an intermediate copy at the call boundary.
Synchronous application code can call `link->send()` without copying the
handle.  Code which outlives the callback must move it:

```cpp
auto link_ = ZuMv(link);
```

The forwarding-reference object is valid only during the call and must not be
captured by reference.  After `complete()` the adapter must not reuse that
particular handle.  The transport retains its independent ownership.

Call `complete(..., true)` exactly once after successful request parsing.  If
an application Parser was constructed and parsing/framing fails, call
`complete(..., false)` exactly once as a terminal notification, then close H1
or reset the affected H2/H3 stream.  A false completion is not a response-safe
application dispatch point.  Remove the separate application request-error
callback.

All Parser calls, including `complete()`, are synchronous and execute on the
configured Rx thread.  Document Target, header, and body spans as
borrowed for the duration of the individual callback only.

## 3. Replace `Server::Parser` with a Thin Adapter

Keep `Server::Parser<Profile>` only as the CRTP adapter required by the H1/H2/H3
decoders.  Give it one application Parser plus the minimum decoder bookkeeping
needed for the active request.

Implement its callbacks as direct forwarding calls:

```cpp
void operation(Method method, const Target &target) {
  m_parser.operation(method, target);
}

void header(Header header, ZuBytes value) {
  m_parser.header(header, value);
}

// Forward bodyInfo/body with the corresponding existing signatures.
```

At protocol completion, forward the owning link handle and success flag to
`m_parser.complete(ZuMv(linkRef), ok)`, then destroy/reset `m_parser` before
admitting the next request.

Delete every application-oriented copy currently made by the adapter:

- request target, path, query, authority, and extended-CONNECT protocol;
- method, scheme, target form, offsets, lengths, fallback strings, and storage
  flags;
- selected or complete headers;
- request body retained for later workload inspection; and
- accounting exposed only to the old workload callbacks.

Retain only decoder framing, pending-body, and flow-control state required to
continue parsing safely.  HPACK/QPACK or H1 parsing may materialize a value
before invoking the adapter; the adapter must not make another byte copy of a
span it presents to the application.

For H1, reset the adapter for the next pipelined request without waiting for a
Response.  Delete every Rx pause or admission condition which exists only to
wait for application response production.

## 4. Define the Server Response Type Contract

The application defines an intrusive `ZmList`; the server consumes its Node
type:

```cpp
struct Response_ : public ZmObject, public Zhttp::Builder {
  // Application-owned response state and Builder implementation.
};

using ResponseQ = ZmList<Response_,
  ZmListNode<Response_, ZmListHeapID<"zhttpd.Response">>>;
using Response = ResponseQ::Node;
```

`ResponseQ::Node` publicly derives from `Response_`.  Do not use containment,
`ZmPQueue`, or `ZmPQTx`.  A server Response leaves the pending list when
transmission starts and is then retained by the active transmitter; it is not
held while awaiting a peer response, retry, acknowledgement, or archive.

Require `Response_` to fulfil the response Builder callbacks already consumed
by `sendResponse_` and to expose:

```cpp
Method::T method() const;
```

`method()` returns the original request method so the encoder can apply
HEAD/CONNECT body rules.  This scalar belongs to the submitted Response; the
server must not retain a request object to recover it.

Do not require a sequence number, priority, request pointer, response ID,
completion callback, retry state, or acknowledgement state.  A Response owns
all bytes and body-producer state needed after `Link::send()` transfers it.

## 5. Add `Link::send()` and Link-Local Tx State

Expose this member on every concrete server-side response-capable link/stream:

```cpp
void send(ZmRef<Response> response);
```

Add a private `Server::Responses<Link_>` CRTP mixin which owns the response
queue and implements the common submission/drain logic without virtual
dispatch.  Derive each H1 TCP link, H1 TLS link, H2 response stream, and H3
response stream from that mixin.  Ensure the new overload does not hide
transport-buffer `send()` overloads by bringing the applicable base overloads
into scope.

`Link::send()` is a thin, thread-safe dispatcher.  It must:

1. consume the owning reference passed with `ZuMv(response)`;
2. return immediately for null after releasing it normally;
3. capture only an owning link handle and the owning Response;
4. post them to the configured Tx thread through the existing linear ring;
5. perform no Builder callback on the calling thread; and
6. release the Response on Tx if the link is stopped/disconnected.

The return type is `void`.  Ownership is never returned to the caller, and the
call does not synchronously report eventual Tx acceptance.

Add these logical members to each response-capable link/stream (names may be
adjusted to local profile conventions, but their ownership must not change):

```cpp
ResponseQ       m_responses; // Tx-owned pending FIFO
ZmRef<Response> m_response;  // Tx-owned active transmitter reference
```

Add private Tx-only operations equivalent to:

```cpp
void send_(ZmRef<Response>); // append tail, start if idle
void sendNext_();            // shift head into m_response, start Builder
void sent_();                // release active response, start next
void cancelResponses_();     // drain pending and release active on teardown
```

`send_()` appends at the tail.  `sendNext_()` shifts the head immediately and
moves it into `m_response` before invoking Builder.  The queue therefore holds
only submitted responses which have not started.  Fixed-body completion may
call `sent_()` inline on Tx.  Streaming/asynchronous completion calls it later
on Tx.  Only after `m_response` is released may the next node start.

If existing async body tasks outlive an encoder call, change them to retain
`ZmRef<Response>` (or the active transmitter which owns it), not a moved/copy
of application response data.  Existing body-task shutdown tracking may remain
where needed to cancel asynchronous body work; it must not become a response
scheduler.

On disconnect, reset, and shutdown, stop the active transmitter, drain
`m_responses`, release `m_response`, and make late posted submissions observe
the stopped state and release their Response exactly once.

## 6. Remove the Old Response Scheduler

Route the existing protocol-specific encoding in `sendResponse_` from
`Link::sendNext_()` using `*m_response`.  Preserve protocol header encoding,
flow control, fixed/streaming body behavior, and transport statistics, but
remove its dependency on `LiveReq`, `RequestMeta`, and workload callbacks.
Refactor `ResponseTx`, `ServerTxOps`, and asynchronous body wrappers to refer to
the active Response rather than moving/copying the Builder subobject into a
temporary.  `m_response` is the sole lifetime anchor until `sent_()`.

Delete the following server machinery after link-local transmission works:

- `Server::LiveReq_`, its queue/node types, phases, error/result fields, and
  retained application Request;
- `H1Ready_`, `H1ReadyQ`, `H1ResponseState`, and `LinkResponseState` insofar as
  they implement old request/response scheduling;
- every `ResponseQueue<Profile>` member, including
  `m_h1TCPResponses`, `m_h1TLSResponses`, `m_h2TLSResponses`, and
  `m_h3QUICResponses`;
- `completeLiveRx_()`, `sendLiveTx_()`, `enqueueResponse_()`, global response
  drain/batch functions, and response-ready flags;
- the heterogeneous response emitter; and
- request GC, timeout scanning, committed/completed posting, and bookkeeping
  whose only purpose was the deleted application exchange.

Do not delete client-side `Pool::LiveReq`; it is unrelated.

After deletion, every remaining server member must be protocol parser/flow
state, link/stream transport state, link-local queued/active Response state,
async body transport state, configuration, or aggregate transport telemetry.

## 7. Make Ordering Policy Explicit

The library appends Responses in the order in which the Tx linear ring receives
`Link::send()` posts.  Rx and Tx are each single-threaded, so calls made on Rx
without intervening producers preserve their order.  Calls from other threads
are ordered only by ring ingress.

For H1, the application must submit exactly one Response for each successfully
handled request, in request order.  The server must not:

- associate a Response with a request;
- assign, inspect, or validate sequence numbers;
- wait for a missing Response;
- reorder Responses;
- serialize application producers; or
- pause pipelined Rx until a Response exists.

An asynchronous application is responsible for arranging ordered calls.  It
may use one FIFO/worker, link-affined worker selection, an application
resequencer, or another strategy.  None of these strategies appears in
`Zhttp::Server`.

For H2/H3, attach `ResponseQ` to the existing per-request stream/link which
owns response transmission.  Never queue a stream Response at a connection
object which could send it on another stream.

## 8. Delete `RequestMeta`

Delete `RequestMeta` from `ZhttpServer.hh` and remove every include, base class,
parameter, accessor, heap ID, and callback which mentions it.  Do not replace
it with `RequestContext`, `Exchange`, `Reply`, or another server-owned
application request representation.

Protocol-required peer addresses, TLS state, stream IDs, and close/reset state
remain accessible through the concrete Link.  An application Parser copies
such values from the Link during `complete()` only if it needs them after that
callback.

The server no longer owns:

- target/path/query/authority/protocol strings;
- copied method, scheme, or target form;
- application body accounting;
- routing or file-planning state; or
- application logging fields.

## 9. Combine `Target.path` and `query`

In `Zhttp.hh`, replace the separate borrowed `path`, `query`, and `hasQuery`
members with one borrowed `pathQuery` span.  Update every constructor/parser
site as follows:

- H1 origin-form: reference the received target directly;
- H1 absolute-form: isolate the contiguous path-plus-optional-query portion;
- H2/H3: reference `:path` directly;
- authority-form CONNECT: use an empty path-query;
- extended CONNECT: reference its `:path` directly.

Do not synthesize a trailing `?`, allocate, or copy.  Applications which need
components call the existing path/query split helper on `pathQuery`.

Delete `ZhttpURL`'s `PathQuery` model.  The outbound request Builder operation
passes one emitter which writes the complete path plus any query directly to
the protocol transmit sink.

Update direct consumers in `zrest`, `zws`, utilities, and tests.  Any
filesystem consumer must split before decoding, normalization, lookup, stat,
open, directory listing, or redirect construction.

## 10. Implement the `zhttpd` Application

Rename its concrete `Workload` to `App` and make it provide `Parser parser()`
and `ResponseQ`.  Preserve listener/connection lifecycle hooks used by the
utility, but remove its old request/response workload callbacks.

### 10.1 Owned request data

Change `zhttpd.hh::RequestData` so it no longer derives from `RequestMeta`:

```cpp
struct RequestData : public RequestHeaders {
  // Only fields used by routing, planning, body validation, and logging.
};
```

Add explicit owned fields for the method, split path/query, authority/host,
body data and counters, connection policy, and any peer/protocol values which
the existing planner or logger actually reads.  Delete fields which have no
remaining reader.

The `zhttpd` Parser copies borrowed values as their callbacks arrive.  It
splits `Target::pathQuery` immediately and stores path and query
separately.  Query bytes must never enter path decoding or filesystem logic.

### 10.2 Request-work FIFO

Define an application work node and simple intrusive list in `zhttpd.cc`:

```cpp
struct Work_ : public ZmObject {
  ZmFn<void(), ZmFnHeapID<"zhttpd.WorkFn">> run;
};

using WorkQ = ZmList<Work_,
  ZmListNode<Work_, ZmListHeapID<"zhttpd.Work">>>;
using Work = WorkQ::Node;
```

The type-erased function is deliberate: it owns the profile-specific LinkRef
and owned `RequestData` without introducing a common virtual Link type.

In `Parser::complete(LinkRef &&link, true)`, move the LinkRef and owned request
into `Work::run`, append the node to `App`'s `WorkQ` on Rx, and signal one
dedicated worker.  The worker repeatedly shifts the FIFO head, invokes it,
plans the response, constructs `Response`, and calls
`link->send(ZuMv(response))` directly.

Protect/schedule `WorkQ` with the existing application threading primitive
appropriate to the chosen worker.  Maintain one scheduled/draining flag so an
empty-to-nonempty transition schedules the worker once and the worker drains
in FIFO order.  Do not put this queue, worker, flag, or type-erased function in
`Zhttp::Server`.

For `complete(..., false)`, do not enqueue partially parsed file work or submit
a Response.  Log the parse failure using already owned safe fields and return;
the protocol then closes/resets.

### 10.3 One concrete response node

Replace `ResponseBase`, `FixedResponse`, `StreamResponse`, and `EmptyResponse`
with one `Response_` in `zhttpd.cc`.  Give it a small mode enum and owned state
for the modes actually emitted by the planner (empty, fixed, redirect/error,
PUT/JSON, and file stream).  Implement Builder callbacks with a `switch` on
that mode.

Define exactly:

```cpp
using ResponseQ = ZmList<Response_,
  ZmListNode<Response_, ZmListHeapID<"zhttpd.Response">>>;
using Response = ResponseQ::Node;
```

The Response owns headers, reason/body bytes, request method needed by response
framing, file path/handle and stream cursor, and any transmit-side logging data.
No Builder callback may refer to Parser or Work storage after
`Link::send()`.

The worker constructs `ZmRef<Response>`, fills it from `ResponsePlan`, and
submits it.  It does not call a server emitter or completion callback.

`zhttpd` is intentionally a model server.  The single FIFO plus one worker is
the prescribed implementation for it, not a reusable server feature or a
recommendation for production static-file servers.

## 11. Error and Lifetime Rules

The library never manufactures an application HTTP error Response.  A false
Parser completion is terminal: after notifying the Parser, H1 closes and H2/H3
resets the affected stream according to existing protocol rules.  Transport
control frames remain internal.  Application-level failures discovered after
a successfully parsed request use a normal error Response and `Link::send()`.

Returning from `complete(..., true)` does not imply that a Response already
exists.  The server retains no request token and applies no application
timeout.  The moved LinkRef keeps the asynchronous application's send endpoint
valid subject to normal disconnect/reset state.

After `Link::send()`, only Tx invokes Builder.  Application code must not mutate
the Response except through an explicitly thread-safe asynchronous body
producer already supported by the Builder contract.

## 12. Implementation Order

Implement in this order so each intermediate state is reviewable:

1. Complete `plan_2.md` and update call sites to client `send` terminology.
2. Change Parser completion to `complete(LinkRef &&, bool)` and update all
   facades/test Parsers while retaining the current server response path.
3. Add the App `Parser`/`ResponseQ` aliases and rename Workload to App without
   compatibility aliases.
4. Add `Link::send()`, per-link `ResponseQ`, active Response ownership, teardown,
   and focused queue/lifetime tests.
5. Drive the existing H1/H2/H3 response encoders from the active Response and
   verify every Builder callback runs on Tx.
6. Remove global response queues, H1 ready scheduling, `LiveReq` response
   correlation, and workload response callbacks.
7. Reduce `Server::Parser` to direct forwarding and delete all application
   request copies and deferred dispatch.
8. Delete `RequestMeta` and migrate remaining direct consumers to owned
   application fields or Link transport access.
9. Implement `zhttpd`'s Parser, `Work_`/`WorkQ`, one worker, unified
   `Response_`/`ResponseQ`, and direct `Link::send()` path.
10. Combine Target into `pathQuery`, update H1/H2/H3 construction, and
    migrate `zhttpd`, `zrest`, `zws`, utilities, and tests.
11. Delete dead phases, heap IDs, configuration, callbacks, and includes; use
    `rg` to prove none of the removed names remain.
12. Build and run the focused protocol/application tests, then the full build
    and test suite.

## 13. Required Tests

Add focused tests which prove:

- Parser callbacks, including `complete()`, execute synchronously on configured
  Rx for H1, H2, and H3;
- callback spans are forwarded without adapter-owned copies;
- a synchronous Parser can use the forwarded LinkRef without a ref-copy at the
  completion call boundary;
- moving LinkRef out of `complete()` supports delayed application processing;
- `Link::send()` may be called from Rx and from another application thread;
- Builder callbacks execute only on configured Tx;
- per-link FIFO drains tail-in/head-out and removes a Response when it starts;
- fixed and asynchronous streaming Responses retain their node until final Tx
  completion;
- disconnect/reset/shutdown releases pending, active, and late-posted Responses
  exactly once;
- H1 continues parsing multiple pipelined requests before Responses are
  submitted;
- the wire observes the order of `Link::send()` calls, with no library
  correlation/reordering;
- H2/H3 queues are stream-local;
- `zhttpd`'s WorkQ preserves receive order and its sole worker submits in that
  order;
- Target preserves an empty query marker (`/p?`) and ordinary query
  bytes in `pathQuery`; and
- `zhttpd` strips query bytes before every filesystem operation.

Run at minimum:

```sh
make -j8
make test
```

Also run the affected standalone `zhttp`, `zws`, and `zrest` test binaries from
the build tree, plus `zhttpd` H1/H2/H3 integration/interoperability tests which
exist in the configured build.  Use `libtool exec` for debugger/sanitizer runs.

Before declaring completion, run searches for:

```text
RequestMeta
Server::LiveReq
Workload::respond
requestError
committed
completed
enqueueResponse_
sendLiveTx_
completeLiveRx_
m_h1TCPResponses (and the other profile response queues)
Target.query
Target.hasQuery
```

Only unrelated client-side `Pool::LiveReq` and independently meaningful uses
of generic words such as `completed` may remain.

## Acceptance Criteria

- `Zhttp::Server` constructs and synchronously calls only the application
  Parser on Rx.
- No server/associated library code copies data presented to that Parser.
- Parser completion is exactly `complete(LinkRef &&, bool)` with an owning,
  movable concrete-link handle.
- The only application response-submission API is `Link::send(response)`.
- Each response-capable link/stream owns one intrusive `ZmList` pending queue
  and one active Response reference; there is no response pool/global queue.
- Builder callbacks run only on Tx, and queued/active Response ownership is
  correct across asynchronous bodies and teardown.
- H1 Rx is never paused to await application response production.  The server
  performs no request/response correlation or ordering work.
- `zhttpd`, not the library, owns request copies, routing/planning, its WorkQ
  and worker, H1 ordering, unified Response implementation, and logging.
- `RequestMeta`, server-side `LiveReq`, workload response callbacks, generic
  emitter, and compatibility shims are deleted.
- `Target` exposes combined `pathQuery`; filesystem code splits it before
  handling a path.
