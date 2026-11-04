# Zfix implementation audit — 2026-10-29

> Historical pre-repair audit. Names, layouts and passing results below describe
> its captured source, not the current breaking repair. See
> [zfix_repair.md](../zfix_repair.md) and the latest [STATUS.md](STATUS.md).


The subsequent [state audit](STATE_AUDIT.md) records further member reductions,
timer consolidation, cache-line isolation and current verification.

Scope: all `zfix_plan.md` implementation work, including all seven Zfix source
roles and the plan's Ztcp/Ztls integration hunks. Every guideline category and
flag in `docs/GUIDELINES.md` was reviewed. Existing dependency binaries are reused;
runtime verification is confined to Zfix on Linux x64. Zi is unchanged.

The 2026-10-28 benchmark report/archive describe their captured pre-audit source
and binaries. They remain historical evidence; they do not measure these repairs.
No new throughput or latency improvement is inferred from source changes alone.

## Guideline coverage

The review includes declarations and uses throughout `src`, `test`, `util`,
`itest`, `example`, `bench` and `interop`: 126 C++ files, plus their build inputs,
harnesses and documentation. Detailed inventories record individual containers,
allocations and retained exceptions; searching for spellings alone was not the
audit. The guideline categories were checked as follows:

| Category | Reviewed invariant or disposition |
| --- | --- |
| Scope, layout, dependencies, build and platforms | Plan-owned changes only; standalone examples and separate regression/integration tools; no compatibility shims. Linux x64 verification; no ARM/Windows runtime claim. |
| C++ language, metadata and style | CRTP/SFINAE/ADL, integers/enums/literals, direct dependencies, template-independent declarations, concise names, tabs, field order/alignment and narrow private access. |
| Matching, algorithms and control flow | Indexed matching, union switches, stable loop expressions/element reads, repeated blocks, nesting, hashes and enum conversions. |
| Storage, capacity and heaps | Concrete identified fixed/variable heaps, pooled I/O, bounds derived from workloads/protocol/schema, padding and owner cache lines, immediate disposal. |
| Containers and intrusion | One node when lifetimes coincide; shadow links for multiple memberships; independent operation state when immutable payloads are shared. Embedded hash lifetime exception explained below. |
| Movement and initialization | Direct output, borrowed spans, stack scratch, destination lifetime, binary/TLS ownership, initialization required for absent-state semantics. |
| Async state and synchronization | Continuation-time decisions, asynchronous `run`/`invoke`, no captured prerequisite-validity flags, completion barriers, logger capture lifetime, exact shared state versus ordinary telemetry. |
| Traversal and liveness | Iterator scope, traversal counts, redundant null tests/aliases, configurable batching/yields, finite teardown release, deterministic test waits. |
| Framework fit and casts | Existing Z matching/formatting/scanning/containers; only real ABI/void/base conversions retained. SDK and C ABI boundaries documented below. |
| I/O, ownership and teardown | Rx/Tx owner separation, fixed handoff metadata and retained handles, raw back-pointers, timer invalidation and FIFO drain, normal DNS/connect completion. |
| State, logging and counters | Real state distinctions rather than duplicate unset flags; owner-local primitive counters; synchronization retained for exact cross-thread admission/results. |
| Components, persistence, acceptance and debugging | No loadable Z factory/unload implementation introduced. Persistence remains application policy. Current-source TAP, boundary probes and sanitizers establish the recorded scope; historical performance data remains separate. |

## Repairs with concrete costs

