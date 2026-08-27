# `zmcp` Implementation Plan

## Goal and initial scope

Add a new `zmcp` module implementing the MCP `2026-07-28` protocol revision for
tool providers and tool callers, while advertising and providing wire
compatibility with `2025-11-25`.  Its primary purpose is to expose the same
OpenAPI-expressible operations through REST and MCP without duplicating request
types, response types, schemas, validation, or application handler bodies.  The
library will depend on `zhttp` and expose the same typed, CRTP-oriented
application style as `zrest`.  There is no `zrest`/`zmcp` dependency in either
direction; applications own any types and handlers shared between them.

The first release will support:

- JSON-RPC 2.0 requests, responses, errors, and notifications;
- `2026-07-28` stateless discovery and per-request metadata, plus the compatible
  `2025-11-25` initialize/initialized lifecycle;
- cancellation, progress, and protocol logging notifications needed around
  tool calls, plus `ping` and `logging/setLevel` on the compatible
  `2025-11-25` path;
- `tools/list` and `tools/call`;
- the same application-owned, reflected request/response contracts and
  compile-time typelist/union pattern used by `zrest`;
- required application-specified MCP `ToolID` values, plus generated
  descriptions, input schemas, and output schemas from operation metadata;
- typed JSON tool arguments and structured JSON results representing every
  declared potential operation response;
- Streamable HTTP over one configurable endpoint, including fixed JSON
  responses and SSE response streams;
- the same H1 TCP, H1 TLS, H2 TLS, and H3 QUIC coverage as `zrest`, including
  persistent H1 connection reuse and H2/H3 session/stream multiplexing, with
  application-visible server context at transport-session, legacy MCP-session,
  and request-stream scope;
- one strictly sequenced request/response lane and exactly `0..1` live `zhttp`
  stream per named `2025-11-25` MCP session; installing a new stream forcibly
  disconnects and replaces the old stream;
- newline-delimited JSON-RPC over stdin/stdout;
- both client and server roles for each transport.

Both roles are required deliverables, not alternative scopes.  Align their
public split with `zrest/src`: shared protocol and operation facilities in
`Zmcp.hh`, client facilities in `ZmcpClient.hh`, server facilities in
`ZmcpServer.hh`, and the component-library boundary in `ZmcpLib.hh`/
`ZmcpLib.cc`.

Defer prompts, resources, sampling, elicitation, task extensions, OAuth, legacy
`2024-11-05` HTTP+SSE compatibility, and child-process creation.  The stdio
client accepts already-open input/output handles; launching and supervising the
child remains an application concern.  The normal server is a single process
whose application configures one `Server` as either an HTTP listener or a stdio
transport.  Keep extension points in the core message dispatcher so later MCP
features do not require transport changes.

Prefer `2026-07-28` and advertise the supported versions in this order:
`2026-07-28`, `2025-11-25`.  A server must accept native stateless requests and
the legacy initialize handshake on the same configured transport endpoint.  A
client probes with `server/discover`, selects `2026-07-28` when offered, and
falls back to a `2025-11-25` initialize handshake when discovery is unsupported
or does not offer the preferred revision.  Advertise and emit only the two
implemented revisions.  Treat any other valid MCP revision as an unsupported
peer capability through the normal discovery/fallback path; do not silently
approximate one revision with the other or add a per-request revision validator.

Normative references:

