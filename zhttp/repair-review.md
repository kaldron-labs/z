# zhttp repair review inventory

Date: 2026-10-18. This is a working contract inventory for `../repair.md`.
The original audit and its dispositions remain in `../findings.md`.

## H1 streaming entry points

| Occurrence | Caller and lifetime | State currently touched |
| --- | --- | --- |
| `Stream_::Base::{txStream,txStream_}` in `ZhttpNative.hh` | Calls the link synchronously with a borrowed writer; its lambda borrows the caller's callback. | Link output path. |
| Native `ClientLink` and `ServerLink` in `ZhttpNative.hh` | `streamTx()` invokes the borrowed printer on Rx; `streamTx_()` invokes it on Tx. End/reset dispatch to Rx and then Tx. | Rx accesses `m_stream`; Tx accesses `m_streamTx` and `m_streamEnd` in its aligned group. |
| TLS `ClientH1AppLink` and `ServerH1AppLink` in `ZhttpClientImpl.hh` and `ZhttpServerImpl.hh` | Same synchronous writer contracts. Native association is published before Tx admission. | Rx accesses `m_stream` and `m_native`; Tx accesses `m_streamTx` and `m_txNative`. Retirement clears the Rx handle and posts Tx-handle clearing while retaining the physical link through that post. |

`StreamBinding` owns negotiation (`enable`, `accept`, `reopen`), input dispatch
(`process`, `disconnected`), and peer terminal state on Rx. The caller must
keep each borrowed printer and result alive until the synchronous call returns.
The owner-affine `streamTx_()` path cannot become an asynchronous post without
changing those caller lifetimes. Tx admission snapshots and end/reset handoffs
are now implemented across all four link shapes. End/reset are asynchronous
commands when called off Rx; callers must stop producing after requesting
termination. Ordered Rx-to-Tx posts publish admission and terminal state.

## Connection lifetime

| Stage | Current evidence | Review obligation |
| --- | --- | --- |
| Admission | H1/H2 server admission uses active-connection counts in `ZhttpServerImpl.hh`; H3 client tracks active and retired links in `ZhttpClientImpl.hh`. | Keep operational admission counts synchronized. |
| Stop ingress/timers | Native server `disconnected()` cancels its idle timer; H2 `stopWire()` and H3 hub stop paths stop ingress. | Check every timer and late callback before replacing a retained owner. |
| Rx drain | Native server `notifyDisconnected_()` runs on Rx; H1/H2 pool hubs use `stopDrain_()`; H3 client `stopLinksBatch_()` accounts for retired links. | Include callbacks that post work back to Tx. |
| Tx drain | Native server posts a Tx barrier before `releaseDisconnected_()` and `linkDrained_()` on Rx; pool `stopDrain_()` posts Rx/Tx/Rx. | Include Tx queue cleanup and any cancellation callback that reposts to Rx. |
| Ownership release | Registries, retired slots, and link-drain completion retain links until their respective drain concludes. | Prove this separately for each `ZmRef(link)` capture before replacing it. |

This is not yet a lifetime proof for finding 8. No owner-reference capture is
removed until the corresponding ingress gate and both drain directions are
accounted for.

## Codec allocations

| Allocation | Concrete type and owner | Storage and failure path |
| --- | --- | --- |
| Discovery request | `DiscoveryRequest`, allocated by `discoverH3()` and `resolveH3()`; returned as `ZmRef<DiscoveryRequest>`. | Now has its own first-base `ZmNHeap` and heap ID. It is a fixed-size object; there is no nullable allocation result. |
| HPACK hash objects | `HPackTxExactHash` and `HPackTxNameHash`, allocated in `HPackTxTable::init()`. | Concrete first-base `ZmNHeap` types and matching `ZuPtr` deletion; `n = capacity >> 5` controls sizing. |
| QPACK hash objects | `QPackTxExactHash`, `QPackTxNameHash`, and `QPackTxSectionHash` in `QPackTxTable::init()`. | Concrete first-base `ZmNHeap` types and matching `ZuPtr` deletion. Entry hashes use `n = capacity >> 5`; section hash uses `maxSections`. |
| Hash table storage | `ZmLHash_Ops<Node>::alloc()` in `ZmLHash.hh`. | Uses `Zm::alignedAlloc` with `ZmHeapFail` retry; `free()` uses `Zm::alignedFree`. Storage is not currently module-identified. A foundational allocator change must be reviewed separately. |

