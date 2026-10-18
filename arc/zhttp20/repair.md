# Repair plan for the zhttp/src audit

Source: [findings.md](findings.md), dated 2026-10-18. Finding numbers below
refer to that report.

This document plans repairs; it does not implement them. Retain the current
build configuration and reuse its existing artifacts. Batch source changes into
large implementation batches; rebuild and retest infrequently, at major
milestones rather than after each repair. Use incremental builds at those
milestones. Defer rebuilding dependent modules until the end of implementation.
Other compilers, release/debug flag combinations, and platforms are out of
scope. Use Valgrind sparingly to investigate memory corruption when needed,
then as a final memory leak and corruption gate.

## Scope and repair rules

- Preserve existing worktree changes and the historical report in
  `zhttp/findings.md`. Do not revert unrelated work.
- Follow the repair procedure in `GUIDELINES.md`: establish the contract,
  violated invariant, owning component, and smallest demonstrated repair for
  each finding. Correct or qualify a finding if further inspection disproves
  it; do not change code merely to remove a flagged pattern.
- Start implementation in `zhttp/src`. Inspect declarations and callers across
  the repository before changing interfaces, and propagate necessary dependent
  changes without compatibility shims. Audit scope is not blanket edit scope.
- Record added allocations, copies, reference operations, container links,
  lookups, scheduler posts, and object-size costs for architectural changes.
- Keep protocol acceptance and external behavior unchanged unless the finding
  demonstrates a defect requiring a behavior change.
- Use separate, reviewable patches for independent repairs. Integrate member
  layout changes with the repair that establishes ownership, avoiding repeated
  churn in the same types.

## Phase 0: establish contracts and occurrence inventory

Before editing implementation, enumerate all occurrences represented by each
finding, including compile-time feature branches and template instantiations.
Use current symbols as well as recorded line numbers, which will drift.

Prepare four small review inventories:

1. H1 streaming entry points: permitted calling shard, borrowed callback
   lifetime, synchronous return semantics, and Rx/Tx state touched.
2. Connection lifetime: admission gate, timer cancellation, Rx drain, Tx drain,
   ownership release, and any posts made by drain callbacks themselves.
3. Codec allocations: concrete allocation type, allocator inherited by that
   type, storage allocator, heap identity, deletion path, and actual bounds.
4. Counters and caches: writer shard(s), operational readers, telemetry-only
   readers, retention bound, and removal trigger.

This phase must distinguish a genuine nullable operation result from allocation
failure handled by `ZmHeapFail`, and a genuinely optional callback from a
no-op default that can safely replace detection.

## Phase 1: contain failures and repair local ownership defects

### Finding 3: HPACK iterator lifetime

In `ZhttpHPack.cc`, put the copy traversal in a block that ends before assigning
`m_neverIndex`. Preserve copy-on-write behavior and the reference-count guard.

Static completion criterion: no iterator or reference into the replaced set
survives the assignment; the QPACK implementation remains consistent.

### Finding 2: H3 filter RNG ownership

In `ZhttpH3.hh`, give the Rx and Tx filter paths separate `ZmRandom` instances.
Group each mutable generator with its owning shard's data and separate those
groups with `alignas(Zm::CacheLineSize)`. Confirm filter installation/removal
and endpoint teardown prevent callbacks from accessing destroyed state.
Retain the feature guard and current drop-probability semantics; add no locks.

Static completion criterion: each generator has one writer shard, and both
filter callback lifetimes end before generator destruction.

### Finding 4: H2 control-message completion

Trace initial SETTINGS and every control-message sender through the native
stream's synchronous rejection and asynchronous error paths. Check explicit
completion-boundary flushes and route rejection through the existing owning
connection error path. Change return types and callers only where needed.

Record SETTINGS as outstanding and arm its timer only after synchronous
submission succeeds. Preserve the distinction between successful submission
and later transport completion. Apply the same treatment to ACK, PING, RESET,
GOAWAY, and WINDOW_UPDATE; do not add retries or exceptions.