- [MCP `2026-07-28` specification](https://modelcontextprotocol.io/specification/2026-07-28)
- [MCP `2026-07-28` transports](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports)
- [MCP `2026-07-28` tools](https://modelcontextprotocol.io/specification/2026-07-28/server/tools)
- [MCP `2025-11-25` lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle)
- [MCP `2025-11-25` transports](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)
- [JSON-RPC 2.0](https://www.jsonrpc.org/specification)

## Postel's Law and validation boundary

Be strict about what `zmcp` emits and liberal about what it accepts.  Positive
protocol interoperability and standards conformance are goals: emitted
messages must be canonical and consumable by other MCP implementations, and
the receiver must ingest valid messages produced by those implementations even
when optional metadata, redundant routing hints, or unknown fields differ from
our own output.  Negative standards conformance is explicitly a non-goal: do
not add checks merely to prove that malformed or non-canonical input is invalid,
and do not add tests which assert a particular failure or diagnostic for such
input.

`zmcp` is a low-latency, non-throwing protocol layer like `ZfJSON`, `ZfURI`, and
`Zrest`.  Expected peer input, including invalid input, must not use exceptions
for control flow.  Use parser results, sentinels, bounded state transitions, and
ordinary unsupported-operation results.  A single outer containment boundary
may catch an exception escaping application code, but the protocol library must
not throw while parsing, classifying, narrowing, routing, or closing peer input.
Every invalid-input path must terminate gracefully without a crash, out-of-
bounds access, stale owner access, unbounded work/storage, or other security
risk.

Retain receiver-side checks only where they are needed for:

- buffer and integer bounds, queue/session limits, and prevention of resource
  or response amplification;
- transport framing and safe determination of message boundaries;
- memory safety, typed-dispatch safety, ownership, and lifetime invariants;
- Origin, authentication, authorization, and equivalent security policy;
- selecting an operation or protocol feature which the implementation actually
  supports.

Treat that list as an allow-list during implementation and review.  For every
receiver-side conditional which rejects, reports, normalizes, or drops peer
input, identify the safety, security, framing, resource-bound, ownership, or
supported-dispatch reason from the list above; delete the conditional when no
such reason exists.  In particular, do not retain a cheap or already-written
check merely because it is harmless, improves a negative diagnostic, or makes
the receiver enforce text from the specification.  Do not replace a deleted
validation with an equivalent pre-scan, normalization pass, warning, metric,
or alternate error path.

Completely corrupt, unframeable, or over-limit input force-closes the affected
connection, session, or stream and produces no diagnostic response to the peer.
Release all partial parser, correlation, and transport state through the normal
owner-shard teardown path.  This silent-close rule is part of the DoS boundary,
not a conformance diagnostic.

Apply the local precedents in this order:

1. Invalid JSON is passed directly to `ZfJSON`; its non-throwing scan result is
   authoritative.  Do not pre-validate, retry with another parser, or construct
   a JSON-RPC parse-error response.  A failed scan closes the affected transport
   scope without peer output.
2. Valid MCP which requests an unsupported method, tool, lifecycle operation,
   or optional feature follows `../kaldron-demo/factory_sim`: use the ordinary
   unsupported-operation path rather than reclassifying the input as invalid.
   In particular, unknown JSON-RPC methods return `-32601`, unknown tools return
   the structured MCP tool error with code `404`, and application errors use
   their numeric code or `-32000` at the outer containment boundary.
3. Other recoverable HTTP parsing, route selection, and body-consumption
   outcomes follow `Zrest`/`Zhttp`: return the normal parser/route failure and
   let the transport apply its established status or close behavior rather than
   manufacturing a stricter MCP diagnostic.

In particular, prohibit a second envelope/schema validator and checks for
duplicate or unknown JSON members, canonical-but-equivalent lexical forms,
redundant header/body agreement, the presence of advisory capabilities or
identity metadata, or `Content-Type`/`Accept` exactness when the bounded body
and selected response mode are otherwise usable.  Do not validate a field just
because the specification labels it required; consume fields needed for safe
dispatch and let reflected loading/defaulting determine the usable value.
Internal compile-time metadata checks and owner-state assertions are unaffected:
they validate programmer invariants, not hostile wire input.

## Relationship to `zrest`

`zmcp` is a thin MCP layer over the same `zhttp` and `ZfJSON` facilities used
by `zrest`, not a parallel protocol framework.  Its only substantial protocol
additions are the compile-time tools catalog, pre-modern MCP session awareness,
and JSON-RPC envelopes.  Reuse `zrest` patterns directly for typed
request/response objects, shared builder/parser contracts, CRTP customization,
compile-time typelists/unions, declared headers, direct `ZfJSON` serialization,
intrusive ownership, configuration, and example/test layout.  Do not introduce
generic transport, reflection, request/response, or JSON machinery in `zmcp`
when the established `zhttp`, `ZfStruct`, `ZfJSON`, or `zrest` pattern already
provides it.  There is no `zrest`/`zmcp` link-time dependency and neither module
owns application metadata; where their application contracts overlap, make
the `zmcp` contract coincide with `zrest` so an application-owned type can
satisfy both independently and without conflicting declarations.

The reference behavior is
`../kaldron-demo/factory_sim/lib/mcp.js` plus the REST/MCP dispatch in
`../kaldron-demo/factory_sim/server.js`:

- one OpenAPI operation becomes one MCP tool;
- Kaldron's REST/OpenAPI layer flattens path, query, and JSON-body fields into
  one tool `arguments` object; `zmcp` consumes only that resulting mainstream
  MCP JSON-object shape and implements no REST-location projection;
- operation identity, title, description, parameter schemas, required fields,
  and all declared responses generate the tool descriptor;
- each possible operation response becomes one output-schema branch containing
  `code` and, for body-bearing responses, typed object/array `data`;
- `tools/call` loads arguments through `ZfJSON`, leaves semantic validation to
  the application, and invokes the existing REST handler;
- REST and MCP authorization select the same operation identity;
- the handler returns the same status/payload result regardless of transport;
- only the final transport adapter differs: REST emits an HTTP response and MCP
  emits a `CallToolResult` with empty `content` and structured operation data.

`zmcp` will not load an OpenAPI YAML/JSON document at run time.  Application C++
types and compile-time `zmcp` request/response metadata are the source of truth;
`zmcp` derives the MCP tool descriptor directly from them.  OpenAPI generation
or REST projection is outside this library.

The complete tool catalog is intrinsic to the compiled server type.  Tools
cannot be added, removed, replaced, renamed, or re-described at run time, and
`tools/list` therefore has no invalidation event or refresh lifecycle.  Emit
the complete catalog in stable typelist order without pagination.  A `zmcp`
client may cache the full catalog indefinitely;
on `2026-07-28`, `server/discover` and `tools/list` advertise
`cacheScope: "public"` and `ttlMs` equal to the named compile-time
`CatalogTTL` default (24 hours, `86'400'000` milliseconds).  Define the default
once with a maintenance comment and use it in emission and tests; do not repeat
the literal.  Do not emit modern cache fields on the `2025-11-25`
compatibility path.

Always expose the full compiled catalog.  `zmcp` does not filter discovery by
caller, authorization context, or session.  Deployments needing different
catalogs run distinct service instances, normally on different ports, each
compiled/configured with its own operation typelist and protected by its own
upstream authentication policy.

The reusable request/response metadata uses the existing, unchanged `ZfStruct`
and `ZfJSON` reflection stack used by `zrest`.  Do not change `ZfStruct` or the
core `ZuFieldProp` declarations and do not introduce a parallel reflected-
structure facility.  MCP-only field annotations such as `x-mcp-header` belong
in a `ZuFieldProp::MCP` enrichment declared by `Zmcp`, exactly as JSON-only
annotations live in `ZuFieldProp::JSON` without changing the reflection core.

The semantic mapping is:

| `zrest` | `zmcp` |
| --- | --- |
| application-owned request/response types | the same application-owned types |
| HTTP method/path operation dispatch | `tools/call`, then declared `ToolID` |
| declarative path/query/header/body loading | direct JSON `arguments` loading |
| application handler | the same application handler, when the app shares it |
| declared HTTP response/status | `{code, data?}` output-schema branch |
| selected response builder | structured MCP tool result or tool error |
| response body policy | JSON object or SSE stream of JSON-RPC messages |
| multi-request builder/parser union | compile-time tool registry and dispatcher |

Unlike REST's several possible wire locations and representations, mainstream
MCP supplies one JSON object in `params.arguments`.  `zmcp` loads that object
directly into the operation's `ZfStruct`-described request type; it has no path,
query, cookie, form, multipart, or REST-header flattening model.  An application
may separately tell `zrest` how to populate the same type from REST.  A
Streamable HTTP server parses every accepted POST at the single endpoint before
dispatching the JSON-RPC method; `tools/call` then performs a second,
compile-time dispatch on the tool name.

## Architecture

Keep `zmcp` as a thin adapter over the existing stack.  It adds only the
static tools catalog, JSON-RPC wrappers, and pre-modern MCP session state.  Its
HTTP and stdio code adapts those three concerns to existing I/O facilities; it
is not a second request/response, JSON, or transport framework.

```text
application-owned ZfStruct request/response types and handler
             /                                  \
optional zrest contract                  coincident zmcp contract
          |                                      |
   REST over zhttp                 generated static Tool catalog
                                                  |
                               JSON-RPC wrappers + legacy sessions
                                           /              \
                            Streamable HTTP adapter     stdio adapter
                                      |                     |
                                    zhttp       dedicated Rx/Tx workers
                                                            |
                                                       Zi::Handle pair
```

The protocol layer must not know whether a message arrived in an HTTP body, an
SSE event, or a newline-delimited stdio frame.  A transport hands it one owned
UTF-8 JSON message plus a response sink.  The peer parses and dispatches it,
and emits complete JSON-RPC messages back through that sink.  This boundary is
also where message-size limits, peer shutdown, and late-response rejection are
enforced.

### Transport sessions, streams, and application context

Treat Streamable HTTP as a persistent transport, not a sequence of one-shot
connections:

`zmcp` integrates only with `zhttp`'s transport-neutral parser, Builder, Link,
session, and stream contracts.  It must not inspect or branch on H1, H2, H3,
TCP, TLS, or QUIC in protocol/session code; those variants exercise the same
adapter through `zhttp` and differ only in transport-level configuration and
verification.

- for H1 TCP/TLS, the `zhttp` stream abstraction corresponds 1:1 with its
  transport link, whose live transport connection carries the sequential HTTP
  request/response exchanges;
- for H2/H3, multiple independent `zhttp` streams are multiplexed over one
  transport link/connection;
- an MCP `2025-11-25` session is identified by `MCP-Session-Id`, owns retained
  server/application state and one current `zhttp` stream handle, and may
  be associated with many successive streams over its lifetime but never more
  than one live stream at a time;
- a `2026-07-28` request has no MCP protocol session, but still runs within a
  retained transport-session context and its own request/SSE-stream context;
- one stdio handle pair is a persistent peer session for its lifetime.

Expose statically dispatched application hooks to create, access, and destroy
session and stream context.  A handler receives borrowed access to the current
application context.  The server retains legacy MCP-session context across its
ordered sequence of streams, transport-session context across the life of the
underlying H1/H2/H3 connection, and stream context until the corresponding HTTP
response/SSE stream closes.  Losing a stream or its connection clears only the
session's current stream reference; the logical session/application context
remains until DELETE, expiry, or application close and may accept a later
stream.  Stdio exposes one session context plus the current request context.
Context is destination-shard-owned and uses non-atomic `ZuObject`/`ZuRef`
ownership while confined there.  Cross-shard work places variable state in an
identified destination-owned object or `ZiIOBuf` and captures only its handle
plus fixed metadata, never borrowed context or request spans.

Mirror the cardinality of `ztcp` link to connection, but keep the abstraction
levels distinct: `link` and `connection` remain transport-level terms, while
the association implemented by `zmcp` is logical MCP session to `zhttp` stream.
The logical MCP session owns one current-stream handle.  Installing a new
authenticated stream for that session first moves out and forcibly closes any
old current stream, then installs the new handle.  Receive, completion, and
disconnect callbacks from an old stream are ignored unless their stream
identity still equals the session's current handle; in particular, an old
disconnect cannot clear its replacement.  This identity rule provides the
same single-live-instance invariant without a second concurrent stream owner or
a queue of bindings.

H2/H3 may multiplex modern stateless requests and streams for different legacy
MCP sessions, but never retain concurrent live streams for the same legacy
session.  The client normally submits the next request only after the preceding
response stream is terminal; if a new stream nevertheless arrives first, it
wins and forcibly displaces the old one.

H3 network/path migration is below this layer: when QUIC preserves the same H3
connection while moving between networks, the transport-session identity does
not change.  MCP-session migration is simply the logical session installing a
new current stream—possibly on a different connection—and displacing the old
stream while preserving session context.

The modern protocol remains stateless at the MCP wire level: an application
must not require `2026-07-28` requests to land on the same connection for
correctness.  Connection-scoped context is an optimization/observability and
resource-lifetime facility; application state that must survive reconnect or
load balancing uses an explicit handle passed in tool arguments.

Use CRTP/static dispatch throughout the hot paths.  Do not introduce virtual
tool interfaces or `std::variant`, `std::function`, or STL containers.  Use
`ZuTypeList`, `ZuUnion`, `ZuSwitch`, `ZmHash`/`ZmLHash`, `ZmFn`, `ZuRef` for
thread-affined ownership, `ZmRef` only across genuine sharing boundaries,
`ZiIOBuf`, and identified `ZmHeap`/`ZmVHeap` storage.

### Proposed source layout

Create:

```text
zmcp/
  Makefile.am
  README.md
  src/
    Makefile.am
    ZmcpLib.hh
    ZmcpLib.cc
    Zmcp.hh             operation metadata, common types, traits, errors
    ZmcpClient.hh       typed client calls and result dispatch
    ZmcpServer.hh       typed server tools and invocation dispatch
    ... private implementation headers only where factoring requires them ...
  test/
    Makefile.am
    README.md
    ZmcpJSONTest.cc
    ZmcpPeerTest.cc
    ZmcpSchemaTest.cc
    ZmcpToolTest.cc
    ZmcpZrestTest.cc
  itest/
    Makefile.am
    README.md
    zmcphttptest.cc
    zmcpstdiotest.cc
  example/
    Makefile.am
    zmcp.cc
    zmcpd.cc
  interop/
    Makefile.am
    README.md
    ... pinned mark3labs/mcp-go fixture ...
```

Keep the installed surface aligned with the compact `zrest/src` split.  Schema,
JSON-RPC, peer, HTTP, and stdio implementation factoring belongs in private
`noinst_HEADERS` unless a type is independently required by callers.  Avoid
include cycles by factoring common declarations into a private underscore
header only when the dependency DAG requires it; do not use tail inclusion.

Treat `zmcp` as greenfield.  It has no existing dependents, and dependent API
compatibility is a non-goal until explicitly directed otherwise.  Propagate API
changes through the new module/examples/tests; do not add shims, aliases, or
forwarders for superseded design iterations.

Apply the `GUIDELINES.md` directory roles exactly:

- `test` contains isolated TAP unit tests using `ZuTest`/`ZuTestUtil`;
- `itest` contains TAP integration programs that launch processes or exercise
  stdio/network transports end to end;
- `example` contains the dependent demonstration programs `zmcp` (client) and
  `zmcpd` (server), never test fixtures;
- `interop` contains third-party SDK/conformance testing;
- add `util` only if `itest` or `interop` needs an independently buildable
  repository-owned fixture, and place it before `itest` in build traversal.

Document every unit/integration test entry point, prerequisites, and direct-run
command in its directory's `README.md`; keep third-party setup and explicit
execution instructions in `interop/README.md`.

### Engineering and audit constraints

Treat every phase boundary as a `GUIDELINES.md` review gate, not only the final
review.  Apply these constraints to all new `zmcp` code and to nearby code
changed while integrating it:

- compile as GNU C++2b without concepts, `requires`, scoped enums, anonymous
  namespaces, STL containers/type erasure, non-specific lambda captures, or
  forwarding wrappers where a `using` declaration suffices;
- prefer C headers where equivalent, direct Z span/string conversions over
  casts, `T(v)` over `static_cast<T>(v)`, `ZuDerive` for complex aliases, ADL
  friend tags for format/metadata customization, and Z formatting/I/O/logging
  facilities over varargs, `FILE`, or ad hoc replacements;
- use `ZuAssert` for compile-time assertions and the layer-appropriate
  `ZmAssert`/`ZiAssert` at run time; integral/Boolean compile-time constants
  fitting in `int` are enums, including named capacity and protocol defaults;
- keep names within the 28-byte limit, use the repository abbreviations and
  getter/setter overloads, and reserve trailing underscores for internal or
  owner-shard entry points;
- follow the library-header skeleton exactly: modelines, copyright/license,
  include guard, component library header, ordered direct dependencies only,
  and a DAG with no tail inclusion; use hard tabs and prevailing Z layout in
  code even though examples in this Markdown file use spaces;
- keep structs all-public with unprefixed data first and classes all-private
  with `m_` data last; order stored members to avoid padding and separate
  independently accessed shard-owned groups at cache-line boundaries;
- use `ZuRef`/`ZuObject` for thread-affined intrusive ownership and
  `ZmRef`/`ZmObject` only for genuinely shared objects; short-lived dependent
  objects use raw back-pointers and are drained by their owner;
- identify every allocation with `ZmHeap`, `ZmVHeap`, or `ZmHeapID`; use
  intrusive container nodes where practical, pooled `ZiIOBuf` for wire data,
  `ZtScratch`/`ZtBuiltin` for workload-sized scratch, and `ZmAlloc` for large
  test objects instead of the stack;
- reject fixed arrays, fixed lookup tables, unexplained capacities, default
  initialization before overwrite, temporary contiguous conversions, needless
  copies/casts, container-wide garbage-collection scans, and unbounded queues or
  hashes; prefer `ZuSwitch`, `ZuUnroll`, `ZuTypeList`, and the appropriate Z
  containers;
- represent unset/state distinctions with existing sentinels or tagged values,
  not parallel Boolean flags; do not add atomic/locked diagnostic counters
  where approximate owner-shard values suffice;
- keep iterator scopes inside the traversal, count successful visits rather
  than snapshotting container sizes, cache loop invariants and repeated indexed
  values, flatten control flow, and factor repeated blocks without putting
  invariant declarations inside templates;
- never poll, synchronously wait on scheduler work, or use elapsed sleeps to
  make concurrent tests pass; use continuations and `ZmBlock`/`ZmSemaphore`
  only at permitted test/main-thread boundaries;
- call `ZiLOG` only with specific value captures and owned diagnostic strings;
  never capture borrowed spans, pointers, or references for deferred logging.

Before each phase is accepted, scan the added diff for every red/amber flag in
`GUIDELINES.md`, not merely the prohibited constructs listed above.  Any
intentional exception must be documented beside the design and covered by a
targeted performance, lifetime, or portability test.

Also inventory every new receiver-side rejection or validation branch against
the Postel allow-list above and delete branches which exist only for negative
conformance, stricter diagnostics, or canonical-input policing.

## Public operation and generated-tool model

Define one `zrest`-shaped `Request` contract for each MCP tool.  Request
builders and parsers share that contract; operation metadata is not specific
to either direction.  Likewise, response builders and parsers share the common
`Response` contract.  Do not
require applications to separately define an MCP Tool object or hand-write
`inputSchema`/`outputSchema`.  The request contract supplies:

- a stable operation ID used by dispatch and authorization;
- a required application-specified `ToolID` used as the exact MCP tool name;
- optional title and description;
- a compile-time response-body policy selecting fixed JSON or SSE streaming;
- one `ZfStruct`-described request type loaded directly from MCP `arguments`;
- a typelist of every potential response, each with status/code, optional
  description, body presence/content type, and reflected payload type;
- optional MCP annotations not derivable from ordinary OpenAPI operation data.

An indicative API is:

```c++
struct AddReq {
  int64_t lhs;
  int64_t rhs;
};
ZfStruct((AddReq, JSON),
  (((lhs), (Ctor<0>, Required)), (Int64)),
  (((rhs), (Ctor<1>, Required)), (Int64)));

struct AddOK : public Zmcp::Response {
  using Body = AddResult;
};
struct AddInvalid : public Zmcp::Response {
  using Body = Error;
  enum { Status = 400 };
};

struct Add : public Zmcp::Request {
  using Object = AddReq;
  using OperationID = ZuStringT<"addNumbers">;
  using ToolID = ZuStringT<"number_add">;
  using Responses = ZuTypeList<AddOK, AddInvalid>;
};

using Requests = ZuTypeList<Add, ...>;
```

The exact macro/property spelling is a Phase 1 design result, not prescribed by
this sketch.  Require a non-empty `ToolID` and diagnose a missing declaration,
missing response payload metadata, duplicate response code, or duplicate tool
name at compile time using `ZuAssert`, never `static_assert`.  Do not enforce
the protocol's recommended tool-name spelling or length as a compile-time
requirement; canonical HTTP emission Base64-encodes a name that is not a safe
plain header value.  `zmcp`
performs no operation-ID-to-tool-name conversion.  Applications will commonly
adopt Kaldron's convention, but that choice and any exceptional spelling remain
entirely application concerns.  Each server catalog has exactly one globally
unique canonical name per tool; do not support aliases or compatibility names.

Enforce one structured-data invariant across the public operation API:

- every JSON-RPC wire message is an object;
- every `tools/call` `arguments` value is the reflected request object;
- operation request fields and response bodies use JSON objects/arrays and
  their reflected scalar members, never an opaque raw or textual body;
- every body-bearing response payload is a reflected JSON object or array;
- every MCP tool result has `content: []` and carries the operation result only
  in the `structuredContent` object;
- a declared no-body response has no `data` member rather than a sentinel body.

Reject a projected operation at compile time when its request/response metadata
cannot satisfy this invariant.  SSE and stdio may add transport framing, but
the framed payload remains one structured JSON-RPC object.

Generate MCP JSON Schema from metadata expressed by `ZfStruct` and `ZfJSON`:
type codes, JSON field IDs and formats, `Required`, `Range`, defaults, enum
maps, vectors, maps, UDT fields, and facets.  The sole MCP-specific field
enrichment is `MCP::Header<...>`, which emits the standardized
`x-mcp-header` annotation for an eligible primitive tool argument.  MCP does
not add field descriptions or a general JSON Schema annotation language.
Tool-level title, description, annotations, operation ID, and analogous
response metadata belong to the common MCP `Request` and `Response` contracts,
following `zrest`; they do not enrich reflected application fields.

`ZmcpSchema` walks the request's reflected fields and directly emits
the tool `inputSchema`.  It walks every declared response and emits an
`outputSchema` union whose branches have the transport-neutral shape:

```json
{"code": 200, "data": {}}
```

`code` is constrained to that response's declared status.  For a body-bearing
response, `data` is required and uses the reflected object/array payload schema.
For a no-body response, the branch contains only `code`; it does not invent a
body value.  The generator also emits name, title, description, annotations,
and operation ID metadata.  `tools/list` serializes this generated descriptor
directly into its Tx stream—no cached DOM, runtime OpenAPI parser, dynamic
registry, or application-authored MCP schema is required.  Its metadata is
compile-time static and the full catalog is indefinitely cacheable.

The MCP adapter loads `params.arguments` directly into the request object and
invokes the application handler with the operation identity, current application
session/stream context, and completion contract.  If an application uses the
same request/response types and handler with `zrest`, that independent REST path
must require no changes or duplicate declarations in those application-owned
types.  The handler returns/selects one of the declared response types; it must
not format an HTTP response or an MCP result.

Model `tools/call` as a discriminated union inside its ordinary typed `params`
object.  `ToolsCallParams::name` is the discriminator and
`ToolsCallParams::arguments` is a `ZfJSON::Union` of the catalog's
object-formatted operation request alternatives.  On load, `name` is a borrowed
`ZuCSpan` and `arguments` temporarily holds the raw object node.  For a
non-trivial catalog, a compile-time `ZuMatcher` over the declared `ToolID`s
selects the operation, and the catalog dispatcher narrows the arguments union
to that operation's request type; compile out the empty catalog and use direct
`==` matching for a one-tool catalog.  On save, the active arguments member
selects the corresponding `ToolID` through a compile-time switch and the union
dispatches the arguments object to that member's `ZfJSON` save handler.  Do not
store a second Tx tool name which can disagree with the active request
alternative.

The reflected loaders are the sole structural conversion/defaulting policy; do
not maintain a second schema validator whose accepted inputs can drift from the
types passed to the handler.  Schema `Required` metadata describes the public
contract, while operation-level semantic rejection remains the application's
responsibility.

An application's independent `zrest` path may emit the response's HTTP status,
headers, and body.  The `zmcp` completion adapter emits a successful JSON-RPC
response containing an MCP `CallToolResult` with `content: []` and
`structuredContent` equal to the generated `{code, data?}` result object.
`data`, when present, is always a JSON
object or array; `zmcp` does not synthesize text or other content blocks.  Set
`isError` according to the selected response policy (by default, non-2xx is an
operation/tool error).  Match Kaldron for unknown tools: return an MCP tool
error with application code `404`, empty `content`, and structured content,
rather than a JSON-RPC unknown-method/invalid-params error.  An unusable
pre-dispatch envelope follows the non-throwing silent-close policy rather than
requiring a conformance diagnostic.  An application/operation failure after
selection is represented as a declared structured response where possible and
otherwise as a structured MCP tool error, not automatically as a JSON-RPC
error.

Handlers complete synchronously by default, matching the ordinary `zrest`
path, but may retain a shared completion token and complete asynchronously.
That token owns only the selected operation/result state, request ID, and fixed
metadata.  It uses a raw back-pointer plus stream identity/generation for its
longer-lived transport reply owner; the owner registers, invalidates, and drains
all dependent tokens before destruction.  It must not retain parser spans, raw
nodes, the HTTP parser, or a reference-counted back-pointer to its owner.  The
same handler/token API must work when called by REST or MCP.  An asynchronous
application must snapshot fixed context data or retain an application-owned
handle explicitly; borrowed session/stream context cannot escape the handler
call.

As with `zrest`, asynchronous scheduling and sequencing are application
concerns.  `zmcp` binds each completion token to the correct request ID and
reply sink, but does not queue application work, serialize handlers, reorder
completions, or impose an execution concurrency policy.  An application using
a session or persistent stdio stream must complete requests in request order;
the default synchronous path satisfies this naturally.  Independent stateless
HTTP POSTs retain their own response sinks.

The client API constructs typed calls and returns one of the declared typed
responses through the call object's `process()`/`failed()` callbacks, following
the typed `zrest` client model.  Use a monotonic per-peer request-ID generator with
overflow handling and a bounded, identified, intrusive pending-call `ZmHash`
keyed by the JSON-RPC ID.  Remove entries directly on every terminal path; do
not scan for completed calls.  Permit incoming string or integer IDs and echo
the same kind/value in server responses.  Other JSON ID forms narrow to absent
or uncorrelatable state and are handled without dereference, allocation growth,
or a dedicated conformance error.  Put owned string IDs on a named string heap.

## JSON-RPC and MCP core

### Owned envelope representation

Implement a small tagged ID and message classification rather than a generic
DOM for every message:

- `ID`: absent, signed integer, or owned UTF-8 string;
- message kind: request, notification, result response, or error response;
- borrowed method/parameter/result nodes valid only while dispatching the
  owned input buffer;
- owned error code/message/data only when a callback must outlive parsing.

Use `ZfJSON::scan()` and `ZfJSON::handler` as-is on mutable owned storage.  Do
not add a second JSON validator or stricter policies for duplicate keys,
unknown members, UTF-8, numeric conversion, or reflected fields.  Parse the
envelope once, classify `method`, then load only the selected typed
`params`/`result` subtree.  Ordinary methods load their reflected parameter
objects directly.  `tools/call` uses the following specialized parameter path:

1. Load `params` into `ToolsCallParams`.  Its `name` field maps to a `ZuCSpan`
   over the owned input buffer, while its `arguments` UDT field maps to a
   `ZfJSON::Union` whose temporary member is `const AnyNode *`.
2. Match `name` exactly in stable catalog order against every operation's
   required `ToolID`: return the structured 404 tool error directly for an
   empty catalog, use `==` for one tool, and instantiate `ZuMatcher` only for
   multiple tools.  An unknown name takes the same error path without narrowing
   arguments.
3. Once the tool is selected, require the arguments union to be in its raw-node
   state as an internal narrowing guard.  If no usable raw object node exists,
   take the ordinary structured tool/application failure path without
   dereferencing a raw node, classifying each malformed shape, or fabricating a
   JSON-RPC method error.
4. Dispatch the matched catalog index with `ZuSwitch`, load the raw arguments
   node with the selected operation alternative's handler, and replace the
   union's raw-node member with that typed alternative before invoking the
   application.  An internal tagged alternative delegates directly to the
   underlying application request handler and exposes that request to the
   application without another payload copy.

All arguments union alternatives must use object formatting.  Generic UDT
loading already applies the union handler's object-format check; do not add a
second raw-node validator or stricter field validator during discrimination.
Keep the owned input buffer alive through name matching and narrowing, then
through synchronous handler use of any request fields that still borrow from
it.  An asynchronously retained request must first move or copy the required
values into identified owned storage; neither a completion token nor deferred
log may retain `ZuCSpan`, `AnyNode`, or parser storage.  Release the input buffer
as soon as no typed request or response context borrows from it.  Never retain
or save the unresolved raw-node alternative.

For Tx, the typed call builder installs the selected operation request in the
arguments union and does not independently store `name`.  The
`ToolsCallParams` save handler switches on the active union member in the same
catalog order, emits that operation's compile-time `ToolID` as `name`, and emits
`arguments` through `ZfJSON::Union`'s member save dispatch.  Maintain a
one-to-one mapping between catalog index and union alternative; if application
request types are reused by multiple tools, use internal operation-tagged
alternatives that delegate JSON formatting directly to the shared request type
without another allocation or payload copy.  Compile out emission for an empty
catalog and reject a void, raw-node, or otherwise non-operation union state
before writing any bytes.  The modern HTTP header builder derives `Mcp-Name`
from the same active alternative/`ToolID` and derives applicable
`Mcp-Param-*` values from that typed request; it must not maintain parallel
routing-header discriminator state.  Result responses continue to load only
the selected pending call's typed `result` subtree.

This intentional Rx/Tx asymmetry belongs in one custom
`ToolsCallParams` JSON handler: load records the borrowed wire `name`, whereas
save ignores that Rx span and derives the canonical name from the active
arguments alternative.  Do not introduce separate hand-written JSON encoders
or duplicate the remainder of ordinary object-field handling.  Select the
custom handler through the established `ZfJSON_Fmt` ADL tag.

`zmcp` performs only the JSON-RPC/MCP classification needed for dispatch; the
application owns operation-level semantic validation and returns a declared
structured response/tool error when it rejects arguments.  Do not implement
batch dispatch, but also do not add a batch-specific rejection pass: a
top-level value which cannot classify as one supported envelope follows the
same safe non-dispatch/close path as any other unusable message.

Align valid unsupported-operation behavior with Kaldron: an unknown JSON-RPC
method returns `-32601` with the recovered request ID, and the outer application
containment boundary preserves an escaped exception's numeric application code
or uses `-32000` when none is supplied.  `ZfJSON` scan failure and corrupt
transport framing follow the silent-close policy above.  Notifications do not
receive JSON-RPC responses.

Do not serialize to an intermediate contiguous string on HTTP Tx paths: write
the envelope and typed payload directly into the provided Tx stream.  Stdio is
the exception because one complete line must be queued atomically; serialize
there into an identified pooled `ZiIOBuf`.

Use an absent ID where no correlatable request ID was recovered.  Preserve
string/integer semantic type and value, store integers only when representable
as `int64_t`, and emit our own integers in canonical decimal form rather than
preserving their lexical spelling.  Unsupported ID shapes do not justify a
separate validator or response.

### Protocol eras, lifecycle, and methods

Implement explicit modern and legacy paths behind one typed peer API; do not
force the stateless revision through a session state machine.

`2026-07-28` server behavior:

1. Answer optional `server/discover` with supported versions ordered
   `2026-07-28`, `2025-11-25`, the static tools capability, server identity,
   and cache hints.
2. Consume recognized protocol version, client capability/identity `_meta`, and
   routing metadata only when needed for a supported feature.  Ignore absent,
   unknown, or redundant advisory metadata rather than validating it on every
   request.
3. Dispatch `tools/list`, `tools/call`, cancellation, progress, and request-
   scoped logging selected by modern `_meta` without an initialize exchange,
   protocol session, session ID, standalone GET stream, DELETE lifecycle, or
   replay store.
4. Let the ordinary method table report a valid but unsupported method such as
   `initialize` on a selected modern request; the same endpoint must still
   recognize a genuine legacy handshake without an era-policing validation
   layer.

`2025-11-25` server behavior uses a flat `Fresh`, `Initializing`, `Ready`,
`Closing`, `Closed` state machine.  Store it as one compact signed state value
with a sentinel, not a set of Boolean flags; use `ZtEnum` only if public/runtime
name lookup is required:

1. Accept `initialize` as the first request, negotiate exactly `2025-11-25`,
   return server identity plus `tools` and `logging` capabilities, and
   establish an HTTP MCP session when session-oriented mode is enabled.
2. Accept `notifications/initialized` and enter `Ready`.
3. Permit `ping` as allowed by that revision.  An operation received before
   readiness must not reach application dispatch; handle it through the normal
   unsupported-state path without throwing or promising a particular negative
   diagnostic.
4. In `Ready`, dispatch `tools/list`, `tools/call`, cancellation, progress,
   `logging/setLevel`, and supported notifications.

On transport shutdown, both paths cancel/drain pending calls and reject late
response tokens without invoking their writers.  The client first probes
`server/discover`; it uses stateless per-request metadata when `2026-07-28` is
offered and otherwise falls back to initialize/initialized for `2025-11-25`.
The public client API hides this protocol-era selection while exposing the
selected revision for diagnostics and testing.

Keep built-in protocol methods separate from the application operation
typelist.  Generate and return the complete tool catalog in stable operation-
typelist order.  Accept the protocol's optional cursor field only as required
for envelope compatibility; do not create pagination state or return a
`nextCursor`.  Do not advertise `listChanged` and do not implement
`notifications/tools/list_changed`.

An invocation may emit requested progress and permitted protocol log
notifications, followed by exactly one terminal `CallToolResult`.  Do not add
an incremental or partial application-result extension.

Cancellation is advisory to the application.  On `2026-07-28` Streamable HTTP,
closing the in-flight POST response stream is the cancellation signal; do not
send a separate `notifications/cancelled` POST.  Use
`notifications/cancelled` for stdio in either era and for `2025-11-25` HTTP.
In every case, `zmcp` only notifies the application; it does not remove queued
work, stop a handler, reorder other work, or suppress an application completion
solely because cancellation arrived.  Queue removal and cancel-on-queue apply
only to asynchronous application scheduling and remain application concerns.
Only a retained asynchronous completion can still be actionable; ignore a
cancellation that arrives after synchronous completion or after the token is no
longer live.
Do not mark a live server completion token terminal merely because cancellation
was observed.  If its transport reply sink has closed, a later completion fails
through the ordinary unavailable-sink path; otherwise the application decides
whether to complete it.

Progress tokens remain opaque string/integer values and are copied only when
progress reporting was requested; modern HTTP progress/log notifications travel
on the originating POST response stream.

## Streamable HTTP adapter

### Server

Model the single endpoint with one `Zhttp::Parser`, not one parser per tool.
Its `operation()` selects the configured endpoint operations:

- `POST`: one JSON-RPC request, notification, or response;
- `DELETE`: optional `2025-11-25` explicit session termination only.

Do not implement a standalone legacy GET stream.  Unselected HTTP methods use
the ordinary `Zrest`/`Zhttp` route-miss behavior; do not add MCP-specific method
validation or diagnostic bodies.

One `Zmcp` server instance owns one endpoint and one full static catalog.  Do
not multiplex caller-filtered catalogs inside it.  Deploy distinct services on
distinct ports when different catalogs or authentication domains are needed.

For POST, emit canonical `Content-Type` and `Accept` headers from clients, but
do not reject an otherwise usable bounded request solely because either header
is absent, non-canonical, or does not mirror our output.  Collect fixed or
streamed request bodies incrementally into a bounded pooled `ZiIOBuf`; reserve
a known fixed length without default-filling it, and append streamed chunks in
place.  An overflow is corrupt transport input and silently closes the affected
scope before JSON parsing.  Accept HTTP chunked/streamed request bodies.

For `2026-07-28`, treat `MCP-Protocol-Version`, `Mcp-Method`, `Mcp-Name`, and
applicable `Mcp-Param-*` as routing hints.  The parsed JSON body and selected
peer/session era are authoritative; accept missing hints and do not cross-check
them against the body.  Dispatch without creating MCP protocol-session state;
transport-session and request-stream application contexts still apply.  A
recognized legacy initialize request selects the `2025-11-25` path; subsequent
requests use the established session era without repeatedly validating a
version header.

Validate `Origin` before protocol dispatch using policy owned by the individual
`Server` instance, never process-global state.  Server configuration supplies
the accepted-origin set or a non-blocking synchronous validator and determines
whether an absent Origin is accepted; rejection returns HTTP 403.  Keep the
example's listener loopback-only by default.  Host and reverse-proxy routing
remain ordinary zhttp/application configuration.

Authentication and authorization are application concerns.  Expose borrowed
request headers and the normalized operation identity through compile-time
header declarations and application hooks matching the pattern in
`zrest/example`: the application parses credentials, authorizes the operation,
and selects the declared response.  Hot-path hooks must not block an I/O shard;
authorization requiring external work uses the normal asynchronous completion
contract.  `zmcp` implements no credential, JWT, OAuth, or catalog-filtering
policy.

Response rules:

- accepted notifications and responses return HTTP 202 with no body;
- JSON-RPC requests return either one `application/json` object or an
  `text/event-stream` response;
- recoverable route/body outcomes use ordinary `Zrest`/`Zhttp` transport
  handling, while corrupt/unframeable/over-limit input closes silently;
- valid JSON-RPC errors normally remain HTTP 200 application responses;
- unselected HTTP operations never fabricate a JSON-RPC tool error.

Select fixed JSON versus SSE statically per operation/tool from its compile-time
metadata, using the same builder/static-dispatch pattern as `zrest`.  Built-in
MCP operations also declare a fixed policy.  Do not provide a server-global
mode, runtime negotiation callback, or post-dispatch switch.  The declared
representation is authoritative even when the peer's advisory `Accept` header
does not enumerate it.

Build fixed JSON responses like `zrest::ResBuilder`: declare content type and
a patchable content length, serialize once into the zhttp fixed-body stream,
then patch the length in `bodyHdrs()`.

Build SSE responses with `Zhttp::BodyPolicy::Stream`.  Each retained emitter
writes complete SSE records (`id`, optional `retry`, and one compact JSON-RPC
`data` value followed by a blank line) and returns `WriteOutcome::Stream` until
the terminal JSON-RPC response, then `WriteOutcome::End`.  The Builder's
`close()` must detach its MCP stream, invalidate its retained emitter, and
release queued messages.  All emitter calls occur on the owning zhttp Tx shard;
cross-shard tool completions place variable result state in an identified
Tx-consumable object or pooled `ZiIOBuf` and post only its handle plus fixed
stream identity metadata.  Dependent emitters use raw back-pointers; the stream
owner invalidates and drains them before destruction rather than being retained
by every queued message.

All streamed messages associated with a call remain on that POST response.
Neither protocol path opens a standalone GET stream or retains an SSE replay
store.  A lost transport terminates the affected response stream and clears the
legacy session's current-stream handle only if it still names that stream;
it does not destroy the logical session or replay the lost response.

Stateful `2025-11-25` operation is a required, first-class mode and is enabled
by default; configuration may disable MCP session creation only for explicitly
stateless legacy deployments:

- in stateful mode, return `MCP-Session-Id` on initialize, require it on later
  requests, and attach its current stream to the retained application session
  context;
- install each valid new stream on the session owner shard using the
  same close-and-replace cardinality as `ztcp` link/connection: move out and
  close a different current `zhttp` stream before storing the new handle; the
  new stream wins rather than being rejected or queued behind the old one;
- accept successive streams for the same logical session on the same or a
  different reusable transport connection, but retain at most one live stream
  for that session at any instant;
- ignore receive, completion, close, and disconnect callbacks from a displaced
  stream unless its identity still equals the session's current handle;
  displacement notifies the application that the old response sink is gone,
  but does not cancel or reorder application work;
- generate IDs with the existing Z TLS/random facilities, not a weak counter;
- use an identified intrusive `ZmHash` of `ZuRef` session references owned by
  one selected shard; sessions derive from `ZuObject`, and no other shard reads
  the hash or session state directly;
- remove a session immediately on DELETE, lifetime expiry, or explicit
  application close; ordinary stream/connection close only clears a matching
  current-stream handle; drain all states during shutdown;
- if expiration is configured, contain one scheduler timer by value in each
  session and erase by key when it fires rather than scanning the table; cancel
  every timer with `ZmScheduler::del`, inhibit callbacks, and drain a same-shard
  continuation before releasing the session.

A missing, unknown, or closed legacy session must never access session state.
Use the ordinary route/session lookup failure and client recovery path without
adding exact-status promises for invalid input.  Keep canonical outbound header
names in one central type list shared by client and server; inbound headers are
consumed only when needed to select a supported path.

### Client

Layer typed MCP calls over a `zhttp::Client` pool.  Every outbound JSON-RPC
message is a new POST with a fixed JSON body.  Send the required content type,
Accept values, selected protocol version, and era-specific metadata/headers;
send a session ID on each legacy request after the server assigns one.  Never
automatically retry or replay an MCP message, regardless of how much of the HTTP
request was committed.  Provide no retry metadata or special replay API.  The
application may issue another ordinary call after observing failure; transport
reconnection serves future calls and must not silently resubmit the failed one.

Delegate authority/configuration pooling, connection/session reuse, H1 keep-
alive, H2/H3 multiplexing, GOAWAY/drain, TLS/QUIC resumption, eviction, and
reconnect backoff unchanged to `zhttp`.  `zmcp` must neither reproduce nor
branch on those mechanisms.  Its only client-side scheduling beyond the
ordinary `zrest` builder/parser pattern is the sequencing needed by a pre-
modern logical MCP session and the rule that a failed JSON-RPC request is not
replayed automatically.

Track exactly one current request/response stream for each legacy MCP session.
A new call never blocks its caller waiting for the preceding stream.  Post it
to the session owner shard and place it in a bounded intrusive submission queue,
or return backpressure immediately when that queue is full; start it by
continuation only after the current stream becomes terminal.  Recovery or
explicit migration may submit the next stream with the same session ID on the
same or a replacement pooled connection; that new stream becomes current and
the server forcibly closes any lingering predecessor.  The client drops its old
stream reference and never intentionally continues to submit through both.  If
the server reports that the logical session is unknown or expired, discard the
ID and perform a new initialize exchange.  H2/H3 may still multiplex calls
belonging to different legacy sessions and modern stateless requests.

The response parser must accept:

- a fixed `application/json` body containing one JSON-RPC response;
- a streamed `text/event-stream` body containing notifications/requests and
  the eventual matching response;
- HTTP 202 with no body for sent notifications/responses.

Implement an incremental SSE decoder over zhttp's Rx queue.  Handle arbitrary
chunk boundaries, CRLF/LF, comments, multiple `data:` lines, `id`, and `retry`;
bound both one line and one event.  Dispatch each complete `data` event through
the same JSON-RPC peer used by stdio.  Do not coalesce the complete HTTP stream
into one allocation.

## Stdio adapter

Use exactly one compact UTF-8 JSON-RPC message per line.  A raw newline is the
frame boundary; an unusable frame or over-limit unterminated line closes the
stdio peer without a diagnostic frame.  Accept LF and a preceding CR, and never
emit pretty JSON.  Nothing except valid MCP messages may go to stdout.  Library
diagnostics use `ZiLog`; the embedding application owns all `ZiLog`
initialization, routing, levels, and stderr selection.

Implement stdio as a persistent duplex transport over `Zi::Handle` values with
one `ZmScheduler` isolated Rx worker dedicated to blocking input and one
isolated Tx worker dedicated to blocking output.  Do not involve `ZiEventLoop`
or fake `ZiMultiplex` sockets.  Provision these workers only when the client or
server is configured for stdio; the normal server configuration selects either
these workers or the Streamable HTTP listener, never both.  A stdio-configured
server takes ownership of the process stdin and stdout handles by default;
client and test construction transfer ownership of an already-open
input/output handle pair.  A caller which must retain either original supplies
a duplicate.  `zmcp` neither creates nor supervises a child process:

- follow the `ZdbPQ::Store` configuration precedent: stdio configuration names
  the Rx and Tx thread slots in the server's existing `ZiMultiplex`/
  `ZmScheduler`; initialization resolves the names with `ZiMultiplex::sid()`,
  rejects missing, shared, duplicate, or ordinary network Rx/Tx slots, and
  verifies that both selected slots are configured `isolated()`.  Zmcp installs
  its blocking workers into those slots; it does not construct standalone
  threads or a private scheduler;
- the dedicated workers are the reason stdio does not use `ZiEventLoop`: they
  make blocking console/pipe/file reads and writes the normal portable path on
  Windows, so no WFMO readiness adapter, overlapped I/O, `OVERLAPPED` lifetime,
  completion event, or handle-type inspection is needed;
- use `ZiFile` for owned handle/error/close, full-write behavior, and blocking
  input.  Call `ZiFile::read(..., false)` so each successful underlying read
  returns immediately, including a short read, rather than waiting to fill the
  supplied buffer.  Do not bypass the framework with duplicated platform-
  specific wrappers in `zmcp`;

- the Rx worker calls `ZiFile::read(..., false)` into pooled
  `ZiIOBuf`s in an infinite loop and returns on EOF or read failure, including
  teardown closing stdin from another thread; after each successful read it
  sets the buffer length and enqueues that owned raw buffer without parsing it;
  when the loop ends it records terminal state behind the already-enqueued
  buffers and makes the same coalesced post;
- after every Rx enqueue, use the `ZmPQRx::rcvd()`/`dequeue()` precedent: under
  one queue lock, set a `dequeuing` flag only on the idle-to-active transition
  and post one drain to the configured peer thread.  The peer-thread drain
  performs newline framing, retains only the incomplete tail, processes a
  bounded batch, reposts itself while buffers remain, then consumes terminal
  state only after prior buffers; otherwise it clears `dequeuing` under the same
  lock when the queue becomes empty so producer and consumer cannot lose a
  wakeup.  Implement this as a compact FIFO-specific helper over owned
  `ZiIOBuf` records; do not inherit the ordered/gap-repair `ZmPQRx` machinery;
- bound the Rx queue by both buffer count and bytes, and enforce the configured
  maximum line size in the peer-thread framer, so a slow configured thread or a
  peer that never sends a newline cannot grow memory without bound;
- the peer owner serializes each message plus newline into one pooled buffer
  and transfers that reference through a bounded queue to the Tx worker.  Like
  `ZiLog::work_()`, the Tx worker is an infinite loop which takes the next
  buffer from its queue, returns when the queue reports teardown, and otherwise
  calls `ZiFile::write()` to consume that complete buffer;
  it returns on write failure and posts the terminal result to the peer owner;
- use a bounded queue and propagate backpressure/failure instead of allocating
  indefinitely;
- serialize writes so concurrent tool completions cannot interleave lines;
- install a custom scheduler wake handler only for the dedicated Rx slot; on
  teardown it closes the owned stdin handle and then uses a `zmcp`-local
  native-thread interrupt (`pthread_kill()` with a non-restarting signal on
  POSIX, `CancelSynchronousIo()` on Windows) to guarantee that the blocking
  read returns.  Do not add this dependent-specific mechanism to `ZmScheduler`.
  Tx needs no scheduler wake handler: closing the owned stdout
  handle breaks any blocking write, while tearing down/signalling its queue
  releases a worker waiting for the next buffer.  The workers treat the
  resulting error as teardown, drain back to their owner continuations, and are
  joined only by the main/test driver;
- EOF begins graceful shutdown, and output-handle failure fails pending calls
  and drains ownership cleanly.

The default synchronous handler processes and completes requests in arrival
order.  An application may retain tokens for asynchronous completion, but it
must preserve per-stream request order itself, as with `zrest`; `zmcp` does not
hold or reorder later completions behind an earlier unfinished request.  The Tx
path preserves the order in which complete response frames are submitted and
never interleaves their bytes.

## Threading, ownership, and limits

Keep HTTP request parsing and transport state on the zhttp Rx shard, HTTP body
production on its Tx shard, stdio input/framing on its dedicated Rx worker,
stdio output/queue advancement on its dedicated Tx worker, and registry/session
state on one documented owner shard.  Public application entry points are thin dispatchers to trailing-
underscore owner methods using `rxRun`/`rxInvoke` or `txRun`/`txInvoke`; the
underscored methods use `ZiAssert` and access only their shard's members.  Never
make cross-shard access acceptable by adding locks, and do not write a function
which touches both Rx- and Tx-owned state.

Use the required three-phase asynchronous teardown for every sharded transport
owner: inhibit new ingress/timer/I/O work and initiate Rx teardown; from the
drained Rx continuation initiate Tx teardown; only from the drained Tx
continuation release ownership and notify completion.  For stdio, initiating a
direction's teardown closes its owned handle, causing a blocked system call to
return.  Rx performs that close and its `zmcp`-local native-thread interrupt in
its custom scheduler wake function; Tx
closes stdout directly and tears down/signals its input queue, without a custom
scheduler wake.  Cancel contained scheduler
timers before their owners can be released and drain late callbacks on the
timer's shard.  Do not block outside the main/test driver thread.  Stream,
session, pending-call, emitter, and completion-token owners must remove or
invalidate every dependent deterministically during these drains.

Cap work performed by any one scheduler turn.  Queued SSE events, stdio frames,
pending-call failures, and shutdown drains are processed in bounded batches
with posted continuations.  One JSON message, including an unpaginated
`tools/list` result, is the indivisible exception: its configured maximum size
is also its explicit per-turn work bound, serialization stops immediately on
overflow, and configuration documentation warns that raising the limit raises
worst-case shard latency.  No scheduler turn performs an otherwise unbounded
container traversal.

Follow the `zrest`/`zhttp` configuration model: define named compile-time
defaults, then expose runtime `Client`/`Server` configuration for operational
values where it makes sense.  Reuse the underlying `Zhttp::Config`,
`Zhttp::ServerConfig`, H2, and QUIC settings instead of shadowing them.  Add
`zmcp`-specific runtime settings only for protocol resources such as:

- maximum JSON message bytes;
- maximum stdio line bytes;
- maximum SSE line/event bytes;
- maximum pending calls per peer/session;
- maximum queued outbound messages/bytes;
- optional legacy session lifetime.

Do not add per-tool overrides for size, timeout, or queue limits.  Align timeout
semantics with `zrest`: the client uses zhttp's request-completion timeout and
the HTTP server uses zhttp's connection idle timeout, both runtime-configurable
and disabled by zero; do not add progress-resetting or a separate absolute MCP
deadline.  Stdio/application execution deadlines remain application concerns.

Use protocol requirements and the existing `zrest` defaults as the starting
point for each new compile-time default.  Allocate wire buffers from named
`ZiIOBufAlloc` pools and variable state from named Z heaps.  A request/parser
must release its body buffer as soon as typed arguments or the retained response
context own everything needed.  Every owner container has an immediate removal
path for success, error, disconnect, timeout, and shutdown.  Cancellation
removes client correlation/queue entries when it is terminal; advisory server
cancellation only notifies the application and removes cancellation lookup
state, while any application-held completion token remains bounded by the
application contract and is invalidated during owner shutdown.

## Build integration

Follow `zrest`'s Automake skeleton and the directory roles in `GUIDELINES.md`:

1. Add `zmcp` after `zhttp`/`zrest` in top-level `SUBDIRS` and
   `DIST_SUBDIRS`.
2. Add `zmcp` to the `configure.ac` module loop and add Makefiles for `src`,
   `test`, `itest`, `example`, and `interop` to `AC_CONFIG_FILES`.
3. Make `libZmcp.la` depend directly on `libZhttp.la` and its required lower
   layers; do not link `libZrest.la`.  Only `ZmcpZrestTest` links both libraries
   to verify the coincident application contract.
4. Install `ZmcpLib.hh`, `Zmcp.hh`, `ZmcpClient.hh`, and `ZmcpServer.hh`; keep
   factoring-only headers private.
5. Make module traversal `src test [util] itest example interop`, omitting
   `util` unless it has a real fixture role.  Keep normal module `make test`
   focused on `test` and `itest`; run third-party interoperability explicitly
   from `interop`.
6. Add no external runtime dependency.  Pin `github.com/mark3labs/mcp-go`
   v1.0.0-beta.1 in `interop` as the predominant Go SDK test fixture; it is not
   a library or installed-program dependency.  Upgrade the pin only as an
   explicit interoperability change with the full matrix rerun.

Preserve the current Clang debug feature flags and prefix.  When the new module
and its `configure.ac`/Automake integration are first added, run the repository
wrapper once with `z.config -c -L -d` plus those flags/prefix so `autoreconf`,
configuration, and every new `Makefile` are generated coherently; then perform
the required top-level build.  Do not rerun configuration or `make clean`
during ordinary targeted phase work unless build inputs actually change.  The
final sanitizer acceptance pass below is the explicit build-type exception and
requires a clean top-level rebuild.

## Implementation sequence

### Phase 1: scaffold and freeze the contracts

- Create the module/build skeleton and library export header.
- Define limits/configuration, JSON-RPC ID/error types, operation/request/
  response metadata, generated-tool traits, session/stream application-context
  hooks, per-operation fixed/SSE response policy, declared-header/auth hooks,
  and compile-time duplicate checks.
- Define `ToolsCallParams` with a zero-copy Rx `name` discriminator and an
  object-formatted `ZfJSON::Union` of catalog-ordered operation request
  alternatives, including an internal tagging policy for reused request types.
- Derive the multi-tool `ZuMatcher` inputs and the Tx
  union-index-to-`ToolID` mapping from the same operation typelist; compile out
  an empty catalog and use direct equality for a single tool.  Do not maintain a
  runtime name table or independently stored Tx field.
- Specify direct loading from the MCP `arguments` object into the reflected
  request type, with no REST location model in `zmcp`.
- Start `test/ZmcpZrestTest.cc` as a small compile-only application fixture
  proving the same request, response, and handler types independently satisfy
  coincident `zrest` and `zmcp` contracts without either library depending on
  the other.
- Review the API before transport work; propagating a breaking change is cheap
  at this point and preferred to compatibility wrappers.

### Phase 2: reflected schema and JSON-RPC codecs

- Derive JSON Schema 2020-12 directly from the existing, unchanged
  `ZfStruct`/`ZfJSON` metadata for scalar, enum, vector, map, UDT,
  optional/required, default, and range cases.  Express the schema and catalog
  as ordinary macro-reflected save views and compile-time unions serialized by
  `ZfJSON`; do not add schema properties to `ZfStruct`/`ZuFieldProp` or hand
  assemble JSON.  Keep emitted schemas valid on the `2025-11-25`
  compatibility path.
- Use each operation's required `ToolID` verbatim and generate its remaining
  metadata, object-shaped `inputSchema`, and response-union `outputSchema`;
  compare schema fixtures with the corresponding Kaldron transformations.
- Implement envelope classification, ID preservation, error mapping, and direct
  serialization.
- Implement `tools/call` argument-union raw-node capture, zero-copy name
  matching, catalog-selected narrowing, active-member-derived name emission,
  argument member dispatch, and typed pending-call result subtree loading.
- Implement the incremental SSE codec independently of zhttp.
- Unit-test schema recursion/overrides, observed `ZfJSON` behavior without a
  second validation layer, Kaldron-aligned unsupported-method/application error
  mapping, supported ID correlation, split input, multiple events, and
  configured size limits.  Test malformed bytes only as a safety/property
  boundary, with no asserted diagnostic.

### Phase 3: transport-neutral peer and tools

- Implement `server/discover`, tolerant modern metadata consumption, client era
  probing/fallback, and the legacy initialize/version/capability state machine.
- Build the immutable tool catalog and dispatch table from the operation
  typelist, and implement built-in method/operation dispatch without a runtime
  registration API.
- Cache the first complete `tools/list` result indefinitely in the Z client;
  provide explicit application-driven cache discard only when the caller
  changes peer/server identity.
- Extend the isolated `test/ZmcpZrestTest.cc` cross-library contract test to
  link both independent libraries and prove their coincident contracts can
  populate the same request type, invoke the same handler, and select the same
  response without a library-level adapter or dependency.  It stays in `test`
  because it launches no process and performs no transport I/O.
- Implement client pending-call correlation, server response tokens,
  advisory application cancellation notification, progress, modern request-
  scoped logging, legacy `logging/setLevel`, timeout, and deterministic
  teardown without an application work queue.
- Implement application context creation/access/destruction for peer sessions
  and request/response streams, with owner-shard teardown.
- Test with an in-memory loopback transport before adding I/O; drive concurrent
  completion with continuations and test synchronization, never sleeps or
  polling.

### Phase 4: stdio

- Implement bounded line framing and serialized output queues over an owned
  `Zi::Handle` pair using dedicated isolated Rx/Tx workers, with a close-only
  custom wake handler for stdin Rx and ordinary stdout/queue teardown for Tx;
  make no `ZiEventLoop` changes and introduce no stdio dependency on it.
- Configure both workers as named slots in the server's existing
  `ZiMultiplex`/scheduler and resolve and validate them during initialization,
  following `ZdbPQ::Store`; do not create ad hoc worker threads or an internal
  scheduler.
- Add a server mode which takes over stdin/stdout, plus client/test construction
  taking ownership of a supplied handle pair; leave process creation and
  supervision to the application.  Instantiate the workers only in stdio mode;
  HTTP-listener mode does not reserve them.
- Retain one application peer-session context for the handle-pair lifetime.
- Exercise fragmented/coalesced frames, concurrent calls, synchronous ordering,
  application-sequenced asynchronous completion, EOF, oversized lines, broken
  output, and shutdown with pipe-based integration tests on the current Linux
  Clang build, including close-to-unblock teardown of both dedicated workers,
  one outstanding Rx drain post under burst input, the enqueue-versus-idle
  flag-clear race, bounded drain reposting, and ordered terminal delivery after
  already-enqueued frames.  Use completion synchronization rather than
  elapsed-time assumptions.

### Phase 5: Streamable HTTP server

- Implement POST endpoint parsing, optional modern routing-hint consumption,
  Origin validation configured on each `Server`, application header/auth hooks,
  bounded request-body accumulation, and ordinary `Zrest`/`Zhttp` route/body
  outcomes; add DELETE only for the legacy compatibility path.
- Implement fixed JSON and retained-emitter SSE response Builders selected
  statically by each operation's response policy.
- Add required stateful legacy session mode with application context,
  a session-owned current-`zhttp`-stream handle, close-and-replace installation,
  stale-stream identity filtering, single-stream sequencing, deterministic
  expiry, and clean shutdown; omit standalone GET and verify the modern path
  allocates no MCP protocol session.  Exercise expiry through an injectable
  clock/timer boundary or explicit timer callback, not a sleep-based test.
- Attach transport-session and request/SSE-stream context on H1, H2, and H3.
- Verify H1, H2, and H3 with the same application implementation.

### Phase 6: Streamable HTTP client

- Implement fixed POST Builders and JSON/SSE response Parsers.
- Add discovery/preferred-version selection, legacy fallback, default stateful
  legacy session handling with an explicitly configurable stateless legacy
  mode, typed tool calls, per-session call serialization, current-stream
  replacement/migration with initialize fallback only for an unknown or expired
  logical session, and cancellation on timeout.
- Retry a request known not to have reached application processing within the
  configured `zhttp` retry budget.  After indeterminate transport failure,
  fallback or redirect, retry only requests whose trusted compile-time
  operation annotations declare them read-only or idempotent; never replay an
  indeterminate non-idempotent tool call.  Verify that an application can
  submit a new ordinary call after terminal failure.
- Verify through the common `zhttp` configuration that calls pass through its
  existing pooled H1/H2/H3 machinery; add no MCP-specific pooling, protocol,
  reconnect, resumption, or GOAWAY implementation.
- Verify disconnect and late-emitter behavior under each HTTP transport.

### Phase 7: examples, interoperability, and documentation

- Align `zmcp` and `zmcpd` with the existing `zrest` example client/server
  structure, CLI/configuration style, generated protocol headers, lifecycle,
  and authentication demonstration, adapted to MCP HTTP/stdio transport.  Use
  the same application-owned request/response and handler pattern without
  making either library depend on the other.
- Add a mandatory pinned `github.com/mark3labs/mcp-go` v1.0.0-beta.1 matrix:
  Go client to Z server and Z client to Go server over stdio and
  Streamable HTTP, exercising both `2026-07-28` and `2025-11-25` negotiation.
- Document the public contracts, threading rules, security defaults, transport
  configuration, supported MCP revision/features, and explicit deferrals in
  `zmcp/README.md`.

## Verification matrix

Unit tests must emit TAP through `ZuTestUtil`.  Integration tests which start
processes or network listeners belong in `itest`; third-party SDK cases belong
in `interop`.

Required coverage:

- modern discovery/direct-call success, tolerant consumption of absent/extra
  routing hints and optional metadata, legacy lifecycle success,
  preferred-version selection/fallback, modern log-level metadata, legacy
  `logging/setLevel`, and clean shutdown;
- `tools/list` stable order and byte-equivalent schema output, empty catalog,
  full-catalog visibility, modern `cacheScope: "public"` and `ttlMs` equal to
  `CatalogTTL`, no `nextCursor`, no list-change capability/notification,
  indefinite Z-client cache reuse without a second request, unknown tool as a
  Kaldron-compatible structured MCP error with code 404, unknown JSON-RPC
  method as `-32601`, application-selected argument rejection, successful typed
  output, tool-level error, and protocol-level error;
- exact generated schemas for direct object-shaped MCP arguments,
  required/default/range properties, nested reflected types, every declared
  response, and no-body responses;
- compile-time and application integration proving independent `zrest` and
  `zmcp` contracts accept the same application-owned request/response types and
  handler without a dependency, plus authorization identity, declared-header
  delivery, selected response code/type, and error-behavior parity;
- synchronous completion by default, asynchronous token completion, no
  library-side application work queue/reordering, and application-preserved
  per-session/per-stream response order;
- application session context retained across a sequence of legacy HTTP
  requests and for the lifetime of a stdio peer, distinct stream contexts per
  request/SSE stream, deterministic context teardown, and modern transport-
  session context without MCP-session affinity;
- exactly zero or one current live stream per legacy logical session,
  client-side submission sequencing, a new stream forcibly closing and
  replacing a lingering predecessor, stale old receive/completion/disconnect
  callbacks being ignored by identity, retained application context across
  replacement, and fresh initialization only when the logical session is
  unknown or expired;
- structured-only MCP results: empty `content`, generated
  `structuredContent`, object/array `data`, and omitted `data` for no-body
  responses;
- integer/string ID echo and correlation, safe ignoring of uncorrelatable
  responses without state growth,
  legacy/stdio cancellation notifications, modern HTTP stream-close
  cancellation, application notification without automatic queue removal,
  cancellation races, application-selected completion after cancellation,
  timeout, progress/logging on the originating stream, and unavailable-sink
  handling after disconnect;
- JSON fragmentation, direct `ZfJSON` parse behavior, application-owned
  semantic validation, configured message bounds, tolerance of unknown fields,
  no extra envelope/schema or batch validator, Kaldron-aligned
  unsupported-method/application error mapping, and allocation cleanup on all
  terminal paths;
- `tools/call` parsing with `params.name` retained as a zero-copy span,
  empty/single/multiple-tool discriminator paths and compile-time `ZuMatcher`
  selection for the multiple-tool case,
  `params.arguments` initially captured as the `ZfJSON::Union` raw-node member,
  catalog-index dispatch and narrowing to every operation request alternative,
  unknown names using Kaldron's structured 404 behavior and unusable arguments
  without unsafe narrowing or shape-specific diagnostics,
  reused application request types without wrapper allocation/copy, and no
  unresolved raw-node state surviving dispatch;
- `tools/call` building with no separately stored Tx name, every active
  arguments alternative mapping to the correct compile-time `ToolID`, and every
  arguments object dispatched through the active member type's save handler,
  with `Mcp-Name` and applicable `Mcp-Param-*` headers derived from the same
  active alternative;
- stdio LF/CRLF, multiple frames per read, frame split across reads, partial
  writes, queue saturation, isolated Rx/Tx ownership, the Rx custom wake
  handler closing stdin plus `zmcp`-local native-thread interruption, ordinary Tx
  close/queue teardown, coalesced cross-thread Rx
  drain posting without lost wakeups,
  close-to-unblock teardown, stdout purity, application-configured `ZiLog`, and
  EOF;
- per-operation compile-time fixed-JSON/SSE selection with no runtime override,
  HTTP POST JSON response and POST SSE response in both eras, isolated Origin
  policies on two `Server` instances, application-owned authorization,
  canonical outbound modern headers, tolerant inbound routing hints and media
  headers, no modern MCP-session allocation, legacy notification 202,
  sequential DELETE, ordinary disconnect preserving the logical session while
  clearing only a matching current-stream handle,
  expiry/application-close teardown, and non-duplication across streams;
- H1 chunking and connection close, H2/H3 logical stream reset, client/server
  disconnect during a call, server shutdown with open SSE streams, and no late
  writer invocation; verify that app-held completion tokens do not retain their
  stream/session owner and cannot access it after the asynchronous drain;
- repeated calls through the ordinary `zhttp` pool, with one MCP integration
  smoke path for H1, H2, and H3 and focused checks only for MCP-owned behavior:
  pre-modern session sequencing and idempotence-aware retry of a failed
  JSON-RPC request.  Prove that known-unprocessed requests may be retried,
  indeterminate non-idempotent tool calls are not, and the catalog's emitted
  `idempotentHint` is derived from the same trusted compile-time declaration.
  GOAWAY/drain, connection reuse, TLS/QUIC resumption, eviction,
  multiplexing, and reconnect backoff remain `zhttp` implementation and test
  responsibilities and must not be reimplemented or redundantly exhaustively
  tested in `zmcp`;
- pinned `mark3labs/mcp-go` client/server interoperability over stdio and HTTP
  in both protocol eras, including structured-only tool results;
- heap telemetry/residue checks after high request counts and repeated
  connect/disconnect cycles, including named heaps for buffers, pending/session
  nodes, owned IDs/errors, async results, and tagged argument alternatives;
- owner-shard assertions and Rx-to-Tx teardown ordering, per-session timer
  cancellation plus late-callback draining, bounded continuation batches,
  queue backpressure, and no polling/sleep-based concurrency tests.

Negative protocol-conformance tests are prohibited.  Do not construct tables
of invalid revisions, envelope fields, ID shapes, lifecycle orderings, HTTP
methods/media headers, or malformed JSON and assert their exact status, error
code, message, or diagnostic body.  Safety tests and fuzz/property tests remain
required for corrupt, truncated, unframeable, and over-limit input, but they
assert only bounded work/storage, no crash or memory error, no diagnostic or
response amplification, silent force-close where required, deterministic
cleanup, and continued correctness for later independent peers.  Origin,
authentication, and authorization tests remain required security tests rather
than negative protocol-conformance tests.

Use the existing configured Clang debug build and run the narrowest useful
target.  Whenever `zmcp/src` changes, rebuild it before any dependent target;
always build a directory's default `all` target before its `test` target:

```sh
# after scaffold/top-level build integration changes:
# first run z.config -c -L -d with the existing feature flags and prefix
make -j8
# during targeted zmcp iteration:
make -C zmcp/src -j8
make -C zmcp/test -j8 && make -C zmcp/test test
make -C zmcp/itest -j8 && make -C zmcp/itest test
make -C zmcp/interop -j8 && make -C zmcp/interop test
# before the repository suite near completion:
make -j8
make test
```

Before acceptance, run the full test matrix in a Clang ASan/LSan build configured
with `z.config -L -D` plus the existing feature flags and prefix, confirming
leak detection is enabled.  A build-type change requires a top-level
`make clean` followed by `make -j8`; do not mix sanitizer and non-sanitizer
module objects.  Then restore a compatible non-sanitized Clang build with
`z.config -L -d` plus the same feature flags/prefix and the same clean top-level
rebuild discipline, and run targeted and high-count Valgrind memcheck with leak
checking through `libtool exec`, never binaries under `.libs` directly.  Record
the exact configuration, toolchain, test commands, and results.  GCC and MinGW
execution are outside this plan, but implementation must remain portable to the
repository's gcc/clang, x64/ARM64, Linux/MinGW targets.

Use `rg` and compiler-warning-clean builds to confirm there are no concepts,
`requires`, scoped enums, anonymous namespaces, `static_assert`, broad lambda
captures, STL substitutes for Z facilities, unidentified allocations, stdio
example/log paths writing non-MCP data to stdout, or unbounded
message/session/stream containers.  Review the final diff manually against
every remaining `GUIDELINES.md` audit flag which cannot be found textually.

## Completion criteria

The work is complete when one set of typed operation definitions is usable
without transport-specific application code by:

1. defining each operation, request, potential responses, and handler once;
2. using those application-owned definitions independently with `zrest` and a
   generated `zmcp` tool, with schema and behavior parity but no inter-library
   dependency;
3. a Z client and Z server over stdio;
4. a Z client and Z server over H1, H2, and H3 Streamable HTTP;
5. the pinned `mark3labs/mcp-go` v1.0.0-beta.1 peer in both client/server
   directions and over both transports for `2026-07-28` and `2025-11-25`;
6. per-operation compile-time fixed JSON response mode and SSE response mode;
7. persistent H1/H2/H3 transport-session reuse with application-visible
   session/stream context and single-live-`zhttp`-stream, sequential legacy MCP
   sessions;
8. repeated modern discovery/direct calls and legacy initialization, call,
   cancellation, disconnect, and shutdown cycles with bounded memory and no
   residue.

Do not declare completion with only `tools/call` happy-path coverage.  MCP
lifecycle, error semantics, transport framing, bounded resource behavior, and
clean teardown are part of the layer's contract.
