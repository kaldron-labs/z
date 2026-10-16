# Zhttp message-body construction refactor

The refactor is divided into cumulative implementation and review phases.
Each phase ends with acceptance criteria which define its completed scope.
These are gates on one intentionally breaking development branch, not
compatibility-supported releases: an intermediate phase need not be
independently landable, and it must not preserve the old API with shims or
parallel paths merely to compile.  Structural criteria must hold when the
phase completes.  Executable criteria are run only at a stable, buildable
checkpoint; if a phase deliberately ends in an interim state which cannot be
built or tested, defer those criteria to the next stable checkpoint rather
than manufacturing a temporary compatibility path or performing a knowingly
unproductive build.

| Phase | Outcome |
| --- | --- |
| 1 | Restore and enforce message-typed Builder and Parser contracts. |
| 2 | Replace `Agent` with a client coordinator composed from typed messages. |
| 3 | Implement synchronous direct body output, retained fixed sends, and late header patching in H1/H2/H3. |
| 4 | Migrate the client and `zhttp.cc` PUT path. |
| 5 | Migrate `Service`, request parsing, and server response construction. |
| 6 | Collapse transmission to one synchronous Tx turn and delete all old producer machinery. |
| 7 | Validate protocol behavior, failure semantics, telemetry, and repository-wide removal. |

### Build and test execution policy

The only build configuration used for this refactor is clang debug.  Reuse an
existing clang/debug build tree when available; if configuration is required,
use `./z.config -d -L <prefix>`.  Do not spend time on gcc, release, or other
configuration matrices during this work.

Minimize rebuilding.  Batch the edits for a phase and use the smallest
affected incremental build and functional unit-test targets at stable
checkpoints.  Do not rebuild merely because a phase boundary was reached, do
not perform clean or repository-wide rebuilds when an affected-target build
is sufficient, and do not build or test when a phase deliberately completes
with the code in an interim state which is known not to compile or run.  Note
the deferral and resume validation at the next stable buildable checkpoint.

Interim validation, including validation within phase 7 before its final gate,
is limited to functional unit tests using deterministic local or in-process
harnesses; it does not include the full external-peer/protocol interoperability
matrix.  Do not run the full interoperability suite during phases 1 through 6
or while phase 7 implementation, audit, or repair is still in progress.  Full
interoperability testing is the final action of phase 7, after the clang/debug
build, functional unit tests, repository-wide removal checks, and final
`GUIDELINES.md` audit and repairs have completed.

## Phase 1 — Restore message-typed contracts

### Message-typed Builders

The application-facing Builder must be tied to exactly one message type.  A
client composes a request Builder with a response Parser; a server composes a
request Parser with a response Builder.  Request/response direction belongs in
those types, not in otherwise generic callback names:

```text
client: RequestBuilder::body()  + ResponseParser::body()
server: RequestParser::body()   + ResponseBuilder::body()
```

Both transmit halves are first-class in this proposal.  Everything specified
for direct client request construction—synchronous typed body emission,
streaming versus retained send, body-dependent header patching, concrete
`ZiTxStream` layering, and in-place framing—also applies to server response
construction.  The message-specific differences are the start line, header
schema, and the standard response-status/method rules which suppress an entity
body.

Consequently, the body-producing Builder callback proposed below is `body()`,
not `requestBody()` or `responseBody()`.  The synchronous post-body callback
is `bodyHdrs()`.  On the receive side, the typed Parser sink likewise calls
`body()`; `responseBody()` is not a second kind of Builder callback.

Protocol implementations may share framing machinery, but their public
facades must preserve the message role.  `RequestBuilder` exposes request-line
construction (`operation`, `host`, optional `protocol`, both header overloads,
and the policy-selected `body`/`bodyHdrs` operations); `ResponseBuilder`
exposes status-line construction (`status`, `reason`, both header overloads,
and the policy-selected `body`/`bodyHdrs` operations).  `Body::None` exposes
neither body operation; body-bearing policies expose `body`, and only fixed
policies expose `bodyHdrs`.  Neither public type exposes the other message's
start-line operation.

At the protocol layer, replace the paired aliases of one unrestricted type
with distinct facades over a common private implementation, for example:

```c++
template <typename Impl, typename Headers, typename Trailers,
  typename BodyPolicy>
class RequestBuilder;

template <typename Impl, typename Headers, typename Trailers,
  typename BodyPolicy>
class ResponseBuilder;
```

`MessageTraits` must select one of these types explicitly.  A common
`Builder_` base may implement header iteration, body layering, framing, and
finish logic, deriving `HasBody` and `Streaming` internally from the policy
tag, but it is not an application-facing type.  This makes it
impossible to call `response(tx)` on a request Builder or `request(tx)` on a
response Builder and prevents the old nominal-alias problem from recurring.

There is still one final concrete transmit Builder object per message.  A
factory may first emit the application value-provider object by value; the
selected protocol facade immediately moves that value into the final concrete
Builder before transmission begins.  “Application Builder” below means that
message-specific value-provider subobject; “protocol facade” means its
H1/H2/H3 wrapper/base.  They are not independently scheduled builders, and
calls between them are synchronous and inlineable.

### Restored Builder member names

Restore the complete succinct application value-provider surface, not only
`header()` and `body()`:

| Request Builder | Response Builder | Both |
| --- | --- | --- |
| `operation(L &&)` | `status()` | `header<Key>(L &&)` |
| `host(L &&)` | `reason(L &&)` | `header(L &&)` |
| `protocol(L &&)` for extended CONNECT |  | `body(Emit &&)` for body-bearing policies |
|  |  | `bodyHdrs(L &&)` for fixed policies |

These names need no `request` or `response` prefix because the containing type
already supplies the message role.  `reason()` is used only by H1; H2/H3 ignore
the reason phrase.  `protocol()` is optional and meaningful only for an
extended CONNECT request.  `operation(l)` calls
`l(method, path, query)`; `host(l)`, `protocol(l)`, and `reason(l)` call
`l(value)`; `status()` returns the status code.  The two `header` callback
shapes are specified below.

Do not restore `contentLength()` as a pre-body value provider; the
`content-length` header is provisioned through `header<Key>()` and patched
through `bodyHdrs()`.  The old protocol-driving members
`request(stream)`, `response(stream)`, `body(stream)`, `finish(stream)`, and
`reset()`, along with `tunnelResponse()` and H3/QPACK hooks, are private
facade/transport machinery rather than application callbacks.  The facade
derives tunnel response semantics from the snapshotted request method and
response status.  Delete the unused old default `bool request()` hook rather
than exposing it on the typed application Builder.  The original
`trailers(stream)` operation was also protocol machinery; values declared in
the `Trailers` schema use the same two `header` provider overloads at trailer
emission time, so do not introduce direction-prefixed trailer callbacks.

### Historical evolution

The old design expressed this intent, but only nominally.  By `8186cae`, the
public API had `H1ReqBuilder`, `H1RespBuilder`, `H3ReqBuilder`, and
`H3RespBuilder`, and applications derived distinct request or response Builder
types from those names.  Their body operation was simply `body(tx)`.  However,
each request/response pair was an alias of the same underlying protocol
`Builder`, which exposed both `request(tx)` and `response(tx)`.  The role was
therefore conveyed by the alias and application type, but was never enforced
by the type system.  The contemporary server example derived its own
`RespBuilder` from `H1RespBuilder`, called `response(tx)`, obtained
`body(tx)`, and finished the response directly; response construction was not
an incidental client-only facility.

`9500cc5` introduced `ClientMessage` and the combined `Agent` application
contract.  One application object now supplied outbound request fields and
consumed inbound response fields, so the adapter prefixed callbacks such as
`requestOperation`, `requestHeader`, `responseHeader`, and `responseBody` to
disambiguate two message roles on that one object.  Its internal request
Builder and response Parser still had locally named `body` operations, but
their forwarding layer leaked direction into the application API.

The same change introduced the high-level `Service`, internalized its protocol
response Builder, and split response construction across `RespOps` plus
`Workload::body(tx, builder, response)`.  That preserved direct server output
but stopped treating the workload's response as a first-class typed Builder.

`55fa35b` then added resumable outbound body production to that combined
surface: `requestBodyCursor`, `requestBody(..., batch)`, `BodyProduce`, and the
associated state machine.  In the same change, the existing inbound
`responseBody` consumer changed from a span callback to an `Rx` callback.
It also routed server request input through `Workload::requestBody(...)`.
Thus `requestBody` and `responseBody` are not symmetric Builder operations:
the former is a Tx producer added in `55fa35b`; the latter is an Rx consumer
introduced with `ClientMessage`.  Their names reflect role multiplexing in
`Agent`, not an HTTP or transport requirement.

This refactor restores and enforces the intended boundary.  Replace the
nominal aliases with distinct request/response Builder facades (sharing an
internal implementation where useful), and give the application distinct
message-typed Builder and Parser-sink subobjects.  Within each such type the
field is unambiguously `body`.  Do not retain prefixed forwarding shims for
compatibility.

This is an intentional breaking API change.  Delete the old callbacks rather
than detecting or forwarding them:

- request Builder: delete `requestOperation`, `requestHost`,
  `requestProtocol`, `requestHeader`, `requestBody`, and
  `requestBodyHeaders`; use `operation`, `host`, `protocol`, `header`, `body`,
  and `bodyHdrs` on the request Builder, with both original `header`
  overloads;
- response Builder: delete any corresponding `responseStatus`,
  `responseReason`, `responseHeader`, `responseBody`, and
  `responseBodyHeaders` producer names; use `status`, `reason`, both `header`
  overloads, `body`, and `bodyHdrs` on the response Builder;