Static completion criterion: rejected output cannot advance successful-send
state, arm an unjustified timer, or silently continue the connection's normal
control flow. Errors reach their owner on the proper shard.

## Phase 2: establish streaming ownership and bounded teardown

### Findings 1 and 22: H1 streaming state

Apply one design across native H1 client/server links and TLS H1 application
links:

- Keep negotiation, input dispatch, and peer-terminal state on Rx.
- Keep output admission, queued output, and Tx termination state on Tx.
- Publish only the small state needed by the destination shard through ordered
  posts. Separate shard-owned mutable groups by cache line.
- Keep genuinely cross-shard public functions thin dispatchers and implement
  owner-only work in trailing-underscore variants with suitable assertions.

Preserve synchronous borrowed-writer calls. Do not post a printer, a reference
to a stack result, or variable-sized callback state merely to make an API appear
shard-safe. Where a synchronous entry point is owner-affine, state that contract
and propagate caller changes; do not invent blocking cross-shard results.

Static completion criterion: each mutable member has one owner, each entry
point obeys its calling contract, and no Tx operation reads the Rx-owned
`StreamBinding` object.

### Finding 6: scheduler-turn bounds

Batch H2 blocked-stream requeueing, initial-window changes, frame cleanup, and
ordered H1 response release/cancellation. Reuse existing work-budget constants
where their units match; otherwise define and justify a concise named bound.

Count work actually inspected, including discarded entries and frames. A batch
of streams is insufficient if one stream can drain an unbounded frame queue.
Never retain an iterator across a scheduler post. Keep continuation state
owner-affine and use stable identities or existing intrusive queues.

For initial-window changes, preserve overflow validation and application
ordering across successive SETTINGS and WINDOW_UPDATE messages. Establish how
interleaving is prevented before splitting the two passes; do not expose a
partially applied update as complete. Preserve HEADERS/CONTINUATION ordering
while permitting a scheduler yield between bounded turns.

Static completion criterion: every relevant turn has a finite work budget,
remaining work gets a continuation, and stop completion waits for that work.

### Finding 8: owner references in continuations

Perform this repair after the streaming and batching contracts are established.
For each captured owner reference, identify who independently retains the owner
until every dependent post is drained. Then replace redundant references with
raw pointers under the three-phase teardown contract: stop ingress and timers,
drain Rx, drain Tx, and release ownership only after dependent work is finished.

Do not mechanically replace every `ZmRef` capture. Retain and document genuinely
required ownership transfers, including references to transferred I/O buffers
and independently owned application messages. Account for callbacks that post
back to the other shard during cancellation.

Static completion criterion: each removed reference has an explicit lifetime
argument, and destruction cannot race any remaining raw-pointer continuation.

## Phase 3: allocation, cache residency, and counters

### Finding 7: concrete heap identity

Give `DiscoveryRequest` its own first-base `ZmNHeap` specialization and ADL
heap ID. For dynamically allocated codec hashes, use concrete heap-identified
types whose CRTP argument and deletion path name the actual allocation type.
Preserve hash semantics, sizing, and ownership; add no generic allocation base
or forwarding allocation operators.

Review hash object allocation separately from internal table storage. Verify
the allocator used by the current `ZmLHash` implementation before choosing a
storage repair. If it requires a foundational allocator change, record that as
a separate, justified scope extension rather than concealing it in the HTTP
patch or duplicating a hash implementation locally.

Static completion criterion: fixed-size objects and variable-size storage each
have the appropriate allocator and identity, without nullable heap-failure
checks or deletion through an incorrect type.

### Finding 5: Alt-Svc retention and admission

First establish whether cache entries must disappear at wall-clock expiry, or
whether expiry invalidates values while bounded storage can remain resident
until a cache operation. These are different requirements: immediate physical
reclamation during complete inactivity cannot be achieved without an event,
and the guidelines prohibit timer-based GC.

