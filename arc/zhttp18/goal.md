# Zhttp type erasure and application transport selection

## Objective

Move the bulk of HTTP client, server, connection, stream, and transport
implementation into library `.cc` files. Retain small typed application adapters
for parsers, builders, header catalogs, intrusive queues, and callbacks.
Applications must explicitly select any nonempty subset of these four profiles
at compile time:

| Profile | HTTP protocol | Transport |
| --- | --- | --- |
| `H1TCP` | HTTP/1.1 | TCP |
| `H1TLS` | HTTP/1.1 | TLS |
| `H2TLS` | HTTP/2 | TLS |
| `H3QUIC` | HTTP/3 | QUIC |

The application type, parser type, builder type, and catalog must stop at the
application adapter. None may appear in the types of native transport hubs,
connections, HTTP protocol machines, compression state, or logical streams.
Selection must work for clients, servers, and the lower-level facilities used
by WebSockets. Propagate breaking APIs through dependents; retain no legacy
implementations or compatibility forwarders.

## Evidence and present structure

The October 15 investigation used clang 23.1.1 with the configured
`-march=x86-64-v2 -O3 -g -DNDEBUG` flags. One `make -C zjrpc/example zjrpcd`
build took 99 seconds. A separate compile with the same flags took 119 seconds
and peaked at 3323 MiB RSS; syntax checking alone took 34.7 seconds and peaked
at 2159.7 MiB. These are separate observations, not phase timings from one run.
The object was approximately 110 MiB, with 77 MB of debug sections. Thousands
of its definitions referred to HTTP/3 and QUIC although its HTTP configuration
enabled only TCP. Re-establish the baseline on the implementation revision.

The relevant expansion paths are:

- `Zjrpc::HTTPServer<App, Catalog>` derives from
  `Zhttp::Server<Impl, ReqParser, ResQ>`. `ZhttpServer.hh` nests protocol parsers,
  response drivers, hubs, and links within that specialization. It stores
  `Hub<H1TCP>`, `TLSHub`, and `Hub<H3QUIC>` and initializes them behind runtime
  configuration checks.
- `Zhttp::Pool<Client, TxQ, ResParser>` has the same coupling through its TCP,
  TLS, and QUIC pools, sessions, requests, attempts, and links.
- `Zhttp.hh` and `ZhttpTransport.hh` expose protocol implementation headers and
  native `Ztcp`, `Ztls`, and `Zquic` templates to application translation units.
- `Zhttp::Hubs` already erases lifecycle callbacks, and `Route<Builder>` erases
  some link operations. Their underlying objects remain application-dependent;
  these existing boundaries do not prevent transport instantiation.
- `zws` directly consumes `ClientHub`, `ProtocolServer`, `ClientLink`, and
  `ServerLink`, including Extended CONNECT. Migrating only the top-level HTTP
  wrappers would leave a second path for the same template expansion.

## Public selection contract

Introduce a lightweight `ZhttpProfiles.hh` containing the existing profile
tags and `Profiles<...>`. Use `ZuTypeList`, detector traits, `ZuIfT`, and
`ZuAssert` to reject empty lists, duplicates, and unsupported tags. Make the
profile set explicit in the server and pool template parameters. Provide
`AllProfiles` as an explicit opt-in alias; do not default new wrappers to all
profiles.

Proposed API shape (names describe the intended API, not existing code):

```cpp
using LocalHTTP = Zhttp::Profiles<Zhttp::H1TCP>;
using SecureHTTP = Zhttp::Profiles<
  Zhttp::H1TLS, Zhttp::H2TLS, Zhttp::H3QUIC>;

struct LocalServer :
  Zhttp::Server<LocalServer, ReqParser, ResQ, LocalHTTP> {
  // Application callbacks only.
};

struct Pool : Zhttp::Pool<App, TxQ, ResParser, SecureHTTP> {
  // Application queue, archive/retrieve hooks, and parser binding.
};

struct App : Zhttp::Client<App, Pool> { };
```

`Client` obtains the selection from its pool type, with one source of truth.
Typed wrappers in `zjrpc`, `zrest`, and `zmcp` propagate the selection to the
HTTP layer. WebSocket wrappers retain an explicit single profile and use the
same concrete library machinery. A later heterogeneous client can use explicit
separate pool types; that is outside this change.

Selection has the following guarantees:

1. Application adapters reference descriptors/factories only for selected
   profiles. They include no native transport or HTTP state machine definitions.