- request Parser sink: delete `requestOperation`, `requestHeader`, and
  `requestBody` consumer names; use `operation`, `version`, `contentLength`,
  `chunked`, `header`, `body`, and `complete`;
- response Parser sink: delete `responseStatus`, `responseContentLength`,
  `responseChunked`, `responseVersion`, `responseHeader`, `responseBody`, and
  `responseEnd`; use `status`, `version`, `contentLength`, `chunked`, `header`,
  `body`, and `complete`.

Phases 2 through 6 propagate these changes through every dependent
application, test, example, interop tool, and document in the repository.  Do
not add deprecated aliases, SFINAE fallback to old callback names,
compatibility traits, or transitional adapters while doing so.

### Phase 1 acceptance criteria

- Request and response Builders are distinct public facade types; neither can
  invoke the other role's start-line operation.
- Request Builders expose only `operation`, `host`, optional `protocol`, both
  `header` overloads, and their policy-selected `body`/`bodyHdrs` operations;
  response Builders expose only `status`, H1 `reason`, both `header`
  overloads, and their policy-selected `body`/`bodyHdrs` operations.
- The new request and response Parser-sink contracts use local message names,
  including `body` and `complete`, with no direction-prefixed forwarding
  callbacks.
- `MessageTraits` selects the appropriate typed Builder or Parser explicitly;
  any shared `Builder_` remains private protocol machinery.
- At a stable buildable checkpoint, focused compile-time functional unit tests
  reject cross-role operations and compile custom header values without
  coercing them to `ZuCSpan`.
- No compatibility alias, fallback trait, or deprecated direction-prefixed
  callback is introduced.
- Audit every phase 1 change against `GUIDELINES.md` and repair all identified
  API, type-system, naming, Z-idiom, layout, and performance issues before the
  phase is accepted.

## Phase 2 — Eliminate `Agent`

Delete `Agent` entirely as part of this refactor.  Its combined application
callback surface is the source of the direction-prefixed message operations,
and retaining it would preserve the wrong ownership boundary under new names.

This does not mean deleting the transport-neutral client coordinator.  The
current `Agent` also owns substantial non-message behavior which remains
necessary: admission, attempt slots, timeouts, URL routing, discovery,
Alt-Svc, TCP/TLS/QUIC pool selection, H3 fallback, redirects, retries,
cancellation, result accounting, and engine lifecycle.  None of that belongs
in a request Builder or response Parser.

Promote those remaining responsibilities into the public `Zhttp::Client`, as
the client-side counterpart of `Zhttp::Service`.  The existing
`Zhttp::Client<App, Profile>` is only a thin normalized protocol-engine base;
rename it to an internal `ClientEngine<App, Profile>` and update its H1/H2/H3
specializations and `ClientPool` uses.  The new public `Client` owns the three
pools and the private `Attempt` state currently held by `Agent`, but it is
composed with distinct application message types:

```text
Client
  -> application RequestBuilder factory -> RequestBuilder::body(...)
  -> application ResponseParser factory -> ResponseParser::body(rx)
  -> application lifecycle/result hooks
```

The message adapter is parameterized by the request Builder and response
Parser-sink types, not by a monolithic owner containing every message-field
callback.  Rx routing produces an immutable or Tx-owned request-build snapshot
containing the selected URL and source handle; the request Builder is
constructed from that snapshot on Tx and must not read mutable Rx-owned
`Attempt` state.  A response Parser sink persists with the logical request on
Rx.
Client-only hooks such as completion, observation, connection state, and
selection remain on the client application contract because they are
transaction/lifecycle events, not HTTP message fields.  Within the already
client- and request-typed context, rename `requestURL`, `requestReplayable`,
and `requestReproducible` to `url`, `replayable`, and `reproducible`; there is
no reason to preserve direction prefixes there either.

Perform a hard rename and deletion across the public API:

- delete `ZhttpAgent.hh` and remove it from `Makefile.am` and `Zhttp.hh`;
- rename `AgentConfig`, `AgentEvent`, and `AgentEventType` to `ClientConfig`,
  `ClientEvent`, and `ClientEventType`;
- rename `Zhttp.Agent.*` heap identifiers and diagnostics to `Zhttp.Client.*`;
- delete the remaining direction-prefixed client hooks
  `requestURL`, `requestReplayable`, and `requestReproducible` after moving
  their behavior to `url`, `replayable`, and `reproducible` in the typed
  client/request context;
- rename Agent tests and namespaces to Client equivalents while preserving
  their routing, retry, redirect, cancellation, and fallback coverage;
- update `zhttp.cc`, interop programs, and the README to instantiate the
  public `Client` by composition rather than derive an application from
  `Agent`;
- provide no `Agent` type alias, compatibility header, or forwarding API.

Most of `ZhttpAgent.hh` will initially move into the new client coordinator;
that is acceptable.  The architectural deletion is the CRTP mega-interface
that mixes coordination with request production and response consumption.
Subsequent factoring of discovery or retry policy is independent of direct
body construction and should not be forced into the Builder.

### Phase 2 acceptance criteria

- `ZhttpAgent.hh`, `Agent`, `AgentConfig`, `AgentEvent`, and `AgentEventType`
  no longer exist as live API or implementation; public names, heaps,
  diagnostics, tests, and active user documentation consistently use
  `Client`.  Historical and migration text in this plan may retain the old
  names when describing what was removed.
- The public client coordinator composes a request-Builder factory, a
  response-Parser factory, and lifecycle hooks without presenting a combined
  message-field CRTP interface.
- The former thin protocol client is internal under `ClientEngine`, and all
  H1/H2/H3 pools instantiate it through the new coordinator.
- Focused functional unit tests for retry, redirect, cancellation, discovery,
  Alt-Svc, protocol fallback, and result accounting pass with unchanged
  behavioral coverage when this phase ends at a stable buildable checkpoint.
- Request construction on Tx uses an immutable or Tx-owned snapshot and does
  not read mutable Rx-owned attempt state.
- Source and build-manifest searches find no live `Agent` compatibility
  type/header or `requestURL`, `requestReplayable`, and
  `requestReproducible` hooks; historical references in this plan are
  excluded from that check.
- Audit every phase 2 change against `GUIDELINES.md` and repair all identified
  ownership, sharding, lifecycle, naming, Z-idiom, allocation, and performance
  issues before the phase is accepted.

## Phase 3 — Replace resumable production with direct body output

### Problem

The PUT path in `zhttp/test/zhttp.cc` currently serializes a `Record` into
`Req::requestJSON` before the request is submitted.  `requestBody()` later
copies slices of that string into an HTTP body stream:

```text
Record -> requestJSON -> HTTP ZiTxLayer -> transport ZiTxStream -> ZiIOBuf
```

The temporary string exists only to make the body conform to the current
cursor/`batch`/`BodyProduce` contract and to obtain a content length.  It adds
an allocation, retains a second representation of the request, and copies the
complete body before the transport can send its first byte.

That is contrary to the original Builder design.  Builders obtain application
values by passing typed lambdas into application callbacks.  The application
calls those lambdas synchronously with the actual value, allowing the Builder
to encode directly into the concrete output stream without type erasure or an
intermediate representation.  Request bodies should follow the same model.

The server transmit path has the complementary problem.  `Service::Session`
constructs an internal response Builder, calls `builder.response(tx)`, and
then exposes both `tx` and that Builder to
`Workload::body(tx, builder, response)`.  Although this path can write
directly, the application does not own a response-typed Builder contract, and
it cannot use the same generic post-body header finalization mechanism.
Server response construction must move to the same typed Builder inversion of
control rather than remain a special callback taking transport machinery as
arguments.

`ZfJSON::save(S &, const O &)` is already suitable for this.  It emits JSON by
calling `operator <<` on `S`; it does not require `S` to be a string.  The body
stream produced by each HTTP Builder is already a concrete `ZiTxLayer` over
the transport's `ZiTxStream`:

```text
Record
  -> ZfJSON::save(body, record)
  -> H1 BodyStream/ChunkedStream, H2 DataStream, or H3 DataStream
  -> native TCP/TLS/QUIC ZiTxStream
  -> pooled ZiIOBufs
```

`ZiTxStream` rolls over fixed-capacity buffers as they fill.  A streamed
client request or server response submits them immediately.  A non-streamed
message retains the resulting buffer chain until its header values have been
finalized, then releases the complete chain to the transport.  JSON
serialization is incremental at the I/O-buffer level in both cases even though
`ZfJSON::save()` is one synchronous call.

### Headroom, tailroom, and in-place framing

The body stream must remain the real layered `ZiTxStream`, not a façade over a
temporary application buffer.  `ZiTxLayer` composes its headroom and tailroom
with the reservations of the layer below it before allocating a `ZiIOBuf`.
The serializer consequently writes entity bytes directly into their final
payload position, leaving space around them for every framing layer.

For example:

- H1 `ChunkedStream` reserves the chunk-size line in headroom and the trailing
  CRLF in tailroom;
- H2 `DataStream` reserves the nine-byte DATA frame header in headroom;
- H3 `DataStream` reserves the DATA type/length varints in headroom;
- the underlying TLS or transport stream retains its own record or packet
  headroom and tailroom.

When a payload buffer is flushed, the HTTP layer rewinds into its reserved
headroom and/or extends into its reserved tailroom, then hands or queues the
same `ZiIOBuf` for the lower layer.  Lower layers do the same in their own
reserved space.  Entity bytes are not shifted or copied to prepend and append
protocol framing:

```text
| lower headroom | HTTP headroom | JSON entity bytes | HTTP tailroom | lower tailroom |
```

This storage contract is central to the proposal.  Replacing the concrete body
stream with an abstract sink that accumulates bytes, even temporarily, would
discard the benefit of the Builder and `ZiTxLayer` designs.

### Contract being removed

The present callback is designed for a resumable byte source:

```c++
template <typename Tx>
int requestBody(
  Request &, Cursor &, Tx &, unsigned batch);
```

It asks the application to stop at an arbitrary scheduler budget and return
`BodyProduce::More`, then reconstructs a body stream and calls it again later.
That is appropriate for something such as a file cursor or an application
queue that has an intrinsic resume point.  It is not appropriate for a
synchronous formatter such as `ZfJSON::save()`: the formatter has no exposed
continuation state and cannot stop after an arbitrary number of output bytes.

Pre-serializing JSON makes the formatter appear resumable by turning it into a
span cursor, but that defeats direct Builder output.  Calling the formatter
again and discarding an already-emitted prefix would be worse: it makes the
work quadratic and risks inconsistent output if the source changes.

Phase 3 replaces this resumable-production abstraction in the core transmit
contract; no new Builder path may call it.  Phases 4 and 5 migrate its client
and server consumers, and phase 6 deletes the now-unreferenced definitions and
residue.  Client request and server response construction are synchronous
Builder operations on their Tx shards.  Incremental output is provided by
`ZiTxStream` buffer rollover, not by repeatedly entering the application with
a reconstructed body layer.  From the application's point of view the
remaining body-policy dimension is only whether the body is streamed, with
optional variants.  Retained whole-message commit is the internal consequence
of selecting a non-streamed body.

### Application contract

Header production retains the original Builder API exactly.  The application
Builder implements these two overloads:

```c++
template <typename Key, typename L>
void header(L &&l);       // invokes l(value)

template <typename L>
void header(L &&l);       // invokes l(key, value)
```

The typed overload invokes `l(value)` zero or one times for `Key`; not invoking
it omits that field.  The runtime-key overload invokes `l(key, value)` for each
additional field it supplies.  Values retain their concrete types in both
forms.  The protocol validates the typed-provider cardinality and fails
message construction if it invokes `l(value)` more than once.  There is no
`headerValue()` helper or replacement API: each message-specific Builder
implements `header()` and calls the protocol lambda directly, as in the
original design.

Each header provider is evaluated exactly once for one message construction.
Fixed mode must not make a discovery pass for `HeaderPad` and then call the
application provider again while rendering or encoding.  Its one pass stores
ordinary H2/H3 logical values, renders ordinary H1 fields, and records every
placeholder slot; `bodyHdrs()` mutates those captured slots.  This matters for
providers which snapshot clocks, counters, nonces, signatures, or other
stateful values, and it keeps typed-provider cardinality meaningful across the
whole message rather than per internal pass.  Optional fixed mode performs
that one pass only after the emitter establishes that a body is present; its
absent branch performs the ordinary bodyless header pass instead.

The `body` callback retains the Builder inversion of control.  For every
body-bearing policy, the Builder passes an emitter to the application; the
application calls it with a statically typed writer, and the Builder invokes
that writer with the actual concrete body stream:

```c++
template <typename Emit>
void body(Emit &&emit) {
  emit([this](auto &body) {
    ZfJSON::save(body, request.putRecord);
  });
}
```

Both lambdas are templates and synchronous.  They should inline completely;
there is no `ZmFn`, virtual dispatch, allocation, retained callback, or erased
stream type.  The writer may also emit raw spans, formatted values, or a loop
of file chunks directly to `body`.

A writer may return `void` for success or `bool` when it needs to report an
application-level failure.  The protocol uses an inline, statically dispatched
`invokeBodyWriter(write, body)` helper which maps a `void` return to `true`,
propagates a `bool` return, and rejects every other return type at compile time.
Implement that dispatch with `if constexpr` and established detector/SFINAE
traits, not type erasure, `requires`, or a retained callback.

For a required streaming body, the protocol-neutral message adapter performs
the equivalent of:

```c++
auto tx = link.transmit(builder);
if (!builder.request(tx)) return failed;

auto body = builder.body(tx);
bool emitted = false;
bool duplicate = false;
bool writerOK = false;
builder.body([&](auto &&write) {
  if (emitted) { duplicate = true; return; }
  emitted = true;
  writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
});

if (!emitted || duplicate || !writerOK || !body.valid())
  return abortStreamed(body); // close H1 or reset H2/H3 if visible
body.flush();
if (!body.valid()) return abortStreamed(body);
builder.finish(tx);
link.finish();
```

This pseudocode is specifically the required streaming path.  A non-streamed
path substitutes a retained-send stream and performs `bodyHdrs()` before
commit, while either optional policy handles zero emissions as described
below.

For a server response the sequence is identical except that the typed facade
starts with `builder.response(tx)`.  The application response Builder supplies
`status`, `reason`, response `header` values, `body`, and, when non-streamed,
`bodyHdrs`.  `Service` owns transport selection and framing but does not
pass its protocol Builder or raw `tx` to the workload.

The message-specific Builder's `body()` is a thin callback in the same style as
`operation()`, `host()`, and `header()`:

```c++
template <typename L>
void body(L &&l) {
  appBuilder->body(ZuFwd<L>(l));
}
```

Here `appBuilder` is already a request Builder in a client or a response
Builder in a server.  It is not the combined Agent object.  The analogous
receive path calls `body(rx)` on a response Parser sink in a client or a
request Parser sink in a server.

The emitter cardinality is internal contract validation.  A required-body
Builder must invoke it exactly once.  An optional-body Builder may invoke it
zero or one times; zero means that this message has no body.  A client body is
built once per attempt and a server body once per response.  Successful
`void` completion or a `true` return from the writer is the source boundary;
`false` is an application-level construction failure.  Do not overload zero
output or an exception with failure semantics.

The concrete body stream exposes `produced()`, including bytes still in its
current unflushed buffer.  A fixed-body writer may snapshot that value into
its application Builder state after formatting; the retained-body limit
guarantees it fits `unsigned`.  The protocol keeps its own authoritative
counter and validates the patched `content-length` against it after
`bodyHdrs()`.

For an optional streaming body, the protocol Builder calls the application's
`body(emit)` before committing headers.  The first and only `emit(write)` call
causes it to finalize streaming headers, create the concrete body layer, and
invoke `write(body)` immediately.  If `body(emit)` returns without emission,
it sends the bodyless header form.  This removes `requestHasBody()` without
buffering a streamed body or retaining either lambda.

`Body::OptionalFixed` determines presence through the same emitter, not by
consulting a second flag.  On its first and only `emit(write)`, the protocol
Builder collects the retained H1 headers or uncompressed H2/H3 fields,
provisions the `HeaderPad` values, creates the retained body layer, and invokes
the writer.  If `body(emit)` returns without emission, it constructs the
bodyless header form and suppresses every `HeaderPad`; it does not call
`bodyHdrs()`.  Emitting a writer which produces zero bytes is distinct from
not emitting a writer: it is a present, zero-length body and is finalized with
`content-length` equal to zero.

For a non-streamed body, the application additionally supplies a post-body
header patcher named `bodyHdrs`.  The Builder invokes it only after the writer
has completed and the body layer has been flushed, while every produced
message buffer is still retained.  `bodyHdrs` receives only the protocol
lambda; values needed by its patchers are Builder state captured while the
body writer ran:

```c++
using ContentLength = ZuStringT<"content-length">;
using ContentSHA256 = ZuStringT<"x-content-sha256">;
using ContentType = ZuStringT<"content-type">;

unsigned contentLength = 0;
SHA256Digest contentSHA256;

template <typename Key, typename L>
void header(L &&l) {
  if constexpr (ZuIsSame<Key, ContentLength>{})
    l(HeaderPad{10, '0'}); // decimal width of UINT_MAX
  else if constexpr (ZuIsSame<Key, ContentSHA256>{})
    l(HeaderPad{44}); // Base64 SHA-256 width
  else if constexpr (ZuIsSame<Key, ContentType>{})
    l(ZuStringT<"application/json">{}());
}

template <typename L>
void header(L &&l) {
  if (request.traceID)
    l(ZuStringT<"x-trace-id">{}(), request.traceID);
}

template <typename Emit>
void body(Emit &&emit) {
  emit([this](auto &body) {
    SHA256TxStream out{body}; // typed forwarding layer; no body buffering
    ZfJSON::save(out, request.putRecord);
    contentLength = out.produced();
    contentSHA256 = out.final();
  });
}

template <typename L>
void bodyHdrs(L &&l) {
  l.template operator()<ContentLength>(
    [contentLength = this->contentLength](ZuSpan<uint8_t> span) {
      ZuStream<uint8_t> s = span;
      s << ZuBoxed(contentLength).fmt<ZuFmt::Right<10>>();
    });
  l.template operator()<ContentSHA256>(
    [digest = contentSHA256](ZuSpan<uint8_t> span) {
      encodeBase64(span, digest);
    });
}
```

`HeaderPad` is a streamable placeholder value: its `operator <<` emits the
requested number of padding bytes.  With no explicit byte, it emits a
non-field-value poison byte so an incomplete custom patch is detectable before
commit; `HeaderPad{10, '0'}` explicitly provisions the valid zero-filled
decimal representation.  Placeholder bytes exist only in retained storage.
`ZuFmt::Right<10>` rewrites the complete span with the zero-padded decimal
value; `encodeBase64()` likewise fills its fixed-width 44-byte span.  The
protocol validates the completed spans after each patcher returns.