`ZmHeapFail` either retries or aborts, so a null check after these allocations
would not implement an allocation-failure contract. Effective bounds depend
on configured codec capacity and `ZmHashParams` rounding; they need review
before any variable-size storage change.

## Counters and caches

| State | Writers/readers | Bound and removal |
| --- | --- | --- |
| `Stats::{activeConnections,activeRequests}` | Admission and release update these; admission compares them with configured limits and shutdown asserts zero. | Operational atomics must stay synchronized. |
| `Stats::retainedBytes` | Response retention/release update it; `retainedAvailable_()` decides admission from it. | Operational atomic must stay synchronized. |
| `Stats::queuedResponses` | Ordered response queue updates it; shutdown checks zero and telemetry reads it. | Verify whether stop logic needs exact cross-shard visibility before changing it. |
| `Stats::{serverFaults,rejectedRequests,parseFailures,bodyFailures,responseBuildFailures,transportFailures}` | Failure paths increment them; public getters read them. No admission or memory decision was found reading them. | Determine Rx/Tx writers per counter, then split any multiwriter counter before removing atomics. |
| `AltSvcCache::m_entries` | `update_()` admits, replaces, clears, or evicts; `get()` removes expired values for the requested origin; `del()` removes directly. | `maxOrigins` bounds entry count. Full-cache admission evicts the first occupied hash slot without scanning expired origins. |

Expiry during complete inactivity has no removal event. The selected contract
invalidates values at lookup time and permits bounded residency until a cache
operation. Immediate physical reclamation during inactivity remains a stated
exception to the deterministic-reclamation guideline.

## Bounded output and disconnect completion

| Work | Budget and ownership | Completion boundary |
| --- | --- | --- |
| H2 headers | Pooled `ZiTxQueue` buffers; whole-chain admission/splice; `FrameDrainBatch` inspected frames per output turn. | END_HEADERS ends the exclusive header chain; control frames and other streams wait behind it. |
| H2 controls | Separate bounded admission; Rx transfers buffers to the Tx control queue. | Native rejection closes output and reaches the Rx owner. SETTINGS ACK readiness follows native submission. |
| H2 stream removal | A retiring active header chain keeps its window alive; remaining frames retire in bounded turns. | Header completion, frame retirement, then the existing Tx/Rx barriers. |
| H1 responses | Tx owns response starts, route release, cancellation, and callbacks; `StreamStopBatch` steps per turn. | One posted continuation drains the remainder before `appDrained` completes on Rx. |
| TCP H1 owner | The native disconnect callback transfers its owner reference through `ServerDrainFn`. | App drain, Tx barrier, Rx release and hub notification. |
| TLS H1 owner | The physical native owns its H1 application link until the final Rx release; a bound drain callback retains the physical native. | App drain, Tx/Rx barriers, clear the H1 owner, then physical `down_()`. |

The raw H1 response-work post is protected by these native drain boundaries;
it adds no per-turn owner reference. This proof does not automatically apply
to client link continuations or multiplexed response teardown. Those captures
remain subject to individual review. Sorted response insertion is still a
linear traversal and remains an explicit liveness-review item.

The diagnostic counters now live in `Stats::rx` or `Stats::tx`. Their writer
ownership matches the table above; operational accounting remains atomic.
The current library compiles, but all newly added runtime cases remain pending
the final dependent validation milestone.

### Next owner-capture review

| Capture family | Evidence and required disposition |
| --- | --- |
| `Responses::send` / `routeSend` | Preserve the handoff reference unless a stronger entry contract is established: an application `ChannelRef` can outlive the native connection and be released immediately after posting a response that must be rejected on Tx. Native registry ownership alone does not cover that post. |
| H1 `responseDisconnected_` | The new native drain callback independently retains the owner through response work and final barriers. Its per-post reference is a candidate for removal; multiplexed paths require a separate proof. |
| TCP H1 Rx-to-Tx stream-state publication | Review against the native `ServerDrainFn` and final Tx/Rx barriers, including reset callbacks that post back to Rx. Keep the initial cross-shard public entry's ownership transfer distinct from owner-shard continuation captures. |
| TLS H1 application publication/retirement | The physical `SrvLink::m_h1` remains owned until `appDrained` and final barriers complete. Review its native/application captures against that exact lifetime; do not apply the server proof to client slot retirement. |

These are review dispositions, not a claim that the existing captures have
already been removed or that finding 8 is closed.


## Server H1 owner-capture dispositions — 2026-10-18

