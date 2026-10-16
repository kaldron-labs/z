# Zhttp erasure implementation plan

Implement [zhttp_erasure.md](zhttp_erasure.md) as one breaking refactor. This
plan prescribes work; it does not report completed implementation or measurements.
The four sections below settle the design, build policy, implementation slices,
and final consistency review. Follow [GUIDELINES.md](GUIDELINES.md), subject to
these explicit user constraints:

- **DO NOT ADD VALIDATION BEYOND THE CURRENT IMPLEMENTATION.** Preserve existing
  checks, accepted/rejected inputs and error paths. Moving or precomputing a check
  must not duplicate it or broaden its scope. Add no defensive assertions,
  profile validator, configuration preflight, header rules or lifetime checks.
  Every retained production check must map to an existing baseline check.
- Preserve compile-time client and server request/response definitions, including
  custom headers and typed handlers. Servers dispatch incoming requests; clients
  first correlate responses to pending requests, then dispatch in that context.
- Call the forbidden/pseudo classification helpers only during catalog
  configuration, as specified below, never per request.
- Keep the current build configuration and artifacts. Batch changes; rebuild and
  test only at the designated major milestones. Defer dependent rebuilding to
  the final slice. Other compilers, platforms, debug/release flags and sanitizer
  configurations are outside scope. Use Valgrind only for corruption debugging
  when needed and the final leak/corruption gate.

These constraints supersede conflicting recommendations in the original design,
including new validation and alternate build configurations. Tests establish
preservation and erasure; they do not authorize new production checks.

## 1. Simplified design and guideline audit

### 1.1 Keep only the required boundaries

Applications instantiate small profile, catalog, parser, builder, queue and
callback adapters. Native hubs/links, wire machines, compression, logical stream
machinery and client/server coordination compile inside `libZhttp` on concrete
library-owned types. Private templates instantiated only on library types remain
appropriate. Do not stop after extracting the native transport layer.