2. An endpoint cannot configure, negotiate, discover, redirect to, or fall back
   to an excluded profile. Runtime settings may narrow the selected set.
3. Disabled profiles allocate no hub, connection pool, compression state, or
   protocol-specific storage for that endpoint. A shared TLS connection may
   support the selected H1/H2 combination.
4. There is no heavy `Core<Mask>` specialization for each of the 15 subsets.
   Selection generates small adapter/registration code; library cores and
   protocol implementations are compiled once independently of the application.

This is an application compilation and capability guarantee. The existing
shared `libZhttp` may still contain all four implementations and depend on
`libZtls` and `libZquic`. Removing those libraries from a deployed TCP-only
binary requires a separate library packaging decision. Do not claim that a
compile-time subset removes shared-library dependencies.

## Concrete implementation and registration

Create concrete `ClientCore`, `PoolCore`, and `ServerCore` implementations.
Give them concrete request/attempt, route, session, and stream records. Keep
protocol-neutral limits, scheduling, admission, routing, retry, cancellation,
and shutdown state in these cores.

Create concrete native adapters for TCP, TLS, and QUIC on both client and server
sides. They may privately instantiate existing `Ztcp`, `Ztls`, and `Zquic`
templates on library-owned types in `.cc` files. Their HTTP H1/H2/H3 machines
must also depend only on library-owned types. This removes the application
multiplier without requiring a simultaneous rewrite of native transport APIs.

Use explicit operation tables plus object/context pointers for the erased
boundaries. Avoid virtual application hierarchies, `std::function`, RTTI,
runtime type registries, and allocations for individual callback closures.
Immutable callback tables have static lifetime and contain only the operations
required by their interface. Each interface has documented ownership and shard
requirements.

The lightweight profile traits expose declarations of exported descriptor
accessors. The selected wrapper registers only its descriptors with the core.
The core operates on those supplied descriptors; it must not contain an
all-profile factory switch or globally registered list that references every
backend. The core copies the small descriptor handles into its own storage and
does not retain a pointer to temporary registration data. Use framework
containers sized from the selected set where storage is needed.

H1TLS and H2TLS descriptors share a TLS transport family. Registration groups
them into one TLS listener/pool with an allowed HTTP-version mask. It must not
open two competing listeners on the same address or make two client TLS
connections merely to negotiate HTTP versions. Concrete H1 and H2 machines
are activated according to that mask and ALPN. Their shared TLS backend remains
library-owned even when both versions are selected.

## Application erasure boundaries

| Boundary | Typed work retained in headers | Concrete work moved into `.cc` |
| --- | --- | --- |
| Endpoint | Bind `App` callbacks and selected descriptors | Lifecycle, listeners, pool coordination, statistics, admission |
| Parser | Construct/reset/destroy the application parser; typed header and completion callbacks | H1 framing, H2/H3 frames, validation, message state, flow control |
| Builder | Read application fields and invoke body producers | HTTP encoding, framing, compression, length patching, commit accounting |
| Headers | Catalog matching and typed callback dispatch | Wire decoding, field-section rules, HPACK/QPACK connection state |
| Client queue | Adapt `TxQ`, archive/retrieve, typed message ownership | Attempts, retries, redirects, discovery, cancellation, link scheduling |
| Route/stream | Typed response submission and application state access | Route identity, lifetime, sequence/generation checks, reset, close, backpressure |

### Parsers and header catalogs

Define a concrete parser sink with operation callbacks for message start,
method/target or status, headers, body information, body chunks, completion,
failure, and reset. Compile the wire parser against this sink once per protocol.
The sink carries the application parser context; it does not carry a concrete
native link type.

Retain compile-time header names, fixed values, and application-specific
matching. A catalog adapter maps incoming fields to compact IDs and dispatches
the corresponding typed application callbacks. Unknown headers and trailers
retain their current callbacks and field-section semantics. Do not scan every
catalog entry for each field or construct a runtime hash for every request.

Expose immutable catalog metadata needed for outbound headers and HPACK/QPACK
seed plans through a small erased view. Preserve fixed-value omission,
sensitive/never-index rules, seed ordering, duplicate-header behavior, and
dynamic header enumeration. Protocol encoders must not instantiate on a
catalog or application builder.

### Builders and body streams

