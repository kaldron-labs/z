# Zgrpc: native gRPC over Zhttp with Zpb

Status: proposed design, based on the current working tree. No library code is
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
The following observations describe existing code, rather than proposed APIs.

| Source | Existing facility | Zgrpc treatment |
| --- | --- | --- |
| [Zrest.hh](zrest/src/Zrest.hh) | Request/response policies, merged header catalogs, compile-time request lists, exact/prefix routing, status-selected response types | Method descriptors and merged metadata catalogs; exact service/method dispatch; one response message type and separate terminal status |
| [ZrestClient.hh](zrest/src/ZrestClient.hh), [ZrestServer.hh](zrest/src/ZrestServer.hh) | `Client<App, Catalog>` and `Server<App, Catalog, Decoder>` compose directly with Zhttp's client/server CRTP endpoints; `call` admits bounded work, `Completion` defers replies, and SSE supports incremental events | Complete Zgrpc endpoints compose directly with Zhttp's symmetric CRTP endpoints; preserve the shared admission/completion/queue/lifecycle pattern while supporting four gRPC cardinalities |
| [ZjrpcClient.hh](zjrpc/src/ZjrpcClient.hh), [ZjrpcServer.hh](zjrpc/src/ZjrpcServer.hh) | `HTTPClient<Impl, Catalog>` and `HTTPServer<Impl, Catalog>` are Zhttp CRTP implementations with bounded pending calls, deferred completion, fixed JSON and SSE responses | Use the same endpoint and configuration vocabulary; select protobuf decoding after method lookup and retain borrowed message storage explicitly |
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

`Zhttp::Client<Impl, Pool>` and `Zhttp::Server<Impl, ReqParser, ResQ>` are the
application-facing CRTP coordinators. Each derived endpoint supplies its own
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

### Required Zhttp work

Zhttp already supports incremental body reception, section-tagged receive
fields, streaming transmission and retained multi-turn server producers with
close/invalidation. Client application writers remain single-turn. Reuse those
facilities; [zhttp.md](zhttp.md) specifies the focused additions and checks.

The principal unary transport addition is outbound response trailers: queue
final fields after all accepted DATA and finish with that section. Verify
trailers-only errors through the existing no-body response path. Existing
receive field and completion callbacks should carry metadata and terminal
status; add a section-completion notification only if the adapter demonstrates
that the current sequence lacks a required boundary. Reuse existing transport
results, extending only missing facts needed for status mapping.

Server-streaming reuses the existing retained response emitter. Client-streaming
and duplex additionally require multi-turn ordinary request production and
request half-close, following the server producer's established lifetime rules.
Verify early final replies, request disposal and producer shutdown through
existing routing and drain paths; repair gaps without discarding a completed
response status. No shared producer rewrite or general terminal-event subsystem
is a prerequisite.

Validate the existing ownership, pooled-buffer adoption, limits, sensitivity and
no-replay paths alongside adapter development. Preserve the performance and
policy requirements below, adding transport APIs only where existing facilities
cannot meet them. A gRPC call remains an ordinary POST, without Extended CONNECT.
Keep H2/H3 implementation headers private and qualify the required behavior on
both transports. Propagate necessary API changes through affected dependents
without compatibility forwarders.

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

The complete `Client<App, Catalog>` and `Server<App, Catalog>` endpoints derive
from `Zhttp::Client<Client<App, Catalog>, Pool<App, Catalog>>` and
`Zhttp::Server<Server<App, Catalog>, Parser<Server<App, Catalog>, Catalog>,
ResQ<Catalog>>`, respectively. Define complete pool, parser and queue utility
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
after retirement. A no-argument `failed()` is supported as in Zrest. Pool
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
| `send(message)` | Submit one typed output through the existing queued-send model; submission is not delivery |
| `send_(message)` | Tx-only submission with immediate success/failure, following Zjrpc queue rejection |
| `end()` | Client request half-close, ordered after accepted messages; responses remain receivable |
| `finish(status, trailers)` | Server terminal result, ordered after accepted responses |
| `complete(Reply<Response>)` | Unary server completion token operation; submits the typed response, status and trailers once |
| `reply(response, status)` | Unary server convenience: one response followed by finish; non-OK may omit the response |
| `cancel()` | Request cancellation; idempotent after termination |

Off-shard methods submit owning command handles. They do not return a pretend
synchronous queue result from `rxInvoke` or `txInvoke`. Asynchronous send failures
reach the owning call's failure path, as in the existing adapters. Concurrent
producers have destination-queue order; callers needing a specific order must
serialize submissions. `end` and `finish` prevent subsequent sends.

`Message<T>` exposes a typed object plus optional retained payload storage.
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
stops its producer when it receives a terminal response.

Rx arbitrates racing terminal events once. A server finish first claims the
terminal decision on Rx, then posts its ordered Tx finish command; a later reset
may prevent transmission but cannot cause a second application completion.
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

