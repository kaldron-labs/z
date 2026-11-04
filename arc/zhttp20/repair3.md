# Repair plan for the repair-review follow-up

Date: 2026-10-18.

Sources: the follow-up review of [zhttp/repair-review.md](zhttp/repair-review.md),
the current implementation, and [GUIDELINES.md](docs/GUIDELINES.md). Historical
implementation and validation evidence remains in [findings.md](findings.md).

This document is a plan, not an implementation or a new acceptance result.
Repair the demonstrated correctness and API defects first. Address added
storage/allocation costs and retained guideline issues separately, preserving
the shutdown and shard-ownership guarantees established by the earlier work.

## Governing requirements

- Follow GUIDELINES.md and applicable module instructions in full. Establish
  the contract, violated invariant, owning component and dependent callers
  before each edit. Preserve unrelated working-tree changes and diagnostics.
- Retain the current build configuration:
  - Rebuild infrequently. Batch source code changes into large batches; do
    not rebuild or retest until major milestones are reached.
  - Rebuild incrementally with `make -j8`, reusing current build artifacts.
    Do not clean or reconfigure.
  - Other build configurations are out of scope: other compilers, other
    release/debug build flags, other platforms, additional build directories
    and sanitizer configurations.
  - Defer dependent rebuilding to the end of implementation.
- Use Valgrind sparingly:
  - To debug memory corruption as needed.
  - As a final memory leak and corruption gate.
  Use `libtool exec` for source-tree debugging tools; never execute `.libs`
  binaries directly.
- Stop remains idempotent and best effort. Complete every shutdown step and
  leave Stopped even on failure. Do not restart resources to undo a failed
  stop. A separately queued start performs fresh startup.
- Main controls process multiplexer startup independently of activation.
  Zdb up/down controls listeners, preserving role/generation fences and
  retirement before reactivation. Keep database readiness distinct from
  process engine state.
- Final releases stopped, drained resources. It must neither initiate stop
  nor wait. Preserve callback-finalization and queued-opposite semantics.
- Polling during shutdown is absolutely prohibited. This applies throughout
  the implementation and affected dependents, including existing code. No
  spin loops, repeated readiness-check posts, sleeps, timer-based retries or
  periodic scans while waiting for work to finish. Complete through existing
  ordering guarantees or notifications from actual completion events. This
  requirement cannot be retained as an amber or waived by a passing test.
- Keep Rx/Tx state on its owning shard. Do not remove lifetime credits or
  synchronization merely to reduce a cost. Prove the replacement drain
  contract before removing a reference, allocation, post or atomic operation.
- Propagate necessary API changes through dependents without compatibility
  shims, pure base-forwarding setters or additional public tuning knobs.
- Record allocations, size-class effects, bytes, copies, reference operations,
  atomics, scheduler posts and inspected work. Distinguish source-proven costs
  from measured latency/throughput; passing functional tests proves neither.

## Skeptical implementation review

Act as a skeptical principal engineer. Preserve required behavior, not the
internal machinery chosen by an earlier repair. For every intended element:

1. Identify the concrete finding and observable contract it serves. Delete
   elements with no necessary consumer, invariant or lifetime responsibility.
2. Try deletion and reuse before replacement: remove redundant functionality,
   flags, counters, queues, indexes, wrappers, modules, algorithms and validation.
   Prove why the simpler existing design is insufficient before adding one.
3. Give each state item one authority. Do not mirror an engine transition,
   container membership, pending count or already-known allocation size.
4. Validate untrusted input once at its boundary. Assert internal invariants
   with the correct Z assertion. Do not add defensive branches, retries or
   nullable-allocation checks for framework or caller guarantees.
5. Reject a generic abstraction for a local scalar operation, a second
   implementation of a Z facility, or a new module without a demonstrated
   responsibility that existing modules cannot own. Factor real duplication
   with established templates/CRTP and side-effect-safe base defaults.
