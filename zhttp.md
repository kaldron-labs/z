# Zhttp additions for gRPC

Status: focused implementation plan against the current working tree. Existing
streaming is the foundation; additions below are proposed, not implemented.
[grpc.md](grpc.md) owns RPC framing, metadata policy, cardinality, deadlines,
protobuf integration and application dispatch.

## Existing support and scope

Zhttp already streams bodies over H2 and H3 without hidden full-body buffering.
It has incremental receive callbacks, section-tagged receive fields, pooled
output buffers, flow control, cancellation and asynchronous endpoint teardown.
Server producers can retain `BodyEmitter`, return `WriteOutcome::Stream`, and
produce further turns on Tx. Their close/invalidation discipline already
handles queued emitter copies. See [zhttp/README.md](zhttp/README.md).

Client request writers also transmit streaming bodies, but their application
production is single-turn: the writer must return `End`. That is sufficient
for a unary or server-streaming request. It does not support a client producing
messages over successive application turns.

The principal missing wire capability is outbound trailers: ordinary builders
currently emit only initial fields. Extend existing production and completion
paths using the implementation contracts below. They prescribe the API and
owning paths without requiring a shared client/server producer rewrite.
[GUIDELINES.md](GUIDELINES.md) is authoritative, including ownership, allocation,
formatting and teardown rules. Propagate necessary API changes directly through
affected dependents.

H2 and H3 remain baseline transports as specified by grpc.md. Use the existing
public CRTP endpoints and transport profiles; keep protocol implementation
headers private. A gRPC call is an ordinary POST. Existing CONNECT frame-mode
operations do not substitute for ordinary request/response production.

## Required deltas

| Capability | Work | Needed for |
| --- | --- | --- |
| Response trailers | Add final ordinary fields to the existing response producer, ordered after accepted DATA | Every successful RPC, including unary |
| Trailers-only response | Verify the existing no-body response path can terminate with all required initial fields | Immediate RPC errors |
| Receive completion | Verify section-tagged fields and existing completion callbacks provide the necessary ordering; add only missing notification | Metadata and terminal status |
| Multi-turn client production | Extend the client producer using the server producer's established lifetime discipline | Client-streaming and duplex |
| Early final response | Verify response preservation and unfinished request disposal; repair demonstrated gaps | Early rejection and completion |
| Replay policy | Close any automatic reissue path that bypasses the RPC no-replay policy | grpc.md's initial call policy |

### Response trailers

Extend `BodyEmitter` with `template <typename B> bool trailers(B &fields)`.
`fields` is a structural field provider following `Builder`: `HdrCatalog`,
`HdrBufSize`, the existing `header<Key, Value>(l)`, `header<Key>(l)` and
`header(l)` overloads, and the sensitivity policy below. Deriving from `Builder`
supplies defaults. It supplies fields only; do not invoke operation, status,
body or `bodyHdrs` callbacks. Reuse catalog matching and fixed/name/literal field
forms. Render runtime declared values using the ordinary name/value form at
submission; do not request mutable offsets or retain patch spans.
Repeated dynamic fields remain repeated provider calls.

This is synchronous and Tx-only, exactly like `emit(write)`. Keep that existing
public naming pattern: `trailers` does not dispatch or retain a provider.
Private driver implementations use `trailers_` and assert Tx ownership. Do not
rename the existing call operator or add an either-shard overload returning a
pretend synchronous result. Off-shard application work posts its owning call or
buffer handle to Tx before invoking either operation.

The application first commits the initial section with an ordinary body turn
returning `Stream`, which may write zero bytes. It then submits more turns or
calls `trailers(fields)` outside an active writer. For example, on Tx:

```cpp
bool ok = emit([&payload](Zhttp::Writer &out) {
  out << payload;
  return out.failed() ? Zhttp::WriteOutcome::Failed :
    Zhttp::WriteOutcome::Stream;
});
if (ok) ok = emit.trailers(fields);
```

The existing body call's boolean reports that the producer handled the turn;
it does not certify successful production or even writer invocation when initial
header construction fails. A build/write/flush failure invalidates that producer,
so a subsequent trailer call returns false. Preserve that behavior; `trailers`
has its own terminal-admission result as specified below.