The baseline advertises only identity support. It never passes compressed bytes
to Zpb: reject unsupported request compression with UNIMPLEMENTED and expose an
unsupported response encoding as a local call failure. A future codec must bound
both encoded and expanded sizes before dispatch. Treat conflicting protocol
metadata, invalid status syntax and unsupported content types as explicit parser
outcomes. Duplicate application metadata remains valid. Test these independently
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

On Tx, allocate a tuned `ZiIOBuf` with transport headroom in `skip` and construct
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
reference; Zhttp divides it into DATA frames. There is no sizing serialization pass,
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

## Metadata, authentication and status

Build metadata name catalogs on the `ZhttpHeaders`/header catalog machinery.
Fixed and selected names use compile-time matching; a bounded dynamic metadata
collection preserves repeated keys and exposes unknown application fields.
Initial metadata and trailers have distinct storage and callbacks. Reserved
protocol fields cannot be overridden through the application metadata builder.
Copy or retain field storage before a parser callback ends. Sensitive metadata
uses Zhttp's never-index facilities and is excluded from default diagnostics.

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

Keep binary status details opaque; no rich-error type registry is required.

## Shards, calls, limits and shutdown

| Owner | State |
| --- | --- |
| Rx | Method selection, parser, received metadata, message counts, application callbacks, deadline timer, terminal decision and live-call registry |
| Tx | Serializer, outgoing metadata, producer state, message queue and local half-close/trailer state |
| Zhttp | Physical connections, HTTP stream transport state and flow control on their existing owning shards |

Public operations immediately dispatch to trailing-underscore implementations;
assert shard ownership there. Each directional state has private encapsulated
data. Cross-shard commands capture a buffer/action handle and fixed call identity,
not arbitrary strings, messages, or references to application stack storage.
Large objects are moved into heap-identified command nodes before posting.
Use `ZmFn`, intrusive `ZmList`/`ZmHash` nodes, `ZmHeap` for concrete objects and
identified `ZtArray`/`ZtString` storage. Do not wrap Rx or Tx state in locks.

Model call invalidation after `ZjrpcCompletion.hh`, without inheriting its IDs or
reply model. `ClientCall<Method>` and `ServerCall<Method>` contain immutable
scheduler routing, call identity and generation, and an atomically invalidatable
raw owner pointer. Off-shard operations only inspect call liveness and post owning actions. Only the designated owner
shard dereferences the owner, after checking generation and liveness again.
An application-held call object may outlive its active RPC, but cannot keep a
link alive. The application must keep the scheduler alive until callers stop using call objects;
shutdown drains all posted actions before owner destruction.

`Limits` uses the shared positive `maxPending`, `maxQueue`, `maxQueueBytes` and
`workBatch` names. `maxPending` bounds client ingress/registered calls and
admitted server work separately; queue count and bytes bound serialized
outgoing messages, including off-shard ingress. A call admitted to ingress may
later fail registration, as in Zrest. Per-message bytes have their own gRPC
limit. HTTP retained-body and transport-output limits remain separate.

Retain only the existing controls applicable to RPC messages:

| Control | Existing equivalent |
| --- | --- |
| Message bytes | `Zrest::Request::BodyLimit`, `Zjrpc::Limits::maxJSONBytes` |
| Pending calls, including off-shard call ingress | `Zjrpc::Limits::maxPending` and `ZjrpcPending.hh` |
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