Repair the demonstrated admission defect without a background scan or timer:
select a bounded, deterministic eviction policy for admission and replacement.
Remove an origin's state directly when it is cleared, replaced, or evicted.
If expiry ordering is needed, use an existing intrusive ordered container and
account for its per-entry storage and update cost; do not add an index without
showing why simpler bounded eviction is insufficient. Keep expired values
unobservable and avoid reparsing or scanning unrelated origins on a hit.

Static completion criterion: cold expired origins cannot permanently prevent
new admission; resident storage stays bounded; each removal trigger is
explicit. Do not mark the strict immediate-reclamation interpretation resolved
without an agreed policy or documented exception to the conflicting rules.

### Finding 9: diagnostic counters

Confirm each counter is telemetry-only. Retain synchronization for operational
admission and memory accounting. Replace unnecessary diagnostic atomics with
primitive state owned by the writing shard. If a counter has multiple writers,
use separate Rx/Tx primitive contributions and aggregate only for a telemetry
read; do not replace a shared atomic increment with competing ordinary writes.
Unclean observer reads are permitted by the project contract.

Static completion criterion: telemetry updates require no atomic operations or
locks, and no operational decision relies on an approximate diagnostic value.

## Phase 4: matching, storage, and common encoding work

### Finding 10: HPACK suppression

Replace per-seed linear searches with suppression indexed by seed position.
Choose storage from the actual configured seed count, initialize it once at
the proper block boundary, and reuse capacity. Preserve suppression semantics
for duplicates, rollback, empty plans, and bootstrap commit. Avoid an arbitrary
fixed capacity or a per-field heap allocation.

Static completion criterion: each selected seed requires constant-time
suppression lookup, with no nested membership scan.

### Finding 11: fixed token matching

Define `ZuMatcher` implementations in the owning `.cc` files for schemes,
migration modes, and Alt-Svc parameter names. Preserve existing case handling,
aliases, unknown-token behavior, and duplicate-parameter behavior. A single
remaining candidate may continue using direct equality.

Static completion criterion: fixed-set dispatch no longer uses sequential
string comparisons, and accepted input remains unchanged.

### Findings 21 and 23: shared encoding and scratch storage

Factor the repeated printable-field rendering and prefix/gap finalization into
small template or local utilities. Consolidate writer-table adapters only
where reserve, commit, failure, ownership, and buffer semantics truly match.
Keep protocol-specific decisions at their current owning layer.

Use the named scratch bound in place of repeated `256` literals where it
represents the same workload. Derive prefix storage bounds from the encoding's
actual maximum and document the derivation. Document capacity defaults and
compaction thresholds with a protocol basis or real footprint/scaling evidence;
do not invent measurements or change capacities just to clear the finding.

For callback-local rendered field names, use identified stack scratch with heap
fallback when the callback lifetime permits. Use existing bulk append or direct
destination storage for incremental string assembly where supported, preserving
length limits, partial-input behavior, and sticky failure state.

Static completion criterion: factoring adds no runtime dispatch, allocation, or
copy; temporary storage has a bounded lifetime and justified size. Performance
claims requiring measurements remain unverified until measured at an appropriate
milestone.

### Finding 22: layout and stream-task allocation

Add concise aliases immediately before the declarations they support. Reorder
members within their logical ownership groups to reduce demonstrable padding;
keep immutable state first and shard boundaries intact. Check aggregate and
constructor initializers and any metadata ordering independently.

For `StreamTaskQ`, investigate embedding the required intrusive task node in the
existing driver allocation. Preserve type-erased dispatch, cancellation,
reference ownership, and exactly-once unlinking. Make this a separate patch only
if it removes the extra allocation without introducing a weak allocation base
or broader lifecycle changes. Otherwise retain it as an explained amber issue.

## Phase 5: interfaces, type declarations, and header hygiene