| Removed capture | Independent owner and last drain |
| --- | --- |
| Native ServerLink streamEnable, streamEndRx_, streamResetRx_, streamTxClose_ | Live native connection, then disconnected Rx handoff and bound ServerDrainFn; appDrained completes before final Tx/Rx barriers and registry release. Includes Tx reset/close posts back to Rx. |
| TLS ServerH1AppLink streamEnable, connected_, disconnected_, end/reset | Physical SrvLink owns m_h1 until appDrained and final Tx/Rx barriers; bound completion retains SrvLink. Neither native nor application pointer is released before its publication/retirement posts. |
| Nonmultiplexed Responses::responseDisconnected_ | TCP or TLS native appDrained ownership above spans the Tx response worker and final barriers. Multiplexed path retains its independent handoff reference. |

Public stream end/reset/disconnect dispatch references remain ownership transfers.
Responses::send/routeSend references remain necessary for application ChannelRef
submissions after native retirement. Client and multiplexed captures remain open.
The new always-fatal response-destructor preconditions check that no worker,
completion, producer, queued response, or retained bytes survive the owner.

## Codec backing-storage disposition — 2026-10-18

The original allocation inventory above is historical. Current dynamic
ZmLHash_Ops<Node, HeapID> uses identified ZmVHeap storage, separately from the
codec hash object's concrete first-base ZmNHeap. Static backing arrays remain
inline. Each codec table supplies its own HeapID; generic dynamic users inherit
the default ZmLHash identity. Cache-line/type alignment is preserved. Costs are
an alignment-sized allocation prefix, size-class rounding (potentially nearly
2x near a payload boundary), and framework cache residency after release.
No extra node allocation or per-entry copy is introduced. Existing allocation
failure behavior is delegated to ZmHeapFail through ZmVHeap. Runtime alignment,
reference lifetime, growth, and recycling coverage is added to existing
ZmLHTest source and has not yet run.


## Remaining implementation inventory after codec milestone

- Finding 19: H2Config TLS accessor/lambda API and all dependent fluent
  expressions; pure link txStream forwarding declarations. Current constructor
  and variable uses were searched repository-wide, including zhttp, zjrpc,
  zmcp, zrest, zum, ztc, zdash, and zgrpc dependents. Do not rewrite QUICConfig
  or independent TLSConfig setters as if they were H2Config.
- Finding 22: StreamTaskQ's separate node allocation needs an intrusive-driver
  design review or an explicit retained amber disposition; current changes
  only reorder the shared task/driver data. Client aliases/layout and overall
  struct shape need final occurrence review after ownership edits.
- Finding 23: queue defaults, compaction thresholds, and any further incremental
  assembly paths need their individual workload/protocol justifications.
- Findings 6/8: finish response insertion liveness and client/multiplexed owner
  lifetime proofs. The server H1 proof above is deliberately limited.
- Finding 24: follow-on loop/read/division and char-equivalent span repairs are
  source-only after the codec build; remaining cases and final width/style
  review are not yet closed.
- repair2: resolver, complete pool/client engines, and all endpoint/service
  migrations remain implementation work. Resolver Main still has its own
  lifecycle enum and callback queues, and final still documents a possible
  control-thread join; engine startup/rollback and scheduler release need to
  replace those contracts before any callback-reference removal there.
- Runtime acceptance: dependent compilation, relevant TAP suites, repeated
  lifecycle tests, and final Valgrind gate remain deferred and required.


## Foundational lifecycle ownership — 2026-10-18

| Owner | Start completion | Stop completion and final |
| --- | --- | --- |
| ZiResolver::Main | Parent ZmEngine completes after its scheduler and event loop are ready. Diagnostics are separate from bool completion. | Admission lock orders accepted posts before stop; SID cancels timers/queries and closes the channel, loop drains, scheduler workers join and its final control handle is detached during self-deletion. Parent completes after releasing that runtime. final is stopped-only and does not join. |
| PoolImpl | Parent completes after native Hubs start and Rx/Tx publication plus queue admission. | Rx ingress/timer cancellation and every discovery credit, native stop, bounded Tx queue drain, then Rx completion notifications. Canonical engine queues replace parallel stop waiter arrays. |
| ClientCore | Parent validates/resets idle state on Tx, starts owned resolver, then pool aggregate. Pool-start failure rolls back an owned resolver before failure completion. | Pool aggregate drains first; owned resolver drains next. Borrowed resolver is never stopped/finalized by the client. |