`SHA256TxStream` above is schematic: it is a statically typed forwarding layer
which updates the digest and byte count while writing directly to the concrete
HTTP body stream.  It does not retain or copy the complete body.  Applications
which need only `content-length` can record `body.produced()` after formatting
without adding a digest layer.  The patcher and mutable value span are
callback-scoped and must not escape the protocol lambda.

After provisioning the header values and constructing the retained body
layer, the required non-streamed adapter performs the equivalent of:

```c++
bool emitted = false;
bool duplicate = false;
bool writerOK = false;
builder.body([&](auto &&write) {
  if (emitted) { duplicate = true; return; }
  emitted = true;
  writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
});
if (!emitted || duplicate || !writerOK || !body.valid()) return failed;
body.flush();
if (!body.valid()) return failed;
bool patchOK = true;
builder.bodyHdrs([&]<typename Key, typename Patcher>(Patcher &&patcher) {
  auto value = headerValues.template mutableValue<Key>();
  if (!value) {
    patchOK = false;
    return;
  }
  ZuFwd<Patcher>(patcher)(value);
  if (!headerValues.template valid<Key>()) {
    patchOK = false;
    return;
  }
  headerValues.template patched<Key>();
});
if (!patchOK) return failed;
```

`headerValues.mutableValue<Key>()` abstracts the protocol-specific storage: it
returns the retained H1 field-value subspan or the H2/H3 uncompressed logical
value span.  `Key` must be declared as a runtime-valued header in `Headers`,
and its initial `header<Key>()` callback must have supplied `HeaderPad`.

`bodyHdrs()` is the additional application call and, like
the existing Builder callbacks, only forwards a typed lambda:

```c++
template <typename L>
void bodyHdrs(L &&l) {
  appBuilder->bodyHdrs(ZuFwd<L>(l));
}
```

This call is strictly synchronous.  `ClientMessage` invokes it directly after
`body.flush()` in the same client-send call stack; `Service` does the same in
the server-response call stack.  After it returns, each freezes and validates
the header state, encodes pending H2/H3 headers, and commits the
already-rendered H1 or newly encoded H2/H3 header buffers.  The call must
remain on that stack; it must not cross a `txRun`/`rxRun`, scheduler,
continuation, or queue boundary.  The application must not retain `l`; every
`l.template operator()<Key>(patcher)` call obtains, patches, and validates its
fixed-width mutable `ZuSpan<uint8_t>` before `bodyHdrs()` returns.

For `Streaming == false` with a body present, the Builder calls
`bodyHdrs()` exactly once, even when `content-length` is the only patched
header.  `Body::None` has no `body` or `bodyHdrs` callback.  An optional
body always calls `body(emit)` to determine presence, but an absent body does
not invoke `emit` or `bodyHdrs`.  For `Streaming == true`, the post-body
branch is not instantiated and `bodyHdrs` is never called.  Streamed
headers must be final before body emission and cannot be derived by inspecting
the emitted body.

The application may call `l.template operator()<Key>(patcher)` successively
for any body-dependent header in `Headers`.  The protocol independently
validates the patched `content-length` against the body layer's `produced()`
counter rather than trusting application state.  Custom keys can cover
digests, signatures, checksums, or any other fixed-width value derived while
the body writer ran.

Body policies no longer take a cursor or producer type:

```c++
namespace Body {
  struct None;
  struct Fixed;
  struct OptionalFixed;
  struct Stream;
  struct OptionalStream;
}
```

When HTTP method/status semantics permit an entity, the policy contract is:

| Policy | `body(emit)` cardinality | `bodyHdrs()` | Transmit behavior |
| --- | --- | --- | --- |
| `None` | not present | never | bodyless headers only |
| `Fixed` | exactly one | exactly once after the writer | retain, patch, then commit |
| `OptionalFixed` | zero or one | once only when emitted | absent is bodyless; present retains and patches |
| `Stream` | exactly one | never | send as buffers fill; H1 chunked, H2/H3 DATA |
| `OptionalStream` | zero or one | never | absent is bodyless; present streams |

Response semantics which prohibit an entity override this table: the protocol
Builder invokes neither `body()` nor `bodyHdrs()`, suppresses
body-dependent placeholders, and emits only the permitted header form.

`Body::None` has no Builder `body` callback.  The other policies all use the
same synchronous Builder construction model.  `Body::Stream` uses the
single-writer form and sends buffers as they fill.  `Body::Fixed` has
`Streaming == false`; it uses the writer plus post-body-header form and
retains all buffers until the late headers are complete.  Optional variants
select body presence through the emitter for a particular message;
`bodyHdrs()` follows only a present fixed body.  A file body can read a
chunk and write it to `body` repeatedly inside the writer; every write still
rolls through pooled transport buffers without a complete file-sized
temporary.

The policy belongs to the concrete application request or response Builder,
not to the whole `Client` or `Service`.  This lets one client request type and
one server response type select different modes, and lets the server's typed
response emitter choose, for example, a `Body::Stream` file Builder or a
`Body::Fixed` JSON Builder for each response.

The transmit `Headers` schema belongs to that Builder for the same reason.  A
streamed file response omits `content-length`, while a fixed JSON response
declares `content-length` and digest slots.  Receive header schemas belong to
the corresponding typed Parser sinks, together with their body limits.  Remove
redundant request/response header-schema, body-limit, and body-policy template
parameters from the top-level `Client` and `Service` where the nested message
types now provide them.

### Streamable header values

Do not coerce a Builder-provided header value to `ZuCSpan`.  The old protocol
lambda erased types before their custom printing behavior could run:

```c++
ZuCSpan value{ZuFwd<V>(v)}; // delete
```

For H1, the ordinary immediate-send path should preserve the value type and
stream it directly:

```c++
impl()->template header<Key>([&stream]<typename V>(V &&v) {
  stream << Key{}() << ": " << ZuFwd<V>(v) << "\r\n";
});

impl()->header([&stream]<typename K, typename V>(K &&k, V &&v) {
  stream << ZuFwd<K>(k) << ": " << ZuFwd<V>(v) << "\r\n";
});
```

The application omits a typed header by not invoking the supplied lambda; an
empty span is no longer the generic omission sentinel.  The runtime-key form
likewise preserves both argument types and streams the value without coercion.

H2 and H3 cannot necessarily stream an arbitrary value straight into HPACK or
QPACK because field representations are length-prefixed and may be Huffman
encoded or indexed.  They first stream `v`, preserving custom `operator <<` or
`ZuPrint` behavior, into a bounded uncompressed logical-value slot.  They then
encode that final span.  This is protocol-required header formatting storage,
not a contiguous message-body copy.  H1 can retain its direct-output fast path:
for a non-streamed message it writes into retained final header buffers and
records the uncompressed value subspan of each `HeaderPad`.  Thus all protocols
present the same mutable uncompressed-span API to `bodyHdrs()`, although H1
can patch final field bytes while H2/H3 must defer compression.

For `header(L &&)`, H2/H3 likewise format the supplied runtime key into
uncompressed name storage, validate the resulting HTTP field name, and format
the value without a `ZuCSpan` coercion.  Typed `header<Key>(L &&)` uses
`Key{}()` directly for the name while preserving the supplied value type.

The H2/H3 slot writer enforces the existing maximum field-value size and fails
on overflow; its storage remains alive until HEADERS encoding completes.  H1
must likewise guarantee that each `HeaderPad` value is contiguous: if the
current header buffer cannot hold all `N` value bytes, roll to another retained
header buffer before writing the placeholder.  A placeholder may never
straddle buffers because `bodyHdrs()` exposes it as one `ZuSpan<uint8_t>`.

### Body-dependent header slots

`Headers` is the compile-time schema for both ordinary and body-dependent
headers.  For `Body::Fixed` and `Body::OptionalFixed`,
`ZuStringT<"content-length">` must be present in the header-key slice; enforce
this with a `static_assert`.  The optional policy suppresses that field when
the body is absent.  Custom digest or signature headers must likewise be
declared in `Headers` before their slots can be updated by `bodyHdrs`.

For a required non-streamed body, the Builder invokes the normal typed
`header<Key>(l)` callbacks before body construction.  An optional non-streamed
Builder performs that pass inside its first `emit(write)`, after presence is
known.  H1 streams ordinary fields directly into retained final header
buffers; H2/H3 stream them into uncompressed logical field slots.  A
body-dependent callback supplies `HeaderPad{N}`; streaming that value writes
`N` padding bytes and records that exact value span for later mutation.
`content-length` is not a hidden exception: its callback supplies a 10-byte
zero pad, the decimal width of `UINT_MAX` (`4294967295`).  Base64 SHA-256
supplies 44 bytes.  `HeaderPad` marks the span as requiring a successful late
patch.

This deliberately relies on leading zeroes being valid in `content-length`.
RFC 9110 defines its grammar as `1*DIGIT` and states that any value greater
than or equal to zero is valid; it does not require a canonical shortest
decimal representation.  The final H1 field can therefore remain 10 bytes,
for example `content-length: 0000000200`, without shifting the
already-built header block.  The same textual value is valid before H2/H3
compression, although those protocols still encode it only after patching.

A patchable header must be a runtime-valued (`void`) entry in `ZhttpHeaders`.
Compile-time constant entries are immutable and cannot be named by
`bodyHdrs`.  `HeaderPad` is accepted only for a present fixed policy;
`Body::None` and both streaming policies reject it.  An absent
`Body::OptionalFixed` suppresses its body-dependent slots instead of emitting
padding or a zero `content-length`.  For `Body::None`, including a HEAD
response which describes the size of the corresponding representation,
`content-length` is an ordinary application value and is not checked against
an emitted body.  `Body::Stream` rejects `content-length` for every HTTP
version under this application contract because streamed headers may not
depend on the body.  H1 additionally maps that mode to chunked transfer
encoding; H2/H3 use their end-of-stream signal to delimit DATA.