Define one concrete writer/view for application output and one concrete body
input/view for application consumption. They expose the span, reserve/commit,
consumption, failure, and backpressure operations required by existing parser
and producer contracts. Preserve mutable borrowed input and direct writes into
pooled `ZiIOBuf` storage. No full-body staging buffer, temporary contiguous
conversion, or extra copy may be introduced by erasure.

Typed adapters call the application producer using the same concrete writer
type for every HTTP profile. A deferred producer stores its state in the
existing retained message/stream object, or a justified pooled node; never
retain a stack callback context after the call. Fixed, optional, streaming,
close-delimited, SSE, HEAD, and bodyless-response behavior must retain their
current accounting and completion semantics.

Resolve protocol dispatch at connection/stream binding and invoke erased
operations at message, header, or buffer-chunk boundaries. Keep byte scanning,
frame processing, compression loops, and buffer filling concrete and direct
inside the library. Measure the remaining indirect calls before accepting the
performance tradeoff.

### Requests, queues, and parser storage

Give retained request/response nodes an embedded concrete record, or an
explicit retain/release interface that adjusts to their actual intrusive base.
Do not reinterpret an arbitrary `ZmList::Node`/`ZmPQueue::Node` as a common
base; the framework's node wrappers can change layout. Retain the existing
typed queue adapter where its key, archive/retrieve hooks, or node type require
templates. Operations cross into the core with concrete records and IDs.

Keep application parser storage owned by the application adapter for the
lifetime of its logical request/stream. Use existing request/stream nodes when
possible; otherwise use a typed pooled node with creation/destruction thunks.
Do not introduce guessed inline buffer sizes or allocate a parser per callback.
The core retains a context plus its lifetime operations, not its C++ type.

Preserve client request and attempt IDs, slot generations, unordered queue
acknowledgements, replay eligibility, partial-send outcomes, and exactly-once
completion/retirement. Erasure must not merge request lifetime with the lifetime
of one transport attempt.

### Routes and lower-level streams

Extend the existing route erasure into a concrete route/stream handle usable
by parser completion, response submission, cancellation, SSE, and WebSockets.
Retain physical-session identity separately from logical-stream identity.
Preserve request sequence and generation checks so a late completion cannot
target a reused connection or stream.

Replace application dependence on native `Link` types, `rxState()`, or
`app()->txRun()` with the required concrete handle operations. Specify borrowed
handles for callbacks and retained handles for asynchronous work. Keep existing
intrusive ownership where retention is required; use raw dependent back-pointers
where the owner drains all dependent work before destruction.

Provide the concrete channel operations needed by H1 Upgrade and H2/H3 Extended
CONNECT. `zws` binds its typed framing/application adapter to that channel. It
must not rebuild an application-specific native hub through the old
`ClientHub`/`ProtocolServer` inheritance path.

## Runtime configuration, ALPN, and fallback

Derive unspecified runtime defaults from the compile-time set. Existing client
defaults enable all transport families; carrying them across unchanged would
make a restricted client invalid. Preserve explicit-setting information during
configuration overlays and validate effective per-pool policies before startup.

Explicitly enabling an excluded profile or forcing an unavailable protocol is
an error. Preference policies rank only selected, runtime-enabled candidates;
a preference with one legal candidate selects that candidate. Validate an empty
effective set as an error. Expose fluent configuration and policy selection only
where supported by the typed wrapper, using SFINAE; validate dynamically loaded
configuration against the same descriptor set in the core.

| Selected TLS profiles | Required behavior |
| --- | --- |
| H1TLS only | Advertise/accept HTTP/1.1; permit the existing no-ALPN H1 case; never activate H2 |
| H2TLS only | Advertise/accept `h2`; fail a connection that cannot negotiate it; no H1 fallback |
| H1TLS and H2TLS | One TLS connection/listener; ALPN preference may choose H2 and permit H1 according to runtime policy |
| Neither | No TLS listener/pool, ALPN setup, or TLS configuration activation |

H3 is independently selectable and may operate without TLS-over-TCP fallback.
Discovery and Alt-Svc candidates are filtered through the effective profile
set. DNS results, retry, redirects, and fallback cannot expand that set.
An HTTPS-only application encountering an HTTP redirect reports an unsupported
route through the normal result path; it must not create a TCP backend.
Preserve retry/replay safety when the next legal protocol is chosen.

Emit Alt-Svc for a server only when its H3 endpoint is actually enabled and
available. Preserve the existing supported protocol scope: H2 means H2 over
TLS; this change does not add h2c or HTTP trailers on application output.