Resolver configuration is an optional Params union and survives stop. Channel
and scheduler runtime resources are rebuilt on restart. Numeric resolution
retains synchronous completion. Query admission while stopped can initiate lazy
startup; admission during stop rejects; explicit start during stop is queued.
A fresh scheduler per restart avoids predecessor-control-thread join chains in
the resolver's completion boundary. The existing scheduler destructor's own-
control-thread detach path is used only after worker drainage; final can delete
an initialized but never-started scheduler because it has no thread handles.

Pool discovery counts cover the race between finish() winning and its callback
posting to Rx. The independent owner keeps the pool alive until each launch's
last Rx post returns its credit, even if generation validation rejects the
result. One extra stop credit covers the cancellation scan itself, preventing
an early callback from completing stop before that scan finishes. These counts
are exclusively Rx-owned; no new atomics, owner references, allocations, posts,
or polling are added to this path. Callback credits also cover TLS fallback
launches, which acquire their credit before the preceding callback releases it.

Retained exceptions and outstanding review:

- Native raw-buffer send cannot be unconditionally hoisted into the composite
  server Link: QUIC's logical stream base has no such member. The compiler
  demonstrated this during the milestone. A constrained native-only adapter
  preserves the actual TCP/TLS overload and removes it by substitution for
  QUIC. The other txStream/complete base functions remain hoisted with using.
- Generic Hubs stopped(false) restoration still needs review. Concrete pool
  shutdown paths have no expected false result after actual drainage; earlier
  request/protocol failures do not become lifecycle failures.
- Pending opposite operations plus callback-driven final/destruction need the
  final engine audit and runtime coverage; no broad destruction-safety claim
  follows from a primitive engine state snapshot alone.
- Process-wide resolver lazy users, including indirect ZiIP adapters, must
  satisfy explicit stop before final/process teardown. Direct result-union
  callers were migrated; the final dependent audit must include indirect users.
- New source tests for retained-options DNS restart, client sealing/idle reset,
  canonical queued startup, and callback finalization remain unbuilt/unrun.
- Remaining zjrpc/zmcp/zrest/zum/ztc engine migrations and repair1 occurrence
  closure remain required. Source implementation is not runtime acceptance.


### Milestone and next engine audit

The resolver/client/pool library batch compiled without warnings. Current
footprints: ClientCore 1024 (+64), PoolImpl 1088 (+64), each parent engine 104,
and resolver Main 960 (baseline not measured). Dependent tests remain deferred.
After that milestone, the synchronous default spawn audit moved Starting's
notification ahead of a possibly inline hook in ZmEngine. The old post-hook
notification could access a released owner. Inline-release regression source
is added to ZmEngineTest; this follow-on remains unbuilt. Pending opposite
operations, callback finalization, generic Hubs restoration, indirect resolver
callers, and remaining module engines are the next implementation/audit scope.


### 2026-10-18 — endpoint implementation continuation (unbuilt)

JSON-RPC, MCP and REST endpoint source now selects parent engines above their
native transport children. MCP stdio facades share IOEndpoint; per-run native
handle duplicates preserve the borrowed originals needed for restart. RPC
output credits, per-link closes, removed-session close chains, disconnect
continuations, REST session cancellation and SSE work are included in their
respective stop paths before final Rx/Tx barriers publish completion. Detailed
contracts, costs, limitations and source-test changes are appended to root
findings.md. Endpoint footprints are unmeasured; no new performance result is
claimed.

No build/test/Valgrind ran in this continuation. The remaining-module source
milestone is still open: zum and ztc remain, as do generic-engine pending
opposite/callback destruction, Hubs restoration, indirect resolver callers,
full post/capture and scheduler-budget audits, and repair1 occurrence closure.
Restart fixture cleanup was reviewed so failed readiness cannot skip required
stop. Relevant suites and the final Valgrind gate remain required.


### 2026-10-18 — service lifecycle continuation (unbuilt)

Zum::Service now has its complete parent engine in source, preserving
discovery/auth/key/SSF registration startup and draining credited HTTP/input
callbacks plus cancelled owner timers before completion. State ownership is
unique; raw callback lifetime is tied to actual drain credits. Runtime cleanup
retains configuration secrets for restart; final wipes them after quiescence.
Boolean lifecycle API migration, separate startup diagnostics, bounded runtime
cleanup and source regression cases are recorded in root findings.md, including
explicit allocation/ref/post and unmeasured footprint costs.