The runtime-key `header(L &&)` overload cannot be used to bypass these policy
checks.  After formatting and validating a runtime field name, compare it
case-insensitively with declared body-dependent keys and protocol-owned framing
fields.  Reject a runtime `content-length`, a runtime duplicate of any
provisioned `HeaderPad` key, and application-supplied H1
`transfer-encoding`; fixed body-dependent fields must come from their typed
`header<Key>()` slots, and streaming bodies cannot acquire a hidden length or
framing override through the runtime-key path.

The `bodyHdrs` protocol lambda takes the header key as a template argument and
the patcher as its function argument.  The application calls it successively:

```c++
l.template operator()<ZuStringT<"content-length">>(
  [contentLength](ZuSpan<uint8_t> span) {
    ZuStream<uint8_t> s = span;
    s << ZuBoxed(contentLength).fmt<ZuFmt::Right<10>>();
  });
l.template operator()<ZuStringT<"x-content-sha256">>(
  [digest](ZuSpan<uint8_t> span) { encodeBase64(span, digest); });
```

For every call, the protocol resolves `Key` in the alternating `Headers` list
at compile time and passes the patcher a fresh mutable `ZuSpan<uint8_t>`
covering the complete provisioned value.  The patcher overwrites that
fixed-width uncompressed value in place; it cannot resize or repoint the
Builder's slot.
The application may successively select different keys, but each provisioned
key must be selected and patched exactly once; a duplicate selection is a
construction failure.  A variable-size derived value must define a
fixed-width wire representation when it provisions the placeholder; decimal
zero padding and fixed-size Base64/hex encodings are the expected cases.  The
patcher has no
failure return; it must not overflow the span and must leave a complete valid
field value.  The protocol validates the observable postconditions, including
remaining poison bytes and key-specific rules.

An undeclared key or compile-time constant header is a compile-time error.
Before commit, reject a non-provisioned slot, a remaining poison byte, an
unpatched `HeaderPad`, a missing `content-length`, a non-decimal
`content-length`, or a final decimal value unequal to `body.produced()`.  No
patch may occur after H2/H3 header encoding or message commit.

Do not patch already-compressed H2/H3 wire bytes.  A placeholder and its final
value can choose different HPACK/QPACK representations, Huffman lengths, or
dynamic-table effects.  For a non-streamed message, the initial header pass
must not update HPACK/QPACK state at all: every header, including ordinary
ones, remains an uncompressed logical name/value field until all required
body-dependent slots have been patched and validated.  Final encoding may
plan table changes and produce retained header or QPACK encoder-stream bytes,
but it must not publish compression-state changes independently of the message
commit.  Commit the planned HPACK/QPACK state with the retained wire buffers;
on construction, patch, validation, or encoding failure, discard both the
buffers and the planned state so encoder and peer state cannot diverge.  The
H3 implementation may instead encode a retained field section entirely with
literal/static representations when it cannot stage dynamic QPACK effects;
that fallback emits no encoder-stream bytes and mutates no QPACK state, while
still encoding only the final patched values.  The generic sequence is
therefore:

1. render retained H1 fields or populate uncompressed H2/H3 logical fields,
   recording every placeholder value span;
2. write and frame the body into retained pooled buffers;
3. invoke `bodyHdrs` to overwrite the uncompressed value spans;
4. leave the patched H1 header block in place, or HPACK/QPACK-encode H2/H3
   fields from the final values into retained buffers while staging any table
   changes;
5. place header buffers before retained body buffers;
6. commit the complete ordered buffer chain and staged compression state as
   one protocol operation.

Here the retained commit set is the ordered message-stream chain plus any
associated compression-control buffers and staged state.  H1 and H2 can commit
one physical message chain.  H3 may also enqueue QPACK encoder-stream buffers;
one protocol commit does not claim cross-stream wire atomicity, but it makes
the complete set eligible together under QPACK's dependency and ordering
rules.  Later references to committing the complete chain include this H3
commit set.

For non-streamed H1, fixed-width patching means no shifting or second rendering
of an already-built field line.  The zeroes remaining before a decimal
`content-length` are part of its valid final value, not whitespace.  For
H2/H3, HPACK/QPACK planning and the eventual table commit see only the fully
patched final values; the initial placeholder contents never enter compression
state or the wire.

The retained-send stream has the same concrete `ZiTxStream`/`ZiTxLayer`
interface and the same accumulated head/tail room.  Its `sendBuf_()` appends
the pooled buffer to a message-local chain instead of releasing it to the
native transport.  Commit releases that chain in protocol order.  "One go"
means one Tx commit making the complete message eligible for transport; TCP
segmentation, TLS records, QUIC packets, congestion control, and pacing remain
lower-layer concerns.

Both `ClientConfig` and `ServiceConfig` must impose a workload-aware maximum
retained-message size, including headers and framing, for non-streamed sends.
Exceeding it fails construction and discards the retained chain before any
message byte is visible to the peer.  The body layer additionally enforces an
entity-byte limit.  Its effective limit is the lesser of the configured body
allowance (or remaining retained-message allowance) and `UINT_MAX`; fail the
writer before `body.produced()` can cross it.  Consequently the fixed-width
decimal `content-length` slot needs only 10 bytes.

### Framing

For `Body::Stream`:

- H1 uses chunked transfer encoding;
- H2 emits DATA and ends the stream;
- H3 emits DATA and finishes the QUIC stream.

The Builder owns all three mappings.  The application writes entity bytes
only and calls no framing API.  Streaming buffers may be released as they fill
and cannot have body-dependent headers or a `bodyHdrs` callback.

For H1 this mapping is unconditional: `Body::Stream` always emits
`transfer-encoding: chunked`, including the terminating zero-length chunk.
There is no immediate-send, non-chunked message body with an unknown length.

For H1, a request or response with an entity body which is not chunked uses
`Body::Fixed`, or the present branch of `Body::OptionalFixed`, because
`content-length` cannot be finalized until the direct body writer completes.
Its retained body layer does not require a length at construction: it counts
bytes while retaining buffers, `bodyHdrs` fills `content-length`,
and commit orders the already-retained H1 header block ahead of those buffers.

H2 and H3 do not inherently defer a finite body.  Their native end-of-stream
signal delimits DATA and `content-length` is optional, so `Body::Stream` can
send buffers immediately.  When the application selects fixed mode
(`Body::Fixed` or a present `Body::OptionalFixed`), Zhttp nevertheless retains
the complete H2/H3 message and calls `bodyHdrs`, because that
is the application meaning of `Streaming == false`.
DATA buffers may be framed while the body is written, but HEADERS are encoded
from final slot values and ordered before DATA only at commit.  Retention here
is a Builder/storage consequence of the application contract, not an H2/H3
protocol requirement.

### Phase 3 acceptance criteria

- `Body::None`, `Body::Fixed`, `Body::OptionalFixed`, `Body::Stream`, and
  `Body::OptionalStream` are non-cursor policy tags with the documented
  emitter cardinality and `bodyHdrs` behavior.
- Both client request and server response writers receive the concrete layered
  `ZiTxStream`; a body larger than one native buffer rolls through pooled
  `ZiIOBuf`s without a complete contiguous body allocation or entity-byte
  copy.
- Present streaming sends release buffers as they fill, use chunked H1 or DATA
  plus the native H2/H3 final boundary, and never instantiate or call
  `bodyHdrs`; an absent `Body::OptionalStream` sends the bodyless header form
  and no body framing.
- Fixed sends retain the complete ordered buffer chain; no byte is released
  before synchronous `bodyHdrs` patching, validation, and final header
  encoding succeed.
- H1 reserves one contiguous mutable value span for every `HeaderPad` and
  patches it in place; H2/H3 patch uncompressed logical values before any
  HPACK/QPACK planning or encoding, and publish staged table changes only with
  the retained message commit.
- Every typed and runtime header provider is evaluated once per message;
  provisioning, rendering, patching, and encoding reuse that captured state
  and never perform a second application header pass.
- A fixed body cannot exceed `UINT_MAX`; its required 10-byte
  `content-length` is decimal, may contain leading zeroes, and exactly matches
  the protocol body's authoritative produced count.
- An undeclared or compile-time constant late-header key fails at compile time;
  invalid, missing, unpatched, poison-containing, or length-mismatched runtime
  values fail before commit.
- HEAD, 1xx, 204, 304, and successful CONNECT behavior follows the documented
  body and `content-length` rules for every protocol.
- Audit every phase 3 change against `GUIDELINES.md` and repair all identified
  stream layering, buffer ownership, allocation, copy, framing, Z-idiom,
  layout, and performance issues before the phase is accepted.

## Phase 4 — Migrate the client and `zhttp.cc`

Change the test client as follows:

1. Remove `Req::requestJSON`.
2. In `initReq()`, construct only `putRecord`; do not call `ZfJSON::save()`.
3. Add `content-length` to `RequestHeaders`, remove the body-policy parameter
   from the client coordinator, and declare the request Builder's policy as
   non-template `Body::OptionalFixed`.