| Finding | Planned repair | Static completion criterion |
| --- | --- | --- |
| 12 | Move exposed struct data before constructors and behavior, after necessary aliases. | Every inventoried struct follows the required declaration order; initialization dependencies remain correct. |
| 13 | Use classes for encapsulated state and structs for shared data. Rename private members with `m_`; remove that prefix from public shared data. Factor a data struct where mixed ownership actually requires one. | No hybrid composite or cross-owner private-data access remains in the repaired types. |
| 14 | Move parameter-independent `Prefix`, stream state enums, and `ValueSpan` out of their enclosing templates. | The moved declarations have no hidden dependence on template parameters or specialization-specific behavior. |
| 15 | Pass producer constructor inputs explicitly or use narrow owning-class accessors; use the existing server context accessor. | Another containing type does not access private `m_` state directly; no broad accessor surface is added. |
| 16 | Replace prohibited named/scoped enums with the project's namespace/struct enum pattern, using `ZtEnum` where appropriate. Use signed storage wide enough for all values, and enums for eligible small constants. | Wire values and sentinel ranges are preserved; dependent declarations and dispatch sites use the new types consistently. |
| 17 | Replace all four blanket captures with explicit captures. | Each capture is necessary, and borrowed data remains confined to the synchronous callback. |
| 18 | Move participation checks into trailing template parameters. Preserve overload ranking and return categories; consolidate overloads only where declaration identity requires it. | Valid and invalid substitution cases retain their intended overload selection without return-type SFINAE. |
| 19 | Hoist pure base functions with `using`. Expose nested/base TLS configuration through accessors and a same-named lambda overload following `ZiMxParams::scheduler()`. Propagate dependent configuration expressions. | Fluent expressions retain the outer type, intended overloads remain visible, and no compatibility forwarders remain. |
| 20 | Add verified direct dependency headers, remove unused includes where demonstrated, order dependency groups, and repair the SSE component-header guard. | Required declarations are directly included or deliberately forward-declared; the header graph stays acyclic. |
| 24 | Cache true loop invariants and repeated indexed reads; remove unnecessary char-equivalent casts; normalize division, member references, file-static names, and braced-case breaks. | Side effects, overload resolution, target-system type punning, and local tab/alignment conventions are preserved. |

Do structural cleanup after ownership changes so newly established data groups
are not moved twice. Do not change public types to smaller signed storage
without first checking their actual numeric range. Do not remove a cast merely
because it looks unusual: distinguish representation-changing casts from
redundant span/string conversions.

## Dependency order and patch boundaries

1. Inventory and contracts from phase 0.
2. Independent iterator, filter RNG, and control-message failure repairs.
3. H1 shard ownership, then bounded scheduler work, then removal of redundant
   owner references. Review their combined teardown ordering before closure.
4. Concrete heap identity, cache policy, and telemetry counters as independent
   patches after their contracts are established.
5. HPACK indexed suppression and token matching, followed by encoding/scratch
   factoring and optional intrusive task allocation.
6. Remaining interface and declaration cleanup, direct includes, and local
   style normalization. Include necessary dependent API migrations in the
   patch that changes the API, not in a later compatibility patch.

## Review and closure

Use source inspection and textual diff review throughout implementation. Batch
changes before incremental builds and tests at major milestones; rebuild and
test dependent modules at the end of implementation. For every repaired finding,
record the final files/symbols, invariant, ownership proof where relevant, and
remaining limitations. Inspect every changed line for necessity and check
dependent callers, feature guards, 98-column width, hard-tab alignment, and the
28-byte naming limit.

Track findings individually as planned, repaired by static review, needs policy
decision, or retained with a justified exception. Split findings 22 and 23 into
their separate subissues when recording completion. Finding 5 must explicitly
record the chosen expiry/residency contract; finding 8 requires a lifetime proof
for each removed owner reference.

Preserve the original audit evidence and append dated dispositions to
`findings.md` during implementation. A static repair is not runtime acceptance.
Record which milestone builds and tests actually ran with the current
configuration. Run Valgrind for the final memory leak and corruption gate, and
earlier only when needed to debug memory corruption. Do not claim verification
for unrun configurations, platforms, sanitizers, or performance measurements.