| Element | Prescribed minimum; redundant work to remove | Applicable guidelines |
| --- | --- | --- |
| Profile selection | Explicit selected descriptors; one concrete core implementation, no heavy `Core<Mask>` or global backend registry | [Static polymorphism](GUIDELINES.md#static-polymorphism-and-constraints), [Code structure](GUIDELINES.md#code-structure) |
| Client/server coordination | Reuse concrete records and move existing algorithms into `ClientCore`, `PoolCore`, `ServerCore`; no parallel scheduler, cache or policy engine | [Repair procedure](GUIDELINES.md#repair-procedure), [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow) |
| Erased interfaces | Static operation tables plus context, containing only operations required by existing callers; no virtual application hierarchy, RTTI or closure allocator | [Callbacks](GUIDELINES.md#callbacks), [Framework fit](GUIDELINES.md#framework-fit) |
| Message dispatch | Keep typed catalogs and existing request/stream bindings; no runtime schemas, handler registry, generic decoded-message object or extra response search | [Compile-time matching](GUIDELINES.md#compile-time-data-and-matching) |
| Header metadata | One immutable catalog view for matching/encoding/seed inputs; reuse cached classification, no request-local map or dynamic-name cache | [Code structure](GUIDELINES.md#code-structure), [Storage and capacity](GUIDELINES.md#storage-and-capacity) |
| Parser/queue storage | Reuse typed session/operation/stream and intrusive message nodes; no guessed parser buffer, generic allocation base or separate refcount | [Containers and intrusion](GUIDELINES.md#containers-and-intrusion), [Heap allocation](GUIDELINES.md#heap-allocation) |
| Body and channel access | One concrete input view, writer and route/channel interface; direct pooled-buffer access, no body staging or per-byte indirection | [Data movement](GUIDELINES.md#data-movement-and-initialization), [I/O sharding](GUIDELINES.md#io-sharding) |
| Configuration | Reuse existing explicit-setting/overlay and policy machinery; selected descriptors bound capabilities, no new configuration framework or server enable/disable API | [Metadata](GUIDELINES.md#metadata), [Accessors](GUIDELINES.md#accessors) |
| Validation | Move existing checks with their owning code; delete proposed duplicate/new checks, including negative fixtures that require new rejection rules | [Repair procedure](GUIDELINES.md#repair-procedure) |
| Files and migration | Extend existing modules, delete superseded implementations, propagate breaking APIs; no compatibility facade or transport API redesign | [Module layout](GUIDELINES.md#module-layout), [Scope and cleanup](GUIDELINES.md#scope-and-cleanup) |

For every added record, operation, allocation and algorithm, identify its existing
caller or required boundary and its owner. Reuse an equivalent facility rather
than adding another. Do not introduce machinery solely to support a speculative
future transport, schema, plugin or heterogeneous client.

Operation tables are immutable with static lifetime. Endpoint bindings and their
application contexts remain alive through shutdown and the final permitted
callback; do not allocate tables or callback closures for individual messages.

### 1.2 Selection and compilation boundary

Use the following public forms; the final profile argument is mandatory:

```cpp
using LocalHTTP = Zhttp::Profiles<Zhttp::H1TCP>;
using SecureHTTP = Zhttp::Profiles<
  Zhttp::H1TLS, Zhttp::H2TLS, Zhttp::H3QUIC>;

Zhttp::Server<App, ReqParser, ResQ, LocalHTTP>
Zhttp::Pool<App, TxQ, ResParser, SecureHTTP>
Zhttp::Client<App, Pool> // Selection comes only from Pool.
```

`AllProfiles` is an explicit alias for all four. Support all 15 nonempty subsets
through `ZuTypeList`, existing traits and `ZuIfT`; preserve existing constraints
without adding diagnostic assertions or a validation pass. Repeated tags must
not register a backend twice. WebSocket wrappers retain explicit single-profile
selection and bind the same library machinery.

Each profile trait names an exported descriptor accessor. A wrapper references
only selected accessors. The core copies the small descriptor handles into
framework storage sized by that selection; no registration temporary escapes.
Descriptors supply family identity and client/server factories. H1TLS and H2TLS
contribute a version mask to **one TLS listener/pool**, not separate competing
listeners or duplicate connections. Group the supplied descriptors during setup;
no all-profile factory switch or globally registered list belongs in the core.

Allocate backend and protocol-specific state only for runtime-enabled selected
families/versions when needed. Keep H2/H3 compression and stream storage out of
common records. Existing initialization progress must support partial unwind.
The selection is an immutable capability ceiling: configuration, ALPN, discovery,
redirects and retries cannot activate an excluded profile.

`libZhttp` may still contain all backends and retain TLS/QUIC dependencies.
Application compilation isolation is the goal; library packaging changes, h2c,
new outbound trailers and heterogeneous pool selection are outside scope.

Use this ownership layout, consolidating definitions where existing files suffice:

| File or group | Responsibility |
| --- | --- |
| New `ZhttpProfiles.hh` | Lightweight tags, selection and descriptor accessor declarations |
| New `ZhttpConfig.hh` | Existing configuration/policy types separated from native implementations |
| `ZhttpCore.hh/.cc` | Shared concrete records, results, input/writer and route lifetime contracts |
| `Zhttp.hh`, `ZhttpClient.hh`, `ZhttpServer.hh` | Small typed application adapters and public declarations |
| `ZhttpClient.cc`, new `ZhttpServer.cc` | Concrete client/pool and server coordination |
| `ZhttpTransport.hh/.cc` | Lightweight backend/channel contracts and shared lifecycle implementation |
| New `ZhttpTCP.cc`, `ZhttpTLS.cc`, `ZhttpQUIC.cc` | Concrete native bindings and descriptor definitions |
| Existing H1/H2/H3, HPACK/QPACK, fields, URL, discovery and SSE files | Existing protocol/facility implementations with application parameters removed |
| Private implementation headers, only where shared definitions require them | Library-only native template glue and concrete definitions; not installed |

Keep this graph acyclic. Move independent enums/records out of application
templates. Profiles/configuration/core contracts must not include native or
heavy protocol definitions. Move constructors, destructors and operations needing
opaque complete types out of line. Export public operations/accessors with
`ZhttpAPI`; retain the project's portable declarations without expanding the
build matrix. Follow [Header layout](GUIDELINES.md#header-layout) and
[Code structure](GUIDELINES.md#code-structure).

### 1.3 Preserve typed dispatch and catalog work

The concrete wire parser uses one concrete sink per protocol. Its operations
cover message start, method/target or status, fields with section identity, body
information/consumption, completion, failure and reset. Preserve the existing
callback ordering, informational-response opt-in, trailers, rejection behavior,
`finishBody`, and completion/reset semantics. Deliver concrete route handles
instead of native links.

The typed adapter retains both request and response catalogs on both endpoints:

| Direction | Existing behavior to retain |
| --- | --- |
| Server receive | Initialize the application request parser, then dispatch operation, fields, body and completion through its compiled catalog/handlers |
| Client receive | Resolve the pending operation/stream and attempt first, initialize the parser from its request, then select and dispatch that request's permitted response alternative |
| Client request/server response output | The selected typed builder supplies its declared/custom fields, operation/status, body and producer through the concrete encoder boundary |

This matches [ZhttpClient.hh](zhttp/src/ZhttpClient.hh),
`ClientSession::bind`/`ParserSink_`: `m_response.init(*m_requestApp)` establishes
request context before response callbacks. H1TCP `Link_::process` uses the pending
operation queue head; H2 stream entries and H3 `ClientStream::appLink` identify
the bound request. Preserve these bindings rather than adding a search when
bytes arrive. Preserve existing attempt/generation checks before callbacks can
reach a reused slot.

On the server, [ZhttpServer.hh](zhttp/src/ZhttpServer.hh),
`Server::Parser::begin`/`operation`/`notify`, initializes from the server and
forwards request parsing. [Zrest.hh](zrest/src/Zrest.hh) uses compiled method/path
matching for requests; its `resMatch` first selects the request's response list,
then matches status. [ZrestClient.hh](zrest/src/ZrestClient.hh) dispatches through
the bound builder. [ZjrpcHTTP.hh](zjrpc/src/ZjrpcHTTP.hh), `HTTPReplyParser`, likewise
retains the request for response/custom-header/SSE delivery. Do not impose REST
method/path routing on applications using a different existing parser contract.

Use `ZuMatcher` for multiple string candidates, `==` for one, and compact IDs
with indexed/`ZuSwitch` dispatch. Put matcher definitions in the owning `.cc`:
library catalogs in the library, application catalogs in the application. Reuse
the existing catalog declaration/definition pattern, including concrete composed
catalog definitions, rather than inventing a catalog compiler. Preserve existing
once-initialized integer maps such as REST response-status lookup. Do not replace
them with linear scans, per-message maps or runtime handler registration.
See [Compile-time data and matching](GUIDELINES.md#compile-time-data-and-matching).

Classify configured names **once per message catalog configuration, never per
request**. The user-named `Zhttp::forbidden`/`Zhttp::pseudo` are currently
`Zhttp::Fields::forbidden`/`Zhttp::Fields::pseudo`. During one initialization pass,
call each helper once per distinct configured name and store its forbidden flag
and pseudo-header ID/sentinel in immutable catalog metadata. Duplicate name
declarations share classification. Publish the metadata before message processing;
share it across messages, connections, streams, retries and compression seed
plans. Do not repeat classification during reset, connect, submit or first use
on a request path.

Known fields consume cached classification/dispatch IDs. For dynamic or unknown
names, carry the existing concrete name classifier's result through current wire
checks and callbacks without calling these helpers, adding another name lookup
or creating an unbounded name cache. Preserve current duplicate-field and
field-section rules. A cached flag must not cause a new rejection or apply a
rule to a protocol/direction where it did not previously apply.

Remove catalog parameters from encoders. Expose immutable field/seed metadata
preserving order, duplicate emission, fixed-value omission and sensitive/
never-index policy. HPACK retains fixed-declaration seeds; QPACK may also seed
declared runtime names. Provider-produced values are not additional seed inputs.
Compression state remains concrete and owned by its protocol shard.

### 1.4 Builders, buffers and parser lifetime

Bind protocol operations once at connection/stream setup. Cross erasure boundaries
at messages, fields and buffer chunks; keep scanning, compression, frame handling
and buffer filling direct inside the library.

Outbound field submission carries a declared ID or undeclared sentinel,
classification and key/value views. Preserve fixed, declared-runtime, undeclared
literal and mutable-declared distinctions. A view is either a borrowed span or
an object pointer with a synchronous printer thunk accepting the concrete writer.
Consume temporary printables/keys within the provider call. Do not retain stack
contexts, render every field into a string, or call a provider twice to determine
length. Use the existing protocol destination or required compression scratch.

Resolve `bodyHdrs` keys to IDs in the typed adapter. Track patch offsets/lengths
in concrete header storage, with capacity derived from patchable catalog fields.
After buffer growth, resolve offsets against the final retained storage before
synchronous patching/commit; never retain spans across relocation. Preserve
existing omitted-field, unique-key and fixed-field patch constraints, without
new checks. See [Data movement](GUIDELINES.md#data-movement-and-initialization).

One concrete input view exposes mutable borrowed spans and queue consumption
without coalescing fragments. One writer exposes reserve/span/commit, accounting,
backpressure and failure. Bodies write directly into pooled `ZiIOBuf` storage.
Header printers bind that writer to the existing header destination. Preserve
optional, fixed, streamed, close-delimited, HEAD/bodyless and SSE behavior.
Deferred producer state belongs in the retained message/stream or a justified
pooled typed node. Preserve emitter invalidation, close and terminal sequencing.

Apply [Tx streams](GUIDELINES.md#tx-streams) to the writer interface: failure is
sticky; explicitly check `bool flush()` at completion; destructor flushing is
best effort. An earlier valid prefix may flush once after rejection, but failure
remains and `flush()` returns false. Retain the output error for diagnostics.
Use existing output status/admission paths, not new validators or exception
translation. Do not catch arbitrary printer exceptions or add `noexcept` merely
for erasure. Allocation failure remains the responsibility of `ZmHeapFail`.

Keep parser storage separate from its current binding. Preserve actual ownership
in `ServerSession`, `ClientSession` and H1 operation nodes: reuse serial parser
storage where currently reused, and preserve operation-lifetime storage where
currently allocated. Do not mandate a new parser pool or claim all H1 parsers
already have connection lifetime. Concurrent H2/H3 streams need distinct active
parser state. Preserve per-message `init`, completion/reset and eventual owning
node destruction; allocate neither a parser per callback nor an erased buffer
of guessed size. Retire the old Rx binding before reusing storage on retry.

Logical requests outlive individual attempts. Reuse existing request/attempt IDs,
generations, replay/partial-send accounting and terminal state; preserve unordered
acknowledgement, archive/retrieve and exactly-once logical retirement. Keep
`ZmPQTx` operations Tx-owned. Embed concrete records in existing typed intrusive
nodes or use retain/release thunks casting back to the actual allocated node
before reference adjustment. Never reinterpret an arbitrary intrusive node as a
common base. See [Containers and intrusion](GUIDELINES.md#containers-and-intrusion).

### 1.5 Routes, WebSockets and shard ownership

Extend existing route erasure with the operations existing callers need: session
and logical-stream identity, request sequence/generation, response submission,
cancel/reset/close, input consumption, Tx scheduling and necessary transport
metadata. Keep session identity distinct from stream identity. Callback handles
are borrowed; asynchronous application use explicitly retains the existing
required ownership. Retention must not permit callbacks into a destroyed app.

Use the same concrete channel for H1 Upgrade and H2/H3 Extended CONNECT. Move
`zws` off application-specific `ClientHub`/`ProtocolServer` and native link
inheritance; retain its typed framing/application adapter. Preserve buffered
bytes at the HTTP/WebSocket handoff. Builder operations must carry authority,
method, scheme, path, protocol and channel-response mode. Preserve peer setting
checks and ordinary versus Extended CONNECT behavior. A successful channel
handshake keeps both sending directions open: no initial H2 END_STREAM or H3
send closure. Failure responses retain ordinary HTTP termination.

Document the owning shard beside each operation. Rx owns parsing, receive flow
control and parser bindings; Tx owns queues, encoding/compression and producers.
Public cross-shard calls immediately post to trailing-underscore owner methods.
Capture handles and small metadata; transfer variable data through destination-
owned queues or pooled buffers. Do not read results of `run`/`invoke` until its
continuation. Add no locks for non-owner access. Use `ZmFn` for continuations
and existing `ZiIOFn`/`ZiIOContext` for I/O, not a new callback framework.
See [I/O function organization](GUIDELINES.md#io-function-organization),
[Cross-shard handoffs](GUIDELINES.md#cross-shard-handoffs) and
[Callbacks](GUIDELINES.md#callbacks).

Shutdown stops ingress/new timer work, cancels timers and drains their owner-
thread callbacks, drains Rx then Tx, finishes terminal callbacks/producer cleanup,
and releases contexts. Preserve raw dependent back-pointers where owners drain
all dependents. Do not add owner references to every timer/buffer/callback.
`ZmScheduler::del` must be followed by the existing owner-thread drain before
owner release. Partial initialization unwinds constructed state. Keep blocking
wrappers on the main thread and destruction after explicit shutdown; preserve
existing assertions without adding defensive ones. See
[Teardown](GUIDELINES.md#teardown) and [Timer teardown](GUIDELINES.md#timer-teardown).

### 1.6 Configuration and negotiation

Move lightweight existing configuration types without redesigning their loaders.
Preserve client `Config::m_set` and `overlay` explicit-setting provenance. Merge
raw overlays, derive unset client defaults once from the selected descriptors,
then resolve existing policy over runtime-enabled candidates before allocation.
Do not mark derived defaults explicit. TCP-only defaults must work; H3-only must
not require TLS-over-TCP. Replace the existing `PreferH3` assumption requiring
both TLS and QUIC with candidate-based preference resolution. A preference with
one legal candidate selects it. Preserve existing no-candidate/error paths;
introduce no second validation pass or new rejection condition.

Typed fluent capabilities use SFINAE only where the selection supports them.
Dynamic configuration reaches the same existing policy/initialization path.
Use [Metadata](GUIDELINES.md#metadata) integrations and `Mutable`/`update()` to
preserve omitted fields; defaults belong in C++. Use same-named nested lambda
configuration accessors per [Accessors](GUIDELINES.md#accessors), not forwarders
for every inner setter.

Preserve server listener activation: current `ServerConfig` starts with families
disabled; `.tcp(config)`, `.tls(config)` and `.quic(config)` explicitly enable
them. Apply the selection to available factories and permitted TLS versions.
Delete the previous plan's new enable/disable setters, server provenance mask
and automatic secure-listener default table: they are not needed for erasure.
Selection-dependent defaults repair the client's all-family defaults; they do
not justify automatically opening server listeners or changing credential policy.
This deliberately narrows the original design's general defaults prescription
to the demonstrated client requirement.

Keep one server address/port, no listener sniffing or per-family address API.
Plain TCP and TLS serving together use separate endpoint instances on distinct
addresses/ports; TLS and QUIC can share a numerical port. Preserve existing bind,
credential and initialization failures; add no listener-conflict precheck.

| Effective TLS profiles | Required behavior |
| --- | --- |
| H1TLS only | HTTP/1.1 with the existing no-ALPN case; no H2 activation |
| H2TLS only | Require `h2`; no H1 fallback |
| Both | One TLS backend with allowed-version mask and existing preference policy |
| Neither | No TLS pool/listener, ALPN setup or TLS configuration activation |

Discovery, Alt-Svc, redirect and fallback operate on registered, enabled
candidates. Preserve replay safety and normal unsupported-route outcomes. H3 is
independent of TLS-over-TCP. Advertise server Alt-Svc only for an enabled,
available H3 endpoint. These are selection/policy changes, not new validators.

### 1.7 Audit every applicable guideline flag

Apply this audit to touched declarations and uses in every affected source
directory, not just `src`. A flag identifies a review question, not permission
for unrelated cleanup. The table covers every category in
[Audit flags](GUIDELINES.md#audit-flags); sections above prescribe the owning
contracts. Saga and dynamic-loading machinery are outside this refactor.

| Guideline category | Required implementation review |
| --- | --- |
| [Repair procedure](GUIDELINES.md#repair-procedure) | Name the violated contract/owner before a change; preserve unrelated work and map moved checks to the baseline |
| [Code structure](GUIDELINES.md#code-structure) | Move type-independent declarations out of templates; use concise local aliases for member alignment, data first in structs, cached invariant loop calls/element reads, indexed matching and `constexpr` `ZuString` literals rather than constexpr spans |
| [Storage and capacity](GUIDELINES.md#storage-and-capacity) | Size storage from catalog/selection/workload/protocol bounds; no guessed inline buffers, GC scans or refcounted objects by value; group fields by ownership/size and separate Rx/Tx groups with `alignas(Zm::CacheLineSize)` |
| [Heap allocation](GUIDELINES.md#heap-allocation) | Each separately allocated fixed concrete type has its own first-base `ZmNHeap<Concrete>` and ADL `ZmNHeapID`; preserve sharding/tuning, no allocator base or custom new/delete forwarding; `ZmVHeap` only for variable allocations |
| [Containers and intrusion](GUIDELINES.md#containers-and-intrusion) | Reuse intrusive ownership, stack memberships with the actual outer node allocation and inner shadow nodes; no separate node/refcount allocation or `ZmList<ZmRef<T>>` for an object that can own its links; document genuine shared-owner exceptions |
| [Data movement](GUIDELINES.md#data-movement-and-initialization) | Direct mutable input/uninitialized output, no erasure-induced body copies, temporary contiguous conversions or initialization before overwrite; deferred `ZiLOG` captures values, using `ZeString` when required |
| [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow) | Factor repeated concrete code, use `switch`/early exits, continuation-based sequencing and notification waits; end iterators at traversal boundaries, count actual visits, avoid needless container aliases/redundant null checks; use built-in hashes such as `ZuSpan::hash` |
| [Framework fit](GUIDELINES.md#framework-fit) | Use `ZiFile`/`ZiLog`, Z formatting/parsing, existing scheduling/containers; use sentinels for unset state instead of redundant booleans; retain the explicit overlay mask because it records provenance |
| [Dynamic modules](GUIDELINES.md#dynamic-modules) | Descriptors are ordinary linked-library contracts; add no loading/unloading, `ZiModule` subsystem or virtual plugin factory |
| [Bad casting](GUIDELINES.md#bad-casting) | Use direct Z span/string/array and char-equivalent conversions; erased contexts cast only to their actual type for pointer adjustment; use `using` for CRTP lookup instead of base casts |
| [Bad code structure](GUIDELINES.md#bad-code-structure) | Narrow accessors/shared data structs instead of external private `m_` access; no enum/integer mapping ladders or needless `this->` |
| [Scope and cleanup](GUIDELINES.md#scope-and-cleanup) | Delete superseded paths/shims, preserve unrelated diagnostics/probes; raw dependent back-pointers plus owner drains, no ownership cycles or per-packet refcount churn |
| [Unnecessary atomics/locks/copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies) | Approximate telemetry remains primitive unless accuracy is required; preserve correctness synchronization for admission/lifetime state |
| [Liveness](GUIDELINES.md#liveness) | Bound I/O scheduling, cancellation and drain work per turn, reuse justified batch limits and post continuations for the remainder |

For temporary headers and work arrays, use **`ZtScratch` with an identified
underlying array and stack storage with heap fallback**. Use `ZtBuiltin` for a
measured hot-path common size, `ZtArray` for cold variable storage and `ZmAlloc`
for an appropriate large single-object temporary. Retained I/O buffers remain
pooled `ZiIOBuf`; scratch cannot outlive its callback. No caller null checks for
framework allocation/growth: `ZmHeapFail` owns failure. Use `ZuObject`/`ZuRef`
for thread-affine ownership and `ZmObject`/`ZmRef` where sharing actually requires
it; use existing `ZmSingleton`/`ZmSpecific` facilities if needed, not homemade
global initialization or `thread_local`. See
[Storage and lifetime](GUIDELINES.md#storage-and-lifetime).

Do not hand-code JSON, numeric conversion, formatting, hashing or configuration
parsers. Retain the existing HTTP wire algorithms; reuse `ZfJSON`, `ZfURI` and
other metadata integrations for application formats, `ZuBox`/`ZuFmt`/`ZuPrint`
and existing number scanners for numeric fields. Do not wrap convertible Z types
in redundant casts or allocate intermediate strings. See
[Formatting](GUIDELINES.md#formatting), [Metadata](GUIDELINES.md#metadata) and
[Strings](GUIDELINES.md#strings).

All introduced names must satisfy the authoritative **28-byte** limit, camelCase,
concise `req`/`res`/`cli`/`srv`/`cxn` vocabulary, overloaded accessors and owner-
thread trailing underscores. Keep proposed role names short; do not mechanically
rename unrelated existing APIs. Library/unit-test basenames use module-prefixed
CamelCase; program and `util`/`itest`/`example`/`bench`/`interop` basenames are
lowercase. See [Naming](GUIDELINES.md#naming).

Use GNU C++2b, framework traits and trailing-template-parameter SFINAE; no concepts,
`requires`, anonymous namespaces, blanket captures or scoped enums. Use `ZtEnum`
where appropriate and `using` rather than forwarding base methods. Follow the
header skeleton, direct includes, hard tabs, 2-column logical indentation and
98-column lines. Structs expose unprefixed data first; classes keep private `m_`
data last. See [Core language](GUIDELINES.md#core-language),
[Static polymorphism](GUIDELINES.md#static-polymorphism-and-constraints),
[Indentation](GUIDELINES.md#indentation) and
[Class and struct shape](GUIDELINES.md#class-and-struct-shape).

## 2. Build and measurement policy

Retain the current configured compiler, platform, flags, dependency versions,
link/debug settings and build directory. Do not run clean/distclean, switch
compilers, create a second build configuration, toggle sanitizers/LTO/optimization,
or rebuild dependencies merely for reassurance. Regenerate changed build metadata
only as necessary with the existing configuration; reuse unaffected artifacts.
This narrows the broader matrix in the original design and guideline acceptance
advice. Apply [Build configuration](GUIDELINES.md#build-configuration) ordering
without changing configuration.

There are only three planned build/test milestones: slices B, C and D below.
Accumulate source, fixture and build-file edits into the large batches specified
there. Between milestones use source/dependency/ownership review, not repeated
builds or test runs. At B/C, incrementally build `zhttp/src` with `make -j4`, then
only the focused migrated `zhttp` fixtures. Build a changed prerequisite library
first only if this refactor actually changed it. Do not rebuild `zws`, `zjrpc`,
`zrest`, `zmcp` or other downstream modules/programs before D. Do not add shims
just to keep unmigrated dependent targets building between slices.

At D, complete consumer edits and cleanup first, then run one incremental
top-level `make -j4` to build libraries and dependents in order. Build a relevant
test directory's default target before its `make test`; subdirectory make alone
does not refresh prerequisite libraries elsewhere. Run the required suites after
that build. Batch fixes from a failed milestone, then rerun the affected checks;
repeat wider testing only if the changed scope requires it.

Capture the baseline source state, compiler version, configure arguments, flags,
machine and exact compile commands in slice A without rebuilding dependents.
Preserve all transitive inputs needed for reproducible
before/after compilation, including dirty source and generated configuration;
`HEAD` alone is not this working tree's baseline. At D, measure baseline and
migrated translation units using the same recorded compiler/flags and isolated
output files, without rebuilding baseline libraries or disturbing incremental
objects. Use the same machine and controlled load. Measure full compile and
syntax-only cost separately, at least three isolated samples, median wall time
and peak RSS. Use `zjrpcd`, the JSON-RPC client
and full-profile HTTP utilities. Capture dependencies, object/debug sizes and
symbols; distinguish object debug sections from linked compressed debug sections.
Record library build cost separately. Historical October 15 numbers are context,
not measurements of this baseline.

For runtime comparison, record workload, message sizes, concurrency, warmup,
sample counts and pool tuning. Preserve and use existing baseline binaries only
with known source/build provenance; retain their matching shared libraries for
comparison at D, or measure them at B before the first library rebuild. If no
trustworthy baseline executable exists, record the missing comparison evidence;
do not silently use stale binaries or create another baseline build tree. Reuse
existing coverage/harnesses, without a new benchmark framework, and run baseline
and final measurements at milestones rather than on every edit.

Use Valgrind sparingly: only to investigate actual suspected corruption during
implementation, then one targeted final leak/corruption gate after functional
checks pass. Run via `libtool exec`; never invoke `.libs` binaries directly.
Do not run the entire matrix repeatedly under Valgrind. See
[Debugging](GUIDELINES.md#debugging).

## 3. Implementation slices and interim acceptance

Each slice includes the necessary source, fixture and build-file changes. A
review gate is not a build gate. Temporary internal migration scaffolding must
have a deletion point in D; no parallel public API survives completion.

### A. Freeze contracts and baseline — review gate only

Inventory active direct/indirect consumers with `rg`, excluding historical `arc/`
copies: `zhttp`, `zws`, `zjrpc`, `zrest`, `zmcp`, `ztc`, `zum`, `zdash`, plus any
additional callers. Include tests, programs and documentation. Record chosen
profiles, callback/queue operations, shard ownership, borrowing/retention and
shutdown conditions. Record existing check locations alongside their owning
operations; keep this a concise migration record, not a validation subsystem.
Capture source/build provenance and comparison inputs per section 2.

**Interim acceptance:** every erased operation has a current caller and owner;
parser reuse, request/attempt separation, response correlation, body production
and channel handoff contracts are explicit. No new listener policy, validation
layer, allocation framework or redundant state is planned. Baseline inputs and
consumer inventory are recoverable. No rebuild or test is required at this gate.

### B. Concrete interfaces and H1TCP — first build milestone

Implement lightweight profiles/configuration, concrete records/cores, copied
descriptor registration, typed queue/catalog/parser/builder adapters and concrete
input/writer/route contracts. Complete native TCP binding and the H1 machine,
client/server lifecycle, admission, pipelining, response ordering and shutdown
needed for an end-to-end H1TCP fixture. Reuse existing H1 behavior for partial
writes, limits, close-delimited input, idle handling and parser rejection.

Migrate small client/server fixtures inside `zhttp`; defer the `zjrpcd` and other
dependent builds. Extend existing contract fixtures for intrusive pointer
adjustment, parser storage lifetime, typed dispatch, printable headers/patching,
producer invalidation and writer failure. Perform the first incremental core and
focused-fixture build only when this whole path is ready.

**Interim acceptance:** focused TCP, cancellation, idle, streaming and contract
coverage passes. Public application headers expose no native/protocol definitions;
fixture source/dependencies and registration show only H1TCP. Native specializations
have library-owned types. Existing accepted/rejected cases, typed callbacks,
parser reuse, header provider counts and body-copy/allocation behavior are
preserved. Any early compile-size observation uses these fixtures, not a
rebuild of dependent applications.

### C. TLS/H2, QUIC/H3 and full coordination — second build milestone

Complete this as one large source batch. Internally move shared TLS/H1/ALPN and
H2 first, then QUIC/H3, then remaining common coordination. Keep the H1/H2 TLS
backend shared. Preserve H2 SETTINGS, HPACK, flow control, stream limits,
Extended CONNECT settings, reset and GOAWAY/drain. Preserve H3 QPACK blocked
sections, control streams, SETTINGS, limits, push, reset/GOAWAY and current early-
data/replay behavior. Read `zquic/GUIDELINES.md` before any necessary changes in
`zquic`; native transport redesign remains outside scope.

Finish concrete DNS/discovery, origin/session caches, scheduling, attempts,
redirect/retry/cancel, timeout, admission, response ordering, statistics and
shutdown. Reuse records such as `ClientAttemptID` and `BodyCommit`. Finish all
profile/policy combinations, catalog initialization and lower-level channel
operations. Add migrated `zhttp` fixtures for these paths, then perform the second
incremental core/focused-fixture build; downstream rebuilding remains deferred.

**Interim acceptance:** singleton profiles and shared TLS negotiate correctly;
H3-only works independently; disabled families allocate no backend/protocol state;
selection, fallback and existing replay rules hold. Parser/correlation and
lifetime cases pass with concurrent streams, late callbacks, partial startup and
slot reuse. No remaining heavy public template implements coordination or wire
machinery. All 15 subset fixtures and the structural checks below pass.

### D. Consumers, deletion and integration — final build milestone

Propagate the finished API through all inventoried callers in one source batch:

| Consumer | Required change |
| --- | --- |
| `zws` | Replace native hub/link inheritance with concrete channels; keep explicit single-profile wrappers, framing and handshake semantics |
| `zjrpc` | Propagate selection, concrete parser/route/SSE/producer interfaces; both `zjrpcd` HTTP and WebSocket branches select H1TCP |
| `zrest` | Propagate selection and concrete input/response/SSE interfaces while retaining request-specific response catalogs and needed transport metadata |
| `zmcp` | Carry the same selection through composed wrappers; adapt HTTP/SSE/WebSocket use without a second selection source |
| `zhttp` programs/fixtures | Explicit `AllProfiles` for full-protocol utilities, exact sets for restricted callers; private protocol tests use library-owned implementations |
| `ztc`, `zum`, `zdash`, other discovered users | Select actual supported profiles and adapt configuration/async routes; do not mechanically choose `AllProfiles` |

Delete superseded application-dependent hubs, links, factories and public heavy
includes. Replace obsolete API-shape tests while preserving their behavior
coverage. Update source/install lists, noninstalled private-header distribution,
README examples and test entry points. The current include-link mechanism is a
module source-directory link (`src/zlib -> .`), not one generated link per header;
reuse it without inventing a new generator. Verify the installed public/private
boundary with a staged install/header consumer using the current artifacts.

Now run the final incremental top-level build, required suites, compile/runtime
comparisons and targeted Valgrind gate under section 2. No alternate toolchain,
platform, sanitizer configuration or clean rebuild is part of acceptance.

**Interim acceptance before the final review:** all active callers use the new
API, all required evidence below is recorded, and old scaffolding/paths are gone.
A TCP-only demo or transport-only extraction cannot satisfy this gate.

### Required evidence at the applicable milestones

Extend existing tests instead of building a parallel suite. Use `ZuTestUtil`
TAP/`log`/`parse` (including `HARNESS_ACTIVE`) and underlying `ZuTest` as appropriate.
Put new unit/compile fixtures in `test`, network/program integration in `itest`,
interop in `interop`, measurements in `bench`, shared standalone tools in `util`.
Do not relocate unrelated tests. Wait for completion notifications with `ZmBlock`
or `ZmSemaphore` on the main test thread; use continuations on I/O threads, never
sleeps/polling as proof of completion. See [Tests](GUIDELINES.md#tests),
[Module layout](GUIDELINES.md#module-layout) and
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

| Evidence | Required coverage; reuse existing entry points |
| --- | --- |
| Structure/selection, B then C | Construct/register client and server fixtures for all 15 subsets, not merely name types. Descriptor references match selection; duplicates do not register twice; TLS shares one backend. Public dependencies exclude `Ztcp.hh`, `Ztls.hh`, `Zquic.hh` and private wire machinery; no native specialization contains app/parser/builder/queue/catalog types |
| Catalogs/headers, B then C/D | Server request dispatch and client correlation before response dispatch, overlapping statuses/headers with different typed handlers, H1 order and H2/H3 out-of-order completion, custom/fixed/unknown fields, informational sections/trailers, existing duplicate behavior, sensitive/omitted/printable fields and patching after growth |
| Classification, B then C | Instrument helpers in fixtures: one call each per distinct name during catalog configuration; zero more on messages, reset, connect, streams, retries or seed plans. Dynamic/unknown names preserve existing rules without helper calls/new checks |
| Body/ownership, B then C/D | `ZhttpTransportContractTest`, hub lifecycle/boundary/parser/message tests: intrusive pointer adjustment, actual parser construction/reset/destruction counts, fixed/optional/streamed bodies, SSE, writer rejection/flush, temporary provider lifetime, emitter close/invalidation, backpressure and no erasure-induced copies/allocations |
| Attempts/liveness, B then C/D | Client cancel/pool/fallback and lifecycle/stream tests: DNS/connect/handshake/body cancellation, partial sends, stale attempts/routes, unordered ack, exactly-once logical retirement, queued responses, slot reuse, partial startup, timer/drain ordering and unrelated queued work progressing during bounded batches |
| Protocols/policy, C | HPACK/QPACK and H2/H3 stream/hub/push coverage; all TLS ALPN cases, H3-only, narrowed runtime policy/overlays, existing no-candidate/listener failure paths, legal discovery/Alt-Svc/redirect/retry and replay safety |
| Integration, D | Existing `zhttpmatrix`, application/server tests, H1 Upgrade and H2/H3 Extended CONNECT (buffered input, pseudo fields, peer capability, open success directions and ordinary failure), JSON-RPC/REST/MCP and other affected dependent suites; configured external interop only |
| Performance/memory, D | Compile time/RSS/object/debug size and runtime latency/tail/throughput/allocation/copy/connection/stream memory comparisons; targeted Valgrind leak/corruption coverage of retained producers, cancellation, stream/channel lifetime and shutdown |

Inspect objects/dependencies under the **current flags**; do not introduce `-O0`,
LTO changes or another build solely for symbol inspection. Supplement symbol
inspection with source/registration and include-graph review so optimization or
linker stripping cannot conceal an application-specialized native path. Compare
singleton/all-profile fixtures: additional selection adds registration glue,
not protocol bodies. Existing allocation telemetry or fixture factories establish
absence of excluded backend state; do not add permanent production probes.

Runtime coverage includes every singleton, H1TLS+H2TLS, H2TLS+H3QUIC and all four.
Use separate plain/secure server endpoints where needed. Cover other subsets
through selection/policy fixtures and network cases only for distinct behavior.
Unavailable external services are recorded gaps, not passes. New negative tests
may preserve existing rejection behavior but must not demand new validation.

The original compile-cost target remains a **50% reduction in `zjrpcd` median
full-compile wall time and peak RSS**, measured comparably at D. Report syntax-only
cost, other selected consumers and library build cost separately. If missed,
identify the remaining expensive templates and revise the boundary; do not claim
historical timing as the baseline or silently weaken the target. Compare runtime
workloads for every profile, including warm small messages, concurrent streams
and stalled consumers. Resolve repeatable regressions beyond measured noise;
compile savings cannot justify per-byte indirection, body staging or refcount churn.

## 4. Final consistency review and completion gate

Before implementation begins, and again after D, check the plan and resulting
change against the following. Resolve contradictions in the owning section,
rather than appending an exception that leaves two competing instructions.

- [ ] Every retained element serves the original erasure/selection objective;
  unnecessary modules, state, algorithms and APIs have been removed.
- [ ] Production checks map to the baseline; no validator, rejection rule,
  defensive assertion or redundant recheck was added.
- [ ] Explicit profile selection and one shared TLS backend coexist with concrete
  library-only wire/coordination machinery; all 15 subsets are accounted for.
- [ ] Both endpoints retain compile-time request/response/custom-header contracts;
  server request dispatch and client request-context response dispatch agree
  with the implementation evidence in section 1.3.
- [ ] Forbidden/pseudo helpers occur only in catalog initialization; wire checks,
  reset and seed construction consume classification without repeating it.
- [ ] Parser reuse, actual node lifetime, attempts, queue acknowledgement,
  printable fields, patch spans, producers and channel handoff remain consistent.
- [ ] Every new allocation/operation has a justified owner; scratch uses framework
  facilities, I/O stays pooled, shard drains protect borrowed contexts and
  no hand-coded formatting/parsing or redundant casts remain.
- [ ] Every applicable audit category in section 1.7 has been reviewed across
  affected source directories; names and file layout meet the linked guidelines.
- [ ] Server explicit activation and client selection-derived defaults are
  distinguished; no automatic listener matrix or unnecessary configuration API remains.
- [ ] Builds stayed incremental in the current configuration, occurred at major
  milestones, and downstream compilation was deferred until D; Valgrind usage
  was targeted. No clean/alternate-configuration requirement remains.
- [ ] Every consumer and installed-header fixture uses the final API; deleted
  paths have no compatibility fallback or surviving temporary scaffolding.
- [ ] Required functional, structural, memory and comparable performance evidence
  is recorded; gaps and unmet targets are explicit, not reported as completion.

Deliver the implementation with a concise touched-module summary and measured
results. This document update itself requires only source/document review; it
must not trigger the implementation's builds, tests or configuration changes.