6. Compare the smallest sound alternatives using actual state, allocation,
   dispatch, reference and post counts. Delete superseded code and its direct
   dependents in the same change; retain unrelated diagnostic probes.

Rules: [Repair procedure](docs/GUIDELINES.md#repair-procedure),
[Framework fit](docs/GUIDELINES.md#framework-fit),
[Scope and cleanup](docs/GUIDELINES.md#scope-and-cleanup), and
[Static polymorphism and constraints](docs/GUIDELINES.md#static-polymorphism-and-constraints).

### Required framework choices

| Need in the intended implementation | Required approach |
| --- | --- |
| Synchronous temporary strings/bytes | Use identified `ZtScratch` stack scratch with heap fallback; use `ZmAlloc` for appropriate large callback-local objects. Do not create a heap-owned handle merely for scratch. |
| Storage retained across callbacks | Use an existing owner allocation or an identified Z container/heap. Stack scratch must not escape its scope; heap fallback does not make that escape valid. Use `ZtBuiltin` only when measured or established common-case occupancy justifies its inline size. |
| I/O payloads | Retain pooled `ZiIOBuf` handles and pool tuning; do not replace them with stack buffers or temporary contiguous copies. |
| Formatting and numbers | Stream with `ZuBox`, `ZuFmt`, `ZuPrint` and Z string/array sinks. Use existing `ZuBox::scan`/Z numeric scanning where needed, checking consumed input and protocol range once at the untrusted boundary. Do not hand-code number conversion or varargs formatting. |
| JSON and other metadata formats | Use `ZfJSON`, `ZfStruct` and the appropriate `ZfCLI`/`ZfURI`/`Zfb` integrations. No bespoke JSON parser, serializer, escaping or test-output encoder. Preserve necessary protocol framing where no existing facility implements it. |
| Span/string/array handoffs | Use their existing conversions directly. Delete redundant char-equivalent, const/span and array casts; do not reconstruct through `.data()` just to change an equivalent type. Preserve a demonstrated representation-changing cast only. |
| Matching, hashes and containers | Use `ZuMatcher`, `ZuHash`, `ZmHash`/`ZmLHash`, `ZmList` and `ZmRBTree` as appropriate. No local hash, tree, free list, string-match chain or allocator implementation. Prefer existing intrusive links. |
| Callbacks and completion | Use `ZmFn`, existing owner-shard dispatch and canonical engine hooks. Keep captures to necessary fixed metadata/handles. No blanket captures, worker blocking, or shutdown polling. |

Rules: [Strings](docs/GUIDELINES.md#strings),
[Storage and lifetime](docs/GUIDELINES.md#storage-and-lifetime),
[Formatting](docs/GUIDELINES.md#formatting), [Metadata](docs/GUIDELINES.md#metadata),
[Bad casting](docs/GUIDELINES.md#bad-casting),
[Compile-time data and matching](docs/GUIDELINES.md#compile-time-data-and-matching),
[I/O and system integration](docs/GUIDELINES.md#io-and-system-integration), and
[Callbacks](docs/GUIDELINES.md#callbacks).

### Names, structure and audit flags

Apply the complete [Audit flags](docs/GUIDELINES.md#audit-flags) checklist to each
intended change and its dependent declarations/callers, not only the flags
that originally found it. Record applicable violations and why any flagged
construct is necessary; do not turn the checklist into extra runtime checks.

- Names: at most 28 bytes, camelCase and established abbreviations; no vague
  new manager/context/controller vocabulary where an existing owner suffices.
  Use overloaded accessors, standard container verbs and trailing underscores
  for internal/shard-owned functions. Follow module-prefixed source/test names
  and lowercase integration basenames. See [Naming](docs/GUIDELINES.md#naming).
- Data: classes have private trailing `m_` members; shared structs have public
  unprefixed data before behavior. No hybrid data ownership, friend access to
  another class's private members, or parameter-independent types nested in
  templates. Use concise local aliases and direct dependency headers.
- Code: use GNU C++2b Z detector/SFINAE conventions, with constraints in trailing
  template parameters. No concepts/requires, named scoped enums, blanket
  captures, anonymous .cc namespaces or pure forwarding functions. Cache true
  loop invariants; use switch for discrete dispatch and flatten control flow.
- Storage: scrutinize fixed capacities, separate nodes/credits, initialization
  before overwrite, temporary copies and heap allocation. Fixed concrete
  allocated objects require their own first-base ZmNHeap and heap ID; genuine
  variable-size storage uses the existing identified variable heap facilities.
- Concurrency: remove unnecessary diagnostic atomics/locks; preserve required
  admission synchronization, shard ownership, raw back-pointers with proven
  drains, and bounded actual work per scheduler turn. No lock may legitimize
  non-owner access. Never replace shutdown polling with another polling form.
- Scope: check framework duplication, dead compatibility code, casts, dynamic
  module rules where applicable, and liveness. A flag with no occurrence needs
  no implementation. Do not expand into unrelated modules to manufacture work.

Also apply [Class and struct shape](docs/GUIDELINES.md#class-and-struct-shape),
[Header layout](docs/GUIDELINES.md#header-layout),
[I/O sharding](docs/GUIDELINES.md#io-sharding), and the hard-tab/98-column source rules.

## Findings and priorities

| ID | Priority | Finding | Classification |
| --- | --- | --- | --- |
| R1 | High | Alt-Svc eviction advances its iterator only inside a debug assertion. | Introduced correctness defect; release admission can exceed maxOrigins. |
| R2 | Medium | H2Config exposes inherited TLS setters returning TLSConfig references. | Introduced dependent API defect; inline configuration loses its outer type. |
| R3 | Medium | Dynamic ZmLHash backing adds an alignment prefix before power-of-two rounding. | Introduced framework-wide memory-footprint cost. |
| R4 | Medium | ResponseRecord carries H1 ordering tree links for every protocol. | Introduced common-path storage cost; H1 algorithmic repair remains necessary. |
| R5 | Medium | Daemon/provider graph credits add separate allocations and completion posts. | Introduced hot-path cost; graph lifetime coverage remains necessary. |
| R6 | High | REST shutdown repeatedly reposts a readiness check without progress. | Mandatory removal; shutdown polling is absolutely prohibited. |
| R7 | Policy | Inactive Alt-Svc entries can retain expired storage. | Retained, explicitly documented deterministic-reclamation exception. |

Do not treat a documented amber as an automatic permission to expand scope,
or a source cost as proof of a measured performance regression. Conversely,
documenting a cost does not establish that it is minimal or guideline-compliant.

## Phase 0: contracts and occurrence inventory

Before implementation, inventory:

1. Alt-Svc update/lookup/removal, admission limits and owning lifecycle.
2. H2Config TLS getters/setters, implicit base conversions, constructors and
   inline expressions throughout source, tests, examples and other dependents.
3. Dynamic/static/local ZmLHash allocation and free paths, resize old-capacity
   handling, codec identities and the heap-manager bootstrap specialization.
4. ResponseRecord concrete allocation shapes, protocol use and route/ready
   queue membership, including cancellation and late application submissions.
5. DaemonWork/ServerWork allocations and every DB/saga/signing/HTTP descendant
   that can survive the application result or request-node retirement.
6. REST work retirement, SSE producer credit release, session cancellation,
   transport-stop completion and final Rx/Tx barriers.

Record introduced changes separately from retained behavior. Use current
symbols rather than historical line numbers. Audit declarations and callers;
audit scope does not authorize unrelated edits.

## Phase 1: repair the two demonstrated defects

### R1: Alt-Svc admission must enforce its bound in every build

Files: `zhttp/src/ZhttpAltSvc.cc`, `ZhttpDiscovery.hh`, relevant cache tests.

Rules: [Assertions](docs/GUIDELINES.md#assertions),
[Repair procedure](docs/GUIDELINES.md#repair-procedure), and
[Algorithm and control flow](docs/GUIDELINES.md#algorithm-and-control-flow).

- Move the required iterator advancement outside the debug-only assertion.
  The iterator must select a victim before deletion regardless of ZDEBUG.
- Preserve the iterator's narrow scope: finish deletion/traversal before
  adding or replacing an entry. Do not add an expired-entry scan or timer.
- Preserve existing replacement, clear and max-age-zero invalidation behavior.
  Keep maxOrigins zero admission behavior explicit and consistent.
- Inspect assertions in the touched admission/removal paths for other required
  side effects. Use the appropriate assertion/fallback only for the invariant;
  required work must not depend on whether an assertion is compiled out.
- Keep this a local iterator operation and invariant assertion. The existing
  nonempty-count contract needs no new victim validator, retry, eviction
  controller, helper allocation or alternate fallback scan.

Acceptance: extend the existing cache test with repeated full-capacity admission
and actual count/remnant checks. Reuse existing zero-limit, replacement, clear,
invalidation and malformed-input coverage rather than duplicating the parser
suite. Source review
must establish identical required execution with or without ZDEBUG; do not
introduce a second build configuration to verify this repair.

### R2: preserve H2 inline configuration without exposing narrowing setters

File: `zhttp/src/ZhttpConfig.hh`, with all necessary dependent migrations.

Rules: [Accessors](docs/GUIDELINES.md#accessors),
[Core language](docs/GUIDELINES.md#core-language),
[Bad casting](docs/GUIDELINES.md#bad-casting), and
[Static polymorphism and constraints](docs/GUIDELINES.md#static-polymorphism-and-constraints).

- Retain the canonical `tls()` accessor and `tls(lambda)` operation returning
  the outer H2Config as an rvalue, following ZiMxParams::scheduler().
- Prevent TLS setters from being callable directly on H2Config with a narrower
  return type. Removing the public using declarations alone is insufficient
  while TLSConfig remains a public base.
- Make TLS storage an implementation detail, using composition or private
  inheritance according to which requires less real change. Select one during
  source review; do not build both designs or introduce a new parameter layer.
  Delete the five public TLS using declarations. Keep only the canonical
  tls accessors/lambda boundary; migrate TLS reads and native arguments through
  `tls()`. Add no forwarding getters/setters or conversion operators.
- Audit initialization, copying/moving, metadata and dependent fluent
  expressions after changing the representation or inheritance boundary.

Acceptance: a small compile-time ZuAssert/type check establishes the outer type
of the supported fluent expression; inspect access control to exclude direct
narrowing setters. Do not add a general detection framework or a test per field.
Exercise mixed H2/TLS configuration in an existing dependent fixture, confirming
that both sets of values survive. Propagate intentional source incompatibility
through repository callers rather than preserving the defective interface.

## Phase 2: remove avoidable storage and allocation amplification

### R3: identified hash backing without unnecessary size-class amplification

Files: `zm/src/ZmLHash.hh`, `ZmHeap.cc`, related heap/hash tests and codec users.

Rules: [Heap allocation](docs/GUIDELINES.md#heap-allocation),
[Storage and capacity](docs/GUIDELINES.md#storage-and-capacity),
[Storage and lifetime](docs/GUIDELINES.md#storage-and-lifetime), and
[Framework fit](docs/GUIDELINES.md#framework-fit).

The current variable heap adds an alignment-sized prefix to a payload whose
size is already known by the table. A 4,096-byte payload plus 64 bytes selects
an 8,192-byte class. The change affects ordinary dynamic ZmLHash users beyond
HTTP; static inline tables and the bootstrap exception are distinct cases.

- Derive allocation and matching deallocation sizes from the table's actual
  slot count and element size. Carry the old allocation capacity correctly
  through resize and every destruction/free path.
- Delete the redundant allocation prefix by supplying the known size at free.
  Reuse ZmVHeap_'s existing cacheI/Cache and ZuSwitch dispatch machinery through
  the smallest canonical sized-allocation path. Do not copy its allocation
  algorithm into ZmLHash, create another allocator family/module, or redesign
  ordinary unsized variable allocations. Factor only genuinely shared code.
- Do not add a size map, allocation registry, per-table capacity mirror,
  extra header, custom free list or tuning knob when current slot metadata
  already determines the required allocation class.
- Preserve concrete heap identity, telemetry/tuning, alignment, allocation
  failure policy and allocator/deallocator pairing. Do not restore anonymous
  malloc storage or invent a generic allocation base for concrete objects.
- Preserve the heap-manager bootstrap escape from recursive manager locking.
  Audit specialization signatures and startup/destruction dependencies.
- Record payload versus allocated bytes at representative small, power-of-two,
  adjacent-boundary and jumbo sizes. Include retained cache/arena effects and
  any template/code-size cost of the replacement; do not assert an RSS or
  latency improvement from requested-byte arithmetic alone.

Acceptance: alignment, grow/rehash, lookup, resize/free pairing, recycling and
bootstrap regression coverage. Compare allocation classes deterministically
under the current configuration; require no unexplained doubling at the
demonstrated boundary and no loss of heap identification.

### R4: avoid charging H1 ordering storage to unrelated responses

Files: `zhttp/src/ZhttpCore.hh`, `ZhttpServerImpl.hh`, response allocation/binding
sites and response-stream tests.

Rules: [Containers and intrusion](docs/GUIDELINES.md#containers-and-intrusion),
[Data movement and initialization](docs/GUIDELINES.md#data-movement-and-initialization),
[Liveness](docs/GUIDELINES.md#liveness), and
[Repair procedure](docs/GUIDELINES.md#repair-procedure).

- Keep logarithmic H1 pending insertion; do not restore the unbounded sorted
  list. Keep unique sequence rejection and separate ready-queue membership.
- First establish a real concrete size/class saving. ResponseRecord is shared
  and type-erased before protocol binding; do not create protocol-template
  record families, factories or new dispatch APIs merely to relocate three
  words. Reuse existing concrete types and intrusions before adding any type.
- Investigate moving H1 tree intrusion to the concrete H1 record/node or
  another existing protocol-specific allocation. Alternatively, reuse storage
  only after proving that the relevant memberships and lifetimes are disjoint.
- Do not add a wrapper allocation per response, hidden contiguous copies or
  an owner reference to recover the saved words. If the current type-erased
  allocation contract cannot support specialization locally, document that
  constraint and compare the concrete alternatives before broadening the API.
- Measure actual sizeof/alignment and allocator classes for representative
  H1/H2/H3 response types. The three link words are 24 bytes on this target;
  the allocated-byte effect depends on the full concrete object.

Acceptance: reverse-order replies across a scheduler yield, duplicate rejection,
ordered transmission, cancellation, early disconnect and late ChannelRef
submission. Publish the resulting per-protocol size/class comparison. Retaining
the common links requires an explicit, justified cost disposition; it must not
be described as a footprint repair.

### R5: consolidate graph lifetime credits where the concrete lifetime permits

Files: `zum/src/zumd_daemon.*`, `zumd_server.*`, their concrete request/helper
families and relevant lifecycle tests.

Rules: [Scope and cleanup](docs/GUIDELINES.md#scope-and-cleanup),
[Heap allocation](docs/GUIDELINES.md#heap-allocation),
[Unnecessary atomic operations, locking and copies](docs/GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies),
[Lifetime](docs/GUIDELINES.md#lifetime), and [Callbacks](docs/GUIDELINES.md#callbacks).

- Identify the allocation that necessarily outlives each complete admitted
  graph, including descendants remaining after an early application result.
  Co-locate the credit with that allocation where the lifetime proof permits.
- First test whether existing owner/child drains already establish that
  lifetime. If they do, delete the extra credit object and redundant accounting
  rather than embedding an unnecessary credit somewhere else. Do not introduce
  a graph framework, generic Work base, second registry or public admission API.
- Do not embed the credit in a request node that can retire before its DB,
  saga or signing descendants. Preserve exactly-once credit return, concrete
  destruction and raw owner back-pointers protected by the full graph drain.
- Audit atomic admission and owner-return posts independently. Remove or
  coalesce work only when the owning-shard and stop/admission race proof
  supports it; do not trade a necessary handoff for a non-owner field access.
- Keep a separate identified concrete credit where sharing genuinely requires
  it and consolidation would add allocations, cycles or broader lifetime
  changes. State which operation families retain it and why.

Acceptance: held signing/DB/saga completions, application completion before
helper retirement, stop during active work, callback-driven finalization and
restart. Record allocations/ref operations/atomics/posts per admitted operation
before and after; benchmark only the affected path if a runtime speed claim is
needed. Do not use synthetic correctness tests as latency evidence.

## Phase 3: event-driven drainage and retained residency

### R6: delete shutdown polling and unnecessary drain machinery

File: `zrest/src/ZrestServer.hh`, with necessary SSE/producer interfaces.

Rules: [Algorithm and control flow](docs/GUIDELINES.md#algorithm-and-control-flow),
[I/O sharding](docs/GUIDELINES.md#io-sharding),
[Cross-Shard Handoffs](docs/GUIDELINES.md#cross-shard-handoffs),
[Teardown](docs/GUIDELINES.md#teardown), and
[Repair procedure](docs/GUIDELINES.md#repair-procedure).

- Polling is forbidden, with no retained exception. Delete the readiness
  self-repost and check affected shutdown callers for equivalent loops, delayed
  retries or readiness-only continuations. A bounded poll is still polling.
- First prove whether cancelAll_, HTTP::stop, the existing owner continuation
  and final Tx/Rx barriers already order all accepted work and SSE/session
  retirement. If so, delete the polling predicate and any accounting used only
  to support it; add no readiness arm, observer list or notification mechanism.
- If a real lifetime remains outside those barriers, keep only its necessary
  credit and re-evaluate completion from existing work/session/transport
  completion points. Notify the owner on the final SSE credit when required.
  No per-byte/chunk posts, new heap object, timer or generic drain coordinator.
- Prove the final-credit/completion-registration race cannot lose a wakeup.
  Keep owner lifetime valid until the final notification and barriers complete.
- Reuse canonical engine transition state; retain only bookkeeping for real
  transport/credit/barrier prerequisites. Do not introduce a second lifecycle
  state machine or call stop from final.
- Keep bounded cancellation continuations that perform actual work. Verify
  their budget counts inspected entries, including cancelled/nonmatching ones;
  each repost must have concrete remaining cancellation work. No continuation
  may repeatedly inspect unchanged readiness while awaiting an external event.

Acceptance: deterministically hold each prerequisite separately, confirm stop
remains pending, then release it and observe one completion after the final
barriers. If notifications are required, cover completion before/after their
registration and simultaneous final credits. If existing barriers suffice,
verify their ordering without inventing an arming protocol for the test.
Reuse existing failed-stop, repeated-stop and restart fixtures. Use
continuations and main-thread ZmBlock/ZmSemaphore; no sleeps or completion
polling.

### R7: resolve the inactive-expiry contract explicitly

Files: `zhttp/src/ZhttpDiscovery.hh`, `ZhttpAltSvc.cc`, cache lifetime owners.

Rules: [General principles](docs/GUIDELINES.md#general-principles),
[Storage and capacity](docs/GUIDELINES.md#storage-and-capacity),
[Timers](docs/GUIDELINES.md#timers), and
[Repair procedure](docs/GUIDELINES.md#repair-procedure).

- Separate logical expiry, admission bounds and physical residency. R1 fixes
  the bound; it does not reclaim entries merely because time passes.
- This policy item authorizes no expiry index, tree, timer, sweep, generation
  field or additional ownership scheme. Keep the existing cache operations
  unless an agreed residency requirement demonstrates a missing removal event.
- Establish which real owner events can reclaim entries deterministically:
  replacement, lookup, explicit invalidation, owner retirement and cache final.
  Do not add background scans, periodic GC or scan-on-admission as a shortcut.
- Immediate wall-clock reclamation during complete inactivity requires a
  triggering event; it cannot be promised while forbidding all such events.
  Record that contract conflict explicitly. If bounded inactive residency is
  retained, it remains a named policy exception, not full compliance with
  immediate deterministic reclamation. Do not silently disable useful caching
  or invent a user-approved exception.

Acceptance: expired alternatives are never returned, explicit owner events
release their owned values, admission remains bounded, and documentation states
the actual residency contract. If policy remains unresolved, mark this item
unresolved separately from the repaired release admission defect.

## Related cost checks within the changed paths

Preserve the separate H2 Rx/Tx native/id snapshots and ordered publication.
Their extra post/reference/storage cost repairs a shard violation; removing
them requires another valid ownership design, not a return to shared access.

While touching drain paths, qualify budgets precisely. H2 collection-array
growth can relocate multiple handles inside one logical batch, and a hash
iterator can scan empty buckets before returning one entry. A returned-entry
budget is not proof of a bound on all inspected slots or allocator latency.
Repair a demonstrated starvation path with owner-affine cursors/intrusive
enumeration or bounded storage where appropriate; otherwise retain the exact
limitation without claiming a hard per-turn latency bound. Do not mechanically
replace every hash or dynamic array in the repository.

## Implementation order and validation milestones

1. Complete the occurrence inventory and source/design review. Implement R1/R2
   together; batch the chosen R3/R4/R5 changes and R6 drainage repair before
   dependent rebuilding. Keep independent patches reviewable. Resolve or
   explicitly retain R7's policy status without disguising it as implemented.
2. At implementation completion, rebuild changed foundational libraries before
   dependents. Use one coherent top-level incremental `make -j8 all` where
   necessary to refresh the hierarchy. Require a warning-free build and build
   default all targets before the corresponding make test targets.
3. Run the affected heap/hash, Alt-Svc/configuration, response-ordering,
   REST/SSE and Zum graph-lifecycle tests against those fresh artifacts.
   Use ZuTest/ZuTestUtil, parse/log conventions and directory make test wiring.
   Keep isolated cases in test and complete-program/network cases in itest.
   Extend existing regressions for actual changed invariants; do not create
   per-field, per-wrapper or implementation-mirroring test matrices. Review
   shutdown source for polling as a mandatory gate, even when tests pass.
4. Check dependent source and examples for the H2 API migration. Re-run real
   DB-role/cluster gates only if implementation touches their activation or
   runtime contracts; preserve earlier evidence with its actual chronology.
5. Run the final Valgrind gate over changed allocation, response, endpoint and
   graph lifetimes. Preserve raw records and inspect selected leak kinds and
   errors. If using the existing opt-in OpenSSL conditional suppression,
   recheck its documented tool/library version and build ID; disclose matched
   contexts and limitations. Do not mask new errors or lost allocations.
6. Batch any resulting repairs before another validation pass. Repeat checks
   only for a concrete remaining risk or required gate. No build matrix or
   routine per-edit rebuild/test/Valgrind cycle is authorized by this plan.

## Closure

Append dated per-finding dispositions to findings.md and update the current
conclusion in zhttp/repair-review.md, preserving historical evidence. Record
files/symbols, invariant/lifetime proofs, dependent API changes, actual storage
and operation counts, tests run and unmeasured performance properties.

R1/R2 require concrete repair and regression coverage. R3-R5 require a repair
or a specifically justified cost disposition consistent with their contracts.
R6 requires complete removal of shutdown polling, by deleting redundant
machinery or using actual completion events with a race/lifetime proof. No
polling path may be retained as a cost disposition. Unresolved guideline
conflicts must remain visible; R7 requires its own policy status.
Do not equate an explained cost, green build or clean Memcheck record
with proof of minimal overhead or full guideline compliance. Review changed
lines for necessity, hard-tab alignment, 98-column source width and the
28-byte naming limit before claiming completion.