4. Complete the typed split established in phases 1 and 2 for the new body
   contract.  Request construction methods such as `operation`, `host`,
   `protocol`, both `header` overloads, `body`, and `bodyHdrs` belong to the
   request Builder.  Response consumption methods such as `status`, `header`,
   and `body` belong to the response Parser sink.  The enclosing Client may
   still coordinate retries and completion, but it must not rename message
   fields to multiplex the two typed interfaces.
5. Remove `requestContentLength()` for the PUT body,
   `requestHasBody()`, `requestBodyCursor()`, and the cursor/`batch` producer.
6. Implement `body` directly on the request Builder and finalize
   `content-length` in its additional `bodyHdrs` callback:

   ```c++
   struct RequestBuilder {
     using BodyPolicy = Zhttp::Body::OptionalFixed;
     using Headers = RequestHeaders;
     using ContentLength = ZuStringT<"content-length">;
     using ContentType = ZuStringT<"content-type">;

     RequestPlan request; // immutable, Tx-owned attempt snapshot
     unsigned contentLength = 0;

     template <typename Key, typename L>
     void header(L &&l) const {
       if constexpr (ZuIsSame<Key, ContentLength>{})
         l(Zhttp::HeaderPad{10, '0'});
       else if constexpr (ZuIsSame<Key, ContentType>{})
         l(ZuStringT<"application/json">{}());
     }

     template <typename L>
     void header(L &&) const { } // no runtime-key fields in this example

     template <typename Emit>
     void body(Emit &&emit) {
       if (!request.put) return;
       emit([this](auto &body) {
         ZfJSON::save(body, request.putRecord);
         contentLength = body.produced();
       });
     }

     template <typename L>
     void bodyHdrs(L &&l) const {
       l.template operator()<ContentLength>(
         [contentLength = this->contentLength](ZuSpan<uint8_t> span) {
           ZuStream<uint8_t> s = span;
           s << ZuBoxed(contentLength).fmt<ZuFmt::Right<10>>();
         });
     }
   };
   ```

7. Move the existing `responseBody(State &, Rx &)` implementation to
   `ResponseParser::body(Rx &)`.  This is a rename and ownership correction,
   not a change to receive streaming.
8. Keep request replayability and reproducibility true.  Each attempt
   constructs a fresh `RequestBuilder` from the immutable request plan and
   deterministically serializes its stable `putRecord` from the beginning.
9. Remove the client `--body-tx-batch` option.
10. Validate request telemetry against the echoed response length and the
   decoded `Record`, rather than against a retained request string.

The server currently retains and reparses the request body.  That is a
separate receive-side issue and does not justify retaining a transmit-side
copy in the client.

### Phase 4 acceptance criteria

- `zhttp/test/zhttp.cc` holds only the source `Record`; `Req::requestJSON` and
  pre-submit `ZfJSON::save()` are gone.
- Each attempt constructs a fresh typed request Builder from an immutable
  request plan and calls `ZfJSON::save()` directly on the concrete HTTP body
  stream.
- PUT uses `Body::OptionalFixed`, provisions a 10-byte `content-length` slot,
  records the produced count in its writer, and patches it synchronously in
  `bodyHdrs`; GET takes the absent optional-body branch.
- Response receive callbacks live on a typed response Parser sink and preserve
  existing receive streaming semantics.
- The body cursor, batch callback, predeclared length callbacks, retained JSON
  string, and `--body-tx-batch` option are absent from the client example.
- Focused functional unit tests for PUT, GET, redirect, retry, decoded-record,
  and request telemetry pass without comparing against a retained serialized
  request string when this phase ends at a stable buildable checkpoint.
- Audit every phase 4 change against `GUIDELINES.md` and repair all identified
  client ownership, Tx/Rx sharding, callback, allocation, copy, Z-idiom,
  layout, and performance issues before the phase is accepted.

## Phase 5 — Migrate `Service` and `zhttpd.cc`

The server side uses the same Builder contract for responses.  Replace the
current `Service::RespOps`, internal unrestricted `Builder`, and
`Workload::body(tx, builder, response)` arrangement with an application
response Builder and a protocol-specific response facade over it.

When request parsing completes on Rx, the workload supplies a concrete
response Builder and a distinct completion token by value through a
synchronous typed emitter.  It may select
different response/body policies without a variant or runtime-erased stream:

```c++
template <typename Emit>
void response(RequestParser &request, Emit &&emit) {
  if (request.file())
    emit(FileResponseBuilder{filePlan(request)}, completion(request));
  else
    emit(JSONResponseBuilder{jsonPlan(request)}, completion(request));
}
```

The emitted Builder owns, or holds a destination-safe reference to, everything
needed for response construction.  It must not retain mutable references into
the Rx-owned request Parser or request state.  `Service`'s typed emitter moves
that Builder into a Tx post; it does not retain the application callback.  The
cross-shard Builder value and closure must contain only small fixed metadata
and destination-safe handles.  Put large or variable response records, file
state, and encoded source data in Tx-owned storage or another established
destination-safe owner and capture only its handle; do not move an unbounded
payload through the scheduler closure.  Snapshot the request method and any
other fixed metadata needed for HEAD or CONNECT response semantics into the
response plan before that move.  A completed request must emit exactly one
response Builder or fail the logical request.

The second emitter argument is the exact completion/reporting API; do not
implicitly copy the response Builder or add a coordinator-specific member to
its HTTP field-provider surface.  Its type may vary with the emitted Builder.
It is a small fixed snapshot or a destination-safe handle which the Builder
may also own when Tx construction must update final counters.  After Tx
construction, Service calls
`Workload::complete(const RequestInfo &, Completion &, bool sent)` on Rx.
The Rx-side emitter therefore snapshots all data needed by that callback
before moving the response Builder.
Completion must not receive or inspect the moved-from Builder.  The snapshot
is small fixed metadata or a destination-safe handle, is captured
independently in the Tx post, and is posted back to Rx with the send result
after the synchronous build reaches its terminal protocol boundary.  This
separates response-build ownership from access logging and exact-once request
completion without copying the response body or retaining the Rx Parser on
Tx.

Each emitted type is a response Builder: it owns `status`, `reason`, both
`header` overloads, and the operations selected by its body policy.  A
body-bearing type owns `body`, and a fixed body type additionally owns
`bodyHdrs`.  For example:

```c++
struct JSONResponseBuilder {
  using BodyPolicy = Zhttp::Body::Fixed;
  using Headers = ResponseHeaders;
  using ContentLength = ZuStringT<"content-length">;
  using ContentType = ZuStringT<"content-type">;

  ResponsePlan response; // small Tx-owned metadata plus source-data handle
  unsigned contentLength = 0;

  unsigned status() const { return response.status; }

  template <typename L>
  void reason(L &&l) const { l(response.reason); }

  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ContentLength>{})
      l(Zhttp::HeaderPad{10, '0'});
    else if constexpr (ZuIsSame<Key, ContentType>{})
      l(ZuStringT<"application/json">{}());
  }

  template <typename Emit>
  void body(Emit &&emit) {
    emit([this](auto &body) {
      ZfJSON::save(body, response.record);
      contentLength = body.produced();
    });
  }

  template <typename L>
  void header(L &&) const { } // no runtime-key fields in this example

  template <typename L>
  void bodyHdrs(L &&l) const {
    l.template operator()<ContentLength>(
      [contentLength = this->contentLength](ZuSpan<uint8_t> span) {
        ZuStream<uint8_t> s = span;
        s << ZuBoxed(contentLength).fmt<ZuFmt::Right<10>>();
      });
  }
};
```

`FileResponseBuilder::body()` opens/duplicates the file and writes successive
spans inside its one synchronous writer call.  With `Body::Stream`, H1 sends
that response chunked and H2/H3 send DATA immediately.  If an application
instead selects `Body::Fixed` to send `content-length` or a body digest, the
entire response is deliberately retained before headers and body are
committed.  The mode has the same meaning on client and server.

On Tx, `Service::Session` constructs the matching protocol `ResponseBuilder`
facade around the moved application Builder and performs the complete send in
one call stack.  Every `body` emitter and `bodyHdrs` patcher call is synchronous
on that stack.  Service must not expose `tx` or the protocol facade to the
workload and must not capture the Rx Parser in the Tx closure.  Completion is
posted back to Rx only after the synchronous response-build call has completed
its final protocol boundary or has failed; the first committed streamed buffer
does not constitute response completion.  Delete `Workload::contentLength()`;
non-streamed response length comes from `bodyHdrs`, and streamed responses
have no body-dependent header callback.  Service-owned fields such as Alt-Svc
join the same protocol-specific header collection: H1 renders them into its
retained header block, while H2/H3 retain uncompressed logical values until
final encoding.  They are not body-dependent and are never exposed through
`bodyHdrs()`.

H1 response plans must retain request order across the Rx-to-Tx handoff.  Do
not begin a later response until the preceding synchronous response-build call
has emitted its final boundary or has failed; merely releasing the preceding
response's first streamed buffer is not sufficient.  H2/H3 use the stable
logical-stream handle captured with the plan.  Neither case permits response
construction to access another shard's mutable session state.

Make request reception typed as well.  Replace `ReqSink` forwarding to
`Workload::requestBody(...)` with an application `RequestParser` sink whose
local methods are `operation`, `version`, `contentLength`, `chunked`, `header`,
`body`, and `complete`.  `Service` continues to own connection metadata,
admission, transport choice, and lifecycle, but does not prefix HTTP field
callbacks with `request`.