| Finding | Repair and effect |
| --- | --- |
| Fixed frame writer compacts the entire encoded body | Return the authoritative output subspan for both fixed and growing storage. Removes one body-length `memmove`; buffer consumers propagate the returned offset into `ZiIOBuf::skip`. |
| Utility retention cache allocates FIFO and hash nodes separately | One intrusive hash node carries shadow FIFO links. One node allocation instead of two; FIFO traversal no longer hashes each key again. Ownership remains local and non-atomic. |
| Trace record separately allocates its node and I/O buffer | One pooled `Zi::IOBufAlloc` contains record data, list links and escaped backing, following `ZiRxQueue`. Removes one fixed allocation and the separate buffer reference owner. |
| Receive tracing repeats a validated frame checksum | Pass the existing successful `FrameView`. The raw-span tracing entry point still validates untrusted input. |
| Trace metadata linearly compares MsgType against every catalog entry | Reuse the catalog index and compile-time dispatch; merge only distinct same-MsgType schemas. Ambiguous binary layouts retain conservative redaction. |
| Selected catalog projection spills temporary positions to heap | Use bounded `ZtScratch` sized from the existing parse result and remaining parse budget. No second payload scan or payload copy. |
| Probe and SDK scalar formatting allocate transient strings | Stack-backed scratch/builtin containers with named fallback heaps. SDK-owned string copies remain where the foreign API retains data. |
| SDK group insertion deep-copies nested group trees | Transfer RAII-owned generated groups through the SDK's existing `addGroupPtr` contract. Explicit counts and exception ownership are preserved. |
| Repeated-division digit sizing, scalar delimiter loops, manual hex output | Use exact `Zu_ntoa` width, bounded `memchr`, shared token splitting and `ZuBox` formatting. No claim that every short-field case is faster without measurement. |
| Attachment release finds and deletes the same key separately | One framework `del(key, owner)` operation validates ownership and removes the node. |
| Owner-owned reconnect timers retain their link | Raw back-pointers with existing cancellation, serial guards and owner-shard teardown barriers. Removes timer reference churn/cycles. DNS/connect operation ownership remains independent. |
| Concrete objects allocate through a polymorphic fallback heap | Direct first-base `ZmNHeap<Concrete>` and ADL heap IDs; no generic allocator wrapper. |
| Actual Rx/Tx cache-line sharing | Separate complete owner groups. Layout probes show no size growth for managed links and 64 bytes for raw links. |
| TLS/trace jumbo accounting ignores allocation size classes | Use existing `jumboBytes` and rounded concrete allocator sizes. Fixes known-backing admission accounting without inventing opaque allocator introspection. |
| Repeated stable expressions, broad enum branches, nested private access | Cache loop invariants, use tagged-union switches and narrow owner operations. No extra state, queue, lock or cross-shard post. |
| Source-role naming and structural violations | Lowercase support/example/integration sources, concrete heap order, direct dependencies, field order, literal types, metadata bounds and local 98-column layout. |

## Container and lifetime disposition

- Static schema/tag/catalog maps are metadata-sized, immutable local `ZmLHash`
  indices constructed once. They have no per-message allocation. A dynamic
  singleton would add allocation/indirection without improving their lifetime.
- Parse positions represent nested group offsets and counts. The union scan and
  selected projection have different field order/strides; projection avoids a
  second wire parse. Retained application groups use their declared destination
  containers; parser scratch never escapes the decoding callback.
- Rx/Tx sequence queues remain the existing intrusive priority queues. Control
  and wire queues contain distinct operation state: one immutable body can be
  shared by multiple admissions/history/replays, so its operation links cannot
  be conflated. No duplicate gap map or extra receive queue was introduced.
- Attachment keys borrow immutable identity bytes and support heterogeneous
  links. The cold index is not a per-message business registry.
- Deferred work and I/O backing must survive callbacks and cross-shard delivery;
  pooled heap allocation is appropriate. Work/input retain payload handles, not
  populous references to long-lived owners. The teardown reference drains them.
- Embedded hash maps remain embedded: the framework's shadow registry skips
  zero-refcount maps. Introducing smart-pointer map owners solely to clear a
  syntactic flag would add unnecessary cold allocations.
- SDK decoded arenas acquire the lifetime required after SDK callback return.
  Required SDK `std::string`/container ownership is retained at that boundary.
- C-string destinations retain the foundation's `malloc`/`free` ABI. Replacing
  those destination allocations with an incompatible heap would be incorrect.
- Raw external frames expose capacity but not arbitrary concrete allocator
  overhead. Their existing charge remains a visible-capacity estimate; no fake
  default-allocator geometry or new public accounting contract was added.

## Validation retained intentionally