Deadlines use one monotonic absolute expiry on the initiating side, starting at
call submission, so pool wait counts. Encode remaining time at send; when
propagating a call, subtract elapsed time. A server installs its own expiry
from the received duration. No timeout is the default unless configured.
Expiry cancels work through the terminal arbiter and notifies the application to stop outstanding work.
[gRPC deadlines](https://grpc.io/docs/guides/deadlines/)

Each active timed call embeds a `ZmScheduler::Timer`. Cancel it before destroying
the call, then drain late callbacks on its owning shard. Cancellation cannot
undo application side effects. Transparent retries are disabled initially,
including Zhttp's ordinary replay behavior. Reconnecting a pool serves new
calls only. A future retry policy must distinguish provably unprocessed attempts
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

1. Add and test Zhttp outbound response trailers on H2 and H3. Verify existing
   trailers-only and receive-completion paths, adding only missing notification.
   These focused changes enable the unary adapter on both transports.
2. Implement catalogs, framing, metadata, unary adapters, status and verification.
   Interoperate in both client/server directions over H2 and H3 before adding
   convenience APIs.
3. Add deferred calls, cancellation, deadlines and full shutdown race coverage;
   enable server-streaming.
4. Complete multi-turn client production, client-streaming and duplex; verify
   existing queue-limit behavior and transport flow control under stalled peers.

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

Which existing public builder/link abstraction should expose trailing fields,
multi-turn request production and terminal events? The design names
operations but does not specify their exact types or integration with both
high-level CRTP client/server endpoints and lower-level typed links. The
client/server CRTP signatures and existing server streaming are settled.
Define the trailer extension for unary first; extend client production for
client-streaming and duplex. Reuse existing results and add terminal information
only where missing. Verify equivalent H2/H3 behavior and update affected H1
dependents when a common signature changes.

### Q002 — Initial headers, trailers and completion ordering

What exact callback sequence distinguishes informational headers, ordinary
initial headers, trailers-only responses and final trailers? Define where a
header section becomes accepted, when metadata becomes observable, and how a
terminal status competes with an adjacent reset or transport-completion event.
Start with the existing section-tagged field and HTTP completion callbacks;
add a section-completion hook only if that sequence lacks a required boundary.

### Q003 — Off-shard admission and bounded command submission

**Resolved — scope reduced.** Follow Zjrpc's existing pending-call ingress limit
and queued-output rejection patterns. No new producer-credit or mailbox
reservation subsystem is required. See “Shards, calls, limits and shutdown.”

### Q004 — Admission callback and readiness routes

**Resolved — removed.** The additional admission callback, caller-selected
notification route and writable/resume APIs are outside scope. Use the existing
queued-send and call-failure model. See “Calls and application dispatch.”

### Q005 — Ordering finish against pending sends

How does an Rx-arbitrated server finish establish an ordering boundary against
messages already posted to Tx? Define which sends precede final status, which
late sends fail, and how `reply` orders its message and finish under the existing
queued-send contract.

### Q006 — Early server completion and unread requests

What concrete Zhttp sequence sends a terminal response while stopping an
unfinished request producer? Decide how much request data may be discarded,
when receive credit is returned, and when a stream reset is allowed without
preventing the peer from observing final status. Cover application rejection
and a server finishing a duplex call while the client is still sending.

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

Does each call object route all actions through Rx, or are there separate invalidatable
routes for Rx and Tx? Give the precise ordering and memory-order guarantees
between liveness checks, posting, invalidation and the final drain. Specify the
application's scheduler-lifetime obligation and what happens when a worker
submits concurrently with endpoint stop. An atomic raw pointer alone does not
establish that a queued action may safely dereference an owner.

### Q010 — Outgoing object and borrowed-field ownership

What types do `call`, `send` and `reply` actually accept: values, intrusive
references, `Message<T>`, or a constrained combination? Define how an outgoing
object with borrowed nested fields keeps its backing storage alive until Tx
serialization, and whether submission transfers ownership before or after
encoding. The receive-side retention flags do not answer this outbound question.

### Q011 — Handler abandonment, exceptions and cancellation

What happens when a server handler drops its last `ServerCall` reference without
finishing, throws, or remains active after cancellation? Decide whether call
abandonment produces INTERNAL, leaves the call open until its deadline, or is a
contract violation. Define exception boundaries and the lifetime of application
stream state, including whether terminal notification waits for worker cleanup.

### Q012 — Metadata storage and duplicate policy

How are selected typed fields and repeated dynamic metadata represented without
duplicate ownership or repeated name lookup? Define merge precedence for
descriptor defaults and per-call metadata; cardinality rules for
reserved fields; handling of malformed binary values; and whether unknown
application fields are retained by default or only on request.

### Q013 — Exact error and terminal-status policy

The mapping table is suggested rather than exhaustive. Specify local outcomes
for invalid content type, unsupported response encoding, malformed or duplicate
`grpc-status`, unknown numeric codes, truncated trailers and response decode
failure. Define which errors permit a gRPC reply, which require stream reset,
and how diagnostics preserve the original cause without a later event replacing
the application-visible result.

### Q014 — Limits, defaults and precedence

Which defaults and precedence should the retained message, pending-call and
queue limits use? Follow the equivalent Zrest/Zjrpc settings and document any
necessary differences for protobuf messages; do not add new budget categories.

### Q015 — Deadline installation and completion

When exactly is the server deadline installed during request receipt? Define
rounding and saturation when encoding remaining time and expired calls before
transmission. Specify whether timeout completion waits for Tx drain or reports
immediately while resources retire asynchronously.

### Q016 — Transmit buffer adoption and partial failure

Can the current public Zhttp Tx layer adopt the detached backward-built Zpb
buffer without copying its payload, including its resulting `skip` and required
head/tail room? Identify the required adapter and the behavior when a buffer
spans multiple DATA frames. Define recovery when serialization fails or transport
admission stops after earlier messages have already been transmitted.

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

Does raw receive mode guarantee protobuf verification, or explicitly promise
only framed opaque bytes? Separate those contracts in the API. For signing,
define whether hooks observe compressed or uncompressed bytes and how a
signature stored inside a protobuf message avoids signing itself. Decide which
parts belong in core hooks and which remain application-owned adapters.

### Q020 — Delivery gates and interoperability coverage

Which delivery milestone is considered the first supported Zgrpc release:
unary interoperability or all four RPC kinds? Select independent peers and the
test matrix for both H2 and H3, including TLS, long-running streams and
cancellation races. H3 support remains part of the baseline.