## Ownership, sharding, and allocation

Document the owning shard on each erased operation. Keep Rx protocol parsing,
receive flow control, and Rx queues on Rx; keep encoding, compression Tx state,
body production, and Tx queues on Tx. Cross-shard entry points immediately post
to the owning shard and capture handles plus fixed metadata. Do not add locks
to permit non-owner access.

Shutdown must disable ingress, drain Rx, drain Tx, finish cancellation and
completion callbacks, and release ownership in that order. App contexts,
operation tables, parsers, builders, and route handles remain valid through the
last permitted callback. Partial initialization must unwind already created
backends. Blocking wrappers remain restricted to appropriate non-I/O callers.

Keep core state inline where public layout can remain lightweight. Opaque
protocol implementations may use one pooled allocation per enabled hub/pool,
with existing heap IDs and tuning preserved or explicit new `ZmNHeapID`s.
Avoid per-call erasure allocations and extra per-packet reference churn.
Connection and stream storage must track workload/configuration bounds.

## File and build layout

Proposed responsibilities; adjust filenames to avoid duplication with existing
concrete facilities as implementation proceeds:

| Files | Responsibility |
| --- | --- |
| `ZhttpProfiles.hh` | Lightweight tags, selected-set validation, descriptor declarations |
| `ZhttpConfig.hh` | Public configuration and policy types, without native transport templates |
| `ZhttpCore.hh` and `.cc` | Concrete records, handles, results, shared ownership and accounting operations |
| `ZhttpClient.hh`, `ZhttpServer.hh` | Public interfaces and thin typed adapters |
| `ZhttpClient.cc`, new `ZhttpServer.cc` | Concrete client/pool and server coordination |
| `ZhttpTransport.hh` and `.cc` | Lightweight transport-facing contracts and shared lifecycle implementation |
| New `ZhttpTCP.cc`, `ZhttpTLS.cc`, `ZhttpQUIC.cc` | Concrete native bindings and profile descriptor definitions |
| `ZhttpH1.cc`, `ZhttpH2.cc`, `ZhttpH3.cc` | Protocol machines and message encoding/decoding |
| Existing HPACK/QPACK, discovery, URL, SSE files | Retained concrete facilities, with application dependencies removed where needed |
| Private implementation headers under `zhttp/src` | Library-only template glue and shared concrete definitions |

Remove heavy transitive includes from `Zhttp.hh`, public configuration,
client/server adapters, and transport contracts. Forward declarations alone
are insufficient if a public inline destructor or member still requires a
heavy complete type; move those operations out of line too. Protocol headers
may expose lightweight wire types needed by callers while their implementation
definitions move to private headers or `.cc` files.

Update `zhttp/src/Makefile.am`, header installation lists, generated include
links, and exported `ZhttpAPI` declarations for Linux and Windows. Private
native specializations stay internal to the library. Shared builds may export
all selected-descriptor entry points; application adapter objects must reference
only the requested ones.

## Implementation sequence

1. **Record contracts and baseline.** Inventory public and lower-level users,
   callback shards, stream operations, queue hooks, and lifecycle guarantees.
   Record full and syntax-only compilation time/RSS, dependency lists, object
   sections, and symbols for `zjrpcd`, the JSON-RPC client, and a full-profile
   HTTP client/server. Measure representative runtime latency and allocations.
2. **Introduce the lightweight interfaces.** Define profiles/configuration,
   descriptors, core records, parser/builder operations, and stream handles.
   Implement catalog and queue adapters. Pin down retain/release and deferred
   producer contracts before moving transport code.
3. **Complete H1 TCP end to end.** Implement concrete client and server TCP
   bindings and H1 machines. Move lifecycle and common message/accounting code
   into `.cc`. Migrate one TCP-only application through the full API. Check that
   its dependencies and symbols no longer expose native/protocol templates.
4. **Complete TLS and H2.** Move TLS connection ownership, ALPN negotiation,
   H1 TLS, and H2 sessions/streams behind concrete bindings. Implement the three
   TLS selection cases and shared-listener behavior. Preserve compression seed
   plans and per-shard state ownership.
5. **Complete H3.** Move QUIC/H3 adaptation, stream ownership, QPACK, control
   streams, and GOAWAY handling into the library. Instantiate native QUIC
   templates only on library-owned types. Validate H3-only operation and allowed
   fallback with mixed selections.