No build/test/Valgrind ran. Daemon's SSF/Requests/OIDC dependency drain contracts
remain to repair before its parent engine and ZTC migration. Ordinary service
waiter/expiry scheduler budgets, generic pending-opposite finalization, Hubs
failure restoration, remaining capture/occurrence closure and all final gates
are still open.


### 2026-10-18 — request and transmitter dependency continuation (unbuilt)

Requests now uses the canonical admission-resource engine. It retains cancelled
deadline nodes through bounded owner retirement, seals counted publication,
and keeps retiring nodes within its admission bound. Immutable token routes
and a done guard prevent late completions from dereferencing a released owner.

SSFTransmitter now has its complete child engine and explicit final. A concrete
graph credit with a raw owner pointer is shared by all real DB/sign/saga/HTTP/
timer descendants. Callback invocation consumes a one-shot handle, selectors
finish at their terminal result, and last graph retirement reaches an owner
barrier before stop completion. One graph is allocated per startup or accepted
public input; internal stages transfer it without new allocations. Detailed
costs, changed pressure behavior, borrowing repairs and source regressions are
appended to root findings.md.

No rebuild, runtime test or Valgrind ran. The remaining-module implementation
milestone stays open for OIDC/provider/Daemon and ZTC, alongside the outstanding
engine, shard, capture, scheduler-budget and repair1 closure audits.


### 2026-10-18 — OIDC dependency continuation (source only)

OIDCHTTP and OIDC protocol state now each own their complete child engine.
Init stays Stopped, stop drains accepted work and owner continuations, restart
retains configuration, and final releases quiescent resources. Adapter clients
are stopped/released in bounded turns; borrowed DNS remains externally owned.
Protocol cancellation uses shadow links under the pending hash and graph
credits cover HTTP responses and DB helper callbacks, including saga work
remaining after an early application result. Variable payloads cross shards in
concrete identified handles with raw owner pointers protected by actual drains.

Root findings.md records costs, source regression cases and the still-open
Server/provider and Daemon dependency propagation. This batch is not executable
acceptance: no build/test/Valgrind ran. Remaining module migrations and all prior
engine, lifetime, budget, occurrence and final verification gates stay open.


### 2026-10-18 — embedded provider continuation (unbuilt)

Server now has its direct engine, stopped init/final, OIDC dependency startup
and explicit provider admission credits. Daemon startup and the direct runtime
fixture use the new boundary. Root findings.md records the 74 callback bodies,
allocation/ref/post costs and source regression additions.

Full provider drainage remains unproven: Requests cancellation can deliver the
application result before DB/saga helpers stop using borrowed provider state.
Explicit helper graph lifetime coverage is the next step. No builds/tests or
Valgrind ran; this source stage is not runtime acceptance. Daemon/ZTC and all
previous final audit/verification requirements remain open.


### 2026-10-18 — provider helper lifetime continuation (source only)

The provider cancellation-vs-DB-helper gap now has an explicit shared credit
through 30 concrete helper/completion lifetimes and 22 helper entry points.
Internal stages share the same credit without allocating per helper. Nine
foreign policy/signing callbacks consume their helper capture when invoked.
Erased credits use ZmPolymorph for correct concrete destruction; source review
corrected an unsafe ZmObject erasure in the preceding unbuilt OIDC batch.

Root findings.md records the ownership argument, reference/vptr costs, propagated
callers and held-signing cancellation regression source. No build/test/Valgrind
ran. Full daemon/ZTC migrations, final shard/capture/budget/engine/repair1 audits
and the deferred verification gates remain open.


### 2026-10-18 — daemon controller continuation (unbuilt)

Daemon now has direct parent engine hooks covering startup rollback, maintenance
cancellation, child joins and final I/O barriers. Final asserts stopped resources
and performs no shutdown wait. Main stops/prepares/restarts around signing
replacement. Maintenance and auth-route work use identified credit handles and
actual helper lifetime holds; costs are appended to root findings.md.

Administrative callbacks/helpers remain outside that credit until the next
implementation stage. Therefore complete daemon lifetime safety is still
unproven, including the new stopped prepare precondition's full drain meaning.
No build/test/Valgrind ran; implementation completion, ZTC, final audits and all
verification gates remain open.


### 2026-10-18 — daemon administrative graph continuation (source only)

Administrative admission/member callbacks and eight more concrete helper
families now share parent credits through actual DB/saga lifetimes. Query and
delegation callback frames retain scopes too. Shutdown drains producers before
stopping the SSF consumer, then drains HTTP. Rx correlation entropy is separate
from the worker RNG and replies retain their original correlation ID.

