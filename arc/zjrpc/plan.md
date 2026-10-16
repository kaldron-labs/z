# Factor JSON-RPC wire protocol out of `zmcp`

## Objective and constraints

Create `zjrpc` (`Zjrpc`, `libZjrpc`) as the shared JSON-RPC wire layer for
**stdio, streaming HTTP and WebSockets**. Make `zmcp` use that layer for all
three transports. `zjrpc` owns the **framework for defining/declaring requests
and responses**; `zmcp` owns the **MCP-specific requests and responses**.

This is an extraction with two required additions: generic JSON-RPC batches
and a WebSocket binding. Preserve implemented MCP behavior except the agreed
Q18 inbound duplicate policy. Ordinary JSON-RPC applications must work without
MCP discovery, tools, sessions, metadata or result wrappers.

Apply [GUIDELINES.md](GUIDELINES.md), especially
[Repair procedure](GUIDELINES.md#repair-procedure): establish each invariant,
identify its owner, and choose the smallest implementation. An existing
container or abstraction is evidence of behavior to preserve, not a requirement
to reproduce its implementation. Delete redundant storage and dispatch while
preserving that behavior.

The task-specific build policy below takes precedence over guideline acceptance
steps that would require alternate configurations or a clean rebuild. Retain
the current compiler, flags, platform and existing artifacts throughout.

### Simplifications required by this review

| Earlier prescription | Revised prescription and reason | Applicable guideline |
| --- | --- | --- |
| A new generic peer framework plus client/server wrappers | Extract only the common correlation, completion and typed dispatch actually consumed by the endpoints. A peer is an ownership scope, not a mandatory additional object, virtual interface or registry. | [Static polymorphism and constraints](GUIDELINES.md#static-polymorphism-and-constraints): factor common code without weak abstractions. |
| A fixed list of public headers including `ZjrpcPeer.hh` | Put declarations/codecs in `Zjrpc.hh`, endpoint templates in client/server headers, and shared transport code in transport headers only where it removes duplication or an include cycle. Add no standalone peer header until actual dependencies require it. | [Module layout](GUIDELINES.md#module-layout), [Header layout](GUIDELINES.md#header-layout): acyclic includes and direct dependencies. |
| Preserve every responder hash and cancellation index, then add a live-ID hash | Use one peer live-request `ZmHash` for inbound lookup and cancellation. Replace overlapping cancellation indexes. An ordinary responder has one completion slot, not another ID hash. Keep work enumeration only where needed for teardown, including notifications and detached HTTP work. | [Containers and intrusion](GUIDELINES.md#containers-and-intrusion), [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow): eliminate extra nodes, lookups and repeated work. |
| Batch-specific transport responders and multiple completion lifecycles | A batch is one aggregate request and one completed response message. Its members reuse ordinary dispatch and result encoding; only aggregation state is new. | [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow): factor repeated blocks. |
| A new numeric-ID codec or implicit broadening from `int64_t` | Retain `int64_t` numeric IDs in ordinary decimal integer notation (Q19). Fractional and exponential forms are out of scope. Reuse `ZfJSON` and framework integer comparison/hash; no new numeric representation, parser or normalizer. | [Framework fit](GUIDELINES.md#framework-fit), [Metadata](GUIDELINES.md#metadata), [Repair procedure](GUIDELINES.md#repair-procedure). |
| Generic HTTP routing, negotiation, sessions and WebSocket machinery | Extract JSON/SSE binding code; let `Zhttp` and `Zws` perform their existing jobs. MCP retains its HTTP routing/session policy. | [Framework fit](GUIDELINES.md#framework-fit): do not reimplement lower-level facilities. |
| Independent limits, counters and scratch allocations in each new layer | Keep each existing budget with its actual owner. Use stack scratch with heap fallback for synchronous temporary data; pooled buffers for I/O; heap-owned state only when it must survive dispatch. | [Storage and capacity](GUIDELINES.md#storage-and-capacity), [Heap allocation](GUIDELINES.md#heap-allocation), [I/O and system integration](GUIDELINES.md#io-and-system-integration). |
| Repeated full behavior matrices and copies of moved tests | Move generic tests with their implementation. Test shared wire semantics once and transport-specific framing/lifetime at its boundary. Keep full `zws` profile coverage. | [Repair procedure](GUIDELINES.md#repair-procedure), [Tests](GUIDELINES.md#tests). |

The resolved requirements below remain in force, including the six concrete
endpoint types, full `zws` support, one completion per batch, and the separate
live `ZmHash` / historic `ZmCache`. Simplification removes implementation
duplication around those requirements.

## Extraction boundary and library layout

Use one hybrid `libZjrpc.la`, linked with its required framework dependencies
and `libZhttp`/`libZws`; do not add core-only libraries, transport plugins or
new build switches. Keep `zrest` independent and retain its shared application
request-type demonstration. Dependency direction is `Zhttp`/`Zws` -> `Zjrpc`
-> `Zmcp`; `Zjrpc` never includes or links MCP.

| Existing implementation | Ownership after extraction |
| --- | --- |
| `Zmcp.hh`: `ID`, `IDFmt`, envelope views, JSON-RPC errors, `saveRequest`, `saveNotification`, `saveResult`, `saveError` | `Zjrpc.hh`. Reuse metadata and `ZfJSON`; remove the moved MCP copies. |
| `Request`, `Response`, response lists, declaration traits and compile-time validation | `Zjrpc.hh` or a factored internal header if needed to avoid a cycle. MCP-specific declarations and constraints remain in `Zmcp`. |
| `Envelope<Reqs>`, `Parsed<Reqs>`, `parse` | Split generic envelope classification from MCP typed narrowing. Scan once; pass parsed views into the application decoder. |
| `PendingCalls`, completion tokens and terminal cleanup | Shared endpoint implementation. Keep outbound correlation separate from inbound lookup; eliminate redundant per-responder indexing. |
| Stdio framing, workers, queues, partial I/O and native interruption | Shared `ZjrpcStdio.hh` implementation and `ZjrpcLib.cc`. Preserve handle ownership and pooled I/O. |
| JSON/SSE framing, builders and HTTP output/backpressure | Shared `ZjrpcHTTP.hh` implementation; MCP supplies existing routing, headers, sessions and fixed/SSE selection. |
| WebSocket transport | A small `ZjrpcWS.hh` binding using `Zws` callbacks and Tx streams; no second WebSocket implementation. |
| MCP `Peer`, `ClientPeer`, versions/`Era`, discovery, initialization, tools, schema, annotations, progress/logging, cancellation messages and session policy | Stay in `zmcp`. Extract their generic mechanics rather than moving MCP state machines wholesale. |

`ZjrpcLib.hh` / `ZjrpcLib.cc` contain exports, library identification and
non-template implementation. `ZjrpcClient.hh` / `ZjrpcServer.hh` contain the
application-facing endpoint templates. A transport header exists to share
implementation, not to create another mandatory adapter/manager object.
Keep HTTP/WebSocket declarations out of the core include.

Follow [Module layout](GUIDELINES.md#module-layout),
[Header layout](GUIDELINES.md#header-layout) and
[Code structure](GUIDELINES.md#code-structure): include guards, ordered direct
dependencies, no tail-inclusion cycles, and template-independent declarations
outside enclosing templates. Do not introduce compatibility aliases or
forwarders for the moved API
([General principles](GUIDELINES.md#general-principles),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

## Endpoint and declaration model

Use common CRTP `Server<Derived, Impl, Catalog>` and
`Client<Derived, Impl, Catalog>` bases with concrete:

| Transport | Server | Client |
| --- | --- | --- |
| stdio | `IOServer<Impl, Catalog>` | `IOClient<Impl, Catalog>` |
| streaming HTTP | `HTTPServer<Impl, Catalog>` | `HTTPClient<Impl, Catalog>` |
| WebSockets | `WSServer<Impl, Catalog>` | `WSClient<Impl, Catalog>` |

Mirror current `ZmcpServer.hh`, `ZmcpClient.hh`, `zmcp.cc` and `zmcpd.cc`.
Each concrete endpoint supplies transport-specific `init(...)` and
`start()`/`stop()`/`final()`; clients expose typed `call<Req>` and servers
typed dispatch. Common bases own only genuinely shared state and behavior.
Transport-only fields belong to the concrete type. Do not use runtime mode
switches to select one of these compile-time types.

A derived method may call a same-named base method when it adds logic.
Hoist pure base forwarding with `using`. Do not rename lifecycle methods to
`start_`/`stop_`/`final_` merely to resolve lookup. Reserve trailing underscores
for internal or shard-specific operations. Use established CRTP lookup and
safe optional callback defaults; add neither virtual dispatch nor a large
detector/policy framework.
See [Core language](GUIDELINES.md#core-language),
[Static polymorphism and constraints](GUIDELINES.md#static-polymorphism-and-constraints),
[Bad casting](GUIDELINES.md#bad-casting) and
[Trailing underscores](GUIDELINES.md#trailing-underscores).

A peer means the scope owning correlation/live requests: a stdio channel,
WebSocket connection, or identifiable HTTP application/session context.
HTTP streams are reply routes. Reuse their existing owner/cancel hooks;
introduce no public `CliLink`/`SrvLink`, generic route registry or second
connection API. Keep `IOLink` as an internal I/O implementation component only
where it isolates workers/framing from endpoint behavior. One link per IO
endpoint does not justify a link collection, factory or separate heap allocation;
prefer in-place ownership if its initialization/lifetime contract permits it.

Preserve compile-time request/response declarations using `ZuTypeList`,
existing request/response traits, `ZfStruct` metadata and typed completion.
No runtime method registry or schema interpreter is needed. Use `ZuUnroll`,
`ZuSwitch` and `ZuMatcher` where their framework contracts fit compile-time
dispatch; do not hand-build a method lookup table.
Keep constraints in template parameters using detector traits / `ZuIfT`, with
`ZuAssert` for declaration invariants; no concepts, `requires` or return-type
SFINAE. See [Compile-time data and matching](GUIDELINES.md#compile-time-data-and-matching),
[Metadata](GUIDELINES.md#metadata) and
[Static polymorphism and constraints](GUIDELINES.md#static-polymorphism-and-constraints).

Optional HTTP metadata supplies declared headers and binding choices through
the existing `Zrest`/`Zhttp` patterns. Configure a shared HTTP endpoint/operation
once where all methods use it; do not require identical routing declarations
on every RPC request. Stdio/WebSocket declarations require no HTTP metadata.
Response types never select an HTTP status: JSON-RPC result/error is the
discriminator. MCP structured-result `code` stays application payload data.

## Wire processing, validation and IDs

Use the [JSON-RPC 2.0 specification](https://www.jsonrpc.org/specification)
for envelope shape, absent versus null IDs, named/positional parameters,
arbitrary result values, optional error data and batches.

Scan mutable input once with `ZfJSON::scan`; classify the envelope and decode
typed data from that tree. The scanner's outer node array is not a wire batch:
the current value is `(*root)[0]`. Serialize envelope views and typed payloads
with `ZfJSON::save` / its field codecs. Do not stringify/reparse between
`Zjrpc` and `Zmcp`, copy each JSON subtree, hand-escape strings, concatenate
JSON fields, or write number scanners/formatters.

Validate untrusted input at the boundary responsible for interpreting it:

- The transport enforces its message/framing/admission bounds.
- `ZfJSON` parses JSON; the RPC decoder checks the envelope shape needed for
  dispatch/correlation, version, ID presence/type and result/error exclusivity.
- Metadata loaders decode typed parameters/results once. MCP applies its own
  method, era and ID restrictions after generic classification.
- Internal code relies on those contracts; it does not repeat the same checks,
  scan for unknown fields, enforce canonical JSON spelling, or walk the tree
  again for conformance policing.

Keep mandatory bounds and lifetime checks. Use `ZiAssert` for internal
owner-shard invariants, not as a parser for hostile input. Use type metadata
for compile-time response selection rather than runtime revalidation.
See [No dogma](GUIDELINES.md#no-dogma),
[Repair procedure](GUIDELINES.md#repair-procedure),
[Assertions](GUIDELINES.md#assertions), [Metadata](GUIDELINES.md#metadata)
and [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).

`Zjrpc::ID` / `IDFmt` own request IDs and their codec for all consumers.
Current `Zmcp::ID` uses `ZuUnion<void, Null, int64_t, IDString>`:
`void` is absent, `Null` is explicit JSON null. Use that distinction directly;
do not add `hasID`, `isNotification` or null-state booleans beside it.
Generate local integer/string IDs, checking outstanding outbound collisions
through the existing pending-call index, not a second used-ID set.

**Q19 resolved: retain the current integer representation.** Numeric IDs are
signed 64-bit integers in ordinary decimal integer notation. Fractional
numerical IDs and exponential notation are out of scope. Preserve
`ZuUnion<void, Null, int64_t, IDString>` and use existing framework integer
comparison/hash and `ZfJSON` field codecs. No numeric generalization, token-key
storage, decimal normalization or arbitrary-precision machinery is needed.
Do not silently floor unsupported fractional forms or overflow the supported
integer range. Any necessary rejection belongs at the ID codec boundary,
using framework facilities rather than a second number scanner or repeated
validation downstream.
See [Framework fit](GUIDELINES.md#framework-fit),
[Strings](GUIDELINES.md#strings), [Bad casting](GUIDELINES.md#bad-casting)
and [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

MCP rejects null request IDs and applies its integer/string rules in `zmcp`.
Generic JSON-RPC keeps explicit null distinct from a notification. Retain
standard error-code constants, while keeping MCP's existing `-32000` application
error mapping. Generic batch invalid-entry errors use the ordinary error
encoder. Preserve MCP's existing malformed-input disconnect/failure policy
through compile-time application dispatch; do not incorrectly claim that
disconnect-only behavior implements JSON-RPC's standard batch error replies.
No new retry/recovery service or runtime error-policy object is required.

## Correlation, completion and storage

Outbound calls reuse `PendingCalls`: allocate a distinct outstanding ID,
register before send, remove before callback, fail/remove on owner closure,
ignore unknown/late response IDs. Inbound/outbound ID spaces remain separate.
Do not retain outbound completed IDs or add per-stream pending tables.
Q18 history is for inbound duplicate detection.

An ordinary inbound work/responder has one completion. Replace a responder's
single-entry `CompletionSet` hash with a direct token slot when extracting it;
retain generation/invalidation and exactly-once terminal behavior. Retain a
real set only where multiple independently addressable entries exist. The peer
live index locates requests for cancellation. Cancellation marks the token
and calls the application; it need not terminate the operation.

Use one live inbound `ZmHash` keyed by ID and one historic `ZmCache` per
identifiable peer:

1. Check live requests first, then history; reject either hit before dispatch.
2. Admit work under the existing work budget and register it before application
   code can complete synchronously.
3. On terminal completion/abort, remove live state and record the used ID in
   history on the owner shard before callbacks or reentrant dispatch.
4. Remove finished work immediately; peer closure drains live and historic
   stores. Preserve required live state when an HTTP reply route merely detaches.

The live hash maps ID to work/cancellation and never evicts or imposes an
independent cap. Existing work admission still bounds active work. Reuse or
replace current stdio/session cancellation indexes; do not add a parallel
ID-to-work map. Work registration for teardown covers notifications and
route-detached work too, so do not delete it merely because response-bearing
work also appears in the live index. Combine intrusive memberships where useful
without inventing another owner.

History contains owned IDs only, never work pointers or response payloads.
Use `ZmCache`'s synchronous `find`/`add` and normal LRU eviction with
`ZmNoLock` on its owner shard; do not exercise asynchronous cache-loading APIs,
add a separate hash/list, pin entries, scan for expiry, or modify the cache.
One `Server` configuration setting, `histSize()`, supplies the application-wide
default **1024 entries**, copied into each peer's cache at creation. Define it
once as a named default with its footprint/trade-off comment; use existing
`ZmHashParams` sizing/tuning conventions and document effective capacity.
Do not add peer whitelists, independent peer configuration or an aggregate
process-wide cache budget.

Duplicate requests do not execute, attach to existing work, or replay results.
Use the existing error encoder for an invalid-request rejection on the
duplicate's route; never overwrite the original completion/cancellation
target. Notifications bypass both indexes. Sessionless HTTP has no implicit
shared peer inferred from TCP reuse; cross-stream detection requires an
application-supplied stable peer context.

This bounded history policy is deliberately stronger than JSON-RPC's
correlation requirement. The
[MCP 2025-11-25 rules](https://modelcontextprotocol.io/specification/2025-11-25/basic)
require sender ID non-reuse within a session; the
[MCP 2026-07-28 rules](https://modelcontextprotocol.io/specification/2026-07-28/basic)
only prohibit reuse while awaiting a response. Historic rejection can therefore
reject otherwise valid completed-ID reuse. Eviction also means old collisions
are no longer detected. Preserve this agreed Q18 policy without describing it
as complete session-wide enforcement or application idempotency.

Use intrusive framework nodes for tokens/work in their indexes/queues.
Do not allocate a node containing `ZmRef<Work>` when the work can carry the node.
Use shadow/intrusive memberships for multiple containers; follow `HeapID<"">`
for inner nodes where the outer allocation owns the concrete size. A separate
non-owning index record is justified only by an actual lifetime/layout
constraint. History needs its own ID node after live work has gone.

See [Containers and intrusion](GUIDELINES.md#containers-and-intrusion),
[Storage and capacity](GUIDELINES.md#storage-and-capacity),
[Heap allocation](GUIDELINES.md#heap-allocation),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup) and
[Liveness](GUIDELINES.md#liveness). These rules remove redundant nodes and
lookup tables; they do not merge inbound and outbound semantics.

## Batch requests

A batch is an aggregate request completed once with a batched response.
Keep ordinary requests on the direct dispatch/completion path; allocate
batch state only for an actual array. Reuse the same declaration framework,
member decoder, handler and result/error codec.

Batch-owned state needs member lifetime, response storage and a count of
unfinished response-producing members. Use existing work/token state instead
of parallel completed/failed/cancelled arrays, a member-ID hash, or another
scheduler. Member IDs use the peer's live index. Bound admission by existing
message/work limits, charging member work rather than only the outer message.
Dispatch bounded turns with scheduler continuations; no eager unbounded task
fan-out. Account for members before dispatch so synchronous completion cannot
finish the aggregate while it is still being initialized.

Members may finish in any order; keep collected entries in completion order
instead of sorting or restoring input order. Omit notification replies.
An all-notification batch emits nothing. Empty/invalid batches and invalid
entries follow JSON-RPC's standard error shapes for the generic binding;
MCP retains its own accepted-message restrictions.

Emit one complete response array as one logical message: a JSON HTTP body,
SSE record, WebSocket message or stdio line. Never stream partial response
arrays as independently completed replies. Reuse output queues and byte
budgets; keep one aggregate response store, with framework arrays/buffers and
ordinary encoding, not an extra JSON DOM plus a duplicate serialized copy.

For outbound batches, one aggregate completion receives the complete response.
Reuse pending entries for member-ID correlation with a reference to the
aggregate owner; there is no synthetic batch wire ID or second batch lookup
table. Complete only after the response array is decoded, then invoke the
aggregate callback once. Route closure drains owned member state; cancellation
does not manufacture an additional JSON-RPC batch ID.

See [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
[Liveness](GUIDELINES.md#liveness) and
[Storage and lifetime](GUIDELINES.md#storage-and-lifetime).

## Transport bindings

All bindings deliver complete JSON message units to the shared decoder and
use the same encoder. IDs correlate calls, HTTP stream identity selects a
reply route, and SSE event IDs are framing metadata.

### Stdio

Move the current newline framer, partial read/write logic, bounded Rx/Tx queues,
dedicated blocking `ZiFile` workers, EOF behavior and native interruption.
Preserve owned handle semantics, Linux/MinGW behavior, stdout wire-only traffic
and existing diagnostic routing. The input/output queue owns retained
`ZiIOBuf` references; no temporary heap string or second line-buffer queue.
MCP supplies its notifications and result policy over the shared emission
path. Do not replace working platform normalization with raw POSIX-only I/O.

See [I/O and system integration](GUIDELINES.md#io-and-system-integration),
[Framework fit](GUIDELINES.md#framework-fit) and
[Repair procedure](GUIDELINES.md#repair-procedure).

### Streaming HTTP

Reuse `Zhttp` parsers/builders, profile framing, streams, backpressure and
resume/abort handling. Keep only the JSON-body/SSE binding code in `Zjrpc`.
An HTTP chunk is not a complete JSON message. Assemble the bounded body or SSE
data record once; pass its owned buffer to the shared decoder.

All JSON-RPC HTTP responses use status **200**, for result and error alike.
Canonical defaults are:

- JSON bodies: `Content-Type: application/json`.
- SSE responses: `Content-Type: text/event-stream`, `Cache-Control: no-cache`.
- Requests accept `application/json, text/event-stream`.

Declare these defaults once through the existing header/building machinery.
Apps enrich headers through compile-time declarations, following `zrest`.
Let `Zhttp` derive content length and streaming framing; do not format HTTP
headers, lengths, MIME negotiation or chunking by hand. Link-level settings
follow `Zrest`/`Zhttp`. JSON-RPC itself does not prescribe these binding choices
or a default endpoint.

MCP keeps protocol/session/routing headers, parameter-header policy,
modern/legacy discovery, sessions/expiry, POST/DELETE semantics, fixed/SSE
selection and non-JSON-RPC HTTP control responses. Reuse `ZuBase64` and existing
metadata for MCP header encoding, not another codec.

Preserve the implemented POST response stream with progress/log notifications
and its terminal reply. No standalone GET stream, replay buffer, automatic
resubmission or generic HTTP server-call channel. Duplex calls use
stdio/WebSockets. Retain H1 TCP, H1 TLS, H2 TLS and H3 QUIC support.

See [Framework fit](GUIDELINES.md#framework-fit),
[Formatting](GUIDELINES.md#formatting), [Metadata](GUIDELINES.md#metadata)
and [Tx streams](GUIDELINES.md#tx-streams): expected allocation/growth,
write/admission and flush failures propagate through existing stream state
and return values; do not add an exception-based transport layer.

### WebSockets

Depend on `zws` and support **every profile it supports**, including H1 WS/WSS
and H2/H3 extended CONNECT. Follow `zws/src/zws.cc` and `zws/src/zwsd.cc`
for hub/profile configuration, target, optional subprotocol, establishment,
callbacks, Tx and teardown. Do not add a profile switch table duplicating
`Zws` capabilities.

Use `messageStart` / `process` / `messageEnd` and one bounded pooled message
assembly buffer where contiguous mutable input is needed by `ZfJSON`.
A necessary assembly copy is not a license for a second staging string or
per-frame JSON parsing. Emit through
`txStream(..., Zws::Opcode::Text)`. Reuse handshake, masking, fragmentation,
control frames, TLS, ping/pong and close timing from `Zws`.

Concurrent calls in both directions and unsolicited notifications share the
same pending/live machinery. Web3 subscriptions and reconnect/replay policy
belong to the application. Drain state on disconnect; a new connection does
not revive old pending calls.

Add `Zmcp::WSServer` / `WSClient`, reusing the stdio MCP message/metadata
policy and `Peer` / `ClientPeer::probe` flow after connection establishment.
Preserve modern discovery and legacy fallback. Unsupported binary messages,
oversized input, malformed JSON and rejected MCP envelopes use existing
`Zws` connection failure/close handling. Valid tool errors remain normal
replies; unknown/late response IDs remain ignored.

See [Framework fit](GUIDELINES.md#framework-fit),
[I/O and system integration](GUIDELINES.md#io-and-system-integration)
and [Cross-Shard Handoffs](GUIDELINES.md#cross-shard-handoffs).

## Storage, allocation and shard rules

| Implementation element | Required mechanism; functionality to avoid | Guideline |
| --- | --- | --- |
| Synchronous temporary strings/arrays, header encoding and diagnostic scratch | Use `ZtScratch` with an appropriately heap-identified array/string type. Pass scratch through the synchronous callback; never retain its view in deferred work. Use `ZtBuiltin` for measured common-size owned storage, `ZtArray` for cold/retained variable storage, and `ZmAlloc` for large single-object stack scratch when appropriate. No arbitrary fixed stack buffer or heap-only temporary. | [Strings](GUIDELINES.md#strings), [Storage and lifetime](GUIDELINES.md#storage-and-lifetime), [Heap allocation](GUIDELINES.md#heap-allocation). |
| Network/stdio I/O and asynchronous output | Use pooled `ZiIOBuf`, moved by reference, preserving pool tuning. Write directly into destination storage/Tx streams. I/O buffers are the explicit exception to stack-preferred scratch. | [I/O and system integration](GUIDELINES.md#io-and-system-integration), [Tx streams](GUIDELINES.md#tx-streams). |
| Parsed payloads and deferred handlers | Decode into owned typed data when sufficient. Retain the existing buffer/tree pair only when deferred code actually needs borrowed JSON. Do not retain every tree by default or copy it into a second DOM. | [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization), [Lifetime](GUIDELINES.md#lifetime). |
| Numeric/JSON/text formatting | `ZfJSON` and metadata for JSON, `ZuBox`/`ZuFmt`/`ZuPrint` for other formatting. No `printf`, `strto*`, custom decimal/escape loops or hand-coded JSON. Protocol delimiter scanning in the existing stdio/SSE framer remains necessary framing code. | [Framework fit](GUIDELINES.md#framework-fit), [Formatting](GUIDELINES.md#formatting), [Metadata](GUIDELINES.md#metadata). |
| Span/string/byte conversions | Pass framework types directly using their conversions. Cast only for a demonstrated representation change; no redundant `reinterpret_cast`, char/byte pointer cast, or wrapper copy. | [Bad casting](GUIDELINES.md#bad-casting), [Strings](GUIDELINES.md#strings). |
| Retained work, tokens and container nodes | Concrete fixed-size `ZmHeap` specialization with `Type_<Heap = ZuVoid>` / `ZuDerive`; variable-size `ZmVHeap`; named heap IDs. Intrusive nodes/refcounts, not separate object/node/refcount allocations or a generic allocation base. Stack allocate objects that do not escape dispatch. | [Heap allocation](GUIDELINES.md#heap-allocation), [Containers and intrusion](GUIDELINES.md#containers-and-intrusion), [Storage and lifetime](GUIDELINES.md#storage-and-lifetime). |
| Owner pointers and refcounts | Raw dependent-to-owner pointers with orderly drain. `ZuRef` for thread-affined sharing, `ZmRef` for actual cross-thread/shared objects. No short-lived token/buffer capturing a refcount to its longer-lived owner. | [Scope and cleanup](GUIDELINES.md#scope-and-cleanup), [Lifetime](GUIDELINES.md#lifetime). |
| Counters, state and limits | Retain existing limits: JSON/stdio line/SSE event 16 MiB, SSE line 64 KiB, pending calls/sessions 4096, queue count 1024, queue bytes 64 MiB, work batch 64. MCP session limits remain MCP-owned. Configure/default once; no duplicated composed defaults, independent live-ID cap, unused timers or process-global budget. Use sentinels instead of extra null flags. | [Storage and capacity](GUIDELINES.md#storage-and-capacity), [Metadata](GUIDELINES.md#metadata), [Framework fit](GUIDELINES.md#framework-fit). |
| State layout and encapsulation | Structs have public unprefixed data first; classes have private `m_` fields at the bottom. Keep narrow ownership and concise aliases for long member types. Group data by owner shard and align only actually contended thread-exclusive groups. | [Class and struct shape](GUIDELINES.md#class-and-struct-shape), [Code structure](GUIDELINES.md#code-structure), [Storage and capacity](GUIDELINES.md#storage-and-capacity). |
| Cross-shard calls | Preserve HTTP client correlation on configured Rx, server dispatch/completion/session work on configured Tx, stdio peer work on multiplexer Tx, and distinct isolated blocking IO workers. Assign WS state one owner. Public off-shard dispatchers capture handles/fixed metadata and post to owner methods. No locks to allow wrong-shard access; no new atomics for owner-only state. | [I/O sharding](GUIDELINES.md#io-sharding), [I/O Function Organization](GUIDELINES.md#io-function-organization), [Cross-Shard Handoffs](GUIDELINES.md#cross-shard-handoffs), [Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies). |
| Cleanup and liveness | Remove finished entries immediately. Stop ingress, drain Rx, drain Tx, release ownership through continuations; cancel existing MCP timers on their owner before destruction. Bound work per scheduler turn; no GC scans, polling, sleeps or I/O-thread blocking. | [Teardown](GUIDELINES.md#teardown), [Timer Teardown](GUIDELINES.md#timer-teardown), [Liveness](GUIDELINES.md#liveness), [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow). |
| Diagnostics | Preserve stdout wire-only behavior. Use `ZiLog`, capturing owned diagnostic values rather than pointers/references in deferred logger callbacks; use `ZeString` where needed. No new telemetry system, logger or exact atomic diagnostic counters. | [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization), [Tracing and debug logging](GUIDELINES.md#tracing-and-debug-logging), [Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies). |

Audit declarations and uses in `src`, `test`, `util`, `itest`, `example`,
`bench` and `interop` where present. Every audit flag has been considered:
the table and preceding sections cover applicable storage, allocation,
containers, validation, control-flow, casting, layout, sharding and lifetime
flags. Saga-step validity, persistence and dynamic-module loading have no
implementation role here; do not add facilities to satisfy inapplicable flags.
Retain ordinary integer enum dispatch; avoid enum/integer mapping switches,
invariant loop calls, repeated indexed reads, default initialization before
overwrite, and iterators held beyond traversal. Use compile-time string
literals with retained extent rather than constexpr spans.
See [Audit flags](GUIDELINES.md#audit-flags).

### Naming and source shape

The authoritative limit is **28 bytes**, from
[Length and abbreviations](GUIDELINES.md#length-and-abbreviations), overriding
the older 32-byte summary. The proposed module, header, endpoint, ID, token
and `histSize` names fit this limit. For new compound names use `req`, `res`,
`cli`, `srv`, `cxn`, `gen`, `ver`, `param` and `evt`; elide repeated owner words.
Existing names in the boundary table identify current code, not a requirement
to perpetuate a verbose extracted name.

Use CamelCase `Zjrpc*.hh`/`.cc` library basenames and `Zjrpc*Test.cc` unit tests;
lowercase `jrpctest.cc`-style integration basenames and `zjrpc.cc`/`zjrpcd.cc`
examples. Use camelCase methods and overloaded accessors such as
`histSize()` / `histSize(value)`. Preserve fluent composed configuration through
the existing same-named lambda pattern; do not forward every inner setter.
Follow [Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes),
[Accessors](GUIDELINES.md#accessors), [Vocabulary](GUIDELINES.md#vocabulary)
and [Standard function name vocabulary](GUIDELINES.md#standard-function-name-vocabulary).
Keep hard tabs, two-column logical indent, header skeleton and 98-column C++
source width per [Code style](GUIDELINES.md#code-style).

## Implementation sequence and build changes

1. Record the current MCP codec, correlation, cancellation and transport behavior.
   Identify actual owners/indexes, apply Q19's integer-only contract, and remove contradictory
   generic-versus-MCP validation assumptions before changing wire types.
2. Add the single `zjrpc` module and extract codecs/declarations. Move generic
   tests with the code; update dependent includes/types directly. Decode once.
3. Extract shared pending/completion and stdio mechanics. Replace redundant
   responder/cancellation indexes with direct completion state and the required
   peer live/history stores. Implement deterministic cleanup and Q18 behavior.
4. Extract HTTP JSON/SSE machinery, retaining MCP control/session policy.
   Keep existing body/header defaults and per-owner backpressure.
5. Bind `Zws`; implement generic/MCP `WSServer` and `WSClient` using the same
   wire machinery and every supported `zws` profile. Add batch aggregation
   once at the shared dispatch/completion layer, not in each transport.
6. Update both example pairs for transport selection, remove superseded MCP
   wire code, and verify the behavioral and allocation boundaries below.

Add `zjrpc/{src,test,itest,example}` with `Makefile.am`; traverse library
sources before dependents. Register the module before `zmcp` in top-level
build/configure inputs and link direct dependencies, including `Zws`.
Update exports, heap IDs, `ZfStruct` metadata and include paths. Regenerate build
inputs only as required to add the module, preserving current configure options
and reusable artifacts. Document integration entry points and noncolliding port
ranges. Do not add an empty directory, external interoperability framework,
benchmark harness or build option without actual content/need.
See [Module layout](GUIDELINES.md#module-layout) and
[Build configuration](GUIDELINES.md#build-configuration).

### Build execution policy

These constraints apply throughout implementation and final acceptance; no
milestone requires a clean build or a different build configuration.

Retain the **current build configuration**, including the compiler, configure
options, release/debug flags and platform. Other compilers, release/debug flags,
sanitizer configurations and platforms are out of scope. Preserve portable
implementation behavior without adding a cross-platform verification matrix.
Do not run `make clean` or reconfigure to change these settings; reuse the
existing build artifacts.

Rebuild infrequently. Batch source, header, build-input and test changes into
large coherent batches; defer rebuilding and retesting until major milestones.
Do not rebuild or retest after individual edits or small implementation steps.
Reach a major milestone before compiling or running tests:

- Shared codecs/declarations, completion/correlation and stdio extraction are
  coherent at the end of step 3. If an intermediate build is useful, limit it
  to the new/changed library layer; defer dependent rebuilding and testing.
- All transports, batches, MCP migration, examples and source cleanup are
  complete at the end of step 6. This is the dependent build and verification
  milestone.

Use incremental `make -j8`, retaining current parallelism and reusing current
objects and libraries. Defer all dependent rebuilding and testing to the end
of implementation; update dependent sources alongside API changes without
compiling or testing them before that milestone. At the final milestone,
rebuild libraries before their dependents in dependency order, then run the
relevant test targets, building their default `all` target before `make test`.
An incremental top-level `make -j8` may perform that
final dependency traversal. After a demonstrated failure, batch its repairs
and rerun only the build/test targets needed to verify them.

Use Valgrind sparingly: focused runs to debug memory corruption as needed,
and a final memory leak/corruption gate over the relevant lifecycle and
transport tests. Invoke it through `libtool exec`. Do not run Valgrind at each
milestone or add separate ASan/LSan builds.

## Verification and acceptance

Move generic codec/dispatch/correlation tests into `zjrpc`; retain MCP schema,
tools, structured replies, metadata and REST parity tests in `zmcp`. Use the
same application declarations in `zjrpc.cc` / `zjrpcd.cc`, following the MCP
examples. Test shared semantics once, then framing/admission/lifetime at each
transport boundary:

- ID absence/null and the explicit Q19 numeric contract; typed named/positional
  decoding, arbitrary result/error data, unknown/late replies and removal before
  callbacks. Ensure MCP ID restrictions and error mapping remain MCP-owned.
- One batch completion and response array; synchronous/deferred/out-of-order
  members, mixed notifications/errors, all-notification/empty batches, bounded
  member admission and closure while members are pending.
- Live-first duplicate rejection without executing the handler or corrupting
  original cancellation state; history eviction cannot lose live work;
  completed/aborted IDs leave live storage; peer teardown drains both.
  Verify configured history capacity and no TCP-derived sessionless HTTP peer.
- Existing partial stdio I/O/EOF/worker teardown, JSON/SSE headers/status 200,
  application header enrichment, queue rejection/backpressure and detached
  HTTP route behavior.
- WS fragmented messages with interleaved control frames, text/binary policy,
  duplex calls, notifications and disconnect. Exercise generic/MCP bindings
  across the `zws` profiles available in the current build while relying on `zws` tests for its
  handshake/masking/control-frame algorithms.
- MCP WS modern discovery, legacy fallback, tools, progress, logging and
  cancellation through existing MCP message/peer code.

Use `ZuTestUtil` / `ZuTest` and TAP; networking/end-to-end cases belong in
`itest`, not unit tests. Synchronize completion with `ZmBlock` (or
`ZmSemaphore` where appropriate), never polling or sleeps. Do not duplicate
framework cache/hash/parser tests or test private implementation shape.
See [Tests](GUIDELINES.md#tests) and
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

Apply the build execution policy above at the final milestone: incrementally
build libraries before dependents, run MCP/JSON-RPC suites and affected
`zhttp`, `zws`, `zrest` regressions under the existing configuration, then run
the final Valgrind leak/corruption gate through `libtool exec`.
Full `zws` profile support remains an implementation requirement;
do not change configuration to enable unavailable profiles for this task.
Alternate compilers, release/debug flags, sanitizer configurations and platforms
are out of scope for both implementation and acceptance. Apply the same
infrequent, incremental build policy to subsequent audit and repair batches.
See [Build configuration](GUIDELINES.md#build-configuration),
[Acceptance](GUIDELINES.md#acceptance) and
[Debugging](GUIDELINES.md#debugging), subject to this task's build execution
policy above.

Compare hot-path allocations/copies and owner references against the baseline:
ordinary MCP extraction should add no redundant work. Account separately for
the deliberately new Q18 history and actual batch/WS state; do not claim those
are free or add permanent instrumentation without need. Use existing heap
identities/tuning to inspect allocation behavior.

Complete when independent JSON-RPC and MCP endpoints use the shared wire
implementation across all three transports, Q19's integer-only contract is preserved,
required tests pass, and no duplicate codecs, redundant indexes, custom
framework replacements or compatibility shims remain.

## Open questions and ambiguities

Question numbers are permanent. Never renumber, reuse, delete or reorder an
existing entry. Append using the next unused number. Keep resolved entries,
record decisions/implications, and update the corresponding plan section.
Use `Superseded` plus a replacement reference if needed; use literal `Qn`
identifiers rather than an automatically numbered list.

| Number | Status | Question or ambiguity |
| --- | --- | --- |
| Q1 | Resolved | **Declaration ownership.** `zjrpc` owns compile-time request/response definition machinery; `zmcp` owns MCP declarations/policy. Reuse `zrest`/`zhttp` patterns and metadata, with no runtime registry or second schema system. |
| Q2 | Resolved | **ID/codec ownership and framework reuse.** `Zjrpc::ID` / `IDFmt` serve request IDs and correlation everywhere. Use `ZfJSON` parsing/formatting, with no numeric codec reimplementation. Q19 retains the existing `int64_t` numeric representation and framework comparison/hash; fractional and exponential numeric forms are out of scope. |
| Q3 | Resolved | **Correlation.** Retain one outbound pending table per peer, register before send/remove before callback/drain on closure. HTTP streams are routes; inbound/outbound ID spaces differ. No outbound history table. Q18 supplies inbound duplicate policy. |
| Q4 | Resolved | **HTTP binding.** JSON-RPC result/error responses use 200; canonical JSON/SSE headers and app enrichment follow `Zrest`/`Zhttp`. Response declarations require no status, and IO/WS require no HTTP metadata. MCP control responses remain separate. |
| Q5 | Resolved | **HTTP streaming scope.** Preserve POST-response SSE and existing MCP POST/DELETE handling. No standalone GET stream, replay or generic HTTP server-call channel. |
| Q6 | Resolved | **Batch aggregate.** One request completion supplies one batched response; member dispatch/collection is aggregate-owned. Emit one logical array message; omit notifications; all-notification batches emit nothing. Use peer member-ID correlation, bounded work and existing cleanup, with no synthetic batch ID or transport-specific batch machinery. |
| Q7 | Resolved | **MCP WebSockets.** Add `WSServer`/`WSClient` over `Zjrpc`/`Zws`, preserving stdio MCP message policy, modern discovery/legacy fallback, metadata and lifecycle. Follow `zws.cc`/`zwsd.cc` callbacks/configuration. |
| Q8 | Resolved | **WebSocket failure behavior.** Oversized/binary/malformed or rejected MCP messages fail the connection through `Zws`; normal tool errors remain responses and unknown/late replies are ignored. No separate WS error/recovery subsystem. |
| Q9 | Resolved | **Full `zws` support.** Use every supported profile, including H1 WS/WSS and H2/H3 extended CONNECT; reuse its capabilities/configuration and verify binding coverage. |
| Q10 | Resolved | **Completion semantics, not redundant containers.** Preserve ID/owner/generation, typed completion, cancellation and invalidation-before-emission. An ordinary responder needs one slot, not its own hash. Shared live lookup replaces overlapping cancellation indexes; batch member state stays aggregate-owned. MCP supplies progress/logging and `ToolReply`. |
| Q11 | Resolved | **Payload and route ownership.** Use pooled `ZiIOBuf`, existing parse-tree ownership and borrowed dispatch views. Retain typed data or buffer/tree only when needed asynchronously. Reuse route cancel/owner hooks; no new route registry or unconditional DOM retention. |
| Q12 | Resolved | **Preserve MCP disposition.** Keep current late-reply, callback/decode, remote/local failure and tool-error behavior. Corrupt MCP HTTP disconnects, rejected stdio/WS fails the channel. Generic JSON-RPC batch invalid-entry replies use the shared error encoder; preserving MCP policy must not suppress generic required replies. Q18 rejection is the agreed MCP behavior change. |
| Q13 | Resolved | **Existing budgets.** Preserve named defaults/admission/pool tuning. No duplicate counters or process-global budget. Charge batch members to existing work admission. Live hash has no independent cap; `Server::histSize` defaults to 1024 for each new peer's historic cache. |
| Q14 | Resolved | **Owner shards.** Preserve configured HTTP client Rx, server Tx and stdio multiplexer Tx ownership, with isolated blocking I/O workers. WS gets one designated peer owner. No locks for wrong-shard access. |
| Q15 | Resolved | **Cancellation scope.** Preserve current notification/HTTP/token cancellation and terminal cleanup; cancellation need not finish the call. MCP retains era-specific policy/session timers. No generic deadline facility. |
| Q16 | Resolved | **One hybrid library.** One `libZjrpc.la` links required transports including `Zws`; no reduced-dependency sublibraries, plugins or extra build switches. Headers separate actual dependencies without requiring extra objects. |
| Q17 | Resolved | **Endpoint/lifetime model.** Common CRTP `Server`/`Client`, concrete IO/HTTP/WS pairs, no public `CliLink`/`SrvLink`. Preserve HTTP client-owned pending state, legacy session versus stateless work ownership, route-detach versus closure, and stdio/WS channel/connection teardown. |
| Q18 | Resolved | **Live hash and bounded history.** Check one non-evicting live `ZmHash` then historic `ZmCache` before dispatch. The live index also serves cancellation; existing work budgets bound it. History contains only IDs, normal LRU, capacity from application-wide `Server::histSize`, default 1024. Move used IDs to history on terminal cleanup; no replay, peers whitelist, per-peer configuration, extra LRU algorithm or TCP-based HTTP peer inference. Null is generic but rejected by MCP. History may reject otherwise permitted completed-ID reuse and cannot detect reuse after eviction; neither property supplies application idempotency. |
| Q19 | Resolved | **Integer-only numeric IDs.** Fractional numerical IDs and exponential notation are out of scope. Retain signed `int64_t` IDs in ordinary decimal integer notation, alongside absent, null and string variants; use `ZfJSON` codecs and existing framework comparison/hash. No widened numeric representation, token-based key, canonicalization or custom numeric parser is required. Unsupported forms must not be silently floored/coerced into supported IDs; keep necessary checks at the codec boundary. |
