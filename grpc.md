# Zgrpc: native gRPC over Zhttp with Zpb

Status: proposed Zgrpc design, assuming [zhttp.md](zhttp.md) has been fully
implemented and verified as a precursor on H2 and H3. This is a planning
assumption, not a claim about the current working tree. No Zgrpc library code is
implemented by this document. API examples below describe the proposed surface.

Build `zgrpc` as a traditional Z shared library, `libZgrpc`, with installed
`<zlib/Zgrpc*.hh>` headers and the `Zgrpc` namespace. It supplies typed RPC
contracts, client builders, server dispatch, streaming, metadata and completion
over `Zhttp` HTTP/2 and HTTP/3. The standalone [zpb module](zpb/README.md)
provides the sole production protobuf codec, `Zpb`, through `libZpb` and
`<zlib/Zpb.hh>`. `zf` supplies field metadata; the protobuf codec and reflection
belong to `zpb`.
Neither the canonical gRPC runtime nor libprotobuf is a dependency.

## Basis and correspondence with existing libraries

The design follows [GUIDELINES.md](GUIDELINES.md), including its authoritative
28-byte name limit, and the metadata/facet model in [zf/README.md](zf/README.md).
Scope is gRPC protocol behavior and the endpoint capabilities now shared by
Zrest and Zjrpc: bounded admission, deferred completion, typed replies,
backpressure, body reception, and full endpoint lifecycle.
Repository engineering conventions still govern implementation. Zmcp and Zpb
inform the design but do not independently justify additional library features.
The following observations describe the reference libraries; the Zhttp baseline
also includes all completed precursor contracts in [zhttp.md](zhttp.md).

| Source | Existing facility | Zgrpc treatment |
| --- | --- | --- |
| [Zrest.hh](zrest/src/Zrest.hh) | Request/response policies, merged header catalogs, compile-time request lists, exact/prefix routing, status-selected response types | Method descriptors and merged metadata catalogs; exact service/method dispatch; one response message type and separate terminal status |
| [ZrestClient.hh](zrest/src/ZrestClient.hh), [ZrestServer.hh](zrest/src/ZrestServer.hh) | `Client<App, Catalog, Selection>` and `Server<App, Catalog, Decoder, Selection>` compose directly with Zhttp's client/server CRTP endpoints; `call` admits bounded work, `Completion` defers replies, and SSE supports incremental events | Complete Zgrpc endpoints compose directly with Zhttp's symmetric CRTP endpoints; preserve the shared admission/completion/queue/lifecycle pattern while supporting four gRPC cardinalities |
| [ZjrpcClient.hh](zjrpc/src/ZjrpcClient.hh), [ZjrpcServer.hh](zjrpc/src/ZjrpcServer.hh) | `HTTPClient<Impl, Catalog, Selection>` and `HTTPServer<Impl, Catalog, Selection>` are Zhttp CRTP implementations with bounded pending calls, deferred completion, fixed JSON and SSE responses | Use the same endpoint and configuration vocabulary; select protobuf decoding after method lookup and retain borrowed message storage explicitly |
| [Zjrpc.hh](zjrpc/src/Zjrpc.hh) | Descriptor-selected decoding, typed `Reply`, borrowed input policy and positive `Limits` | Typed gRPC result/status, selected decoder and positive limits |
| [Zmcp.hh](zmcp/src/Zmcp.hh) | Contract assertions, schema generation from field metadata, typed deferred completion and borrowed context | Reference for typed dispatch; MCP schema/discovery facilities are not carried into this design |
| [Zpb.hh](zpb/src/Zpb.hh) | Backward builders, pooled `IOBuilder`, facets, `verify`, handlers, nested values, maps and unions | Encode directly into pooled buffers; verify each external message before materialization |
| [ZpbReflect.hh](zpb/src/ZpbReflect.hh) | Native message-schema export and descriptor import | Optional schema tooling; runtime RPC dispatch uses typed message mappings and method catalogs |

The REST and JSON-RPC APIs align in capability and naming, although their
transport-specific call arguments and reply values differ. Follow that common
shape rather than copying either wire format. Reuse patterns without introducing
dependencies on `zrest`, `zjrpc`, or `zmcp`.
Their JSON envelopes, MCP discovery, JSON-RPC IDs, notification semantics and
SSE framing do not belong in gRPC. HTTP stream identity already correlates a
call. REST URI parameters become protobuf fields; authentication and ancillary
context become metadata.

REST response alternatives do not translate to several protobuf response types
chosen by status. Each RPC declares one output type. Applications needing
several successful outcomes use a protobuf message containing a `Zpb::AsUnion`
field. Errors use `Zgrpc::Status`, with opaque binary status details.
An empty protobuf message remains a message, distinct from no message.

## Scope and dependency boundary