ZumDaemonTest source and target cover never-started/partial-init final and
reconfiguration. Root findings.md records scope/cost details and unresolved
acceptance. No build/test/Valgrind ran. ZTC, final audits and all executable
verification gates remain open; source coverage is not completion evidence.


### 2026-10-18 — ZTC publisher lifecycle continuation (unbuilt)

Ztc::App now uses a complete parent engine with stopped init/final, fresh
runtime scheduler per start, owned native request-loop slot, sealed accepted
input credits, bounded subscription/index/tail retirement and timer/worker
barriers. A watcher-unregistration ordering race and replay iterator lifetime
were corrected. Root findings.md records contracts, costs and unresolved
registry/fanout budgets. README and lifecycle/PID-failure test sources were
updated; no build/test/Valgrind ran. Agent/Hubd, remaining audits and the full
executable acceptance gate remain open.


### 2026-10-18 — telemetry HTTP dependency continuation (unbuilt)

Agent audit found synchronous starts/stops hidden in PingHTTP init/send/final.
Its state now has a complete engine, asynchronous destination/resolver control,
accepted-work credits in the existing Call allocation and stopped final.
Dependents and lifecycle test sources are updated. Root findings.md records
scope/costs/open failure and integration proofs. No build/test/Valgrind ran.
Agent/Hubd migrations, final audits and executable gates remain incomplete.


### 2026-10-18 — Agent engine continuation (unbuilt)

Agent now owns a complete parent engine, asynchronous HTTP/WebSocket startup
and rollback, owned telemetry scheduler slot, bounded shutdown routing cleanup,
provisioning credits and final Rx/Tx barriers before scheduler retirement.
Final releases stopped configuration without waits. Root findings.md records
ordering/lifetime/cost arguments and remaining normal-routing audits. Lifecycle
and loopback-runtime test sources were added; no build/test/Valgrind ran. Hubd,
final audits, engine/Hubs issues and executable acceptance remain incomplete.


### 2026-10-18 — clarified cluster activation contract (source only)

User requires multiplexer process startup in main and listener activation only
under Zdb up/down. repair2.md records this distinction. Premature zumd listener
startup was removed; a DB-thread generation-checked continuation starts after
preparation, and down requests daemon stop. Hubd engine/readiness API migration
is partial and propagated, but its actual listener activation wiring is still
incomplete. Root findings.md identifies that gap explicitly. No builds/tests/
Valgrind ran, and full acceptance remains open.


### 2026-10-18 — role listeners and aggregate stop retry (source only)

Hubd now wires configured listener aggregates to DB up/down, counts actual
transition completion and joins them at parent stop. Main configures listeners
without opening them and keeps multiplex startup independent of DB role. A held
activation-completion regression source and docs were added. Root findings.md
records costs, contracts and remaining failure/budget/callback gaps.

Hubs normal stop now targets every registered hub idempotently on retry and
retains the active marker after a failed stop; its old skipped-retry bug has a
new regression source. Full operational restoration after stopped(false) is
still unresolved. No build/test/Valgrind ran; executable acceptance remains open.


### 2026-10-18 — canonical engine completion continuation (source only)

ZmEngine now reserves queued opposite state before invoking completion callbacks
and completes already-settled failure/cancellation queues from local storage.
This removes the demonstrated post-callback owner replay and stranded cancelled
waiters without adding per-module lifecycle state. New reentrant deletion and
cancelled-waiter test sources are recorded in root findings.md, along with
contracts/costs. No build/test/Valgrind ran. Full integration/reentrancy proof,
Hubs restoration, remaining audits and executable acceptance remain open.


### 2026-10-18 — stop contract corrected by user

Completed stop is best effort and ends Stopped even if its callback reports
failure. This supersedes earlier restoration/Running requirements in this
review. repair2.md and ZmEngine now express that contract. Hubs continues all
shutdown steps and reports combined diagnostics without restarting stopped hubs;
ClientCore always stops its owned resolver after pool completion. Tests were
updated in source only. Root findings.md records the correction and associated
Hubd/error-drain changes. User-authorized subagents now handle remaining audits.
No build/test/Valgrind ran; full completion remains unverified.

### 2026-10-18 — delegated shutdown and native rollback review (source only)

