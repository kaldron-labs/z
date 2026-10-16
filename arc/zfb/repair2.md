# Repair plan: Zdb and its data stores

Implement the demonstrated repairs in [findings.md](findings.md) and the data-store
copying and type-coverage findings in [findings3.md](findings3.md), against the
current worktree.
Preserve existing uncommitted changes. HEAD `6e9a1252` is the comparison baseline.
For remaining long saga acceptance, the user approved a corrected canonical
baseline: `6e9a1252` plus only the current scheduler's `queueCount` ordering guard
in `tryPush_` (the exact patch is recorded in
`zdb/interop/evidence/repair2-20261010/control85/scheduler.patch`). Label these
results explicitly as the corrected baseline, retain canonical FlatBuffers and
the existing build configuration, and preserve unmodified-baseline failures as
separate evidence. Do not backport coordinator lifecycle changes or represent
corrected results as unmodified HEAD. This changes the comparison baseline for
that acceptance work; it does not change production scheduler behavior.
Preserve the native `ZiFB` design and contracts in [goal.md](goal.md); this document supplements
[plan.md](plan.md), whose recorded acceptance predates these findings.

This is a plan, not an execution record. No implementation, build, test or
benchmark is performed by writing it. Static phase acceptance below is distinct
from subsequent executable acceptance.

Zdb owns the persistence contract; its data stores are memory, sqlite3 and
postgres. The requirements below concern Zdb operations and their data-store
implementations.

This document consolidates the former data-store plan in `repair3.md`. Shared repairs
are implemented once and validated through memory, sqlite3 and postgres
consumers. Completion requires both the shared and data-store requirements below.
Preserve HEAD data-store behavior except for these explicit repairs; trace HEAD
when conversion, lifecycle or resource-policy details need clarification.
The codec, reflection, pooled I/O construction and codec interoperability
coverage are owned by `zi` as `ZiFB`.

## Scope and priorities

The problems being repaired are unnecessary copying and type-coverage gaps.
Preserve existing validation, verification boundaries, error handling,
diagnostics, callbacks, lifecycle and resource policies. Do not add setup/rebind
rejection, unrelated boundary checks or new error-completion
paths. This correction aligns with [repair3.md](repair3.md) and supersedes earlier
requirements for those changes, including historical question resolutions below.
The explicitly authorized exceptions are the narrow existing-table diagnostic fix
in §5.1.1 and enforcement of the existing Zdb buffer-size policy for data-store
parameters in §5.2. Preserve counting, mismatch detection and error completion
while recording the failing field identity separately. These exceptions qualify
the corresponding preservation rules in `repair3.md`.

| Finding | Disposition | Owning code |
| --- | --- | --- |
| postgres field resolution for supported columns | Complete supported wire conversions; retain the existing complete concrete OID mapping and error behavior | `zdb_pq/src/ZdbPQ.hh`, `zdb_pq/src/ZdbPQ.cc` |
| postgres schema failure diagnostic indexes by the column count | Retain failing field identity separately; preserve counting and error behavior (§5.1.1) | `zdb_pq/src/ZdbPQ.cc` |
| Data-store parameter sizing must obey the existing Zdb buffer cap | Bound counts and lengths before arithmetic, allocation and encoding (§5.2); apply equivalent sqlite3 checks | postgres and sqlite3 parameter planning/binding |
| sqlite3/postgres CStringVec/Float32 bindings | Complete supported column conversions, preserving HEAD data-store representations | postgres and sqlite3 binding/result paths |
| Owning bitmap decoder result copied into Tuple | Repair first in memory-store ownership work | `zdb/src/ZdbMemStore.hh` |
| Missing `CStringVec` and Float32 bindings | Repair both read and reverse-write paths | `zdb/src/ZdbMemStore.hh` |
| Composite UDTs and UDT arrays proposed as store Values | Remove the proposed extension; retain the HEAD scalar/vector column contract and explicit Bitmap/IP representations (RQ2 corrected) | Zdb store bindings and fixtures |
| Missing conversions leave unresolved field representations | Implement metadata-driven conversion coverage; preserve existing error handling | `zdb/src/ZdbMemStore.hh`, `ZdbMemStore.cc` |
| Blanket 32-byte alignment | Separate supported maximum from actual schema alignment | `zi/src/ZiFB.hh`, `zi/src/ZiFBReflect.hh`, Zdb builder callers |
| Lvalue keys copied again when entering an index | Move at proven last-use sites using existing container APIs | `zdb/src/ZdbMemStore.cc` |
| Metadata defaults differ from old schemas | Preserve the resolved `Deflt` contract; correct comparisons | Existing fixtures/benchmark metadata |
| Scheduler overflow ordering check | Retain ordering; no speculative branch removal | `zm/src/ZmScheduler.cc` |
| Delete secondary-key serialization | Repair core Zdb to select primary-key fields (RQ1 resolved) | `zdb/src/Zdb.hh`, delete consumers |
| Whole-vector updates | Retain current operation contract | `ZfFieldFilter`, table/store consumers |
| Transmit output, saga payload staging, cold-find and recovery materialization | Build directly in transmit buffers where supported; retain justified destination ownership copies (RQ3 resolved) | Saga and store selection paths |
| Existing temporary update/delete `MemRow`, mock delegates, index key snapshots | No redesign without a demonstrated smaller correct replacement | Memory store and mock wrapper |

Recursive UDT decomposition through nested `ZfStruct` metadata belongs to format
serialization, including `ZiFB`; it does not establish a data-store column type.
Zdb store fields have one-to-one cardinality with columns. Each field must use a
supported scalar code or scalar-vector `...Vec` code, with the existing explicit
Bitmap/IP store representations retained. Composite UDTs and arrays of composite
UDTs are not supported column values. Do not add composite Values, flatten a
field into multiple columns, create composite postgres types or use opaque Bytes
as a substitute. Saved saga definitions remain explicitly declared Bytes and
may contain recursive codec values inside that payload. No new storage feature,
validation policy, allocation layer or compatibility shim is required.
Canonical FlatBuffers interoperability remains mandatory.

## Engineering rules

Apply [GUIDELINES.md](GUIDELINES.md) to every touched declaration and caller,
including tests, integration programs, benchmarks and interop fixtures. In
particular:

- [Repair procedure](GUIDELINES.md#repair-procedure): identify the invariant and
  its owner; make the smallest demonstrated repair. An unchanged cost is not
  evidence of a migration regression.
- [Data movement](GUIDELINES.md#data-movement-and-initialization): construct in
  uninitialized destination storage; eliminate owning temporaries and copies at
  last use. Do not default-initialize storage immediately before overwriting it.
- [Storage](GUIDELINES.md#storage-and-capacity),
  [heap allocation](GUIDELINES.md#heap-allocation), and
  [storage/lifetime](GUIDELINES.md#storage-and-lifetime): use existing tunable
  Z heaps, pooled `ZiIOBuf`, and workload-sized `ZtScratch` for temporary arrays.
  Preserve current heap IDs, default capacities, growth and ownership transfer.
  Do not add allocation-null checks handled by `ZmHeapFail`.
- [Framework fit](GUIDELINES.md#framework-fit),
  [type mechanics](GUIDELINES.md#type-mechanics), and
  [compile-time matching](GUIDELINES.md#compile-time-data-and-matching): use Z
  containers, `ZuMv`, traits, `ZuSwitch`, `ZuMatcher` and existing stream/error
  formatting. No private numeric parser/formatter, redundant span casts, STL
  replacement containers or linear name matching.
- [Static polymorphism](GUIDELINES.md#static-polymorphism-and-constraints): use
  templates and SFINAE for wire variants; keep schema generation specialized at
  compile time. Runtime BFBS consumers may dispatch cached wire kinds; buffer
  capacity/error/streaming branches remain permitted.
- [I/O sharding](GUIDELINES.md#io-sharding) and
  [cross-shard handoffs](GUIDELINES.md#cross-shard-handoffs): preserve owner-thread
  access, asynchronous completion and buffer-handle handoffs. Do not capture
  large payloads, add locks, remove required posts, or treat `run` as blocking.
- [Code layout](GUIDELINES.md#code-style) and
  [naming](GUIDELINES.md#naming): hard tabs, two-column logical indentation,
  98-column lines, vertically aligned `ZfStruct` typecodes, concise names under
  the authoritative 28-byte limit. The adapter and I/O-layer integration live
  in `zi`, in `ZiFB.hh`/`ZiFB.cc` and `ZiFBReflect.hh`/`ZiFBReflect.cc`.

## Phase 1 — Repair Tuple ownership and binding

Batch the related `ZdbMemStore` changes before rebuilding.

### 1.1 Construct bitmap ownership once

In `loadValue`, directly placement-construct the non-vector result from
`ZiFB::valueAt<W>(p)`, or move its named result with `ZuMv`. Prefer direct
construction if it removes the temporary without duplicating decoding logic.
Keep ordinary vector iteration over borrowed elements unchanged.

Audit the same constructor pattern for all mapped results. Do not add a
bitmap-only allocator, container, special copy routine or decoder. Cover insert,
mutable update, primary-key delete, lookup/select keys and saga target operations,
which share this loader.

**Static acceptance:** the nonempty bitmap reaches its final Tuple storage with
one owning word-array allocation and no subsequent deep copy. Empty bitmap,
missing-field defaults, IP, strings and vector construction retain their existing
semantics. Tuple-to-`MemRow` and update assignment still move ownership.

### 1.2 Normalize equivalent field representations

- Bind both `CStringVec` and `StringVec` to the existing owning `StringVec` Value,
  just as scalar `CString` already binds to owning `String`. Keep each decoded
  element a borrowed span until its destination string is constructed.
- Bind Float32 and Vector/Float32 to the existing double/`FloatVec` owning
  storage. Do not add another family of owning containers solely for wire width.
- Preserve the schema's wire width independently of the owning Value type.
  Current `WireValue<I>` derives Double from double storage; merely accepting
  Float in `xField` would then read eight bytes from a four-byte field and is
  incorrect. Apply float-width handling to scalar reads/defaults, vector stride,
  scalar writes/default omission, and vector writes.
- Use the existing cached `XField` and BFBS field metadata to select Float32 or
  Float64 once per field operation. Factor typed wire-width operations with
  templates, outside element loops. Add cached metadata only where existing
  metadata cannot express the choice; no second schema lookup or per-element
  wire-kind branch.
- Narrow double storage back to Float32 when emitting a field whose schema says
  Float32. Retain canonical floating-point/default behavior, including signed
  zero, infinities and NaNs; use existing numeric primitives.

Trace reverse construction through `saveOffset`, `saveValue`, `saveTuple`,
`saveRow`, key projections, find/select results and recovery envelopes. All must
write the width described by BFBS, regardless of internal storage normalization.

**Static acceptance:** `CStringVec` binds without another owning representation;
Float32 and Float64 scalar/vector paths use their correct wire widths in both
directions. Missing defaults come from the same BFBS/native contract as existing
fields. Nonempty vectors allocate their final outer array once, with only the
required owning allocations for long string/blob elements.

### 1.3 Preserve the store column contract (RQ2 corrected)

The original memory, sqlite3 and postgres implementations at HEAD bind one field
to one supported column value. Their owning/parameter Values contain supported
scalars and scalar vectors; they do not contain arbitrary composite UDTs. Bitmap
and IP have explicit existing store representations. The native codec's recursive
UDT support does not extend the stores' accepted column types.

A field declared `Time` must be convertible to and from `ZuTime` through ordinary
C++ conversions. Use those conversions and the existing Time wire handling; no
application transformer or chained representation mapping is needed. Apply the
same declared-code contract to other supported column representations.

The following metadata describes a recursive codec object:

```cpp
struct MappedTime { ZuTime v; };
ZfStruct(, MappedTime, (v,, Time));
struct Foo { MappedTime t; };
ZfStruct(, Foo, (t,, UDT));
```

An array of metadata-defined UDTs is itself declared `UDT`, not a scalar-vector
`...Vec` code. That is valid recursive codec metadata but does not make the array
a supported store column. Preserve codec recursion without adding store support
for composite UDTs or their arrays.

Remove the repair's generic composite Value/Atom/structural-count storage,
object-binding graph, recursive store load/save/comparison paths, associated
metadata plumbing and composite-store acceptance fixtures. Remove the attempted
`ValueBase` mapping composition and application-to-Time transformer fixture.
Keep Float32/CStringVec repairs, supported-column defaults and wire widths,
explicit Bitmap/IP paths, actual schema alignment and ownership improvements.
Preserve existing schema acceptance, errors, callbacks and reopen behavior;
remove validation introduced solely for the discarded composite machinery.

**Static acceptance:** one field remains one column; active supported fields and
operation-projected void Values retain their established semantics. There is no
new composite-column representation or chained mapping mechanism. Reopen keeps
supported column metadata and wire views valid. Native types declared with a
scalar code use the required ordinary C++ conversions.

### 1.4 Serialize deletion using primary-key fields (RQ1 resolved)

Treat the use of `ZfFieldFilter::Del` in core Zdb's `rowSaveDel` as the defect.
Select key 0 fields using the existing framework key-field machinery; do not
change the meaning of `ZfFieldFilter::Del` globally or introduce another
independently maintained primary-key predicate.

The current `saveObject` interface takes a field filter. Its existing equivalent
of selecting `...KeyFields<0>` is
`ZuStructKeyFilter<0>::template Filter`. Apply that selection to the original
row metadata so original row wire IDs, defaults and helper representations are
preserved. Do not extract an owning key Tuple or serialize a compact key-table
layout with renumbered slots: deletion consumers read this payload using the
row schema. No new serializer API is needed solely for this selection.

postgres and sqlite3 already construct delete predicates and decode parameters
from key 0 only; the memory store does likewise. `findDel<KeyID>` first resolves
the specified lookup key to a row, then calls `del(row, ...)`; a secondary-key
lookup does not require secondary fields in the eventual delete payload.

Audit replication/recovery and serialized-buffer cache indexing for assumptions
that deleted payloads contain secondary keys. Propagate the primary-key-only
contract through current consumers, without compatibility shims or
store-specific wire formats. Preserve removal/invalidation of every secondary
index entry using the existing row/cache state where required; do not read an
omitted secondary field as its default and treat it as the deleted key.

**Static acceptance:** delete serialization reads and emits only active key 0
fields at their original row slots. Secondary-only strings, vectors and bitmaps
are not accessed or built. Direct delete, secondary-key `findDel`, saga delete,
replay/recovery and cache invalidation retain correct primary/secondary index
behavior. All three stores continue to bind the same primary-key fields.

## Phase 2 — Use actual schema alignment

A global replacement of 32 with 8 is incorrect for forced-alignment fields.
Separate the supported alignment ceiling from the alignment actually required by
each buffer.

1. Derive the typed row requirement from `ZiFB::BufferAlign<T>` and combine it
   with the outer Record/message/header requirements. Retain one metadata-derived
   alignment value for type-erased table operations; derive it during table
   setup, not by scanning fields on each write.
2. Permit the pooled builder/storage to use that actual alignment, while retaining
   support up to `Wire::MaxAlign`. Prefer the existing builder alignment argument
   of `ZiFB::IOBuilder`/`IOStorage`; preserve their existing runtime-alignment
   support and direct transmit-buffer construction.
3. Apply actual alignment consistently to storage attachment/capacity rounding,
   arena alignment, growth, nested starts/finishes, buffer reuse and detachment.
   Current `IOStorage` rounds usable capacity using its runtime `m_align`,
   independently of the template ceiling. Preserve this behavior, front scratch,
   reserved tail room and the downward-written tail across reallocations.
4. For reflected store output, derive/cache required row and key alignment during
   BFBS binding, including inline/nested and forced vector requirements. Use it
   in `saveRow`, find/select and recovery builders. Recompute on schema rebind.
5. Audit other blanket-maximum callers. Typed standalone saga definitions already
   use their `BufferAlign`; retain that behavior. Do not change raw-span copying
   to reapply object omission/default rules.

Include data-store result-builder constructors for key projections, full rows, nested
Record envelopes and saga/recovery results. Cache data-store runtime schema requirements
at setup, including reachable nested values and enclosing message alignment.
Preserve existing `ZiFB::BufferAlign<...>::N` callers and pooled base/skip
alignment. Validate extracted nested rows and growth with 16/32-byte fixtures;
measure wire size, usable capacity, padding and pool growth around real capacity
boundaries. Report timing separately from layout results.

Use the existing alignment metadata machinery. If a transitive requirement is
missing, fix its owning trait/reflection calculation rather than hard-coding a
second schema traversal. Preserve actual allocation base alignment as well as
relative wire offsets, including nested buffers and framed payloads.

**Static acceptance:** an eight-byte schema no longer loses capacity or gains
padding solely from the supported 32-byte ceiling. Sixteen/thirty-two-byte forced
alignment remains correct through growth, nesting and framing. All original
pool tuning and heap IDs remain unchanged. No extra row buffer or payload copy
is introduced. Padding reduction is a layout result; its latency benefit remains
unmeasured until release acceptance.

## Phase 3 — Remove proven last-use key copies

Inspect insert and secondary-index update sites where an owning key Tuple is
built and then passed as an lvalue into a retaining index node. Use the existing
container's move-capable API with `ZuMv(key)` when the key has no subsequent use.
Check the actual selected overload and node construction; a move at the call
site is ineffective if the callee still copies.

Retain independent ownership for separate index entries and for snapshots needed
across mutable-row updates. Do not borrow keys from row storage that can change
or be destroyed. Do not move keys needed by a missing-row diagnostic, comparison,
or later deletion. Factor only demonstrably repeated logic; no new index type or
heterogeneous key-view abstraction is necessary for this repair.

**Static acceptance:** each repaired index insertion transfers its temporary key
storage rather than deep-copying it, without use-after-move or changes to ordering,
descending/grouped keys, lookup, updates, deletion and index lifetimes. Any site
whose existing API cannot safely transfer ownership remains documented rather
than triggering a container redesign.

### 3.1 Preserve direct transmit-buffer construction and remove relay copies (RQ3)

Follow the current implementation in `zi/src/ZiFB.hh` and
`zi/src/ZiTxStream.hh`. `ZiFB::save(S &, ...)` selects its destination path at
compile time using the existing `ZiIsTxStream<S>` ADL trait. Preserve both paths
and audit callers against the path they actually select.

#### Transmit-buffer destination

For a `ZiTxStream` destination, `save` obtains detached output storage through
`stream.allocBuf(alignedHeadRoom)`. `IOBuilder` constructs the object backwards
directly in that `ZiIOBuf`, preserving required alignment and reserved tail room.
`builder.buf()` detaches the owner, and `stream << builder.buf() << Zi::Flush{}`
uses the buffer-owning `operator <<(ZmRef<ZiIOBuf>)`. That insertion transfers the
buffer reference rather than copying the completed bytes into another buffer.
`ZiTxLayer` likewise passes the owner to its lower stream after preparation.

```text
object → backward construction in ZiIOBuf → buffer-owner handoff → transmit
```

`ZiIOBuf` storage grows through `IOStorage::grow` and its existing `realloc`
mechanism. Initial allocated capacity does not bound the object size or determine
whether a completed-payload copy is needed. Growth preserves front construction
scratch and the end-relative output; count any relocation cost separately from a
copy into a second destination buffer. `IOStorage::detach` can also compact the
finished bytes within the same buffer when construction scratch forced capacity
beyond the usable output limit. Preserve and account for this case.

The current implementation separately consults `ZiTxStream::maxSize()`, the
configured per-emitted-buffer limit including header/trailer reservations.
It is not the initial allocation size or a prohibition on `ZiIOBuf` growth.
When the completed bytes exceed the aligned usable limit calculated from
`maxSize()`, aligned head room and tail room, `save` inserts the completed span
and flushes; ordinary stream splitting then copies into emitted buffers.
Document and measure this fallback separately from buffer-owner handoff. Do not
introduce another limit or change the stream's existing splitting policy.

Audit transmit callers for scratch/string/array staging followed by span insertion
where the existing buffer-owning route is applicable. Preserve the concrete
transmit-stream type or its tagged base through wrappers so `ZiIsTxStream` still
selects direct construction. A span insertion copies bytes; an owning buffer
insertion transfers the owner. Do not substitute one for the other when reporting
elided copying. Preserve output ordering when a stream already holds a prefix,
layer preparation, flush behavior, shard ownership and asynchronous lifetimes.

#### Retained payload and generic destinations

Current `MSaga::save` already clears the retained `SagaPayload` and calls
`ZiFB::save(data, def)`. `SagaPayload` is a builtin/array destination, not a
transmit stream. This path uses `ArrayStorage` over `ZtScratch` with heap fallback
and inserts the completed span once into retained payload storage.

```text
saga definition → backward scratch construction → one copy into SagaPayload
```

Preserve replace semantics, builtin/heap tuning, heap identity and codec-error
propagation. Return the codec error from saga save and report failed submission
through the existing callback; do not introduce an exception for serialization
failure. Retain existing catches for application/allocation exceptions.
Do not reintroduce a temporary pooled builder, intermediate string
or byte array, or a private growth/copy algorithm. This retained-payload boundary
has different ownership requirements from transmit-buffer handoff. Encoding the
retained Bytes into SagaData and decoding into owning data-store values are
separate representation/ownership boundaries; justify their copies individually.

Audit saga submission/recovery and cold find/select for additional relay copies.
Use existing owner transfers, moves or direct destination construction where
applicable. Preserve external verification, saved-definition verification during
recovery, constructor semantics and cross-shard buffer-handle handoffs. Borrowed
scratch or builder views must not escape their owner's lifetime.

#### Acceptance

Use existing `ZiFBIOTest` transmit fixtures and extend only missing coverage:

- Prove buffer identity from detached allocation to send on direct handoff,
  including the tagged base-stream type and growth beyond initial capacity.
- Cover scratch-induced growth and detach compaction when the completed output
  remains within the configured emitted-buffer limit. Account for relocations
  without describing them as a copy into a second destination.
- Cover the actual splitting fallback, prefix ordering, layered owner handoff,
  head/tail reservations, size prefixes, identifiers and forced alignment.
- Codec construction failure emits no object bytes and returns its codec error.
  Allocation admission/send failure remains sticky in the destination stream;
  `Error::OK` from `save` alone does not prove destination success. Check both.
- For generic destinations and `SagaPayload`, prove one completed-span insertion,
  correct reuse and builtin/heap transitions, including large payloads.

**Static acceptance:** eligible transmit output constructs in its destination
`ZiIOBuf` and transfers ownership without a separate completed-payload copy.
Growth, compaction, splitting and retained-payload copies are identified by their
actual paths and boundaries. No unjustified intermediate owning storage remains.
Executable and performance acceptance must establish the exercised paths,
allocation counts and copied bytes; source inspection alone is not a timing result.

#### Direct serialized reads

Review the saga read paths as well as construction. Compare canonical HEAD
accessors with `ZiFB` borrowed views, including replication-cache key access,
envelope metadata, store parameter binding and recovery. Read fields directly
from the retained FlatBuffer wherever their lifetime permits; do not load an
owning C++ object merely to inspect its fields. Identify each materialization
that remains and the ownership or mutation boundary requiring it. Compare with
HEAD before attributing an existing copy to the migration.

Measure borrowed reads separately from an explicitly labeled owning-consumer
control over identical serialized input. Keep the input owner alive, consume
equivalent fields and exclude construction and verification from read timing.
Report allocation-counter coverage: Z heap telemetry excludes `ZmVHeap` jumbo
allocations, and observer allocations must not be attributed to the reader.
These controls supplement the matched complete-saga workload; they cannot
establish the cause or acceptance of its latency regression on their own.

## Phase 4 — Complete the operation/type review

Review both declarations and consumers using the matrix below. Reuse the existing
consolidated FlatBuffers fixtures and Zdb fixtures when executable acceptance is
requested; do not create another collection of individual `ZiFB...` programs.

| Coverage | Required invariant |
| --- | --- |
| All integer widths, Bool, Float32/64 | Correct width, sign/endian conversion and defaults; no owning temporary |
| Int128/UInt128, Fixed, Decimal, Time, DateTime | Inline conversion and absence defaults without heap intermediates |
| CString/String/Bytes | Correct borrowing on read and ownership at persistence boundaries |
| CStringVec/StringVec/BytesVec | Empty/nonempty and short/long elements; one outer allocation; required child ownership only |
| Every signed/unsigned integer Vec width | Correct element width/stride and one destination allocation |
| FloatVec, FixedVec, DecimalVec, TimeVec, DateTimeVec | Correct element mapping, wire width and reverse encoding |
| Explicit Bitmap/IP store representations | Correct existing column conversions and ownership; no generic composite UDT or UDT-array column support |
| Constructor fields, Init, Skip, Required, Deflt | Correct construction/filtering; no skipped getters/setters or duplicate default logic |
| Direct insert/update/delete | Correct operation field selection, Tuple moves, keys and version/sequence handling |
| Cold findUpd/findDel and select/recovery | Correct rebuilt payloads and lifetime; no stale binding or width mismatch |
| Saga submission, forward steps, compensation, replay, cleanup | Same intent ordering, idempotence, callbacks and shard ownership |
| Transmit output and saga save/restart/recovery, large payloads | Direct ZiIOBuf construction and owner handoff where applicable; generic retained-payload copy, growth/compaction/splitting accounted separately; correct verification and lifetime |
| Canonical payload/BFBS interchange | Both producer directions, Float32 vectors, defaults and forced alignment |

Keep external-input verification at its boundary and saved saga definition
verification during recovery. Do not add verification for buffers just generated
and consumed in the same process. Preserve the compiled matcher and cached BFBS
bindings; no textual formatting/parsing enters Tuple decode.

**Static acceptance:** every supported scalar/vector column representation has
an explicit binding, with active/filter membership respected. Composite UDTs and
their arrays remain outside the store column contract.
Existing setup validation and error handling remain unchanged. Every
repair is traced through both C++ → wire → Tuple and Tuple → wire → C++ paths,
including updates, deletion keys and saga bookkeeping. Re-read the final diff
against every applicable guideline/audit-flag family; record unresolved concerns
without presenting them as measured regressions.

## Phase 5 — Repair sqlite3 and postgres data stores

Owners are `zdb_pq` and `zdb_sqlite`. Complete supported column conversions
while eliminating unnecessary intermediate copies.
Batch source edits and builds under the common acceptance policy. The shared
alignment and copy repairs in Phases 2 and 3 apply to both stores.

### 5.1 Complete field resolution; preserve open/error behavior

`OIDs::name()` is a static mapping from postgres `Value` discriminators to type
names. It covers every concrete `Value` alternative. It does not take a
`ZfFieldTC` directly, and its null return is not evidence that a numeric postgres
OID disappeared.

The existing resolution chain is:

```text
ZfFieldTC + wire metadata → xField() → Value discriminator → postgres type name
```

The original `xField()` leaves its discriminator at zero (`void`) when a field
representation is unhandled. Vector/String accepts `StringVec` but omits
`CStringVec`; scalar/vector Float32 lacks cases while Double is handled.
`OIDs::name(0)` subsequently returns null. Complete these conversions into the
existing column representations. Do not replace
missing coverage with added rejection or change the existing missing-name branch.

Historical source tracing found the log-and-`Unsent` branch already present in
repository root `d2df7e7f` (2025-12-20). Later changes switched logging in
`73eb5d99`, added namespace handling in `5b9d4ed0`, and reused the resolved name
in `6637bfa5`; the branch's completion behavior remained unchanged. This is
source-history evidence, not a reproduced runtime failure.

#### 5.1.1 Narrow repair to the existing-table failure diagnostic

The column counter is intentional. Preserve its existing increments and the
completion predicate:

```cpp
!m_openState.failed() && m_openState.field() >= m_xFields.length()
```

The defect is confined to the failure diagnostic, which uses that count to select
`m_xFields[i]`. Column recognition increments the count before OID matching; a
mismatch on the final recognized application column leaves the count equal to
the field total and sets failure. The success branch is correctly skipped, but
the diagnostic then indexes past the array. Catalog order can also make an
in-range count identify the wrong field.

An OID mismatch is possible with fully resolved field types: an existing column
can retain `int4` while current metadata expects `int8`, for example. The existing
comparison checks the column's catalog OID against the store's resolved type
mapping. Preserve that comparison and its acceptance policy.

Owners: `StoreTbl::mkTable_rcvd` and its existing open-state storage in
`zdb_pq/src/ZdbPQ.cc` / `ZdbPQ.hh`.

1. Retain one separate failing-field index, initialized to the existing unsigned
   null sentinel for each fresh table-schema check. Keep the column count intact.
2. When the existing OID comparison fails for a recognized application field,
   retain the already resolved `field` index from `m_fieldMap`. Do not derive it
   from the count, change increment ordering or add another metadata lookup.
3. At the existing end-of-results failure branch, use that retained index for the
   field-specific diagnostic. If there is no application field identity, emit a
   table-level diagnostic with the existing failure/count information and omit
   the field dereference. This covers reserved/unknown columns, missing columns
   and malformed results without inventing a field index or adding validation.
4. Keep the existing severity, failure flag, result drainage and `open_failed`
   completion point. Do not add setup rejection, change schema acceptance,
   introduce an early callback or redesign the open state machine.

Acceptance: existing successful opens retain the same result. Exercise a
one-field mismatch, a mismatch after all application names are recognized and
shuffled catalog order; each field-specific diagnostic identifies the actual
failing field. Existing failures without an application field produce a general
diagnostic without indexing `m_xFields`. Callback completion and lifecycle remain
unchanged. Reuse existing fixtures and batch executable checks at a major milestone.

### 5.2 Enforce the existing parameter buffer-size policy

The existing `Zdb_::MaxBufSize` in `zdb/src/ZdbBuf.hh` is `100 << 20`
(100 MiB). The user has confirmed that this policy applies to postgres parameter
buffers. Enforce the same policy in sqlite3 parameter buffers. Reuse this constant;
do not introduce another configurable limit or change the resource policy.
A source-buffer size bound alone does not bound an expanded store representation.
The earlier multi-gigabyte Int8 example demonstrates unsigned arithmetic wrap,
but does not establish that such a vector can reach the encoder under existing
Zdb limits.

Owners: postgres `vecSize`, `vecVarSize`, `VarAlloc` and parameter submission;
sqlite3 `vecSize`, `encodedSize`, `planInput` and `bindInput`.

1. For postgres fixed-width arrays, derive a maximum count before multiplying:

   ```cpp
   maxCount = (limit - sizeof(VecHdr)) / (sizeof(VecElem) + elementSize);
   ```

   Here `limit` is `Zdb_::MaxBufSize`, further restricted by any smaller existing
   backend/API limit. Check header fit before subtraction, then reject a count
   above `maxCount` before calculating the encoded size, allocating scratch or
   writing elements. Use the actual store element width, including conversions.
   With a 20-byte header, four-byte element prefixes and one-byte Int8 values,
   the 100 MiB cap permits at most 20,971,516 elements.
2. For variable-width arrays, bound the count by the minimum per-element overhead
   first. Maintain a remaining byte budget; check each prefix and payload against
   that budget before adding their sizes. Apply equivalent pre-arithmetic bounds
   to bitmap and other converted representations. Check borrowed scalar strings
   and bytes as well as converted parameters before submission.
3. Bound aggregate conversion scratch by `Zdb_::MaxBufSize`, checking each planned
   part against the remaining budget before adding it or recording narrowed
   offsets/lengths. Enforce each parameter's applicable backend/API limit before
   narrowing. Perform all checks before allocation and encoding, so backend
   rejection cannot follow an overflowing allocation calculation or write.
4. sqlite3 inspection found `vecSize`/`encodedSize` use 64-bit sizing;
   `planInput` checks each converted value against the cached `SQLITE_LIMIT_LENGTH`
   and `UINT_MAX`, and aggregate scratch against `UINT_MAX`. These checks do not
   enforce the 100 MiB Zdb cap. Use the smaller of the Zdb cap and the connection
   length limit for each parameter, and the Zdb cap for aggregate scratch. For
   fixed-width vectors, check `count <= (limit - sizeof(uint32_t)) / width`
   before sizing. For StringVec/BytesVec, use the four-byte count header and
   four-byte per-element length prefixes with the remaining-budget method.
   Borrowed String/Bytes currently return zero from `encodedSize` and are bound
   directly in `bindInput`; check their actual lengths too. Retain existing
   sqlite3 limit checks and `SQLITE_TOOBIG` failure handling.
5. Preserve existing error reporting and completion paths in both stores; route
   these size-policy failures through those paths. This codebase is generally
   nothrow: do not add exceptions where the old path did not throw. Postgres sizing
   must report failure through return values/sentinels and existing operation
   failure handlers, without new throws or catches. sqlite3's existing planner
   already used `SQLITE_TOOBIG` for size rejection. Report size-policy failure
   without throwing on the hot path; construct the local diagnostic directly
   instead of treating parameter encoding failure as a connection error. Retain scratch tuning,
   borrowed scalar/string/blob inputs and direct conversion writes into aggregate
   scratch. Do not add intermediate owning native containers or redesign parameter
   planning, schema validation or lifecycle behavior.

Acceptance: cover the derived maximum count and one above it for each distinct
fixed-width encoding, variable-width prefix/payload budget exhaustion, borrowed
String/Bytes and aggregate scratch exhaustion. Include sqlite3 connection limits
below the Zdb cap. Exercise arithmetic/planning with synthetic counts or lengths
where possible; do not allocate multi-gigabyte fixtures. Batch executable checks
at a major milestone under the retained build configuration.

### 5.3 Complete equivalent data-store field bindings

Owners: postgres `xField`, `loadValue`, `saveValue` / `saveOffset`; sqlite3
`resolveType`, input planning/encoding/binding and result tuple serialization.

1. Accept `CStringVec` wherever the wire field is Vector/String, using the
   existing data-store StringVec representation and borrowed element spans. Retain
   the distinction between Bytes, UInt8Vec and BytesVec.
2. Accept scalar Float and Vector/Float in addition to Double at the `ZiFB`
   wire boundary. Preserve HEAD's `ZdbPQ` and `ZdbSL` handling of data-store floating
   values, including storage types, accepted OIDs, vector/blob formats and
   conversion behavior (SQLQ2 resolved). Trace the HEAD paths before adapting
   the wire readers/writers; do not redesign or tighten data-store handling.
   Read Float32 wire payloads at four-byte width and emit Float32 results at
   four-byte width as required by the schema, using the established data-store
   conversion contract.
   Preserve HEAD's narrowing, overflow, NaN and signed-zero behavior, including
   key semantics; introduce no new representability checks or bitwise guarantees.
3. Preserve wire width independently of normalized store type. Use existing field
   metadata and typed dispatch once per field operation, outside element loops.
   Changing the resolver alone is insufficient: audit defaults, vector strides,
   postgres array sizes, result writing and default omission in both directions.
4. Preserve one field per column and the existing supported scalar/vector types.
   Retain explicit Bitmap/IP conversions. Do not add generic composite UDT or
   UDT-array columns, field flattening, composite-type creation or opaque storage
   substitutes. Recursive codec support is independent of store-column support.
5. Preserve existing schema acceptance, validation and error handling in both
   stores. Do not replace compile-time metadata requirements or missing field
   conversions with runtime probing or new rejection paths.

Acceptance: extend sqlite3's `zdbsltypes.hh` and postgres integration fixtures
with native `float`, float vectors and CStringVec while retaining double and
StringVec controls. Cover insert, mutable update, primary/secondary-key queries
where supported, select, delete, reopen/recovery and saga round trips. Include
empty/single/multi-element vectors, absent nonzero defaults, signed zero,
infinities and NaNs according to existing codec/data-store semantics; avoid equality
assertions that assume NaN equals itself. Cover explicit Bitmap/IP columns and
ordinary C++ conversions for native types declared with supported scalar codes.
Use existing fixture verification against
the actual result schemas; add no production validation or error-handling changes.

## Existing costs deliberately retained

- **Whole-vector updates:** eliminating them needs a different dirty-field/patch
  contract. Keep the existing `Mutable` filter and full-field serialization.
- **Backward construction:** preserve direct construction into transmit buffers
  with owner handoff. Generic destinations retain scratch construction and one
  completed-span insertion where ownership requires it. Phase 3.1 distinguishes
  these paths and accounts separately for growth, compaction and splitting; no
  replacement builder or forward/two-pass construction is required.
- **Cold lookup/recovery:** retain only materialization justified by a wire or
  ownership boundary; remove redundant relay copies under Phase 3.1. A borrowed
  data-store Tuple cannot outlive its storage or cross thread ownership merely to
  avoid a required owning copy.
- **Scheduler:** retain the overflow-ordering guard. Removing it for a branch-count
  reduction can reorder saga intent and effect callbacks; the findings establish
  overhead, not an unnecessary correctness mechanism.
- **Defaults:** retain Z metadata as the source of truth. Performance comparisons
  must use equal semantic schemas, default values and field-presence behavior;
  do not alter production defaults to mimic obsolete hand-written schemas.

### sqlite3 and postgres consumers of shared repairs

- Retain HEAD's operation, ownership and data-store semantics except for the
  explicit repairs here. Validate data-store propagation of shared changes once available;
  record unavailable dependencies as pending. This responsibility boundary does
  not narrow completion of the consolidated plan.
- Measure single-element mutations in large vectors separately from inserts and
  small-row updates. Retain full-field updates and current primary-key semantics;
  a dirty-field/element or old-key/new-key protocol needs a separate design.
- Trace memory indexes, replication, mocks, sagas and both data stores before reducing delete
  payloads. Test large secondary-key deletes, all affected index entries and
  replay/recovery. sqlite3 and postgres continue to bind only key zero.
- Measure direct transmit-buffer construction and owner handoff, relocation and
  splitting fallback separately from generic saga definition construction,
  SagaPayload/SagaData copies, data-store conversion and cold recovery. Apply Phase 3.1 without adding an
  owning data-store Tuple or changing nested verification or lifetime contracts.
- Wire omission still materializes a store column value from metadata defaults or
  mapped native fallback. Keep comparison schemas and defaults matched.
- Retain borrowed data-store views and direct pooled/scratch writes; memory-store
  bitmap/index repairs do not justify introducing data-store owning temporaries.
- Attribute scheduler dispatch separately from data-store conversion; retain ordering.
- Audit saga/store posts for redundant type erasure before `ZmRingFn`: retain
  concrete callable/capture types through templated paths rather than allocating
  a referenced closure solely to carry it in a ring capture pack. Distinguish
  real asynchronous ownership (rows, buffers and store callback contexts) from
  such wrappers; ring overflow may still require allocation.
- Check `invoke` and `invoked()` separately. Use direct calls only where the
  destination thread is guaranteed for every supported shard configuration.
  Keep required queued `run` boundaries and existing validation/error policy.
  Debug-only `ZmAssert(invoked(...))` has no release cost; existing release
  `ZiAssert` checks must not be mistaken for newly added dispatch.

## Build and executable acceptance policy

No builds or executable checks were run for this plan. During subsequent
implementation and acceptance, apply these constraints:

1. Batch source changes into large batches across the phases above. Use static
   interim acceptance; do not rebuild or retest until major milestones are
   reached. Rebuild infrequently and incrementally, reusing current build
   artifacts. Build changed `src` libraries before their tests.
   Defer unrelated dependent rebuilding until the final integration milestone.
2. Use `-j4` for `zum` and everything beyond it; use `-j4` for aggregate builds
   so they cannot exceed that limit. This explicit user cap overrides the
   guideline's generic `-j8` command examples.
3. Retain the current build configuration. Other compilers, release/debug build
   flags and platforms are out of scope. Do not switch configurations or perform
   a clean rebuild to establish another configuration. If build-system changes
   require `autoreconf`/reconfiguration, use `z.config` and preserve the current
   configuration.
4. Use existing TAP/interop/integration entry points for agreed acceptance work.
   Allocation-sensitive coverage should establish the bitmap ownership count and
   vector destination ownership without adding production instrumentation or a
   benchmark framework. Use continuation-based completion and `ZmBlock`, not
   polling or timing-dependent sleeps.
5. Run performance acceptance only in a consistently configured release build.
   Retain the current configuration for this work; if it cannot satisfy the
   release-only requirement, record performance acceptance as outstanding.
   Compare identical workload/schema/default/pool settings and state the measured
   scope. Per resolved RQ4, report allocation counts, payload sizes and latency
   distributions separately for codec, store operations and sagas; demonstrate
   removal of identified redundant allocation/copy costs and no material
   regression against matched recorded-baseline workloads. No additional absolute
   saga latency target is prescribed. Keep loopback-network results separate;
   kernel/clock limits are not evidence of codec regressions.
   The user subsequently authorized `z.config -p` for profiling diagnostics.
   Retain the existing compiler and feature settings; build matched HEAD/current
   profiling copies separately from the release artifacts. Instrumented timings
   do not replace the release performance acceptance above.
6. Use Valgrind sparingly: to debug memory corruption as needed and as a final
   memory leak and corruption gate, using
   `libtool exec`; do not run `.libs` binaries directly.
7. Finish with the current dependents rebuilt consistently, supported operation
   and type coverage accepted, and canonical interoperability checked. Update
   `findings.md`, `findings3.md` and the execution record with exact source and
   configuration scope, work performed and remaining limits. Prior acceptance does not certify these
   new repairs, and planned checks must not be reported as completed.

### Data-store and shared executable acceptance

Reuse existing fixtures before adding focused TAP checks with `ZuTestUtil`.
Register any new entry points in module `Makefile.am` files and document commands
and database prerequisites. Apply the batching, `-j4`, incremental-build, current
configuration and sparse Valgrind policy above to every check below.

- Run `make -C zdb_pq/test test` and `make -C zdb_sqlite/test test`, followed
  by both integration targets. postgres requires `ZDB_MODULE` and
  `ZDB_CONNECT`; use a disposable configured database with the required Z
  postgres types/extensions. A skipped integration run is not a pass.
- Run affected shared `zi` and `zdb` tests, including `ZiFBTest`,
  `ZiFBReflectTest` and `ZiFBIOTest` under `zi/test`, and applicable canonical
  FlatBuffers checks under `zi/interop` for codec, alignment or projection
  changes. Run `zf` tests if field metadata changes. Run the full enabled suite
  before final acceptance.
- Exercise repaired conversions, ownership and teardown using tools supported by the
  current configuration. Sanitizers apply only if already enabled; do not create
  another configuration. Use `libtool exec` for source-tree debugging tools.
  Synchronize callbacks with `ZmBlock`/`ZmSemaphore` and use a harness timeout
  to expose hangs. Valgrind remains limited to corruption debugging as needed
  and the final memory leak/corruption gate.
- Record compiler/platform, data-store versions, configuration, commands, failures,
  skips and performance data. Other compilers, sanitizer configurations and
  platforms are out of scope, not pending gates. Apply RQ4's matched-performance
  contract; record checks the current configuration cannot support.

Data-store acceptance requires both-direction CStringVec/Float32 conversion,
the supported scalar/vector column contract, correct schema
alignment, elimination of unnecessary copies and coverage of applicable shared
repairs, plus the narrow diagnostic correction in §5.1.1 and existing size-policy
enforcement in §5.2. Preserve existing
validation and error-completion behavior. Retain unmeasured costs
in findings; source inspection does not establish a benchmark improvement.

Assess the recently introduced postgres implementation edits individually for
necessity and redundancy against the completed type bindings and existing
invariants. Scope alone does not determine whether an edit should persist.
The static OID-name mapping covers every concrete Value alternative; complete
field conversion coverage rather than adding rejection for unresolved supported
fields. Remove redundant validation and catches introduced solely to handle
those new validation exceptions. Retain necessary size-policy checks from §5.2
and their routing through existing operation failure handlers. Replace the broader
diagnostic changes with §5.1.1’s separately retained field index and general
fallback. Preserve unrelated work and existing behavior.

## Open questions and ambiguities

The following RQ numbers are permanent and independent of Q1–Q17 in `plan.md`
and SQLQ1–SQLQ7 below. New shared questions use the next RQ number; new data-store
questions use SQLQ8 onward (the retained historical identifier prefix).
Resolve questions in place, retain their numbers and decision history, and append
new questions with new numbers. Do not renumber or reuse resolved numbers.
Established requirements such as canonical interoperability, stream contracts,
metadata defaults, pool tuning and build limits are not reopened here.

1. **RQ1 — Delete secondary keys — resolved: select key 0 fields.**

   **Original question:** retain the all-key delete payload or remove the cost
   of serializing secondary-only fields?

   **Explanation:** `ZfFieldFilter::Del` selects fields belonging to any key.
   Core Zdb currently uses it for deletion, although memory, postgres and
   sqlite3 stores consume only key 0. postgres's prepared delete predicate and
   parameter decoding use `m_xKeys[0].fields`; sqlite3's predicate and binding use
   `m_xKeyFields[0]`. Their database engines maintain secondary indexes from the
   stored row. The memory store obtains secondary keys from its existing row.

   Secondary-only fields therefore add producer serialization, payload bytes,
   possible buffer growth and transmission work without supplying required
   store delete parameters. Fields shared with key 0 remain necessary. This
   defect is inherited from HEAD; its timing impact is unmeasured.

   **Resolution:** core Zdb must use primary-key field selection
   (`...KeyFields<0>`), rather than `ZfFieldFilter::Del`. With the existing
   serializer API, use `ZuStructKeyFilter<0>::template Filter` against original
   row metadata. Keep the generic all-key filter's semantics unchanged.
   `findDel<KeyID>` may find a row through a secondary key, but the resulting
   deletion is identified by that row's primary key.

   Phase 1.4 prescribes the repair and propagation through replication/recovery
   and cache consumers. Those consumer audits are implementation obligations,
   not a reason to retain the incorrect all-key serialization contract. The
   earlier proposal to defer this cost is superseded. No implementation change
   or executable check has been performed for this resolution.

2. **RQ2 — UDT persistence — corrected: preserve supported columns.**

   The earlier resolution conflated recursive codec metadata with store column
   support and is withdrawn. HEAD binds one field to one supported scalar/vector
   column, retaining explicit Bitmap/IP representations. Composite UDTs and UDT
   arrays are not supported columns. Phase 1.3 requires removal of the resulting
   composite-store extension and fixtures. Recursive codec serialization remains
   unchanged. Explicitly declared saga Bytes retain their payload contract.

3. **RQ3 — Saga and lookup copies — resolved: permit required copies,
   remove redundant copies.**

   **Original question:** should existing saga staging and lookup/recovery copies
   be retained or repaired?

   **Original resolution:** backward FlatBuffers construction in scratch followed
   by a copy into a generic retained destination is acceptable. Redundant copying is unacceptable,
   irrespective of whether it also existed in HEAD.

   Use the existing generic `ZiFB::save(S &, ...)` for the saved saga definition,
   with `SagaPayload` as the destination through existing stream/append
   facilities. This builds backwards in `ZtScratch` with heap fallback and
   copies the completed span once into retained storage. Remove the explicit
   temporary pooled I/O builder and bespoke copy sequence. This replaces one
   final copy with one final copy while eliminating unnecessary pooled staging;
   it does not eliminate the required destination copy.

   Audit remaining saga, cold-find and recovery copying and remove redundant
   intermediates. Retain copies required by actual wire-construction or ownership
   boundaries and document those boundaries. Do not preserve a redundant copy
   solely because it predates this migration, or add a forward/two-pass builder
   to avoid the justified generic scratch-to-destination copy. Phase 3.1
   prescribes the work. Whole-vector update membership remains unchanged.

   The earlier blanket proposal to defer these costs is superseded. No
   implementation change or executable check has been performed for this
   resolution.

   **Current implementation clarification:** `ZiFB::save` is transmit-stream
   aware. For tagged `ZiTxStream` destinations it builds backwards directly in
   a growable `ZiIOBuf` and hands off the buffer owner through `operator <<`.
   The generic scratch-plus-copy description above applies to retained array
   destinations such as `SagaPayload`; it is not a universal construction rule.
   Initial buffer capacity does not determine copy elision. Preserve direct
   handoff and distinguish growth relocation, detach compaction and the current
   `maxSize()` splitting fallback as specified in Phase 3.1. This clarification
   supersedes any earlier requirement for a completed-span copy on every output.

4. **RQ4 — Performance acceptance — resolved as proposed.**

   **Original question:** is a matched release-build comparison sufficient, or
   are additional numerical targets required for individual operations?

   **Resolution:** use matched release workloads and report allocation counts,
   payload sizes and latency distributions separately for codec, store operations
   and sagas. Demonstrate removal of the identified redundant allocation/copy
   costs and no material regression on matched workloads against the recorded
   baseline. Preserve equal semantic schemas, defaults, field presence, payloads
   and pool settings when comparing implementations.

   Do not invent an absolute saga target or infer codec performance from loopback
   throughput. Record measurement conditions and variability; static ownership
   and layout improvements alone do not establish a timing result. The existing
   release-only performance and build-resource requirements remain in force.
   The current build configuration must be retained; other configurations are
   out of scope under the build policy above.

   This resolves the acceptance contract; no new benchmark or executable check
   has been performed for this resolution.

   **Baseline follow-up — 2026-10-10:** the user approved establishing matched
   HEAD baselines for store and saga workloads after repository review found
   recorded codec measurements but no corresponding store/saga records. Retain
   the current build configuration and reuse suitable artifacts. Keep baseline
   code isolated from production; baseline preparation is not performance
   acceptance.

   **Corrected-baseline follow-up — 2026-10-11:** the user approved the
   canonical `6e9a1252` implementation with only the existing `queueCount`
   ordering guard for remaining long saga acceptance. Keep the comparison
   explicitly labeled and preserve original-baseline failures separately, as
   specified at the beginning of this plan.

## Data-store question and decision history

SQLQ1–SQLQ7 retain their historical identifiers independently of RQ and `plan.md`
Q numbers. The next data-store question identifier is **SQLQ8**; never renumber, remove or reuse
one. The following original questions, ambiguities and decisions are retained
from `repair3.md`. The `SQLQ` prefix is retained solely to preserve question
identities; the subject is Zdb and its data stores. References to the former
plan’s phases 1–3 correspond to §§5.1–5.3 above,
phase 4 to Phase 2, and phase 5 to sqlite3 and postgres consumers of shared repairs. References
to separate-plan delivery boundaries describe the historical data-store responsibility;
completion now covers the entire consolidated plan. These entries record
requirements decisions, not implementation or acceptance results.

1. **SQLQ1 — Recursive UDT persistence — corrected: codec/store distinction.**

   The earlier requirement to persist nested UDTs and their arrays as columns
   was incorrect and is superseded by the user's explicit clarification and the
   original HEAD implementations. One field maps to one supported column type.
   Complete scalar/vector conversions; preserve explicit Bitmap/IP handling.
   Do not add composite columns or use recursive codec metadata as evidence of
   store support. RQ2 and Phase 1.3 specify the realignment.

2. **SQLQ2 — Float32 store storage contract — resolved: preserve HEAD handling.**

   **Question:** should native Float32 values always use the existing normalized
   double-width data-store representation, or must postgres also accept/use native
   `real` / `real[]` columns and sqlite3 use a distinct four-byte vector encoding?

   **Ambiguity:** phase 3 assumes double-width store storage. Supporting four-byte
   wire fields does not by itself settle accepted postgres OIDs, durable blob format
   or whether an existing table with native Float32 columns should open.

   **Resolution:** align with HEAD. `ZdbPQ` and `ZdbSL` already handle this
   correctly; the repair must not change that handling. Preserve their data-store
   storage representations, accepted column/OID mappings, vector/blob encodings
   and floating-point conversion behavior. Repair missing `ZiFB` bindings and
   wire-width conversion using those existing contracts.

   **Decision history:** the original question and proposed storage policy are
   superseded by the user's instruction to preserve HEAD. Do not infer a new
   column rejection policy or introduce another store storage format from this
   repair. Phase 3 now explicitly requires tracing and retaining the HEAD
   behavior. No implementation or executable check was performed for this
   resolution.

3. **SQLQ3 — Float32 representability on data-store reads — resolved: align with HEAD.**

   **Question:** when an externally written double-width data-store value cannot be
   represented as Float32, should reading fail or use native narrowing behavior?
   Are NaN payload bits and signed zero required to survive persistence exactly?

   **Ambiguity:** normal Float32 writes widen without loss, but data-store edits or
   queries can produce larger finite values or extra precision. The phrase
   “existing codec/data-store semantics” does not define overflow, NaN payload or
   signed-zero acceptance for the newly supported path, especially for keys.

   **Resolution:** preserve HEAD's data-store numeric conversion semantics,
   including narrowing, overflow, nonfinite values, signed zero and keys. Adapt
   the `ZiFB` wire boundary to those semantics. Add no new representability
   rejection policy or stronger bitwise preservation guarantee. Determine exact
   expected results by tracing HEAD, rather than choosing a new numeric policy.

   **Decision history:** resolved by the user's instruction to align remaining
   behavioral questions with HEAD. The proposed policy above is superseded by
   this preservation requirement; phase 3 reflects it.

4. **SQLQ4 — Reopen, rebinding and retry after failed open — resolved: align
   with HEAD.**

   **Question:** must a failed open be retryable for the same table ID in the
   same running Store, and may reopen change the schema's binding/layout, or
   only refresh equivalent schema metadata?

   **Ambiguity:** phase 1 permits rebuilding or rejecting changed metadata and
   requires subsequent valid opens, without defining same-table retry. postgres
   currently retains table state after some store setup failures; a partially
   created database table may also survive a failed open.

   **Resolution:** retain HEAD's reopen, schema refresh/acceptance, retry and DDL
   lifecycle semantics while repairing callback completion and diagnostic safety.
   Trace existing behavior and propagate metadata correctly; do not add a new
   same-ID retry guarantee, schema-evolution policy or DDL rollback mechanism.

   **Decision history:** the proposed additional retry guarantee was not an
   established requirement and is superseded by the user's HEAD-alignment
   instruction. Phase 1 and its acceptance criteria now preserve that boundary.

5. **SQLQ5 — Parameter limits versus resource policy — resolved: align with HEAD.**

   **Question:** should expansion rejection enforce only protocol/data-store and
   existing container limits, or also a configurable per-operation scratch cap?

   **Ambiguity:** overflow-safe planning can still accept an operation requiring
   very large aggregate scratch. The signed libpq length boundary is not a
   guarantee that postgres accepts every value below it. A new lower memory
   cap would introduce a workload policy absent from the current plan.

   **Resolution:** retain HEAD's resource limits, heap tuning and allocation
   failure policy. Repair arithmetic overflow and enforce actual representable
   protocol/data-store/container bounds before encoding or narrowing; do not add a
   configurable scratch cap or arbitrary lower limit. Use internal test seams
   for small rejection fixtures without adding public configuration.

   **Decision history:** resolved by the user's HEAD-alignment instruction.
   Phase 2 now distinguishes required sizing correctness from new resource policy.

6. **SQLQ6 — Shared repairs and completion boundary — resolved: align with HEAD
   and retain prior explicit repairs.**

   **Question:** does completion of repair3.md require delivery of repair2.md's
   shared primary-key-only delete and redundant saga-copy repairs, or only data-store
   changes plus validation against those shared repairs when available?

   **Ambiguity:** phase 5 says both “measure separately” and “apply shared copy
   reductions,” while the completion paragraph lists neither shared repair.
   Their behavior is already resolved by RQ1/RQ3; the open issue is ownership
   and delivery dependency, not whether to retain redundant work.

   **Resolution:** preserve HEAD's operation, ownership and data-store semantics
   except for explicit repairs already required here or by resolved repair2.md
   decisions. Implement shared repairs once; this plan covers their data-store consumer
   propagation and regression checks. Do not make unrelated repair2.md work a
   new delivery gate. Record applicable shared dependencies and pending checks
   accurately. Retain the existing matched-performance contract without new
   targets or protocols.

   **Decision history:** resolved by the user's HEAD-alignment instruction,
   without reopening earlier explicit delete/saga repair decisions. Phase 5 and
   completion criteria now state the data-store responsibility and dependency boundary.

7. **SQLQ7 — Build and platform acceptance scope — resolved: existing build only.**

   **Question:** should this data-store repair inherit repair2.md's current-build-only
   policy, or is the broader GCC/Clang, sanitizer, ARM64 and Windows matrix in
   this plan an additional required delivery scope?

   **Ambiguity:** repair2.md explicitly excludes switching compilers, build flags
   and platforms and requires incremental reuse of the current configuration.
   This plan's validation section would require additional configurations if
   interpreted as mandatory execution in this workspace.

   **Resolution:** the existing build configuration is the only build
   configuration in scope. Reuse incremental artifacts and inherited resource
   limits; do not switch compilers, flags, sanitizers or platforms, or establish
   another configuration through a clean rebuild. Run applicable checks within
   that configuration and record unavailable checks honestly. Other build
   configurations are out of scope, not deferred delivery requirements.

   **Decision history:** the broader matrix and proposed external validation
   gates are superseded by the user's explicit scope restriction. The validation
   section above now uses only the existing configuration.

No implementation, build, test or benchmark was performed to resolve
SQLQ3–SQLQ7; these entries record requirements decisions only.

**Scope correction:** earlier requirements here for setup rejection, checked
parameter sizing, diagnostic repair and changed error completion are superseded.
The current repair addresses copying inefficiency and type-coverage gaps, plus
the subsequently authorized narrow failure-diagnostic fix in §5.1.1 and enforcement
of the existing parameter size policy in §5.2. These current requirements supersede
the historical sizing decisions above. Existing
validation, mismatch detection and error-completion behavior remain unchanged. Historical references
to former phase numbering do not reinstate superseded work.