The complete library supports unary, server-streaming, client-streaming and
duplex calls. These correspond to gRPC's four RPC forms, with independent
message order in each direction. [gRPC core concepts](https://grpc.io/docs/what-is-grpc/core-concepts/)

The supported transports are HTTP/2 and HTTP/3 through Zhttp's common logical
stream interface. Both are baseline targets; Zgrpc does not force H2 or disable
H3 discovery. Applications configure transport preference through Zhttp, which
owns discovery, ALPN and connection selection. Exclude H1 fallback and automatic
redirects for RPC calls. Identity encoding is mandatory in the first delivery.
Compression, cleartext H2 prior knowledge, health and server reflection are
not prerequisites or committed follow-on work for this library. Do not silently route `http:` through the
existing H1 TCP profile. gRPC-Web, xDS, hedging, service-config load
balancing and a synchronous blocking API are outside this design's baseline.

H1 can stream bodies, but does not supply the independent multiplexed,
bidirectional stream model used by native gRPC. Require H2 or H3 rather than
equating body streaming with RPC transport support. H3 is covered by
[gRFC G2](https://github.com/grpc/proposal/blob/master/G2-http3-protocol.md),
which preserves gRPC request/response semantics and message framing while
defining the H3 transport mapping.

```text
application objects + ZfStruct PB facets + method catalog
                         |
           Zgrpc client / server / call lifecycle
                  |                     |
       Zpb verify / handler / save      metadata + status
                  |                     |
             Zgrpc message framing and queues
                         |
           Zhttp common logical request stream
                         |
              H2 / Ztls    H3 / Zquic
                         |
                         Zi
```

`Zhttp::Client<Impl, Pool>` and
`Zhttp::Server<Impl, ReqParser, ResQ, Selection>` are the application-facing
CRTP coordinators. Each derived endpoint supplies its own
HTTP parser, builder and queue types; server `init(hub, config)` has no
application-pointer argument. Zgrpc follows the Zrest/Zjrpc composition pattern:
the Zgrpc endpoint is the HTTP `Impl`, while its separate application object is
passed to Zgrpc initialization. Lower-level protocol links keep their existing
ownership contracts. The server's optional response-header seed catalog belongs
on `ResQ::T::ResponseHeaders`, as required by the current HTTP coordinator.

`Zhttp` continues to own HTTP framing, HPACK/QPACK, connection and stream IDs,
flow-control windows, ALPN, pooling, GOAWAY, and reset transport. `Zgrpc` owns
RPC cardinality, gRPC message envelopes, status, deadlines and application dispatch.
Zgrpc must use Zhttp's public, protocol-neutral API rather than directly including
its H2/H3 implementation headers or implementing another HTTP stack.
Use Zhttp's existing transport results and callbacks, retaining native error
details needed for diagnostics and RPC status mapping. `Zpb` continues to
own protobuf payload encoding, verification and field semantics. Its optional
size-prefixed format follows the protobuf delimited-message convention;
gRPC uses its own five-byte envelope around an unprefixed Zpb payload.

### Completed Zhttp precursor and adapter contract

All required deltas and supporting contracts in [zhttp.md](zhttp.md) are assumed
available through installed public headers on both H2 and H3. Zgrpc consumes
that surface; transport implementation and qualification are not Zgrpc delivery
steps. The precursor owns HTTP producer lifetime, section acceptance, early
request disposal, receive accounting, buffer adoption, sensitivity and replay
suppression. No shared producer rewrite, new terminal-event subsystem or native
H2/H3 access is required.

**Response production.** Use retained `BodyEmitter` with `BodyPolicy::Stream`.
Commit initial fields through an ordinary body turn returning
`WriteOutcome::Stream`, even when it writes zero bytes. Submit subsequent turns
on Tx, then call `bool BodyEmitter::trailers(B &fields)` outside an active writer.
The structural field provider follows `Builder`'s catalog and header overloads;
only fields and sensitivity are evaluated. Values are rendered synchronously
into owned queued storage; Zhttp retains no provider or borrowed value after
the call.

A true trailer result means terminal admission, not transmission. It seals
producer admission and orders the final section after all accepted DATA,
including flow-control-stalled bytes. Later body or trailer submissions fail.
A body turn's boolean alone does not certify successful writing: check the
writer's sticky `failed()` and checked `flush()` result. Failed production
invalidates the emitter; trailers then fail through the existing result path.
Producer close cannot discard an accepted terminal command or retire its
response before completion. Use the no-body response path for trailers-only
errors, placing status in the terminating initial section without manufactured
DATA or a second field section. H1 and fixed-body emitters reject trailers.

**Request production.** Ordinary client streaming retains the same `BodyEmitter`
across turns. Return `Stream` to keep request output open and `End` to half-close
it while responses continue. A zero-byte `Stream` turn remains open; a single-turn
`End` also uses this path. Do not set Content-Length for streaming production.
Implement `ReqBuilder::close()` to drop the adapter's retained emitter. Zhttp
invalidates emitter copies before invoking close once per producer/attempt on
Tx, outside active body callbacks. Queued copies reject later work safely.
Close is distinct from request `completed` and `retired`; request end does not
retire the response parser or request record. Server producers retain their
corresponding existing close/invalidation discipline. gRPC requests end normally,
without request trailers or Extended CONNECT.

**Receive ordering and early replies.** Consume `Parser::header(FieldSection, ...)`
for informational, final-initial and trailing fields, preserving informational
suppression; retain selected metadata
before borrowed field storage expires. Use the precursor's accepted-section
boundary: `headersDone(Channel *, FieldSection::T)` if the completed implementation
needed that hook, otherwise the existing channel-aware `bodyInfo` for accepted
initial fields. Acquire the server's `Route` there so duplex dispatch and early
rejection do not wait for request completion. A field callback alone does not
establish section acceptance. Normal `complete(channel, true)` establishes receive
end, including trailers-only replies and H3 FIN delayed after HEADERS. Respect
the parser's immediate reset contract. Do not publish terminal RPC success before
normal HTTP completion and RPC validation.

Early final responses use Zhttp's native response-finish disposal path. It stops
request body delivery, discards unread bytes with receive-credit accounting,
and orders H2 request-stop signaling after complete response output; H3 stops
request reception independently of response transmission. Normal client response
completion on Rx posts producer invalidation to Tx; response failure uses its
failure path. Final-initial headers alone do not stop request production.
A completed response survives subsequent local request-send cancellation.
Slots and owners remain until Rx and Tx drain. Zgrpc adds no public directional
stop API and uses existing exchange cancellation for whole-call aborts.

**Bounds and buffers.** Set the effective HTTP receive `bodyMax(uint64_t(-1))`
for RPC streams and enforce protobuf-message limits in Zgrpc. Keep
`retainedBodyMax` finite: it bounds queued decoded input, independently of total
stream bytes. Zhttp fragments DATA into available retained capacity and accounts
consumption through its existing receive path. `retainedMessageMax` retains its
output role; fixed output remains bounded by the existing minimum of fixed and
retained limits. Consume fragments incrementally rather than retaining an entire
RPC body. Retain extracted storage only while queue reuse cannot overwrite it;
fragmented messages assemble directly into one consumer-owned pooled buffer.
Deferred decode must own bytes and call state before HTTP completion resets the
parser, with its terminal marker ordered behind extracted data.

Use `Writer::allocBuf(headRoom)`, `headRoom()`, `tailRoom()`, `maxSize()` and
`operator <<(ZmRef<ZiIOBuf>)` for Zpb output. The completed adoption/splitting path
preserves detached buffer skip/length and framing space without a whole-payload
staging copy. Zhttp retains receive-credit and transport-buffer ownership; Zgrpc
adds no allocator, parser pause or public credit API.

**Metadata and replay policy.** Override `Builder::neverIndex(ZuBSpan name) const`
for sensitive metadata in initial and trailing providers. A true policy forces
never-index representation even for a matching seeded value and prevents dynamic
value insertion; false preserves transport sensitive-name defaults. Supply secrets
at runtime, never in fixed catalog seeds. Use catalog matching for policy names.

Every Zgrpc request overrides `ReqBuilder::replayable() const` to false before
submission and configures `maxRetries(0)` / `maxRedirects(0)`. Zhttp snapshots
that policy before the first attempt and gates all attempt recreation, including
TLS unprocessed results, redirects, retries and H3/H2 transitions. Refusal uses
`ResultCode::ReplayUnsafe` with existing diagnostics. Initial discovery and
selection remain allowed. An idempotent flag cannot override false; multi-turn
production with a true replay policy fails asynchronously on its first `Stream`.
Use existing `Result`, `ResponseResult` and attempt callbacks, including the
precursor's native error details where needed. GOAWAY preserves the native
acceptance boundary and one terminal result through cancellation and drain.

## Public contracts and catalogs

`Zgrpc::Method` supplies defaults. A concrete descriptor declares `Service`,
`Name`, `Request`, `Response`, `ReqFacet`, `ResFacet`, `Kind`, `ReqHeaders`,
`ResHeaders`, and `Trailers`. The default facets are `ZuFacet::PB`; the default
kind is unary. Per-message size limits follow Zrest body-limit and Zjrpc
message-limit precedent, while endpoint work limits follow their shared
`Limits` vocabulary.

Use `ZtEnumNS` for `Kind` (`Unary`, `ServerStream`, `ClientStream`, `Duplex`),
status codes and public result enums. Use SFINAE/detector traits to expose only
operations valid for the role and kind. CRTP defaults are no-ops where safe;
missing mandatory dispatch must fail compilation.

```cpp
#include <zlib/Zpb.hh>
#include <zlib/Zgrpc.hh>

struct LookupReq { uint64_t id; };
ZfStruct(, (LookupReq, PB),
  (id, (Ctor<0>, PB::FieldID<1>), UInt64));
Zpb::AsMessage<"example.LookupReq"> Zpb_Fmt(LookupReq *);

struct LookupRes { uint64_t value; };
ZfStruct(, (LookupRes, PB),
  (value, (Ctor<0>, PB::FieldID<1>), UInt64));
Zpb::AsMessage<"example.LookupRes"> Zpb_Fmt(LookupRes *);

struct Lookup : Zgrpc::Method {
  using Service = ZuStringT<"example.Store">;
  using Name = ZuStringT<"Lookup">;
  using Request = LookupReq;
  using Response = LookupRes;
};

using Methods = ZuTypeList<Lookup>;
ZgrpcCatalogDerive(Store, Methods);
// In the application's .cc file:
ZgrpcCatalogImpl(Store);
```

`ZgrpcCatalogDerive` validates descriptors and derives initial/trailing metadata
catalogs; `ZgrpcCatalogImpl` defines the out-of-line `ZuMatcher` implementation.
The canonical key is computed from `Service` and `Name`, avoiding an independently
maintained path string. Match once, then dispatch the integer with `ZuSwitch`.
Compose catalogs with `ZuTypeConcat`; reject duplicate full paths across services.
Unlike REST prefix routing, no path stripping, query matching or URL decoding
occurs. Virtual hosting remains a `Zhttp` routing concern.

Compile-time checks cover valid nonempty service/method identifiers, unique
paths, valid kinds, PB message mappings, unprefixed facets and compatible
metadata declarations. Field numbering follows Zpb without a new Zgrpc policy.
Ordinary core fields use complete `ZfStruct` order unless every ordinary field,
including skipped fields, declares `PB::FieldID<N>`. A single explicit ID
requires all such IDs. Application metadata must match the peer's protobuf
schema.

Provide `ReqBuilder<Impl, Method>`, `ResParser<Impl, Method>`,
`ReqParser<Impl, Method>` and `ResBuilder<Impl, Method>` for embedding, together
with catalog-wide `MReqBuilder`, `MResParser`, `MReqParser` and `MResBuilder`.
Keep these lower-level adapters directly usable, as in Zrest. Templates contain
typed dispatch and codecs; non-template framing, metadata utilities and
lifecycle mechanics live in `.cc` files. Catalog adapters hold only active
method state, using `ZuUnion` where appropriate.

Zgrpc defines its transport selection internally:

```cpp
using Selection = Zhttp::Profiles<Zhttp::H2TLS, Zhttp::H3QUIC>;
```

This uses Zhttp's existing API. The selection excludes H1 from client pools and
server listeners; runtime configuration controls preference between H2 and H3
and retains H3 discovery. The public Zgrpc endpoint templates remain
`Client<App, Catalog>` and `Server<App, Catalog>`.

The complete `Client<App, Catalog>` and `Server<App, Catalog>` endpoints derive
from `Zhttp::Client<Client<App, Catalog>, Pool<App, Catalog>>` and
`Zhttp::Server<Server<App, Catalog>, Parser<Server<App, Catalog>, Catalog>,
ResQ<Catalog>, Selection>`, respectively. The internal `Pool<App, Catalog>`
passes the same selection to `Zhttp::Pool<Client, TxQ, ResParser, Selection>`,
with its client endpoint, transmit queue and catalog-wide response parser as
the first three arguments. Define complete pool, parser and queue utility
types before their endpoint; avoid eager nested-type lookup on an incomplete
CRTP `Impl`. The endpoint fulfills HTTP callbacks and stores a non-owning
`App *` for gRPC dispatch. Its `init` passes only the hub and HTTP configuration
to the HTTP base. `ClientConfig` and `ServerConfig` wrap the matching Zhttp
configuration and carry `Limits`; use `limits()` and `http(lambda)` in the same
pattern as Zrest/Zjrpc. Applications can still use the lower-level adapters.
`init`, `start`, `stop`, `stop(done)`, `final` and `up` follow the existing
endpoint lifecycle, including draining admitted work before `stop(done)`.

## Calls and application dispatch

`Client::call<Method>(pool, request, call)` handles unary calls. It returns
`bool` admission, as in Zrest/Zjrpc: `false` causes no callback; `true` commits
the endpoint to exactly one `process(Result<Response>)` or `failed(Failure)`
at logical RPC completion, before call retirement and without waiting for
transport retirement, following REST's completion timing. Successful completion requires normal HTTP receive completion
and all RPC validation, including any owned deferred receive work. Local deadline
expiry reports failure on Rx without waiting for transport retirement; resources
drain asynchronously. Retirement performs cleanup and drain accounting without
delaying or repeating the application callback.
A no-argument `failed()` is supported as in Zrest. Pool
selection and the request object are explicit. `open<Method>(pool, observer)`
returns an owning `ClientCall<Method>` reference on admission or null on
refusal; that call permits `send`, `end` and `cancel`. The return value is the
streaming operation handle, so no separate admission callback is needed.
Callback execution is Rx-owned.
Acceptance never means delivery.

Server dispatch is `app()->request(Method *, Context &, Message<Request>, call)`
for unary and server-streaming methods, after the single request and peer
half-close have been validated. For client-streaming and duplex methods,
`app()->open(Method *, Context &, call)` selects application stream state at accepted
headers, followed by `message`, `peerEnd` and cancellation callbacks. Context
contains callback-scoped peer, authority, initial metadata and deadline views.
Here `call` is a `ServerCall<Method>` reference backed by a deferred completion
token. As with Zrest/Zjrpc, it exposes `live()`, `cancelled()` and terminal
`complete`/`finish` behavior, and the application may retain it for deferred
work. Retaining it does not retain the context or its borrowed pointers.

| Call operation | Contract |
| --- | --- |
| `send(message)` | Return `bool` admission: off Tx, bound pending send posts before submitting an owning command; on Tx, return `send_`'s result; admission is not delivery |
| `send_(message)` | Tx-only submission with immediate success/failure, following Zjrpc queue rejection |
| `end()` | Admit one request-end command, ordered after preceding sends; responses remain receivable |
| `finish(status, trailers)` | Atomically admit one terminal command; submit trailers on Tx after preceding sends, then mark response production finished |
| `complete(Reply<Response>)` | Unary server completion token operation; atomically claim terminal admission before posting the response, status and trailers |
| `reply(response, status)` | Unary convenience using the same one-shot claim as `complete`/`finish`; one response followed by finish; non-OK may omit the response |
| `cancel()` | Request cancellation; idempotent after termination |

Off-shard methods submit owning command handles. An off-shard `send` returns
false on local ingress refusal without posting or scheduling a failure callback;
true means the send command was admitted for Tx processing. It does not report
the eventual serialized-queue result from `txRun`. Asynchronous send failures
reach the owning call's failure path, as in the existing adapters. Concurrent
producers have destination-queue order; callers needing a specific order must
serialize submissions. `end` and `finish` close production when their commands
run on Tx; sends processed after that boundary fail. An off-shard send admitted
behind either command can therefore fail asynchronously.

Follow the shared REST/JSON-RPC/MCP atomic one-shot completion admission for
`complete` and `reply`. Their owner action runs on Rx to validate the token and
select the reply, then transfers owned response data to a Tx command for
serialization and terminal output.
They share the response direction's terminal admission claim with `finish`, so
only one terminal command can be pending or accepted. Duplicate attempts fail
before allocating or posting another command. The claim only reserves that
command; it does not invalidate the call, claim an Rx terminal result or mark
Tx production finished. Preceding sends remain eligible to execute. The terminal
command performs its Tx work without claiming admission again. Apply the same
one-shot rule separately to the client's request `end` command. Release a claim
if command construction or posting fails before submission; once submitted,
processing or failure consumes it. Colocate these shared atomic admission fields
with the token's aligned Tx-related member group, like the pending-send counter.

`Message<T>` exposes a typed object plus optional retained payload storage.
Follow `Zjrpc::ObjectValue`: store ordinary objects by value and `ZmObject`
types by `ZmRef<T>`. `Message<T>` carries that value and optional backing;
`call`, `send` and `reply` accept it by value, with construction from the
corresponding object value for ordinary submissions. `Reply<T>` carries the
same message representation. Move it into the owning command before posting,
then retain it through Tx serialization. This transfers the supplied value or
reference, without deep-copying borrowed fields. Retained backing keeps nested
views alive; application-owned external views must outlive serialization, as
in Zrest/Zjrpc. A refused submission creates no asynchronous ownership obligation,
but need not restore a value already moved into its by-value argument.
The callback may move the message into deferred work. A borrowed object detached
from its message remains valid only while the backing storage lives. Owning
object types can release encoded storage immediately after decode. Retention
policy is explicit (`ReqBorrowed`/`ResBorrowed`), following `Zjrpc::Request::Borrowed`.
The declaration covers nested values as well as root fields.
Use the actual native field types to determine retention: borrowed spans need
their backing payload, including spans nested inside owning containers.
Zpb does not supply a retained-message
wrapper; `Message<T>` and its buffer retention belong to Zgrpc.

Unary `Reply<T>` carries an optional typed response, status and trailers. It is
the value submitted by a server completion, distinct from the retained token.
Unary client completion
provides `Result<T>`: terminal status, optional response,
initial/trailing metadata and transport diagnostics. It publishes a successful
response only after final status and cardinality checks. Streaming observers
receive messages as they arrive and then one `finished(Status)`; already delivered
messages are not retracted by a later error.

Follow the existing deferred-work model: the endpoint retains the active work
and its token. Dropping the application's last call reference does not finish
the RPC or synthesize an error; it remains active until explicit completion,
cancellation, deadline or transport teardown. Catch application exceptions at
dispatch boundaries, following Zjrpc/Zmcp's internal-error response pattern:
finish with INTERNAL while a valid terminal section can still be produced;
otherwise use the call failure path. Contain exceptions from terminal and
cancellation callbacks. Cancellation invalidates the call and notifies the
application once; application workers own their cleanup and retained data.
Terminal notification and endpoint drain do not wait for arbitrary worker
cleanup. Late token operations fail safely.

### State and cardinality

Track Rx and Tx directional states separately. Rx progresses through headers,
messages, peer end and retirement; Tx progresses through headers, messages,
local end and drain. A separate Rx-owned terminal decision records the first
completion, cancellation, deadline or failure. Half-close is not terminal.

| RPC kind | Request side | Successful response side |
| --- | --- | --- |
| Unary | Exactly one message, then end | Exactly one message, then OK |
| ServerStream | Exactly one message, then end | Zero or more messages, then OK |
| ClientStream | Zero or more messages, then end | Exactly one message, then OK |
| Duplex | Zero or more messages, then end | Zero or more messages, then OK |

A non-OK terminal result may omit the unary response. Extra messages in a
non-streaming direction are protocol errors. A server may reject or finish a
stream before request half-close; arrange bounded request disposal and transport
closure without overwriting an already accepted terminal status. The client
stops its producer through Zhttp on normal terminal response completion or
response failure, rather than on final-initial headers alone.

Rx arbitrates racing terminal events and application notification once. Server
`finish` posts directly to Tx without first claiming the Rx terminal decision.
On Tx, preceding sends run first; the finish command submits terminal trailers
through the retained emitter, then marks response production finished after
terminal admission succeeds. Zhttp orders those trailers after accepted DATA,
including flow-control-stalled output. Later sends fail and repeated finishes
cannot submit another terminal section. Trailer refusal follows the existing
failure path. `reply` submits its response and finish in one Tx command, in that
order, finishing only if response submission succeeds. Tx reports the accepted
finish or failure to Rx for terminal arbitration and notification. Cancellation,
deadline or transport failure can prevent output, but cannot cause a second
application completion; Rx notification does not reorder the Tx commands.
After local client timeout, late responses are discarded. Stream slots remain
until Tx drain and queued events are accounted for; removal is event-driven.

## Wire profile

Use POST to case-sensitive `/package.Service/Method`, `te: trailers`, and
`application/grpc+proto`; accept bare `application/grpc` as protobuf. Each
message has a one-byte compression flag and four-byte big-endian payload length.
HTTP DATA boundaries do not delimit messages. Responses use HTTP 200 and finish
with `grpc-status`, including success; trailers-only errors are supported.
`grpc-message` uses percent encoding. Binary metadata uses `-bin` names and
base64: accept padded/unpadded values and split comma-joined values before
decoding. Reserve `grpc-` keys. `grpc-timeout` has up to eight digits and one
of `H`, `M`, `S`, `m`, `u`, `n`. Compression is per message. The authoritative
wire rules and transport-error mappings are the
[gRPC HTTP/2 protocol](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md)
and its [HTTP/3 mapping](https://github.com/grpc/proposal/blob/master/G2-http3-protocol.md).
Use the same Zgrpc envelope and metadata handling, with the Zpb payload codec,
for both transports.

Invalid base64 in binary metadata is a protocol error, including an invalid
value within a comma-joined field. Fail the call and terminate its HTTP stream
through the existing failure path; do not discard the entry and continue.
Successfully decoded binary metadata remains opaque.

The baseline advertises only identity support. It never passes compressed bytes
to Zpb: unsupported request compression yields UNIMPLEMENTED as the local
diagnostic status and terminates the stream; unsupported response encoding also
fails the call and terminates the stream. A future codec must bound
both encoded and expanded sizes before dispatch. Treat conflicting protocol
metadata, invalid status syntax and unsupported content types as protocol errors
that fail the call and terminate the stream. Duplicate application metadata
remains valid. Test these independently
from HTTP field syntax, which remains Zhttp's responsibility.

`Zgrpc::FrameRx` retains a five-byte prefix accumulator, length, received offset
and optional message buffer. That fixed prefix size is protocol-mandated.
Validate the flag and length before allocating. Handle fragmented prefixes,
fragmented payloads, coalesced messages and zero-length messages. End-of-stream
with a partial prefix or payload is an error. Limit messages and bytes processed
per scheduler turn and post a continuation when buffered work remains.

### Zpb integration and memory traffic

Use the typed codec and storage contracts in [zpb/zpb.md](zpb/zpb.md).
Never enable `Zpb_SizePrefixed<>` for an RPC facet: it produces a varint length
prefix, not a gRPC frame. Check `Zpb::Config<Facet>::SizePrefixed`
at compile time for both request and response facets.

On Tx, allocate a tuned `ZiIOBuf` through `Zhttp::Writer::allocBuf(headRoom)`
with transport headroom in `skip` and construct
`Zpb::IOBuilder{buf, maxSize, tailRoom}`. Its `maxSize` bounds the total allocation,
including `skip` and transport tailroom; its writable capacity excludes both.
Allow five additional writable bytes for the gRPC envelope. Encode once using
`Zpb::save<Facet>(builder, object)`. Check `failed()` rather than testing the
returned span, since a valid empty message has zero payload bytes. Save the
payload length, call `builder.push(5)`, then write the flag and big-endian length
directly into that new prefix. Recheck builder failure before `buf()` detaches
the result. Do not use `builder.fixed32()` for the gRPC length.

Derive `maxSize` from the configured payload limit plus the five-byte envelope,
transport headroom and tailroom, respecting Zpb's `INT_MAX` implementation
ceiling. Check the encoded payload against its own limit before prepending the
envelope. Perform arithmetic in a wider type before narrowing. `buf()` moves
the completed buffer out and adjusts `skip` and `length` to its encoded suffix;
the builder's byte span borrows that buffer. Queue the resulting buffer by
reference through `Writer::operator <<(ZmRef<ZiIOBuf>)`; Zhttp divides it into
DATA frames. There is no sizing serialization pass,
intermediate string, or separate send path bypassing the transport queue.
The generic `Zpb::save(stream, object)` overload is therefore not the RPC Tx path.
Builder growth may move the encoded suffix; account for that cost.

On Rx, a contiguous message can borrow a retained transport buffer slice.
For a fragmented message, incrementally fill one pooled message buffer, consuming
transport fragments as they arrive. This allows credit to advance even when a
message exceeds the transport's stream receive window. Zpb accepts
contiguous spans; the fragmented case necessarily copies. It does not flatten
the whole RPC stream or repeatedly concatenate partial messages.

For each external payload call `Zpb::verify<T, Facet>(bytes, limits)` before
`Zpb::handler<T, Facet>(bytes).ctor()` or `new_(destination)`.
`Zpb::VerifyLimits::maxDepth` bounds nesting depth; gRPC framing separately bounds
message bytes. Handlers do not provide the JSON handler's `valid` contract and
can assert on malformed input. Keep verified
bytes unchanged through materialization. Preserve Zpb's existing repeated,
duplicate, nested merge, unknown-field and default semantics. Application
validation is a distinct hook after decode. Zpb verification checks native
narrowing and required-field presence, but does not enforce application value
domains or reject native null sentinels; `Range` is not a protobuf validation
policy.

Use constructor fields and direct placement construction for new messages.
`load` and `update` retain their documented field filters; a stream of RPC
messages is not an implicit series of updates to one object. Decoding and
re-encoding may discard unknown fields; raw forwarding explicitly retains bytes.

Offer typed `sendRaw<Method>` and receive raw-policy adapters for applications
already holding protobuf bytes. They still enforce framing, size and cardinality.
Outbound locally trusted bytes need no redundant verification; externally
supplied raw bytes must be verified at their ingress boundary or explicitly
handled as opaque forwarding data. Raw mode does not select another wire codec.
Raw receive adapters expose framed opaque payloads, like Zrest's raw body mode;
they do not promise protobuf verification or materialization. Applications
requiring typed validation use the verified typed receive path or explicitly
verify at their ingress boundary.

## Metadata, authentication and status

Build metadata name catalogs on the `ZhttpHeaders`/header catalog machinery.
Fixed and selected names use compile-time matching; a bounded dynamic metadata
collection preserves repeated keys and exposes unknown application fields.
Initial metadata and trailers have distinct storage and callbacks. Reserved
protocol fields cannot be overridden through the application metadata builder.
Copy or retain field storage before a parser callback ends. Sensitive metadata
uses the initial/trailing provider's `neverIndex` policy described above and is
excluded from default diagnostics.

If descriptor metadata defaults and per-call metadata supply the same name in
the same field section, emit both values as separate fields. Do not check for
name collisions at compile time or runtime, deduplicate, or apply override
precedence. Applications are responsible for the metadata they supply.

Authentication and authorization are application policy. Applications use the
ordinary metadata and dispatch hooks; Zgrpc adds no authorization pipeline,
credential provider or pending-authorization queue. Transport credentials remain
with Zhttp/Ztls/Zquic.

The REST signing equivalent is an optional hook over the exact encoded protobuf
payload prior to enqueue, using existing metadata hooks to carry application
headers. Inbound application checks use ordinary receive/dispatch hooks. It must not insert arbitrary prefix/suffix bytes into a
protobuf message. Applications define signatures in declared protobuf fields or
custom metadata. Streaming message authentication needs an explicit application
schema; one initial signature cannot authenticate later unknown messages.
Do not assume protobuf serialization is a canonical signing representation.
The hook observes the exact uncompressed payload before the five-byte envelope;
the baseline supports identity only. Signature fields, self-exclusion rules and
any verification/re-encoding scheme belong to the application. Core hooks do
not insert or rewrite an embedded signature.

`Status` contains a standard numeric code, message and optional owned/retained
binary details. `Code` preserves standard values 0–16, from OK to
UNAUTHENTICATED. Keep HTTP status and native transport error separately for
diagnostics. Suggested library mappings are unknown method → UNIMPLEMENTED,
limit violation → RESOURCE_EXHAUSTED, malformed protobuf/framing or cardinality
→ INTERNAL, cancellation → CANCELLED, deadline → DEADLINE_EXCEEDED, and
connection loss → UNAVAILABLE. Application validation may return INVALID_ARGUMENT;
applications may return UNAUTHENTICATED or PERMISSION_DENIED.
[gRPC status codes](https://grpc.io/docs/guides/status-codes/)

Missing or unusable terminal status is never success. When `grpc-status` is
absent, map HTTP 400 → INTERNAL, 401 → UNAUTHENTICATED, 403 → PERMISSION_DENIED,
404 → UNIMPLEMENTED, 429/502/503/504 → UNAVAILABLE and all other codes → UNKNOWN.
Do not apply this fallback when a usable gRPC status exists.
[HTTP-to-gRPC status mapping](https://grpc.github.io/grpc/core/md_doc_http-grpc-status-mapping.html)

Protocol errors fail the call and terminate the affected HTTP stream through
Zhttp's existing failure/reset path. This includes invalid content type or
encoding, malformed binary metadata, malformed or duplicate `grpc-status`,
unknown status codes, missing terminal status, truncated trailers, malformed
protobuf/framing, cardinality violations and response decoding failure. Do not
attempt recovery trailers or continue dispatching messages after detection.
If HTTP reception has already ended, record failure and stop remaining local
production; no reset of an already retired stream is required. Preserve the
original diagnostics and the documented status mappings, including HTTP fallback
for missing `grpc-status`. Apply the existing terminal arbiter so a later event
cannot replace an application-visible result or notify twice. Stream termination
does not require closing the shared H2/H3 connection. This policy also applies
to Zrest, Zjrpc and Zmcp: detected malformed protocol input terminates its
stream/session without generating an application error response. Intact,
well-formed input with an unrecognized method, tool or application value retains
the protocol's ordinary error response; recognition is separate from integrity.

Keep binary status details opaque; no rich-error type registry is required.

## Shards, calls, limits and shutdown

| Owner | State |
| --- | --- |
| Rx | Method selection, parser, received metadata, message counts, application callbacks, deadline timer, terminal decision and live-call registry |
| Tx | Serializer, outgoing metadata, producer state, message queue and local half-close/trailer state |
| Zhttp | Physical connections, HTTP stream transport state and flow control on their existing owning shards |

Follow REST, JSON-RPC HTTP and MCP HTTP ownership: request dispatch, application
handlers, cancellation callbacks and unary completion selection run on Rx.
Tx owns response serialization, output fields, producers and terminal output.
Do not move handlers onto Tx to simplify response production. Output field/body
providers run on Tx and must not access Rx-owned application state. Transport
cancellation hooks are installed/removed on Tx and post RPC notification to Rx.

Public operations immediately dispatch to trailing-underscore implementations;
assert shard ownership there. Each directional state has private encapsulated
data. Cross-shard commands capture a buffer/action handle and fixed call identity,
not arbitrary strings, messages, or references to application stack storage.
Large objects are moved into heap-identified command nodes before posting.
Use `ZmFn`, intrusive `ZmList`/`ZmHash` nodes, `ZmHeap` for concrete objects and
identified `ZtArray`/`ZtString` storage. Do not wrap Rx or Tx state in locks.

Model call invalidation after `ZjrpcCompletion.hh`, without inheriting its IDs or
reply model. `ClientCall<Method>` and `ServerCall<Method>` contain immutable
scheduler routing, call identity and generation. Each Rx/Tx destination has its
own atomically invalidatable raw owner pointer; sends, end and streaming finish
go directly to Tx, while unary complete/reply, cancellation and application
callbacks go to Rx. A selected unary reply transfers owning output to Tx.
Off-shard code
only tests liveness and posts token-retaining actions. Only the destination
shard loads and dereferences its owner after checking generation and liveness
again. Invalidate each route on its owning shard before releasing that owner;
queued actions that observe invalidation discard their payloads. Use the
framework atomic operations as in the reference completion tokens; atomic
liveness is an admission hint, while owner-shard serialization and teardown
ordering protect dereferences.
An application-held call object may outlive its active RPC, but cannot keep a
link alive. The application must keep the scheduler alive until callers stop using call objects;
shutdown stops ingress, invalidates routes and drains admitted owner work before
owner destruction. A submission racing stop is either refused or admitted and
then processed/discarded on its destination. An externally retained invalid
token does not delay `stop(done)` or retain an endpoint. The application must
quiesce worker submissions before destroying the scheduler.

`Limits` uses the shared positive `maxPending`, `maxQueue`, `maxQueueBytes` and
`workBatch` names. `maxPending` bounds client ingress/registered calls and
admitted server work separately; queue count and bytes bound serialized
outgoing messages on Tx. `maxQueue` also separately bounds each call's pending
off-shard send posts by count; `maxQueueBytes` applies after serialization, not
to unencoded command objects. A call admitted to ingress may
later fail registration, as in Zrest. Per-message bytes have their own gRPC
limit. Configure HTTP `bodyMax(uint64_t(-1))` for long-lived RPC streams;
HTTP queued-input retention and transport-output limits remain finite and
separate from that cumulative bound and the gRPC message limit.

Use the existing defaults: `maxPending = 4096`, `maxQueue = 1024`,
`maxQueueBytes = 64U << 20` and `workBatch = 64`. The protobuf counterpart to
Zjrpc's `maxJSONBytes` is `maxMessageBytes = 16U << 20`, applied to each payload
before allocation/encoding. Endpoint configuration snapshots these positive
limits at initialization; invalid zero limits reject initialization. Following
Zrest's `BodyLimit` convention, `Method::MessageLimit` defaults to zero to inherit
the endpoint message limit; a nonzero value overrides it for that method's
request and response payloads. There is no per-call limit overlay. HTTP limits
remain independent and must also permit the selected message size.

Implement send-post admission in Zgrpc's `send`, only when dispatch to Tx is
needed. Each call token holds a `ZmAtomic<unsigned>` pending-send counter and
an immutable `maxQueue` snapshot. Colocate the counter with the token's other
Tx-related member data, beginning that group with `alignas(Zm::CacheLineSize)`
and separating it from Rx-owned data, per GUIDELINES.md's sharded member layout
rules. The counter alone is shared atomic admission accounting; it does not
change ownership of the surrounding Tx state. Increment before allocating/posting
the owning command. If the incremented count exceeds `maxQueue`, decrement it
immediately and return false. Otherwise post the command and return true. Use `<= maxQueue`
for acceptance so the configured maximum is usable in full.

The posted lambda retains the call token, decrements its counter as its first
action, then checks owner/generation/liveness and invokes `send_` if still live.
It decrements even when invalidation causes the send to be discarded. Shutdown
drains accepted posts before destroying owners; the retained token keeps the
counter alive. Calls already on Tx invoke `send_` directly without changing the
pending-post count. Tx continues to enforce its serialized message-count and
byte limits independently. This atomic counter is admission accounting, not
permission to access Tx-owned state off shard; it adds no locks, byte
reservations or producer-credit API.

Retain only the existing controls applicable to RPC messages:

| Control | Existing equivalent |
| --- | --- |
| Message bytes | `Zrest::Request::BodyLimit`, `Zjrpc::Limits::maxJSONBytes` |
| Pending calls, including off-shard call ingress | `Zjrpc::Limits::maxPending` and `ZjrpcPending.hh` |
| Pending off-shard send posts per call | Atomic count in Zgrpc `send`, using `Limits::maxQueue`; decrement when the posted lambda starts |
| Queued output messages and bytes | `Zrest::Limits::maxQueue` / `maxQueueBytes` for SSE and corresponding `Zjrpc::Limits` controls |
| Work per scheduler turn | `Zjrpc::Limits::workBatch` |

These are implementation limits, not gRPC protocol mandates. Reuse their
configuration and rejection patterns rather than adding hierarchical budget
reservations, retained-object accounting or producer-credit APIs. Preserve
Zpb's nesting verification limit and Zhttp's transport configuration.

Zhttp owns H2/H3 flow control and transport buffering. Zgrpc consumes fragments
through the existing receive path and submits output through the existing Tx
path. Incremental message assembly must continue to consume fragments when a
message is larger than a stream window. Queue refusal is a failure under the
existing send contract; this design adds no blocked/writable/resume API or rate
limiter. Application-held decoded messages follow ordinary buffer ownership,
without a separate budget handle or release-accounting callback.
Follow the Zrest SSE/Zjrpc failure model for admitted output: encoding failure,
serialized-queue refusal or writer failure fails the call and aborts its HTTP
stream through the existing failure path. Previously sent messages cannot be
retracted; do not attempt a replacement success or recovery trailer section.
Local pending-post refusal still returns false without failing the call. Preserve
the original failure diagnostics and notify the application at most once.

Align deadline semantics with grpc-go, using ZmScheduler timers and the existing
Rx/Tx ownership model. Keep one monotonic absolute expiry on the initiating
side, starting at call submission, so pool wait counts. Compute the remaining
duration when constructing request headers; if it is nonpositive, fail locally
with DEADLINE_EXCEEDED without submitting the request. Propagation uses the
remaining duration, never a fresh copy of the original timeout. No timeout is
the default unless configured.

Follow grpc-go's `EncodeDuration`: choose the finest unit in `n`, `u`, `m`, `S`,
`M`, `H` whose upward-rounded value fits eight digits (at most 99,999,999).
Use quotient/remainder arithmetic to avoid overflow. The standalone conversion
of a nonpositive duration is `0n`; an already expired client call takes the local
failure path above. Match Go's signed 64-bit nanosecond duration range, clamping
larger durations to `INT64_MAX` nanoseconds, including overflow when decoding an
incoming hours value. Rounding the wire duration never extends the client's
own absolute expiry.
[grpc-go duration encoding](https://github.com/grpc/grpc-go/blob/master/internal/grpcutil/encode_duration.go),
[timeout decoding](https://github.com/grpc/grpc-go/blob/master/internal/transport/http_util.go).

On the server, install the deadline on Rx when initial request headers are
accepted, before application dispatch and without waiting for request DATA or
half-close. The expiry is that local monotonic time plus the decoded duration;
an incoming zero timeout is immediately expired. Malformed timeout syntax is
a protocol error under Q013. At expiry, the Rx terminal arbiter records
DEADLINE_EXCEEDED if still active, invalidates call operations and notifies the
application to stop work. Cancel the stream through Zhttp, posting Tx cleanup;
notification does not wait for Tx drain, wire reset transmission or application
worker cleanup. Keep owners and stream resources until their existing drains
finish, and suppress duplicate terminal notification. Apply these semantics to
both H2 and H3.
[grpc-go server deadline installation and cancellation](https://github.com/grpc/grpc-go/blob/master/internal/transport/http2_server.go),
[client context completion](https://github.com/grpc/grpc-go/blob/master/stream.go).

Each active timed call embeds a `ZmScheduler::Timer`. Cancel it before destroying
the call, then drain late callbacks on its owning shard. Cancellation cannot
undo application side effects. Transparent retries are disabled initially,
using `replayable() == false`, `maxRetries(0)` and `maxRedirects(0)` under the
completed Zhttp replay gate. Reconnecting a pool serves new calls only. A future retry policy must distinguish provably unprocessed attempts
from possibly executed calls and require explicit replayable message ownership;
an idempotent flag alone cannot replay an arbitrary streaming producer.

Stop follows the existing endpoint lifecycle and Zhttp close/drain behavior.
Stop ingress and timers; drain Rx and invalidate call objects; drain Tx,
producers and emitters; then release ownership. Use the framework's three-phase
asynchronous teardown, never block an I/O thread. Remove finished entries after
their necessary drain events. No new grace-period policy is required.

## Protobuf schemas

Applications supply Zpb message types/facets and method descriptors matching
the peer's protobuf schema. Runtime dispatch uses native `ZfStruct` PB mappings
and method descriptors. The [zpb module](zpb/README.md) provides
`Zpb::Reflect::exportSchema` in `<zlib/ZpbReflect.hh>` for native message schemas.
Reflection also links through `libZpb`; it is not a runtime dispatch dependency.
Service-schema export and service generation are outside this library's
requirements. Interoperability fixtures supply matching `.proto` service
declarations directly.
Native schema export takes an explicit file name, package and
`Zpb::Reflect::Syntax::Proto2` or `Proto3`; the C++ mapping carries no source
syntax. Follow [Zpb's support matrix](zpb/zpb.md#schema-and-wire-boundary) for
presence and enum semantics. Framework-specific `Zpb::Values`
mappings retain their declared message schemas rather than becoming aliases for
well-known protobuf types.

## Files, build and delivery

| File | Responsibility |
| --- | --- |
| `zgrpc/src/ZgrpcLib.hh`, `ZgrpcLib.cc` | Visibility/version definitions and library initialization conventions |
| `zgrpc/src/Zgrpc.hh` | Method contracts, kinds, statuses, limits and catalog macros |
| `zgrpc/src/ZgrpcFrame.hh`, `ZgrpcFrame.cc` | Incremental envelope receive and pooled transmit framing |
| `zgrpc/src/ZgrpcMeta.hh`, `ZgrpcMeta.cc` | Metadata, timeout and status wire utilities |
| `zgrpc/src/ZgrpcCall.hh`, `ZgrpcCall.cc` | ClientCall/ServerCall objects, terminal arbitration and common call lifecycle |
| `zgrpc/src/ZgrpcClient.hh`, `ZgrpcServer.hh` | CRTP adapters and complete endpoints |

Add module `Makefile.am` files, configure outputs, top-level traversal and
installation/pkg-config metadata following adjacent modules. Production depends
on `libZhttp` and `libZpb` (including their existing Zi/Zf dependencies).
Use `zpb/src` for source-tree protobuf includes and `zpb/src/libZpb.la` for
linkage, following [zpb/src/Makefile.am](zpb/src/Makefile.am); build `zpb/src`
before its Zgrpc dependents. Canonical protobuf/gRPC
tools belong only in explicit `interop` targets. `src` precedes `test`, `util`,
`itest` and remaining directories in dependency order. Examples and harness
programs use lowercase names; library and unit-test basenames use `Zgrpc`.

Delivery sequence:

The completed Zhttp precursor is the entry condition for this sequence; its
transport tests have already qualified the contracts above. Each Zgrpc milestone
still verifies its own use of those contracts on both H2 and H3.
The first supported release requires all three milestones and all four RPC
kinds on H2 and H3; the unary milestone is an intermediate development gate.

1. Implement catalogs, framing, metadata, unary adapters, status and protobuf
   verification against the completed transport surface. Interoperate in both
   client/server directions over H2 and H3 before adding convenience APIs.
2. Add deferred calls, cancellation, deadlines and full shutdown race coverage;
   enable server-streaming using the retained response emitter and trailers.
3. Add client-streaming and duplex using the available multi-turn request emitter
   and half-close. Verify RPC queue limits, early final status preservation and
   flow control under stalled peers. No additional client transport phase is
   required.

### Verification and acceptance

Unit TAP tests in `zgrpc/test` cover every prefix split, zero-length payloads,
multiple messages, malformed/truncated payloads, over-limit lengths before
allocation, builder failures, metadata duplicates/binary values, status fallback,
cardinality, catalog rejection and PB facet rules. Include constructor-only
fields, nested borrowed values, unknown fields, repeated/map/oneof payloads and
stable field IDs. Frame tests must not assume DATA frame boundaries.

Integration TAP tests in `zgrpc/itest` run native endpoints for all four kinds,
simultaneous streams, early terminal replies, existing call/queue limits,
timer/cancel/finish races, off-shard call operations after invalidation,
shutdown with queued producers, and partial disconnects. Use scheduler barriers
and `ZmBlock`/`ZmSemaphore` for deterministic coordination. Exercise messages
larger than the stream receive window and one transport-stalled stream beside an active
stream, on both H2 and H3. Cover transport selection with H1 excluded, H3
discovery, GOAWAY, and directional cancellation after an early final response.
Verify adapter configuration and use of the precursor contracts: no replay on
unprocessed/redirect/transport-transition paths, never-index runtime metadata in
both field sections, total stream bytes exceeding finite input retention, and
retained emitter close with queued sends or terminal trailers. HTTP contract
regressions remain in Zhttp; these tests establish Zgrpc's integration with them.

Hold Tx at a scheduler barrier while concurrent workers submit sends. Verify
that at most `maxQueue` posts per call are admitted, refused sends post nothing,
and processing or invalidation decrements every admitted post exactly once.
After releasing the barrier, verify that further sends can be admitted and
that serialized output limits still apply independently.

Interop tests in `zgrpc/interop` use an independent canonical gRPC peer in both
directions over both H2 and H3 with the same `.proto`: unary/streaming, trailers-only errors, empty
messages, deadlines, cancellation, binary metadata and concurrent calls. Initially
use a small checked-in schema fixture. Test uncompressed messages and rejection
of unsupported compression.
Choose peers with actual support for each transport, such as grpc-dotnet for
H3; an H2-only peer cannot qualify the H3 path. The H3 mapping records
[grpc-dotnet implementation](https://github.com/grpc/proposal/blob/master/G2-http3-protocol.md).

Build with top-level `make -j4`, then relevant module `make test` targets after
their `all` targets. Verify gcc/clang on supported Linux/MinGW configurations,
clang ASan/LSan and Valgrind; run instrumented source-tree programs through
`libtool exec`. Document exact toolchains and any untested platform. Acceptance
requires bounded long-running memory, deterministic teardown, no duplicate
terminal callbacks, and actual independent-peer interoperability.

## Open questions and ambiguities

IDs in this register are permanent. Assign new entries the next unused number;
never renumber, reuse or delete an ID. Keep resolved entries in place, change
their status to **Resolved**, and record the decision and affected design
section. Superseded entries retain their ID and link to their replacement.
The entries below identify unresolved details; they do not override decisions
already stated above. Entries are **Open** unless explicitly marked **Resolved**.
Entries retired by scope reduction retain their IDs and a short decision record.

### Q001 — Public Zhttp stream boundary

**Resolved — completed precursor contract.** Use the public `BodyEmitter`:
Tx-only `trailers(B &fields)` terminates streamed responses, retained client
production uses `WriteOutcome::Stream` / `End`, and `ReqBuilder::close()` releases
application emitter state after invalidation. Existing result/completion and
retirement callbacks remain authoritative. Both CRTP endpoints and typed adapters
use the installed surface. See “Completed Zhttp precursor and adapter contract.”

### Q002 — Initial headers, trailers and completion ordering

**Resolved — transport boundary delegated to the completed precursor.**
Section-tagged fields precede their accepted-section boundary; normal
`complete(channel, true)` establishes receive end, including delayed H3 FIN.
Use `headersDone` only if supplied by the completed precursor; otherwise use
channel-aware `bodyInfo` for the accepted initial section. Retain borrowed
metadata before callback return and publish usable terminal status only after
normal completion and RPC checks. No parallel terminal callback is required.
Application completion precedes RPC call retirement. Deferred receive work owns
its data across HTTP cleanup; the RPC call cannot retire until that work and
its terminal callback finish. Transport retirement does not gate notification;
retirement only releases resources after the necessary drains and never repeats
notification.
RPC error precedence remains in Q013. See “Completed Zhttp precursor and adapter
contract” and “State and cardinality.”

### Q003 — Off-shard admission and bounded command submission

**Resolved — atomic send-post admission.** Zgrpc `send` increments a per-call
atomic counter before posting to Tx, rolls it back and returns false above
`maxQueue`, and otherwise returns true after posting. The lambda retains the
call token and decrements first, including when invalidation discards the send.
On-Tx sends bypass this counter. Serialized output remains separately bounded
by `maxQueue` and `maxQueueBytes`; unencoded posts have a count bound only.
Keep the existing pending-call ingress limit. No producer-credit or mailbox
reservation subsystem is required. See “Calls and application dispatch” and
“Shards, calls, limits and shutdown.”

Terminal submissions use REST's one-shot atomic claim instead of the send-post
counter: `complete`, `reply` and `finish` share one response-terminal claim;
request `end` has a separate claim. Repeated terminal commands cannot accumulate
while Tx is stalled. Claims do not invalidate preceding sends or replace Tx's
production state.

### Q004 — Admission callback and readiness routes

**Resolved — removed.** The additional admission callback, caller-selected
notification route and writable/resume APIs are outside scope. Use the existing
queued-send and call-failure model. See “Calls and application dispatch.”

### Q005 — Ordering finish against pending sends

**Resolved — one-shot admission and Tx queue order.** Atomically claim terminal
command admission before allocating/posting, following REST completion. Post
`finish` directly to Tx behind preceding sends. Its command submits terminal
trailers, then marks response production finished after terminal admission
succeeds; subsequent sends fail. Zhttp keeps
trailers behind accepted DATA. `reply` submits its response followed by finish
within one Tx command, using the same claim as `complete` and `finish`.
Duplicate terminal submissions fail before posting. The admission claim changes
neither call liveness nor Tx's finished state. No prior Rx terminal claim may
invalidate preceding sends. Rx separately arbitrates cancellation, deadlines,
failures and terminal notification once. See “Calls and application dispatch” and “State and
cardinality.”

### Q006 — Early server completion and unread requests

**Resolved — completed precursor contract.** The native response-finish path
stops request delivery and disposes unread bytes with receive-credit accounting.
H2 request-stop signaling follows terminal response output; H3 stops request
reception independently. Client normal response completion or failure invalidates
its producer on Tx, preserving a completed response through local send
cancellation. Existing drains retain slots until both owners finish. Zgrpc
retains the early server route and adds no directional-stop API. See “Completed
Zhttp precursor and adapter contract” and “State and cardinality.”

### Q007 — Retained messages and aggregate memory accounting

**Resolved — removed.** No aggregate accounting of application-retained messages
or budget-handle lifetime is required. Retain backing buffers using the ordinary
borrowed-data contract. See “Shards, calls, limits and shutdown.”

### Q008 — Receive credit and assembly reservations

**Resolved — scope reduced.** H2/H3 credit remains owned by Zhttp. No Zgrpc
reservation scheme or manual receive-credit API is required. Verify incremental
assembly through the existing receive path for messages larger than a stream
window. See “Zpb integration and memory traffic.”

### Q009 — Call invalidation and owner destruction

**Resolved — shared HTTP ownership and completion-token teardown model.**
As in REST/JSON-RPC/MCP HTTP, dispatch handlers and unary completion selection
on Rx, then transfer owned response data to Tx for serialization/output.
Streaming send/finish commands target Tx directly. Capture
immutable routes and owning token/action handles; dereference raw owners only
on their destination shard after rechecking invalidation and generation. Keep
separate Rx/Tx routes, invalidate them on their owners, and drain owner work
before destruction. Stop races yield refusal or safe processing/discard.
Applications quiesce token calls before scheduler destruction; retained tokens
do not retain endpoints. See “Shards, calls, limits and shutdown.”

### Q010 — Outgoing object and borrowed-field ownership

**Resolved — Zjrpc object values and explicit backing retention.** `Message<T>`
stores a value or `ZmRef<T>` according to the existing `ObjectValue` convention,
plus optional retained backing. Submission moves it into the owning command
before posting and retains it through serialization. External borrowed fields
remain the application's lifetime obligation; no implicit deep copy is added.
See “Calls and application dispatch.”

### Q011 — Handler abandonment, exceptions and cancellation

**Resolved — existing deferred completion behavior.** Endpoint work retains its
token; dropping an application reference does not finish the call. Dispatch
exceptions follow Zjrpc/Zmcp's internal-error pattern where a response remains
possible, otherwise fail the call. Contain callback exceptions. Cancellation
invalidates the token and notifies once without waiting for application worker
cleanup. See “Calls and application dispatch.”

### Q012 — Metadata storage and duplicate policy

How are selected typed fields and repeated dynamic metadata represented without
duplicate ownership or repeated name lookup? Define cardinality rules for
reserved fields and whether unknown application fields are retained by default
or only on request. Malformed binary metadata follows the protocol-error stream
termination policy in Q013. Cross-source
name collisions between descriptor defaults and per-call metadata are settled:
emit both fields without collision checks or override precedence.
See “Metadata, authentication and status.”

### Q013 — Exact error and terminal-status policy

**Resolved — terminate the stream on protocol error.** Invalid content type or
encoding, malformed binary metadata, malformed/duplicate/unknown or missing
terminal status, truncated trailers and protobuf/framing/cardinality/decode
failures fail the call and terminate its stream through the existing Zhttp
failure/reset path. Do not discard malformed metadata and continue, attempt
recovery trailers, generate application error responses or close unrelated
streams. Intact unknown methods/tools and application validation failures retain
ordinary error responses in Zrest, Zjrpc and Zmcp. Preserve the original diagnostics,
the documented status mappings and one application-visible terminal result.
See “Wire profile” and “Metadata, authentication and status.”

### Q014 — Limits, defaults and precedence

**Resolved — existing limit defaults and descriptor override convention.** Use
4096 pending calls, 1024 queue entries/posts, 64 MiB serialized queue bytes,
64 work items per turn and 16 MiB per protobuf message. Snapshot positive
endpoint limits at initialization; a nonzero method `MessageLimit` overrides
the endpoint message limit, following Zrest `BodyLimit`. HTTP limits remain
independent. See “Shards, calls, limits and shutdown.”

### Q015 — Deadline installation and completion

**Resolved — align with grpc-go.** Install the server deadline at accepted
initial headers before dispatch. Encode remaining client time using upward
rounding and the finest unit that fits eight digits, with Go's signed 64-bit
nanosecond duration saturation. Expired clients fail before request submission.
Expiry notifies on Rx and cancels the stream without waiting for Tx/resource
drain or application cleanup. See “Shards, calls, limits and shutdown” for the
grpc-go source references and exact conversion rules.

### Q016 — Transmit buffer adoption and partial failure

**Resolved — existing output failure path.** Buffer adoption uses the completed
precursor. Encoding failure, serialized-queue refusal and writer failure fail
the call and abort its stream, following Zrest SSE/Zjrpc; there is no rollback
of prior output or recovery trailer attempt. Pending-post refusal alone returns
false without failing the call. Preserve original diagnostics and notify once.
See “Zpb integration and memory traffic” and “Shards, calls, limits and shutdown.”

### Q017 — Zpb verification cost and materialization bounds

**Resolved — scope reduced.** Use Zpb's `VerifyLimits` for nesting depth and
Zgrpc's frame limit for message bytes before materialization. Additional
element/work budgets or codec changes are not requirements of this design.
See “Zpb integration and memory traffic.”

### Q018 — Schema identity across facets and files

**Resolved — removed.** Service-schema export and new generator modes are outside
scope. Applications supply matching method descriptors and protobuf schemas.
See “Protobuf schemas.”

### Q019 — Raw forwarding and signature boundaries

**Resolved — raw-body and application signing precedent.** Raw receive exposes
framed opaque payloads without typed verification; typed reception verifies
before materialization. Signing hooks observe exact uncompressed payload bytes
before the envelope. Embedded signatures and self-exclusion rules remain
application-owned. See “Zpb integration and memory traffic” and “Metadata,
authentication and status.”

### Q020 — Delivery gates and interoperability coverage

**Resolved — complete baseline release.** All three milestones, all four RPC
kinds and independent-peer qualification on H2 and H3 are required. Unary is an
intermediate gate. Use the canonical peers described in “Verification and
acceptance,” including grpc-dotnet for H3, with the checked-in schema and the
specified streaming, deadline, cancellation and concurrency matrix.