Engine/Hubs regression sources now cover failed-stop deletion, fresh queued
restart and best-effort failed rollback. Endpoint stop-result propagation spans
all ten HTTP/WS parents without skipping drains. PoolImpl reports its hub error
after final barriers. ZiMultiplex rolls back already-started scheduler workers
on native handle creation failure; ZiMxLoopTest has isolated failure/retry source
coverage. Resolver runtime schedulers have a concrete identified heap.

H3 client shutdown retires active registry entries before abort, preserving
existing retired ownership and pending credits through actual completion. This
removes the unbounded scan of already-stopping entries without extra storage.
Root findings.md records ownership, result/cancellation distinctions, costs and
remaining proof obligations. Current source checks pass; no executable tests or
milestone builds ran. Role-retirement, final capture/budget/occurrence closure
and runtime/memory acceptance remain open.

### 2026-10-18 — final source dispositions; incremental build underway

Server H2 disconnect collection/notification is now bounded and ends in final
Tx/Rx barriers. Logical H2 native/id handles have distinct Rx/Tx snapshots.
H1 pending response ordering now uses an intrinsic unique shadow tree, avoiding
the unbounded sorted-list insertion; this adds three tree-link words to each
ResponseRecord without a wrapper allocation. Existing TCP/TLS stream fixtures
cover reverse order, duplicate rejection and cancellation across a yield.
Four client/server H2 collectors now create their copied state capture before
moving the same local handle into the completion callback, independently of
function argument evaluation order.

Finding 8's retained client/multiplexed handoff families have explicit registry,
retirement and executing-callback dispositions in root findings.md. Generation
checks do not replace lifetime ownership. Finding 22's StreamTaskQ allocation
remains an explained optional amber: embedding heterogeneous driver ownership
would change type-erased cancellation/reference release rather than establish
a local allocation removal. Finding 23 retains documented configurable queue
defaults, compaction heuristics, full-slot hash rebuild costs and bounded spare
payload residency; no workload-optimal sizing or latency measurements are
claimed. Alt-Svc's bounded inactive residency exception remains as recorded.

Requests owner startup/result propagation and Hubd listener-stop/bounded-role
retirement are implemented in source; role-handler and cancellation regressions
were added. Root findings.md records costs and the distinct superseded-opposite
contract. Process multiplex startup remains separate from DB-controlled active
listeners. The implementation milestone incremental build is running with the
existing clang/debug configuration. No executable tests or Valgrind have run
yet; these source dispositions do not satisfy runtime acceptance.

### 2026-10-18 — HTTP functional validation

The stable incremental build refreshed the HTTP library and default test targets
without compiler diagnostics. All 26 unit executables passed (262 top-level TAP
tests). Lifecycle and client-streaming integrations passed; the server-streaming
fixture initially hit unrelated control-frame pressure before its intended
terminal response rejection. A targeted GDB schedule demonstrated the second
WINDOW_UPDATE rejection. QueueTrailers now sends an empty request while retaining
its response queue/window pressure and terminal assertions. The affected target
rebuilt warning-free and all 38 server-streaming cases passed. No production
capacity was changed. Root findings.md records the exact logs and the native
multiplexer fault-fixture correction. Dependent daemon/cluster and final Valgrind
acceptance remain open; no memory-clean or performance result is claimed yet.

### 2026-10-18 — final acceptance

Both repair plans now have runtime acceptance in the retained current build
configuration. The coherent top-level incremental `make -j8 all` completed
warning-free. HTTP's 26 unit executables and three integrations passed, including
the corrected H2 rotation, server-streaming queue-pressure and REST retirement
fixtures recorded in root findings.md. Current final Memcheck coverage is 32
executables / 308 TAP checks across foundation, HTTP, seven endpoints and ten
daemon graphs; all completed parent records have zero unsuppressed errors and
zero definite/indirect/possible lost bytes. The sole matched exception is the
reviewed opt-in OpenSSL GHASH conditional rule, with its version/build-ID scope
and stack-matching limitation explicitly retained.

The final memory gate also verifies the debug-reference adoption and scheduler
last-credit deletion repairs; MCP HTTP's actual native connection leak is gone.
Fresh Zum/Ztc functional groups passed 37 executables / 96 checks. Three earlier
real DB-role/cluster gates passed, preserving main-controlled process runtime
and Zdb-controlled listeners. See root [findings.md](../findings.md), final
acceptance section, for exact logs, chronology and limitations. The documented
optional amber dispositions remain; no benchmark, alternate configuration or
exception-free crypto definedness claim is made.

### 2026-10-18 — repair3 source milestone; acceptance pending