HTTP semantics which prohibit a response entity remain Builder-owned.  HEAD,
1xx, 204, 304, and successful CONNECT responses do not invoke `body` or
`bodyHdrs` and emit no body framing; application-provisioned
body-dependent slots are suppressed.  HEAD and 304 may still provide an
ordinary representation `content-length` when HTTP semantics permit it; it is
not computed from an emitted body.  Such a value comes from an ordinary typed
header value on the selected bodyless response form, not from a `HeaderPad` or
`bodyHdrs`.  The protocol facade rejects or suppresses `content-length` where
it is forbidden, including 1xx, 204, and successful CONNECT responses.

### Phase 5 acceptance criteria

- Server request parsing targets a typed application `RequestParser` with
  locally named callbacks; no `ReqSink` forwarding to
  `Workload::requestBody()` remains.
- A completed request synchronously emits exactly one concrete response
  Builder, which is moved to Tx with destination-safe state and no mutable
  reference to the Rx Parser or session state.
- Completion and reporting use a separately snapshotted token or handle and
  are posted back exactly once after Tx construction; they never observe the
  moved-from response Builder or capture the Rx Parser on Tx.
- Response Builders own `status`, `reason`, both `header` overloads, and their
  policy-selected `body`/`bodyHdrs` operations; `Service` never passes raw
  `tx` or a protocol Builder to the workload.
- Fixed JSON responses patch headers before releasing any byte; streamed file
  responses cross pooled buffers and use chunked H1 or immediate H2/H3 DATA.
- H1 responses remain in request order, while H2/H3 use the captured stable
  logical-stream handle.
- Focused functional unit tests for HEAD, 1xx, 204, 304, and successful CONNECT
  prove that the server does not call `body` or `bodyHdrs` or emit entity
  framing when prohibited, when this phase ends at a stable buildable
  checkpoint.
- Audit every phase 5 change against `GUIDELINES.md` and repair all identified
  service ownership, Tx/Rx handoff, response ordering, callback, allocation,
  copy, Z-idiom, layout, and performance issues before the phase is accepted.

## Phase 6 — Collapse adapters and delete producer machinery

Complete propagation of the distinct request-Builder and
response-Parser-sink types through `ClientMessage`.  Remove any remaining
translation of request `body` into `requestBody` or response Parser `body`
into `responseBody`.  Apply the mirror image to the remaining `Service`
adapters: request parsing targets a request sink and response construction
targets a response Builder, both using `body` within their own type.  Phases 4
and 5 establish these paths; phase 6 removes residual adapters and the old
production path rather than creating another message abstraction.

Simplify both client request and server response transmission to one Tx-owned
construction turn.  The streamed path creates the native stream, emits
headers, invokes the body writer, emits the final boundary, and allows buffers
to flow to the transport as they fill.

The non-streamed path instead:

- obtains the native stream's capacity and accumulated head/tail-room contract;
- creates a retained-send stream using the native allocator;
- renders retained H1 headers or collects uncompressed H2/H3 logical fields,
  recording each body-header placeholder's mutable value span without sending
  it or updating HPACK/QPACK state;
- invokes the body writer through the protocol body layer backed by that
  retained stream;
- flushes the body and records its produced length;
- invokes `bodyHdrs`, permitting successive templated-key patchers over mutable
  uncompressed value spans;
- validates all required late slots, especially `content-length`;
- retains the already-rendered and patched H1 header block, or encodes H2/H3
  HEADERS from final values while staging compression-state changes;
- orders those header buffers before the retained body/DATA buffers;
- releases the complete chain and publishes staged HPACK/QPACK state in one
  protocol commit;
- publishes the completed commitment state to the owning side (`BodyCommit`
  for the client attempt).

No buffer from a non-streamed request or response is released before every
late header has been finalized and validated.

Delete the existing body-production machinery rather than retaining a second
path:

- `BodyDeflt::TxBatch`;
- `BodyProduce` in its entirety;
- `BodySend` in its entirety;
- cursor template parameters from all `Body` policies;
- `Body::EmptyCursor`, `BodyCursor`, `BodyCursorStorage`, and
  `requestBodyCursor()`;
- `requestHasBody()`, `requestContentLength()`, and the message-local
  predeclared body length; optional body presence is whether `body()` invokes
  its synchronous emitter;
- `ClientConfig::bodyTxBatch()` (formerly `AgentConfig::bodyTxBatch()`), its
  storage, validation, and CLI wiring;
- the coordinator's `requestBodyBatch()`;
- all direction-prefixed message-field callbacks listed under
  "Message-typed Builders", including `requestBody()` and `responseBody()`;
  replace them with local names on the appropriate message-typed Builder or
  Parser sink and propagate the break through every dependent;
- `ClientMessage::startTx()`, `send(unsigned batch)`, cursor construction,
  producer-turn state, and `BodyProduce` validation;
- `BodySend::More` continuations and producer rescheduling in
  `ClientPool`/`TLSClientPool`;
- message-local Tx generation used only to reject stale producer resumptions;
- producer-resumption drain barriers in close/completion paths;
- `producerFailed`/`m_producerFailed`; report a synchronous request-build
  failure directly instead;
- `Service::RespOps::contentLength()`, `Workload::contentLength()`, and
  `Workload::body(tx, builder, response)`; response construction belongs to
  the application `ResponseBuilder`;
- `ReqSink` forwarding to `Workload::requestBody()`; route the protocol parser
  directly into the typed application `RequestParser::body()` sink;
- tests and README material that specify cursor/batch production.

Also remove the current prohibition against `content-length` in request
`Headers` and remove pre-body `contentLength()` production on the server.
Replace both with policy-specific checks: a present non-streamed entity body
requires the key, while every streaming body rejects it.  Delete the H1/H2/H3
Builder paths that synthesize `content-length` from a separate
predeclared-length callback.  Non-streamed request and response Builders use
the finalized typed value instead: H1 patches its retained field span, while
H2/H3 encode the patched logical value.  H1 `transfer-encoding: chunked`
remains Builder-owned and must not appear in application `Headers`.

Replace that machinery with one `sendRequestTx_()` call which constructs and
finishes the complete request through its Builder.  It returns success or
failure directly.  Give `Service::Session` the equivalent single synchronous
response construction call.  The pool's attempt generation remains where it
is needed to reject stale Rx completion callbacks; it is not body-producer
state.

Audit and complete the deletion across the repository, not as a compatibility
shim.  The client and server migrations from phases 4 and 5 must already be
present; the entries below identify residual implementation and dependency
sites which phase 6 must finish removing:

- `ZhttpConfig.hh`: verify that the production enums/defaults are absent, the
  four body-bearing policies are non-template body-mode tags, and no cursor or
  producer residue remains;
- client coordinator (moved out of deleted `ZhttpAgent.hh`): compose distinct
  request-Builder and response-Parser-sink application types, remove
  direction-prefixed field forwarding, remove cursor and batch forwarding,
  and stop validating body-batch configuration;
- `ZhttpMessage.hh`: verify that the phase 1 typed facades and phase 3 body
  contract are the only paths, then delete any remaining producer state or
  direction-prefixed forwarding;
- `ZhttpH1.hh`, `ZhttpH2Message.hh`, and `ZhttpH3.hh`: verify direct use of the
  restored message-local providers and fixed-only `bodyHdrs`, then delete any
  residual old provider calls, public stream/compression hooks, and the unused
  default `bool request()` hook;
- `ZhttpService.hh`: verify the phase 5 request-Parser/response-Builder
  composition, Rx-to-Tx ownership, H1 ordering, and synchronous finalization,
  then delete any remaining `ReqSink`, `RespOps`, or direction-prefixed path;
- `ZhttpClientPool.hh` and `ZhttpTLSClientPool.hh`: invoke that send once and
  remove continuation rescheduling and resumption-drain posts;
- `zhttp/test/zhttp.cc`: verify the phase 4 direct PUT serialization and remove
  any remaining cursor/batch or retained-JSON path;
- `zhttp/test/zhttpd.cc`: verify the phase 5 typed response Builders and remove
  any remaining `(tx, builder, response)` body callback;
- transport-contract and cancellation tests: replace producer-turn coverage
  with Builder-stream, framing, boundary-cancellation, and telemetry coverage;
- `zhttp/README.md`: document only the synchronous Builder body contract.

Do not leave deprecated overloads, aliases, forwarding callbacks, or old body
policies.  Dependent code must migrate to the new contract.

Keep client request and server response construction, body counters, framing
finalization, and send cancellation on their Tx shards.  Client `BodyCommit`
is not producer machinery and remains as completed-send telemetry.  Its
`produced` value is obtained from the body layer after `flush()`.  The server
tracks the equivalent state for failure and shutdown even if it does not
expose identical public telemetry.  For a non-streamed body, require the
finalized `content-length` slot to equal the produced value before commit.  For
a streaming body, successful writer completion is the source boundary and
`builder.finish()` maps it to the transport-specific final boundary; a `false`
return takes the protocol-specific abort path instead.

For non-streamed messages, `BodyCommit::committed` changes only when the retained
chain is released.  A body or late-header failure therefore reports produced
bytes, zero committed bytes, zero reset bytes, and discarded bytes equal to
the produced entity length; the chain is destroyed without a transport reset.
Once committed, transport failure/reset accounting proceeds normally.

A non-streamed writer, header patch, or encoding failure destroys the retained
chain and exposes no partial message.  A streamed writer can fail after bytes
have been committed; H1 must close the connection, while H2/H3 reset the
logical stream.  The application receives one terminal failure and the
connection is reused only when the protocol proves that reuse is safe.

Cancellation cannot interrupt an arbitrary synchronous formatter halfway
through its call.  Cancellation posted to the owning Tx shard runs before or
after that call.
That is an honest property of `ZfJSON::save()` and of the original synchronous
Builder contract.  If a future application needs an asynchronous or
cooperatively scheduled body source, design that source and its continuation
contract explicitly at that time; do not retain the current cursor/batch
machinery speculatively.