Early header inspection supplies session ordering before business processing.
Full dispatch separately enforces combined schema placement, recognized
field duplicates, selected values and trailer/signature semantics. It cannot be
replaced by the prefix result. Unknown fields remain skipped; no new FIX-specific
native receive limits, cancellation or resumption APIs were added.

Protocol checksums, binary length boundaries, overflow checks and public
formatter/parser failure contracts remain at untrusted boundaries. Repeated
checks were removed only where the same internal caller already establishes the
invariant. Test-only held-worker gates intentionally create the in-flight states
under test; they are not production blocking or polling.

## Verification

The current Clang 21 optimized/ZDEBUG/ASan Zfix build passes without warnings,
using unchanged dependency binaries. ASan leak detection and nonzero sanitizer
exit codes were enabled for runtime checks:

- Unit/schema gate: 30 programs, 309 scopes, including negative schema fixtures.
  The fixed-writer capacity test now also asserts that finishing preserves the
  body address; its rebuilt 11-scope program passes.
- Native integration: 14 programs, 225 scopes, TCP/TLS session, recovery, raw,
  reset, ingress, transport, demo and process checks. The trace rotation fixture
  now derives its two-record budget from an actual record charge; the original
  count, rotation and bound assertions remain.
- Independent codec, SDK load and dependency checks pass. The canonical process
  matrix passes all 76 cases across all four Native/QuickFIX pairings and TCP/TLS.
- Standalone client/server examples pass over TCP and TLS.
- Existing codec fuzzer completes 100,000 runs with no failure artifacts or
  sanitizer findings. Its existing 4,096-byte internal frame bound is unchanged.
- Eleven isolated fixed-writer boundary/large-field cases preserve body addresses
  and valid frame checksums. The same probe with the preserved old library
  produces a valid frame but fails the address invariant, as expected.
- Managed/raw layout and four maximum-width probe-scratch scopes pass, including
  stack-use-after-return detection for scratch lifetime. No sanitizer workaround
  or dependency change was required.
- Distribution registration covers all 126 current C++ files and this report.
  Their payloads match source; all C++ lines fit the local 98-column convention.
  The historical benchmark archive SHA256 remains
  `bb066944cf075af408fed5210c7199182b306e42bf7152adfb23092e9b4a8587`.

Detailed per-file inventories, before-source hashes, diffs and terminal logs are
retained under `/tmp/zfix-perf-audit/`: `root-coverage.md`, `codec-review.md`,
`session-transport-notes.md`, `support-coverage.md`, `support-inventory.json`,
`support-notes.md`, `unit.log`, `itest2.log`, `interop.log` and `canonical/`.
No new latency/throughput measurements, dependency-module tests, ARM or Windows
verification were performed for this audit.

## Performance path follow-up — 2026-10-29

The follow-up checks the current implementation after the member-state and
cache-line composition repairs. It covers integer conversion, selected catalog
dispatch, archive restoration, session recovery, queue ownership, workload
generation and validation. All 125 current C++ files and seven build inputs are
hashed for the coordinated build; no dependency source or binary was changed.

| Finding | Repair and concrete effect |
| --- | --- |
| Every integer width used a runtime-configured 128-bit accumulator | Specialize the existing checked scanner on compile-time width/signedness. Accumulate locally in unsigned, 64 or 128 bits, then publish the magnitude once on success. Full consumption, lexical restrictions and overflow checks remain. Assembly confirms narrow loops avoid wide multiplication/carry and per-digit output stores. |
| Identical-source discriminator alternatives construct and project a second position tree | Compile-time dispatch parses the actual common object/facet once and decodes the selected alternative from that tree. Heterogeneous or unmodeled-selector alternatives retain union projection. Canonical Report eliminates a second 512-byte root tree and its initialization/projection; this was usually stack scratch, so no scalar heap saving is claimed. |
| Archive restoration structurally parses the same body twice | Capture the root discriminator during the existing ordinary-schema parse and retain semantic checking. Original archive bytes are still copied once into their independent immutable owner. An unmodeled root selector also correctly terminates the final nested group, including when that group models the same tag. |
| Steady input reads the same clock twice and repeats an empty recovery transition | Use one realtime sample for receive time and its UTC projection. Ready, fully recovered input skips the later unchanged Session transition and its clock/event construction; probe disposition, gaps, synchronization and draining retain their full paths. |
| Queue peeks and immediate ownership transfers acquire unnecessary references | Use existing raw head peeks before callbacks in Wire/control queues, and move pending Tx and ordinary in-order Rx handles into their owning queues. High-key input retains its required independent handle. |
| Broker generates a quantity/price and immediately overwrites them | Add an explicit-value Model update and use retained response scalars directly. Cancellation uses scalar inputs and one initial cache lookup path; the post-insertion original lookup remains because insertion can evict its node. |
| Workload byte checks regenerate data already retained in Model | Compare complete incoming spans with existing expected spans using framework equality. Independent pattern/group oracles and all callback failure paths remain. No validation-result cache or new buffer is added. |
| Broker builds unused nested response groups; replacement/example templates deep-copy immutable groups | Build only the response model's used byte buffers and borrow existing group storage with the framework's array shadow operation. Synchronous example encoding and Model-before-template destruction order establish the borrowed lifetimes. |