The follow-up found release-path eviction and narrowing H2 configuration defects
that earlier green gates did not establish. R1 now selects the victim outside
its debug assertion; R2 hides the TLS base and migrates native H1 callers through
TLSConfig/the canonical tls boundary. Numeric conversion uses ZuBoxed scanning
with the necessary protocol range/overflow boundary. R3 adds canonical sized
VHeap allocation/free, eliminating LHash's redundant prefix while preserving
unsized minimum/VShift tuning and the heap-manager bootstrap escape.

The mandatory no-polling audit also repaired REST cancellation/completion, SSE
close publication, RPC/MCP final-credit notification, Requests deactivation,
active H2 header retirement and logger overflow stop. Remaining continuations
consume actual bounded work or follow completion notifications. Stop is still
idempotent/best effort and ends Stopped on failure. Process multiplex startup
and Zdb-controlled listener activation remain separate.

R4 retains common response tree links with a concrete layout/alternative
analysis; R5 removes separate auth-route/maintenance credit allocations and
normal return posts while retaining necessary heterogeneous graph credits.
R7 remains an explicit inactive-cache-residency policy exception: logical expiry
and bounded admission do not imply physical reclamation during inactivity.
No timer, expiry index or background scan is added. Root findings.md records
per-family lifetime proofs, costs, guideline references and measurement limits.

One coherent current-configuration incremental build is now running. Fresh
functional, concrete-layout and final Memcheck acceptance remains pending;
previous acceptance above is historical, not repair3 validation. No measured
latency, throughput or full guideline-compliance claim is made.

### 2026-10-18 — repair3 closure and current conclusion

R1 release eviction and R2 fluent configuration defects are repaired, including
required H1-only caller migrations. R3 uses known-size canonical VHeap backing
without its redundant prefix; class-counter tests verify the 4,096-byte payload
class rather than the former 8,192-byte class. Existing unsized tuning and the
heap-manager bootstrap escape remain intact.

R6 removes shutdown readiness polling throughout the affected REST/SSE,
RPC/MCP, Requests, H2 header-retirement and logger-overflow inventories. Actual
completion notifications or bounded destructive/output work drive progress.
Publication, callback registration and raw-cursor retirement races have explicit
source proofs and fresh regressions. Stop is idempotent/best effort and remains
Stopped on failure; final does not initiate or wait for stop. Main multiplexer
startup and Zdb-controlled listener activation remain separate.

R4 retains 24 common tree-link bytes: the measured response node stays in its
128-byte class, and an overlay/alternative requires broader container or erased
record changes. R5 removes one 32-byte credit allocation in each auth-route and
maintenance graph and ordinary owner-return posts; heterogeneous credits remain
necessary. Fresh storage evidence also exposes costs: several RPC controllers
are 64 bytes larger, REST Server is 80 larger, WorkNode adds eight logical bytes
without a class increase, and SSEProducer saves 64 bytes/a class step. Logger
stop has one cold completion-closure allocation. No module-wide net memory or
measured latency/throughput improvement is claimed. R7 remains an explicit
unresolved inactive-cache-residency policy exception, with no new timer or scan.

The coherent incremental default-all build completed in the retained clang/debug
configuration after compiler-guided fixes, reusing artifacts throughout. Fresh
functional coverage passed 70 executables / 552 top-level TAP checks, including
the selected real IAM/auth-route integration. Other publisher/WSS cluster
results remain historical. Final Memcheck passed 39 normal parent executables /
369 TAP checks: every normal parent has zero unsuppressed errors and zero
definite/indirect/possible lost bytes.

Two memory qualifications remain explicit. Nine intentional assertion-abort
children produce 60 possible-loss contexts (48,120 bytes across fork snapshots)
because they never run teardown; independent review found the exact intended
assertions and no invalid-access diagnostics. They are not clean child runs,
and no loss suppression was added. Separately, the existing version/build-ID
checked OpenSSL conditional exception remains enabled; current aggregate parent
suppression counters are 2,228 reports / 15 contexts, with core rule-name
attribution limited by its aggregate-only logs. The daemon's one suppressed
context is explicitly the reviewed OpenSSL rule. No exception-free crypto or
all-descendant memory-clean claim follows.

See root [findings.md](../findings.md), repair3 closure, for per-finding guideline
references, costs/layouts, exact logs, source lifetime proofs and coverage
limits. No required repair3 implementation or selected validation remains;
retained costs, policy conflict and memory exceptions remain visible rather
than being presented as full guideline/performance compliance.