`fields` and its values need live only through `trailers`. `true` means the
terminal section was admitted, not transmitted. Invalidated, already-ended or
unsupported producers return false before invoking provider callbacks. Queue
refusal after rendering fails the producer through its existing result path;
no partial rollback is promised. Fixed-body emitters and H1 reject trailers.
gRPC requests use ordinary request end; request trailer support is outside this
increment. Use `WriteOutcome::End` and `Abort` for ordinary end and cancellation;
no redundant emitter `end` or `reset` methods are needed.

Add `bool (*trailers)(void *, const CatalogView &, FieldProvider &)` to
`BodyEmitRecord`. Unsupported bindings reject without invoking the provider.
Declare the template on `BodyEmitter` in `ZhttpCore.hh` and define it after the
field adapters in `Zhttp.hh`, avoiding a header dependency cycle. Bind its typed
field adapter on the stack; use no-op base/patch hooks for this final immutable
section. Reject pseudo-fields through existing HTTP field classification.
Extend `BodyEmit`/`StreamBody` to enqueue the terminal section and invalidate
producer admission through their existing state changes.

Embed the single terminal command in existing producer storage where its drain
lifetime allows; otherwise use a concrete identified `ZmNHeap` intrusive node.
Use pooled `ZiIOBuf` storage for rendered fields, storing offsets/lengths and
field metadata, never printable contexts or caller pointers. Use framework
arrays for variable field counts bounded by existing header/queue limits.
Render each printable once. Do not add fixed capacities or a separate general
command queue. Keep the command and rendered storage alive until emission or
cancellation drains it, without borrowing the application's emitter or field
provider. Retain the actual response record through the existing queue ownership
until its single completion notification; closing its producer does not retire
that response. Invalidating producer admission must not remove an accepted
terminal command or release an embedded command's containing allocation.
Queue ownership retains
the command storage; native link back-pointers remain invalidatable and raw.

Accepted trailing fields close producer admission. Emit them after the entire
accepted DATA prefix, including data stalled on stream flow control. Later body
or finish submissions fail. Render borrowed provider values into owned queued
storage before returning; encode the section when eligible so shared compression
state follows wire order. Reuse existing queues and invalidation machinery.

On H2, emit trailing HEADERS through HPACK with END_STREAM and correct
HEADERS/CONTINUATION framing; preceding DATA must remain open. On H3, emit a
trailing HEADERS section through QPACK, then FIN after the section. Preserve the
existing compression profile and cleanup. A trailers-only response terminates
its initial field section through the existing no-body path, without a second
section or manufactured DATA.