The exact parse-budget tests now derive their bounds from the retained tree.
The same-source nested dispatch fixture succeeds at 416 bytes and fails at 415;
the former projection needed two such trees. The existing archive fixture now
uses its ordinary schema's three positions rather than the old four-position
synthetic selector schema. A one-byte-short budget still fails. Other focused
regressions cover 1/8/16/32/64/128-bit bounds and signed minima, selector scope and
binary data, steady recovery versus probe/gap/drain behavior, full cached byte
comparison and borrowed nested-group lifetime across follow-up updates.

Several apparent shortcuts were rejected after checking their actual contracts:

- Keep libc sample sorting. A proposed `ZuSort` replacement was reverted:
  4,096 values with one nonzero sample take 8,394,722 comparisons through its
  two-way partition versus 28,671 through libc. Equal output does not justify
  that regression; no foundation repair is needed for this scoped task.
- Keep SeqTx's preflight lookup. It caches synchronous retrieved history and
  lets a retrieval hook hold the sender before generic failed-work bookkeeping.
  Moving that operation inside generic send changes observable held-work state;
  avoiding the lookup would require additional state or a new hook restriction.
- Keep reserved-envelope selector validation. Its full parse checks aggregate
  parser budgets, structure, duplicates and regions, including externally supplied
  Raw frames. Header/trailer-only matching would weaken those guarantees.
- Keep early header admission and complete selected-message validation: they
  establish different session-ordering and schema guarantees. Keep immutable
  replay/output separation, mutable TLS records, foreign-callback arena ownership
  and the foundation's C-string allocation ABI.
- Do not replace hash deletion with `delNode` merely to claim a saved lookup:
  this implementation still hashes the key and walks the same bucket.

No new persistent state, container, lock, atomic, receive policy or generic queue
API was introduced. The accompanying state audit records the separate ownership
and packed cache-line layout repairs.

Current-source Clang 21 GNU C++2b/O3/ZDEBUG/ASan verification passes:

- Warning-free complete Zfix build; 30 unit programs / 320 scopes, with the
  positive and negative schema/send/reply compile fixtures.
- 14 native integration programs / 225 scopes over TCP/TLS; independent codec,
  five SDK load scopes and eight dependency/diagnostic cases.
- All 76 canonical Native/QuickFIX process cases and both standalone TCP/TLS
  example pairs, with every expected actor exit and empty example stderr.
- 100,000 existing codec fuzz runs without failure artifacts; the harness's
  internal 4,096-byte frame bound is unchanged.
- Both three-shape codec smoke runs agree on final frame sizes and decoded
  identity. Their concurrent timings are not a performance comparison.

The first full unit run exposed the obsolete four-position archive assertion;
after deriving the exact schema bound, the rebuilt test and all 30 programs pass.
Earlier compile failures and diagnostic experiments remain separate from passing
terminal evidence. Logs, before snapshots, assembly, source/dependency hashes and
the independently aggregated verdict are under `/tmp/zfix-path-audit/`.
All eight dependency DSOs and the published 2026-10-28 benchmark archive remain
byte-identical. No fresh latency result or additional platform pass is claimed.