6. **Finish common coordination.** Move the remaining client pool scheduler,
   discovery, retry/redirect/cancellation machines and server admission,
   response ordering, streaming, and shutdown into the concrete cores. Eliminate
   templates whose parameters are no longer needed; do not stop after moving
   only the lowest transport layer.
7. **Migrate all consumers.** Update `zhttp` programs and tests, `zws`, `zjrpc`,
   `zrest`, `zmcp`, `ztc`, `zum`, and `zdash`, plus any additional users found by
   repository search. Select each program's actual profiles explicitly. The
   current `zjrpcd` HTTP branch selects H1TCP; its WebSocket branch selects H1TCP
   through the erased channel API. Full protocol utilities opt into all four.
8. **Remove old paths and finalize.** Delete superseded application-dependent
   hubs, links, sessions, factories, and heavy public includes. Update the README
   examples and configuration documentation. Complete correctness, compile-cost,
   and runtime performance checks below before declaring the migration done.

Stages are migration checkpoints for one breaking refactor. Temporary internal
scaffolding must be removed by the final stage; keeping the old public path is
not a completion option.

## Validation and completion criteria

### Structural and compile cost

- Compile lightweight client and server fixtures for all 15 nonempty subsets;
  include expected compile failures for empty, duplicate, and unknown profiles.
  Verify subset descriptor references in object symbols without relying on
  optimization or linker dead stripping.
- Inspect dependency files and symbols: ordinary application headers must not
  pull `Ztcp.hh`, `Ztls.hh`, `Zquic.hh`, or private H1/H2/H3 machines into the
  application translation unit. No native hub/link specialization may contain
  the application, parser, builder, queue, or catalog type.
- Compare singleton and all-profile fixtures. Adding a profile should add small
  selection glue, not reproduce a protocol implementation in the object.
- Repeat original `zjrpcd` measurements with the same toolchain, flags, machine,
  and controlled build load. Measure the full JSON-RPC client too. Record median
  compile wall time, peak RSS, object/debug bytes, and library build cost. A
  proposed minimum gate is a 50% reduction in `zjrpcd` compile time and peak RSS;
  this is a target to validate, not a predicted result. Explain any remaining
  expensive application templates before accepting a weaker result.

### Behavior and lifetime

- Use existing `ZhttpTransportContractTest`, `ZhttpHubLifecycleTest`,
  `ZhttpClientCancelTest`, parser/message, H2 stream, HPACK/QPACK, and boundary
  tests, extending them for the concrete interfaces with `ZuTest`/`ZuTestUtil`.
- Extend `zhttp/itest/zhttpmatrix.cc` and application/lifecycle/stream/fallback
  integration coverage for each singleton, H1TLS+H2TLS, H2TLS+H3QUIC, and all
  profiles. Cover the remaining mixed subsets through configuration/selection
  tests and exercise any distinct runtime behavior they expose.
- Verify excluded runtime configuration is rejected, defaults derive correctly,
  overlays preserve explicit settings, H2-only peers cannot fall back to H1,
  and discovery/redirect/retry cannot select an excluded profile.
- Exercise cancellation during DNS/connect/handshake/body transmission,
  partial initialization, queued responses, stale routes, slot reuse, graceful
  shutdown, flow-control stalls, and exactly-once terminal callbacks.
- Run H1 Upgrade, H2/H3 Extended CONNECT, SSE, and dependent JSON-RPC/REST/MCP
  integration coverage. Preserve existing external HTTP/2 and HTTP/3 interop
  checks where configured; report unavailable external services explicitly.

### Runtime and build discipline

Measure small-message latency (including tail latency), streaming throughput,
allocations, copies, and per-connection/per-stream memory for all four profiles
against the baseline. Include warm connections, concurrent streams, and
backpressure. Resolve a repeatable regression outside measured run-to-run noise;
do not accept per-byte indirect calls or new body staging to meet compile goals.

Build libraries before dependents using `make -j4` and run the relevant module
tests. After the API migration, use a top-level `make -j4` to catch missed
dependents. Use the existing configured build consistently; if changing build
type, follow `z.config` and the required clean rebuild. Validate gcc and clang,
Linux and Windows/msys2/mingw exports, and ARM where available. Record toolchains
and any platform coverage gaps.

The refactor is complete when the heavy implementation is compiled within
`libZhttp`, all callers use explicit profile selections and erased boundaries,
all four profiles retain their behavior, and the measured compile improvement
holds without an unaccepted runtime regression.