Zhttp handles HTTP fields and termination. Zgrpc supplies `grpc-status` and
interprets trailers-only responses. The wire contracts are the
[gRPC H2 protocol](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md)
and [H3 mapping](https://github.com/grpc/proposal/blob/master/G2-http3-protocol.md).

### Receive fields and completion

`Parser::header(FieldSection, ...)` already distinguishes informational,
final-initial and trailing fields. Start by documenting and testing its ordering
with `bodyInfo`, body callbacks, normal completion, failure and parser reset.
Zgrpc must retain selected metadata before borrowed field storage expires and
publish terminal status only after normal response completion establishes that
it is usable. An individual field callback does not establish HTTP acceptance.

If those callbacks cannot identify an accepted section boundary needed by the
adapter, add a section-completion hook through the installed parser bindings,
with this exact public contract:

```cpp
bool headersDone(Channel *, FieldSection::T) { return true; }
```

Name it after the existing internal `headersDone_` boundary. Call it on Rx after
HTTP validation of the complete section and before `bodyInfo` for initial
fields; call it once for trailers after their fields. Suppressed informational
sections remain suppressed. Return false has the same rejection behavior as
`bodyInfo`: no later application prompts, one failed completion. It exposes a
borrowed ordinary channel so a server can acquire its existing `Route` at initial
headers. Bind it through `ParserOps`, `ParserBinding`, `TypedParser` and the
client/server bound adapters together. No separate end-stream argument is needed:
normal `complete(channel, true)` establishes receive end, including an empty
body or delayed FIN. Keep the existing immediate `reset()` contract.

Use a side-effect-safe base default and direct typed dispatch, rather than a
new detector hierarchy. Do not add a parallel terminal-event API or a duplicate
`peerEnd` notification. H3 FIN may arrive after HEADERS; tests must cover that
ordering. When the hook is unnecessary, acquire the early response route through
the existing channel-aware `bodyInfo` callback. Reuse existing completion/result
reporting for reset, connection failure and unprocessed attempts, extending only
information needed for RPC error mapping.

### Multi-turn client production

After unary and server-streaming support, extend ordinary client request
production to retain an emitter across turns. Reuse `BodyPolicy::Stream`,
`WriteOutcome::Stream` and `End`, pooled output and existing queue admission.
A zero-byte open turn must not end the request; `End` half-closes the request
while response reception continues. Keep the existing single-turn path usable.

Implement this in `ClientSession`'s streaming branch; retain `MessageTx` for
fixed-body work. Streaming policies use the retained producer path whether the
first writer returns `End` or `Stream`; a single-turn `End` remains valid. Bind
the same public `BodyEmitter` to a client producer record instead of the current
stack-borrowed emitter. Allocate that producer from a concrete identified
`ZmNHeap`. Reuse the server's
`StreamState` transition helpers for initial/open/writing/terminal transitions;
factor only helpers genuinely common to both roles. The native request driver
stays behind an invalidatable raw back-pointer under the existing owner drain.

Add `void close() { }` to `ReqBuilder`, matching `ResBuilder::close`, and bind it
through `RequestOps`/`RequestRecord`. Invalidate retained emitters before calling
close once per producer/attempt on Tx, outside any active writer or initial
`body(emit)` callback. Application close drops its retained emitter; queued copies
reject later turns without dereferencing the driver. This hook is distinct from
the submitted request's once-only `completed` and final `retired` callbacks.
Request `End` seals only request output; keep the response parser and request
record alive until existing completion and Rx/Tx retirement finish. Reject
Content-Length for streaming production, as on the server.

Applications intending multiple turns override `replayable()` to false before
submission. The first `Stream` result is the point where this intent becomes
observable to the producer: if the snapshotted replay policy is true, fail and
invalidate the producer through the existing asynchronous request result. Do
not promise admission-time rejection based on a writer result that has not yet
occurred. A single-turn `End` may retain its ordinary replay policy.

### Early replies, cancellation and retirement

Exercise the existing response routing, cancellation and drain paths before
changing them. The server must be able to respond while request DATA is arriving.
A completed final response must survive stopping an unfinished request, including
queued client producer actions. Stop application delivery of discarded request
bytes and preserve flow-control accounting. Use existing exchange cancellation
for deadline or whole-call aborts.

Repair only missing early-completion behavior. On H2, preserve the complete
response before any RST_STREAM(NO_ERROR) used to stop the request, as described
in [RFC 9113 section 8.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-8.1).
On H3, stop the request receive direction without resetting the response send
direction; see [RFC 9114 section 4.1](https://www.rfc-editor.org/rfc/rfc9114.html#section-4.1).
Implement request disposal in the existing native response-finish path: post a
fixed request/attempt identity to Rx, stop parser body delivery, and consume or
discard queued request bytes through existing receive-credit accounting. Order
native request-stop signaling as described above, after the terminal response
output. On the client, normal response completion on Rx, rather than receipt of
final-initial headers, posts producer invalidation to Tx. A response failure also
invalidates the producer through the failure path. Keep the completed response
result distinct from subsequent local send cancellation. Use internal owner-only
helpers ending in `_`; early completion needs no new public directional-stop
family. A server retains its existing `Route` from accepted initial headers
rather than waiting for request completion. Native slots remain until both
owners drain; no lock, timer cleanup or link reference cycle is permitted.

## Supporting implementation contracts

Implement and verify these alongside the adapter paths that need them. Preserve
the existing API where it already meets the contract.

### Input limits and ownership

Current H1/H2/H3 parsers and `BodyRx` apply a cumulative bound derived from
`retainedBodyMax`. Separate these meanings in the owning paths: add overloaded
`bodyMax() const` / `bodyMax(uint64_t)` to `Config` and `ServerConfig`, following
`retainedBodyMax`'s fluent return types, client overlay bit and pool inheritance.
Use the existing default cumulative ceiling for `bodyMax`; retain the current
retention default. On receive, `retainedBodyMax` bounds queued decoded body
bytes. Preserve its existing fixed-output entity bound as well: fixed output
remains capped by the minimum of `retainedBodyMax`, `retainedMessageMax` and
`FixedBodyMax`. `retainedMessageMax` keeps its existing output retention role;
`bodyMax` is a receive lifetime bound, not a new output limit. Document these
meanings in the README and update all affected callers directly.

Pass both bounds through parser/session bindings. Protocol parsers enforce total
bytes and Content-Length against `bodyMax`. Change `BodyRx::push` and `splice`
retention checks from monotonic `m_received` to current queue length. Account
consumption using the existing `prompt`/`discard` operations. Fragment a legal
DATA payload into available retained capacity and prompt consumption between
fragments; never reject solely because a frame exceeds the retained allowance.
If the consumer leaves no capacity, follow existing bounded backpressure or
failure semantics; do not spin. Zgrpc configures `bodyMax(uint64_t(-1))`, consumes
fragments incrementally, and enforces its own protobuf-message size limit.

Use existing `Input`/`ZiRxStream` extraction and pooled buffers. Retain contiguous
payload storage only while its bytes cannot be overwritten by queue reuse;
assemble fragmented messages directly into one consumer-owned pooled buffer.
Deferred decoding owns bytes and call state before HTTP completion resets the
parser, with a terminal marker ordered behind extracted data. Zhttp keeps receive
credit ownership. No parser pause protocol, public credit API or accounting of
application-held messages is added.

### Pooled output

Use the existing `Writer::allocBuf(headRoom)`, `headRoom()`, `tailRoom()`,
`maxSize()` and `operator <<(ZmRef<ZiIOBuf>)`. Do not add another allocator API.
The Zgrpc codec adapter uses these operations to allocate and detach its backward
builder buffer, sets the resulting skip/length, and transfers it through the
existing buffer insertion path. Fix adoption/splitting in `RetainedTx` and native
H2/H3 output if necessary; preserve pooled allocation tuning and avoid a
whole-payload staging copy. Transport framing and encryption copies remain
transport responsibilities.

Follow GUIDELINES.md's Tx-stream contract: sticky `failed()`, explicit checked
`flush()` at completion, and false/result reporting for queue refusal. Allocation
uses the framework heap failure path, without nullable-allocation checks or new
exception handling. Partial output acceptance is not transactional rollback.

### Field sensitivity

Add `bool neverIndex(ZuBSpan name) const { return false; }` to `Builder`, using
the existing compression vocabulary. The policy is fixed for the field section
and is evaluated synchronously for every emitted field, including declared and
dynamic fields. Applications with several policy names use a `ZuMatcher` catalog,
not repeated string comparisons. The same policy applies to trailer providers.

Carry its boolean through `FieldOutput` and the owned trailer field record into
HPACK/QPACK. Combine it with existing transport sensitive-name defaults; false
does not disable those defaults. A true field uses a never-index representation
even when its value matches a static or seeded entry. Do not mutate shared encoder
policy from a callback. Keep existing shared catalog seeds, which contain
public fixed declarations. Sensitive application values must be supplied at
runtime, with empty catalog value lists or dynamic fields; do not put secrets
in fixed seed declarations. `neverIndex` controls each emitted field, not prior
catalog seeding. A sensitive field uses neither an indexed field representation
nor dynamic value insertion, including when a public seeded entry happens to
match. No new sensitive-name catalog type or connection configuration is needed.

### Replay and transport results

Add `bool replayable() const { return true; }` to `ReqBuilder` and bind it through
`RequestOps`/`RequestRecord`. Snapshot it into the logical request's fixed
metadata at submission, before the first attempt, and preserve it across wire
resets. Pass that immutable value to Tx; do not read mutable Rx-owned `LiveReq`
state from the producer. Zgrpc overrides it to false and sets `maxRetries(0)` /
`maxRedirects(0)`. Keep `idempotent(method)` separate: it cannot override false.

Guard all recreation of an admitted HTTP attempt, including TLS `Unprocessed`,
redirects, retries and H3/H2 transitions. Centralize the replay decision before
attempt retirement or `nextAttempt_`, and audit every caller so no path bypasses
it. Reuse `ResultCode::ReplayUnsafe` on refusal, preserving HTTP status and
request/attempt diagnostics. Selection/discovery before the first HTTP attempt
is allowed; pool reconnection for later requests is unaffected.

Reuse `Result`, `ResponseResult`, parser completion and existing attempt callbacks.
If native error information required by gRPC mapping is missing, append an error
domain using `ZtEnumNS` and a numeric native code to the existing result record,
with neutral defaults, and propagate them from the owning transport failure.
Do not introduce another completion callback or comprehensive event record.
GOAWAY classifies attempts beyond the peer's native acceptance boundary as
unprocessed: H2's last-stream boundary is inclusive; H3's request boundary is
exclusive. Streams within that boundary may continue; the connection takes no
new assignments. Preserve one terminal decision and native diagnostics
through later cancellation and drain.

## Implementation rules and locations

| Files | Prescribed extension |
| --- | --- |
| `ZhttpCore.hh`, `Zhttp.hh` | Emitter trailers, field provider erasure and sensitivity; existing Writer remains the buffer API |
| `ZhttpFields.hh`, bound parser adapters | Section completion hook if needed; pass cumulative and retained bounds separately |
| `ZhttpServerImpl.hh`, `ZhttpServer.cc` | Existing response producer final fields, ordered terminal command, early request disposal |
| `ZhttpClient.hh`, `ZhttpClientImpl.hh`, `ZhttpClient.cc` | Request close/replay binding, retained client producer, replay gate and final-response invalidation |
| `ZhttpConfig.hh` | Fluent cumulative body bound and client pool overlay |
| `ZhttpFieldImpl.hh`, H1/H2/H3 parsers | Queue retention versus total-body accounting |
| H2/H3 builders, HPACK/QPACK | Final-section encoding, native end, sensitivity and existing native error propagation |

Paths in the table are under `zhttp/src`. Keep typed catalog/CRTP adapters in
headers; put non-template command rendering, lifecycle helpers and shared policy
mechanics in `.cc` files. Public additions belong in installed headers, without
exposing native stream or compression types. Update erased bindings in the same
change as their typed interfaces.

Use GNU C++2b with existing SFINAE/`ZuIfT` patterns, no concepts or `requires`.
Use safe base defaults and direct dispatch for optional callbacks. Keep names
within 28 bytes, camelCase and existing vocabulary. Use hard tabs, tab stops of
8 and logical indentation of 2; retain header skeletons and direct dependencies.
All-public structs have unprefixed data first; classes keep private `m_` data at
the bottom. Use framework containers, concrete identified heaps and pooled I/O
buffers. Avoid STL replacements, temporary contiguous conversions, default
initialization before overwrite and duplicate render passes.

Rx owns parsing, receive credit and terminal arbitration; Tx owns producers,
encoders and ordered output. Public either-shard operations are thin dispatchers
capturing owned handles and fixed identity to owner-only `_` helpers. No locks
make non-owner access acceptable. Stop prevents ingress, drains Rx, invalidates
and drains Tx, then releases owners through the existing continuations. Keep
API additions focused on the contracts above and update affected dependents
without shims or forwarders.

## Delivery and verification

1. **Unary transport delta:** add response trailers on H2 and H3. Verify
   trailers-only responses and existing receive completion, making only missing
   callback changes. Test DATA/trailer order under flow-control stalls and
   producer invalidation. Build the unary Zgrpc adapter on this surface.
2. **Server streaming:** reuse the existing multi-turn server producer. Exercise
   early replies, cancellation and shutdown with the adapter; repair any missing
   routing or drain behavior. Perform targeted ownership, buffer and policy
   checks as the adapter reaches those paths.
3. **Client streaming and duplex:** extend client production across turns and
   verify request half-close, concurrent response reception, early final response
   and late producer rejection. Test long-lived bounded-memory operation.

The supporting contracts are implemented with the first adapter paths that need
them, before those paths are declared supported: replay suppression and custom
metadata sensitivity apply to unary too; client multi-turn lifetime work waits
for step 3. Buffer adoption and receive bounds must pass for each claimed path.

Use focused TAP tests for changed contracts in `zhttp/test` and endpoint coverage
in `zhttp/itest`. Exercise installed public headers on both transports. Cover
fragmented fields and DATA, delayed H3 FIN, stalled DATA before trailers, queue
refusal, reset and stop with outstanding producers. Specifically test terminal
command drain after builder close, trailer rejection after a failed body turn,
single-turn replayable requests versus invalid replayable `Stream` results,
no-replay unprocessed/redirect/fallback paths, sensitive seeded-name matches,
independent cumulative/retained input bounds, fixed-output limit preservation,
and detached-buffer splitting. Use deterministic scheduler barriers. Run affected
existing HTTP and dependent regression tests; additional independent HTTP
qualification infrastructure is not a prerequisite to starting the RPC adapter. Zgrpc's independent gRPC peer tests establish interoperability
in both roles on H2 and H3, as required by grpc.md.

For implementation changes, build with top-level `make -j4`, build each affected
module's `all` target before `make test`, and use `libtool exec` for instrumented
source-tree binaries. Record tested toolchains and untested platforms. This
plan makes no implementation or test-result claim. Keep grpc.md's question IDs;
resolve individual entries only when the corresponding implementation and tests
establish the answer.