### Phase 6 acceptance criteria

- Client and server each perform transmit construction in one synchronous
  Tx-owned call; no body callback, writer, emitter, or patcher survives that
  call stack.
- Fixed construction follows collect/write/flush/patch/validate/encode/commit
  order, and streamed construction follows write/finalize without retained
  whole-message commit.
- `BodyProduce`, `BodySend`, cursor types/storage, Tx batch configuration,
  producer continuations, rescheduling, drain barriers, and producer-only
  generation/failure state are deleted from headers, sources, tests, CLI, and
  documentation.
- H1/H2/H3 no longer synthesize `content-length` from a pre-body callback;
  fixed Builders provide a finalized typed slot and streaming Builders reject
  it under this contract.
- The client retains only attempt generation needed for stale Rx completion
  rejection and retains `BodyCommit` solely as completed-send telemetry.
- If phase 6 ends at a stable buildable checkpoint, the affected transmission,
  coordinator, and service targets compile with no unresolved old callback
  names or transitional overloads.  Otherwise defer that incremental build as
  required by the build policy; searches of live source, build manifests,
  configuration, and tests must still find none of the deleted production
  machinery.  Historical and removal text in this plan is excluded.
- Audit every phase 6 change against `GUIDELINES.md` and repair all identified
  dead code, compatibility residue, ownership, sharding, allocation, copy,
  Z-idiom, layout, and performance issues before the phase is accepted.

## Phase 7 — Integrated validation

### Tests

Complete the following functional unit-test additions, updates, and removals.
Run the resulting suite in the clang/debug build before, and separately from,
the full interoperability suite:

- A PUT record is serialized without `Req::requestJSON`.
- A JSON string larger than one native Tx buffer crosses several pooled
  `ZiIOBuf`s, proving that serialization writes through the stream rather than
  expanding a temporary contiguous buffer.
- Client request and server response Builders both write through the concrete
  protocol body stream; `Service` never passes `(tx, builder)` to its workload.
- Compile body-bearing request Builders with `operation`, `host`, optional
  `protocol`, both `header` overloads, and `body`; compile body-bearing
  response Builders with `status`, `reason`, both `header` overloads, and
  `body`, without any direction-prefixed forwarding member.  Fixed policies
  additionally compile `bodyHdrs`; streaming policies neither require nor
  instantiate it, and `Body::None` compiles with neither body operation.
- Compile request Parser sinks with `operation`, `version`, `contentLength`,
  `chunked`, `header`, `body`, and `complete`, and response Parser sinks with
  `status`, `version`, `contentLength`, `chunked`, `header`, `body`, and
  `complete`, without direction-prefixed forwarding members.
- Server request callbacks execute on Rx; the emitted response Builder is
  moved to Tx; its `body` and `bodyHdrs` execute there without reading the
  request Parser.  H1 responses remain in request order.
- H1 `Body::Stream` uses multiple valid chunks and one terminating chunk for
  both requests and responses.
- H1 `Body::Fixed` and the present branch of `Body::OptionalFixed` emit
  `content-length`, never chunked encoding, and release no bytes before the
  late-header callback, on both client and server paths.
- H2 and H3 streaming bodies use multiple DATA frames and exactly one final
  stream boundary without retaining the complete body merely because it is
  finite.
- Every streaming Builder rejects `content-length`.  A present streaming body
  uses chunked H1 or H2/H3 DATA with the final stream boundary; an absent
  `Body::OptionalStream` emits no body framing.
- H2 and H3 non-streamed bodies release HEADERS before retained DATA and expose
  only final, not placeholder, uncompressed values to HPACK/QPACK planning,
  encoding, and dynamic-table state.
- An H2/H3 fixed-message failure before commit discards staged header and any
  QPACK encoder-stream buffers and leaves HPACK/QPACK state unchanged; an H3
  literal-only transaction has no encoder-stream or table changes to discard.
- A body-dependent value is patched through its mutable uncompressed
  `ZuSpan<uint8_t>` without resizing it.  A 200-byte body produces the valid
  fixed-width H1 field `content-length: 0000000200`; H2/H3 compress
  only that final value, never the initial placeholder state.
- Force an H1 placeholder to the end of a header buffer and verify that the
  Builder rolls before it, exposes one contiguous value span, patches that
  span in place, and does not render the field a second time.
- An ordinary header value that is not convertible to `ZuCSpan` but has a
  custom stream insertion/printing implementation is rendered correctly by
  both the typed and runtime-key `header` overloads in H1, H2, and H3.
- A typed header provider which invokes its value lambda more than once fails
  message construction.
- Runtime-key headers cannot duplicate a typed body-dependent slot or inject
  `content-length`/H1 `transfer-encoding`; name matching is case-insensitive.
- A custom Base64 SHA-256 body header is finalized and decoded correctly for
  H1, H2, and H3; the application updates its digest through a typed forwarding
  layer during `body()` and `bodyHdrs()` uses only the captured digest state.
- One `bodyHdrs` call successively selects `content-length` and a custom digest
  header as template keys and patches both mutable uncompressed spans.
- An undeclared or compile-time constant `bodyHdrs` key fails at compile time;
  missing, poison-containing, and length-mismatched patches fail before any
  non-streamed request or response byte is released.
- GET through `Body::OptionalFixed` and `Body::OptionalStream` calls
  `body(emit)` once, does not invoke `emit`, suppresses every `HeaderPad`, and
  emits neither body framing nor a writer call.
- A required body with zero emissions and any body policy with duplicate
  emissions fail cardinality validation.  Fixed forms release no bytes;
  streaming forms close H1 or reset H2/H3 if headers or body bytes are already
  visible.
- Body writers returning `void` or `true` succeed; a writer returning `false`
  discards a fixed message before commit and aborts a streamed H1/H2/H3
  message according to its protocol visibility.
- An optional fixed writer which is emitted but writes zero bytes is treated
  as a present body: it calls `bodyHdrs()` once and emits the fixed-width
  zero `content-length`, unlike the absent branch.
- HEAD, 1xx, 204, 304, and successful CONNECT server responses invoke neither
  `body` nor `bodyHdrs` and emit no entity framing; status-specific
  `content-length` rules remain enforced.
- A streamed file response crosses several pooled buffers without retaining
  the complete file; H1 uses chunked framing and H2/H3 use DATA.
- A non-streamed JSON response patches `content-length` and a digest before
  any response buffer is released.
- A failed non-streamed response releases no bytes; a failure after a partial
  streamed response closes H1 or resets H2/H3 and reports one terminal result.
- Redirect/retry invokes the writer once per attempt and reproduces identical
  entity bytes.
- Produced/committed/reset/discarded telemetry remains correct.
- A retained-body limit test fails before the entity-byte count crosses its
  configured cap, and the effective cap cannot exceed `UINT_MAX`.
- A non-streamed-body test rejects a final `content-length` unequal to the
  measured body length.
- Delete bounded-turn, cursor-restart, over-batch, and mid-producer
  cancellation tests whose only purpose is to specify the removed machinery.
- Retain cancellation tests at the Tx-task/message boundary and verify
  exact-once completion and correct committed/reset/discarded telemetry.

### Result

The PUT hot path becomes:

```text
putRecord -> ZfJSON::save(concrete HTTP body ZiTxLayer, putRecord)
          -> pooled transport ZiIOBufs
```

The server response path has the same shape:

```text
response value/file -> ResponseBuilder::body(concrete HTTP body ZiTxLayer)
                    -> pooled transport ZiIOBufs
```

The Builder remains responsible for transport-neutral message construction,
the application remains responsible for producing typed content, and the
compiler retains the concrete stream type through every layer.  JSON is
written immediately into its final pooled I/O buffers with no complete
request-body allocation or copy.  Server responses use the identical direct
path and do not receive raw transport machinery through the workload API.
Streaming mode releases buffers as they fill; non-streaming mode retains them
until body-dependent headers are final and commits the complete ordered chain.

### Phase 7 acceptance criteria

- The resulting functional unit-test suite passes in the clang/debug build for
  both client request and server response construction across H1, H2, and H3.
- Large JSON and file bodies demonstrate direct multi-buffer output with
  preserved layered headroom/tailroom and no complete temporary body.
- Fixed-message tests observe zero peer-visible bytes on writer, patch,
  validation, size-limit, or header-encoding failure; streamed failures close
  H1 or reset only the affected H2/H3 stream as specified.
- Produced, committed, reset, and discarded telemetry is correct for success,
  failure, cancellation, redirect, and retry paths, with exact-once terminal
  completion.
- The complete repository builds and its functional unit-test suite passes in
  the clang/debug configuration; all affected examples, interop programs, and
  README material use only the new typed synchronous contract.
- Repository-wide searches find no live code, build, configuration, test, or
  active-documentation use of `Agent`, `BodyProduce`, `BodySend`, body
  cursor/storage, body batch, direction-prefixed message callbacks, or
  compatibility adapters.  Historical and removal references in this plan
  are excluded.
- Audit the complete refactor against `GUIDELINES.md`, repair every identified
  API, ownership, sharding, allocation, copy, buffer, Z-idiom, layout, and
  performance issue, and rerun the affected clang/debug build and functional
  unit tests.  Only then run the full interoperability suite as the final
  validation activity of phase 7.  If it exposes a defect, repair and re-audit
  the affected code, rerun the affected clang/debug functional unit tests, and
  repeat the full suite; the last validation action must be a successful full
  interoperability run before phase 7 is accepted.
