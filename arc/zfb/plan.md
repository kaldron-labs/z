# Implementation plan: ZiFB (originally ZfFB)

Implement [goal.md](goal.md) by following the prescriptions and dependency
order below. The numbered decision record at the end retains Q1–Q17 and
their resolutions; the implementation slices incorporate those decisions.
The slices prescribe the implementation; the execution record below identifies
completed checks and their evidence. The original migration acceptance applies
only to its recorded source snapshots and scope. Subsequent repairs required by
[repair2.md](repair2.md) remain incomplete; the latest execution entries and
[requirement audit](zdb/interop/evidence/repair2-20261010/audit55.md) record their
accepted checks, current build and outstanding gates. Historical component names
below identify the sources and artifacts used at each checkpoint.
Older execution entries retain their original pending statements as history.
`AGENTS.md` and `GUIDELINES.md` apply subject to the explicit scope here.

## Subsequent constructor-metadata correction — 2026-10-09

The acceptance results below apply to the recorded source snapshots. Subsequent
static review found omitted `Ctor<N>` properties in the narrow/wide benchmark
and additional ordinary construction fixtures; see the
[fixture audit](zi/interop/README.md#constructor-metadata-audit--2026-10-09).
The corrections use existing framework construction, preserve field defaults
and wire metadata, and retain deliberate `Init` coverage. No codec change is
needed. Earlier explanations attributing the narrow-load zero store to an
unavoidable adapter contract are superseded: it resulted from the benchmark's
missing constructor metadata. No builds, tests or measurements were run for
these corrections; their performance and executable acceptance are unmeasured.
Archived CSVs, source snapshots and manifests remain unchanged.

### Release benchmark rerun after constructor correction

The benchmark was subsequently rebuilt incrementally with `make -C zi/interop
-j4` and rerun with the same release canonical reflection object. Three fresh
processes per load workload (11 rounds, one million operations, 64-byte payload,
CPU 6) show native narrow-load process medians 1.974–2.015 ns versus canonical
2.005–2.042 ns, and wide-load medians 61.605–62.103 versus 71.663–72.507 ns.
The former narrow-load deficit is absent; wide loads are 14–15% faster. Narrow
and wide fresh/reuse saves also favor native. All embedded prechecks pass.
[Raw results, source snapshot and hashes](zf/interop/evidence/ctor-metadata-release/README.md)
record this focused rerun. It supersedes the initial unmeasured benchmark status
above, without extending acceptance to the other edited fixtures. No separate
test suite or dependent rebuild was run.

## Unit-test consolidation — 2026-10-09

Replace the nine incremental FlatBuffers unit-test executables with `ZfFBTest`
(metadata, wire, builder, views, values, loading, unions, streaming) and
`ZfFBReflectTest` (reflection, BFBS/FBS generation, external-schema verification).
Shared fixture metadata lives in `zf/test/ZfFBTest.hh`; the seven negative
compilation selectors remain in `ZfFBTest.cc`. All assertions are retained.
`ZfSkipTest` remains format-independent and canonical interop stays in `zf/interop`.
Build declarations and current fixture references are updated. Static comparison
retains all 273 assertion expressions and seven negative selectors. Historical
execution records retain the original names. No build or tests were run;
regenerate build inputs through `z.config` before the next incremental build.

## Working-tree guideline audit — 2026-10-09

Reviewed the current tracked delta and new implementation source against every
section and audit-flag family in [GUIDELINES.md](GUIDELINES.md), including the
supplementary [QUIC guidelines](zquic/GUIDELINES.md) for its allocation changes.
Scope includes source, test, integration, example, benchmark and interop code;
canonical generated headers, frozen comparator code and historical evidence are
external contracts/evidence rather than formatting targets. This is a source
review; no build, test or benchmark was run for these repairs. Earlier acceptance
and timing results describe their recorded snapshots.

| Guidelines | Review and repairs |
| --- | --- |
| [Repair procedure](GUIDELINES.md#repair-procedure), general principles, scope/cleanup | Retain unrelated implementation and intentional test coverage; repair demonstrated violations without new containers, validation layers or codec fast paths. No legacy `zfb` compatibility layer is restored. |
| [Indentation](GUIDELINES.md#indentation), multiline layout, compact layout/alignment | Align multi-field `ZfStruct` typecodes with hard tabs; preserve property/accessor tokens and field order. Split compressed multi-field declarations, wrap long properties and migrated accessor expressions to 98 columns, and normalize changed indentation to tab stops of eight with two-column logical nesting. Align exposed reflection data and pointer/reference members. |
| [Class and struct shape](GUIDELINES.md#class-and-struct-shape), bad code structure | Move `PendingAlert` data before behavior; make the public-data subscription/pending composites structs. Move native detach operations into `ZmThreadContext`, preserving the thread wrapper's release/return behavior. Put view metadata friends in its initial private section. |
| [Header layout](GUIDELINES.md#header-layout), module layout, naming | Add direct byte-order/assertion/comparison/C-library dependencies to the new headers/sources. Rename the SQLite integration metadata header to `zdbsltypes.hh`, updating its guard, include and distribution declaration. Add the latest benchmark rerun to `EXTRA_DIST` with the existing evidence. Preserve mandated canonical schema names and existing heap IDs. |
| [Core language](GUIDELINES.md#core-language), static polymorphism/constraints, ADL, enums | Replace implicit lambda capture with named captures and integer-literal macros with width-preserving functional casts. Remove enum-to-integer casts from wire-invariant assertions. Retain SFINAE, ADL and compile-time metadata dispatch; generic dependent construction remains templated. |
| [Storage and capacity](GUIDELINES.md#storage-and-capacity), heap allocation | Preserve named pooled I/O allocation, default capacities, growth/downward-writing, alignment and ownership transfer. Keep workload-sized `ZtScratch` fallback and metadata-bounded inline offsets. Fixed wire arrays represent protocol widths, not arbitrary capacities. |
| [Containers and intrusion](GUIDELINES.md#containers-and-intrusion), callbacks/lifetimes | Retain intrusive ownership and existing node integration. Shared row references in independent secondary indexes and pooled buffers shared with the alert writer have documented ownership exceptions. No new locks, owner references or allocation bases are introduced. |
| [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization), bad casting | Preserve direct writes and raw serialized-span copies. Remove the redundant cast of the canonical interop byte pointer. Keep required wire reinterpretation casts: a `ZuLittleEndian<T>` object is not a char-equivalent pointer. Preserve constructor metadata and deliberate `Init` tests. |
| [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow), liveness | Cache invariant benchmark connection counts and repeated telemetry handles. Remove repeated empty-span/length predicates where span truth already proves length. Use a scoped telemetry case with its final `} break;`. Retain existing bounded scheduler/snapshot turns and continuation-based I/O. |
| [Framework fit](GUIDELINES.md#framework-fit), strings/formatting/parsing | Retain `ZuMatcher`, existing enum lookup, `ZuBox`/`ZuFmt`, percent codec and Z stream contracts. Exact FBS floating defaults retain `ZtString::sprintf("%a", ...)` on `ZtScratch`: decimal `ZuFmt::FP` cannot preserve all subnormals. This is an existing formatter exception to the varargs flag, not a private formatter/parser. |
| [Dynamic modules](GUIDELINES.md#dynamic-modules-1), persistence | Keep `ZiModule` factories and resident store implementations; schema views borrow table-owned BFBS. Keep store/saga persistence and coordinated breaking API propagation. |
| [I/O sharding](GUIDELINES.md#io-sharding), unnecessary atomics/locks/copies | Preserve owner-thread dispatch and raw dependent back-pointers. Do not introduce synchronization for telemetry or move variable payloads into callback captures. Existing scheduler synchronization protects scheduling/lifetime, not approximate diagnostic counters. |
| [Tests](GUIDELINES.md#tests), acceptance/debugging, build configuration | Preserve TAP and blocking primitives, embedded benchmark prechecks, external-boundary verification and trusted local readers. Retain the configured release artifacts and the user's `-j4` cap; no rebuild/reconfiguration, separate tests or Valgrind run in this audit. Executable acceptance of these repairs remains outstanding. |

Final static checks: all 260 multi-field metadata macros in the 156 reviewed
C++ files have aligned typecodes; changed/new implementation code has no
remaining over-98-column code lines or flagged implicit captures/integer-literal
macros. The benchmark snapshot differs only in whitespace. `git diff --check`
is clean. These checks do not establish compilation or run-time correctness.
The renamed integration header and new evidence distribution entries require
regeneration through `z.config` before the next affected build; none was run here.

Structural/semantic schema checks remain at the external BFBS boundary; local
compile-time-generated BFBS and locally generated payloads continue directly to
trusted views. Capacity/error/streaming branches remain necessary under Q2.
Wire verification, enum correspondence assertions and schema bounds are retained
for their established contracts, without speculative extra checks.

## Authority, scope, and build discipline

Implement the functional requirements in `goal.md` and the resolved Q1–Q17
record below. The user's latest review/build instructions supersede earlier
requests for a mandatory ASan configuration, clean builds, per-slice dependent
builds, and optional-only Valgrind acceptance. This plan prescribes future
implementation; it does not authorize unrelated cleanup or report
implementation as complete.

Retain the current configured build and its artifacts: Clang 23, x86-64 Linux,
`-march=x86-64-v2 -O3 -g -DNDEBUG`, `/usr`, PostgreSQL, SQLite and GTK enabled.
The initial debug correctness/memory gates preceded the authorized release
transition through `z.config`; incompatible debug artifacts were cleaned.
Performance acceptance is exclusively release. Keep this release configuration
for subsequent fixes and focused correctness/memory checks. Do not repeat the
clean or return to debug merely to rerun checks. ASan with `make clean` remains
an optional diagnostic exception; never run Valgrind against ASan binaries.
Keep code suitable for GCC and MinGW/Windows; their builds/tests are out of scope.

Batch source edits by the slices below. There are two planned build/test
milestones: M1 after the new core/I/O implementation and interop work, and M2
at the end after all dependent edits and build cleanup. Between milestones,
use source/contract review for interim acceptance; do not run speculative
rebuilds or repeated tests. Defer rebuilding existing dependent libraries,
applications, and their tests until M2. An actual failure permits focused
debugging of the failing target, not an expanded configuration matrix.

Use incremental `make -j4` for owning libraries, dependents, tests, interop,
top-level traversal and dashboard. Run one compiler batch at a time. The
four-job limit is mandatory for Zum and every subsequent module, including
Ztc, Zdf and Zgtk, to avoid exhausting system memory. Do not overlap builds
or other compiler invocations that would exceed four jobs. Retain this limit
through the final acceptance refresh.
Preserve unaffected artifacts within each configuration. The required clean
for the debug-to-release switch has already occurred. Do not run `distclean`
or add an alternate build tree. Regenerate only for pending Autotools input
changes, through `z.config`: with the retained release settings and prefix,
use `./z.config -c -L /usr`; omit `-c` when only reconfiguration is needed.
Do not invoke `autoreconf`, `configure`, or `config.status` directly, or
hand-edit generated Makefiles. The wrapper selects `-O3 -DNDEBUG`; retain the
complete emitted flags with results.

Valgrind is for needed memory-corruption debugging and the final M2 leak/
corruption gate; use `libtool exec`. ASan is not a substitute for that gate.

This explicit scope overrides the broader configuration/rebuild defaults in
[Build configuration](GUIDELINES.md#build-configuration) and
[Acceptance](GUIDELINES.md#acceptance). Retain their dependency ordering,
incremental-target, TAP, and source-tree execution conventions.

## Mandatory implementation contracts

- `ZfFB` lives in `zf` and derives its wire traits, typed operations, and
  runtime reflection from ordinary `ZfStruct` declarations.
- Preserve canonical BFBS binary schemas for reflection. Compile-time
  generators specialize the emission code from `ZfStruct` metadata; that
  code saves to a templated destination `S` using `operator <<`. The normal
  reflection path uses an in-memory buffer destination and passes its binary
  span directly to the consumer. Schema-generation decisions must not branch
  at runtime. Buffer capacity, error handling, byte-copy primitives, and all
  actual data-streaming operations are exempt (Q2). No `.bfbs` file, external
  codegen step, or prebuilt static byte blob is required by this path.
  Each `Table` owns a separate BFBS buffer generated during its construction
  and retained through completion of its dependent store operations (Q13).
- Delete `zfb` and every `ZfbStruct`/`ZfbEnum`/generated-type binding API.
  Do not introduce a replacement struct declaration macro or compatibility
  forwarding layer.
- Keep `ZiIOBuf` and `ZiIP` integration in `zi`, above the core codec.
- Follow the Z save convention: template save operations on stream type `S`,
  requiring `operator <<` (Q12). Support destinations such as `ZiTxStream`,
  `ZiFileTxStream`, `std::cout`, and `ZuStream` through that contract.
  Provide binary BFBS and textual FBS output as library functions instantiated
  with caller-compiled metadata; emit one self-contained textual schema per
  selected root. Do not add standalone schema-export executables (Q14).
- Verify all external data before loading, including reflected payloads.
  Data generated and parsed within the same process needs no validation or
  re-verification; use trusted access, including for generated BFBS spans.
  Already verified external buffers also use trusted access thereafter (Q4).
- Canonical FlatBuffers interoperability is mandatory. Keep canonical
  libraries and codegen confined to a separately invoked interoperability
  harness after migration.
- Feature scope is the repository's used features, including the BFBS
  reflection contract, plus the explicit Q3 decisions below. Map canonical
  `required` to existing `Required`; provide a `ZfFB`-specific field property
  for forced alignment and a compile-time `load`/`save` parameter for
  size-prefixed roots. Sorted application vectors and wider offsets are out
  of scope. Missing scalar fields that read as their wire default are in
  scope, including implicit zero/false and explicit nonzero defaults.
  Nullable scalars declared with `= null` and union vectors are unused and
  out of scope under the repository-use rule. Q6 explicitly adds metadata-only
  `Skip` accessors to `ZuStruct`/`ZfStruct`. Every format's `load`/`save`
  skips these fields without calling `get` or `set`; their metadata remains
  available. `ZfFB` maps `Skip` to canonical `deprecated`, retaining the
  declared field's schema slot and type.
- Number the complete schema before projecting fields: all implicit or all
  explicit IDs per table, unique and contiguous from zero, including `Skip`
  and union discriminator slots (Q5/Q6). Use facet-specific optional field
  name properties like `ZfJSON`; default to the exact existing camel-to-snake
  conversion (Q7). Map metadata by equivalent semantics and discard unmapped
  metadata in schema translation (Q8).
- Use the existing default resolver: `Deflt` if supplied, otherwise the
  existing Zf fallback (Q9). Missing inline values produce that null/default;
  missing `Required` fields are errors (Q11). Already serialized spans retain
  the original serializer's inclusion decisions: copy complete spans without
  reapplying defaults or operation filters (Q10).
- Backwards/legacy compatibility is a non-goal (Q1). Existing persisted
  data, deployed peers, C++ APIs, and application schema names/IDs/layouts
  impose no compatibility requirement. Propagate changes through all current
  consumers without shims or legacy readers. Canonical FlatBuffers payload
  and BFBS interoperability remains mandatory for the new schemas.
- Reproduce the custom `Zfb::IOBuilder` allocation, growth, ownership-transfer,
  and alignment behavior, including downward writing directly into pooled
  storage (Q16). Preserve borrowed reads and nested buffers. Establish
  performance against that implementation before removing the baseline;
  dependency removal does not justify a performance regression.
- Use the current `Zfb` canonical C++ dependency for interop (`flatbuffers >=
  24`, currently 25.12.19 in the inspected workspace), with matching `flatc`
  and recorded versions (Q15). Follow the retained configuration and M1/M2
  build discipline above; Q17 records the superseded configuration decision.
- Treat the existing unrelated working-tree changes as independent work.
  Keep edits limited to the codec, its consumers, and necessary build/tests/docs.

## Minimal design and guideline audit

Every new element must serve a `goal.md` consumer or a resolved requirement.
Use existing Z mechanisms before adding an abstraction. Apply the following
audit to declarations and uses across `src`, `test`, `util`, `itest`,
`example`, `bench`, and `interop`, limited to affected code.

| Element / audit area | Prescribed implementation and simplification | Applicable guideline |
| --- | --- | --- |
| Structure metadata | Extend the existing `ZuStruct`/`ZfStruct` field machinery for `Skip`; retain one complete declaration and derive active filters. Do not add another field-list macro, shadow declaration, or independently maintained wire schema. | [Metadata](GUIDELINES.md#metadata), [Framework fit](GUIDELINES.md#framework-fit) |
| Compile-time schema work | Use `ZuTypeList`, `ZuSeq`, `ZuUnroll`, `ZuType*`, and `ZuString`/`ZuStringT`. Resolve the reachable schema and emission order at compile time. No custom typelist/string package, runtime registration graph, hash, or global type registry. | [Compile-time data and matching](GUIDELINES.md#compile-time-data-and-matching), [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow) |
| Reflection | Use canonical BFBS bytes and thin borrowed views as the runtime wire description. Reuse store field records for links to existing `ZfVField`/keys. Do not duplicate BFBS in an owning native graph, universal descriptor hierarchy, or thunk registry. | [Repair procedure](GUIDELINES.md#repair-procedure), [Framework fit](GUIDELINES.md#framework-fit) |
| Code organization | Keep non-type-dependent primitives out of templates and in the existing shared library. Factor typed/erased operations over those primitives; use ADL, SFINAE and `if constexpr`. No concepts, new virtual codec hierarchy, abstract allocator hierarchy, or forwarding-only wrappers. | [Static polymorphism and constraints](GUIDELINES.md#static-polymorphism-and-constraints), [Code structure](GUIDELINES.md#code-structure), [Header layout](GUIDELINES.md#header-layout) |
| Transient storage | Use caller-scoped `ZtScratch` with a heap-identified Z array/string for offset vectors, temporary field work, and formatting scratch. Pass scratch through existing callbacks. Use neither raw `alloca` nor heap-only temporary vectors. | [Storage and capacity](GUIDELINES.md#storage-and-capacity), [Heap allocation](GUIDELINES.md#heap-allocation), [Storage and lifetime](GUIDELINES.md#storage-and-lifetime) |
| Persistent storage | Each `Table` owns one heap-identified `ZtArray` byte buffer for BFBS. Keep codec views on the stack or borrowed. I/O payloads retain pooled `ZiIOBuf`/`ZmRef` ownership. No shared schema cache, separately allocated descriptor objects, owner/refcount wrapper, or custom `operator new`. | [Heap allocation](GUIDELINES.md#heap-allocation), [Containers and intrusion](GUIDELINES.md#containers-and-intrusion), [I/O and system integration](GUIDELINES.md#io-and-system-integration) |
| Capacity and layout | Derive metadata bounds from field/type counts and wire widths; retain tuned pool/growth behavior. Use named wire alignment constants. Avoid arbitrary fixed arrays, fixed lookup tables, duplicate lengths, or zeroing storage before writing it. Protocol-mandated inline IP arrays remain. | [Storage and capacity](GUIDELINES.md#storage-and-capacity), [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization) |
| Serialization and copying | Reproduce `IOBuilder`'s downward arena and live-region relocation. Write vectors directly into uninitialized storage. Copy unchanged self-contained serialized spans with `memcpy`; do not reserialize fields, reapply filters, or make temporary contiguous copies. | [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization), [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow) |
| Validation | Check compile-time metadata when resolving the schema; verify external spans once at ingress. Typed/reflected trusted reads share unchecked wire primitives. No new verified-buffer wrapper, repeated table/row checks, generated-BFBS verification, or revalidation after local copies. | [Repair procedure](GUIDELINES.md#repair-procedure), [No dogma](GUIDELINES.md#no-dogma) |
| Formatting/parsing | Keep object JSON in `ZfJSON` and structured printing in `ZfStructPrint`/`ZuPrint`. Render FBS syntax through `S <<` with `ZuBox`/`ZuFmt` and existing string/escaping facilities. Add only FBS-specific punctuation/grammar; no JSON, decimal, float, hex, or string parser/formatter replacement. No FBS parser is required. | [Formatting](GUIDELINES.md#formatting), [Metadata](GUIDELINES.md#metadata), [Framework fit](GUIDELINES.md#framework-fit) |
| Names and conversions | Preserve the existing `ZtCase::camelSnake` semantics; reuse/factor its character transform for compile-time use rather than add a second naming algorithm. Pass convertible spans/strings/arrays directly. Only cast for an actual wire representation boundary. | [Framework fit](GUIDELINES.md#framework-fit), [Bad casting](GUIDELINES.md#bad-casting), [Strings](GUIDELINES.md#strings) |
| Stream/error behavior | Template output on `S` with `operator <<`. Honor the destination's existing state and failure conventions; for Z Tx streams keep sticky failure and explicit completion-boundary flush checks. Do not require `failed()`, flush, seeking, or allocation APIs from every generic `S`. | [Tx streams](GUIDELINES.md#tx-streams), [Formatting](GUIDELINES.md#formatting) |
| Runtime algorithms | Reuse existing one-time store field association and keyed BFBS lookup. Use `ZuMatcher` where actual multi-token matching is needed and `switch` for runtime type codes. Avoid custom hashes, extra indexes, duplicated dispatch, invariant work inside loops, or speculative writer strategies. | [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow), [Compile-time data and matching](GUIDELINES.md#compile-time-data-and-matching) |
| Ownership/concurrency | Retain existing Rx/Tx ownership and buffer-reference handoff. No new locks, atomics, threads, GC scans, or refcount captures of longer-lived owners. Drain callbacks before table/buffer teardown. | [Scope and cleanup](GUIDELINES.md#scope-and-cleanup), [I/O sharding](GUIDELINES.md#io-sharding), [Lifetime](GUIDELINES.md#lifetime), [Teardown](GUIDELINES.md#teardown) |
| Store modules | Keep the existing `ZiModule`/`ZmPolymorph` factory interface; migrate it together. Do not substitute function tables or opaque callback protocols, or invent cross-DLL type identity. Loaded store code stays resident under the existing contract. | [Dynamic modules](GUIDELINES.md#dynamic-modules), [Dynamic modules contract](GUIDELINES.md#dynamic-modules-1) |
| Naming/style | New code names are at most 28 bytes under authoritative `GUIDELINES.md`; use concise camelCase and project abbreviations. `src`/unit-test basenames use module-prefixed CamelCase; owned `interop`/`bench`/`itest` basenames are lowercase. Private class state uses `m_`; public struct data comes first without `m_`. Use accessor overloads, internal trailing underscores, tabs, and the header skeleton. | [Naming](GUIDELINES.md#naming), [Length and abbreviations](GUIDELINES.md#length-and-abbreviations), [Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes), [Class and struct shape](GUIDELINES.md#class-and-struct-shape), [Indentation](GUIDELINES.md#indentation) |
| Cleanup/tests | Delete obsolete bindings, schemas, wrappers, and unused helpers. Retain diagnostics and unrelated changes. Use `ZuTestUtil`/TAP, `log`, and deterministic existing synchronization; no sleep-based tests or standalone fuzz infrastructure added speculatively. | [Audit flags](GUIDELINES.md#audit-flags), [Scope and cleanup](GUIDELINES.md#scope-and-cleanup), [Tests](GUIDELINES.md#tests) |
| Remaining structure flags | Put nondependent declarations outside templates; use concise aliases only where needed for member layout. Keep real counts rather than shadow lengths, cache invariant element reads, and use compile-time string literals rather than constexpr span objects. Order native members for locality without changing logical metadata order; retain existing thread-exclusive layout. Access private state through its narrow interface and use enum values directly where convertible. | [Code structure](GUIDELINES.md#code-structure), [Storage and capacity](GUIDELINES.md#storage-and-capacity), [Bad code structure](GUIDELINES.md#bad-code-structure) |
| Traversal and asynchronous work | Keep iterators inside traversal scope, count actual visited elements where filtering matters, and remove checks already proved by the iterator. Continue after asynchronous completion, not after posting; preserve saga prerequisite failure exits. Preserve existing bounded I/O work and continuations; add no polling, locks, or atomic telemetry counters. | [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow), [Liveness](GUIDELINES.md#liveness), [Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies) |
| Diagnostics and representation | Retain `ZiLOG` probes and platform-normalizing I/O. Capture required log values by value (`ZeString` for scratch strings). Use sentinel state where already supplied by Z types; do not add parallel null flags. Preserve necessary wire-presence queries independently of logical default values. | [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization), [Framework fit](GUIDELINES.md#framework-fit), [Scope and cleanup](GUIDELINES.md#scope-and-cleanup) |

The required custom code is the FlatBuffers wire algorithm, schema mapping,
and its integration. Formatting, matching, byte-order conversion, storage,
allocation, I/O, and lifecycle facilities must come from Z where available.
Use `ZuByteSwap`/existing endian wrappers; do not hand-code their equivalents.

Delete the previous plan's blanket `Skip` property blacklist: the field is
metadata-only and all object operations skip it regardless of incidental
properties. Diagnose only contradictions that prevent a supported canonical
schema, such as a required deprecated wire field. Do not add runtime checks.
At each slice review, account for every audit category above in its affected
declarations and uses. A nonapplicable flag requires no new mechanism; fix
only demonstrated violations within scope.

## Component boundaries

Implement the following components with exactly two `ZfFB` headers, matching
the other format adapters. Keep common non-template code in the shared library.
Organize `ZfFB.hh` like JSON/URI/ASN1/CBOR/TOML/YAML/Cf: field properties and
type metadata, input/output functions, field save/load and object handlers,
then public entry points. Follow their section comments and namespace layout;
retain namespace boundaries needed for ADL, not former helper-file boundaries.

| Location | Responsibility |
| --- | --- |
| `zu/src/ZuStruct.hh` and dependent field machinery | Add metadata-only `Skip` to the accessor taxonomy; preserve declarations while excluding skipped fields from object access and tuple/key extraction. |
| `zf/src/ZfFB.hh` and `ZfFB.cc` | Load/save, field properties, wire traits, borrowed views, downward arena, reader/writer support, and verification. |
| `zf/src/ZfFBReflect.hh` and `ZfFBReflect.cc` | Reflection, BFBS emission and FBS rendering; includes `ZfFB.hh`. No additional helper headers, standalone reflection library, or general-purpose graph package. |
| `zi/src/ZiFB.hh`; `.cc` only for real non-template code | Pooled `ZiIOBuf` integration and `ZiIP` transformation; consumes `ZfFB` without reverse dependencies. Do not add an empty implementation unit. |
| `zf/test/ZfFB*Test.cc` | Isolated metadata, wire, reflection, view, and construction tests without `zi` or canonical FlatBuffers. |
| `zi/test/ZiFBTest.cc` | Pool growth, headroom, detach/reuse, cleanup, and IP integration tests. |
| `zf/interop` | Canonical payload and in-memory BFBS interoperability, independent fixtures, and tests of the `ZfFB` binary BFBS and textual FBS library functions instantiated with test metadata. |
| `zi/interop` or dependent module `interop` | Interoperability involving IP, pooled nesting, or application schemas that require higher layers. Do not make `zf` targets depend on these layers. |
| `zf/bench` and higher-layer benchmarks where needed | Core latency/size measurements and pooled-buffer comparisons under representative workloads. |
| Existing dependent modules | Application framing, transport/file output, SQL encodings, ownership, and application validation. |

Keep the common engine in the shared library and type-dependent dispatch in
templates. Use Z traits, ADL, SFINAE, containers, allocators, and printing;
no concepts, general-purpose STL replacement layer, or new virtual codec
hierarchy. Runtime store operations can use generated function pointers and
existing monomorphic `ZfVField` facilities where an audited operation requires
them; do not generate a new callback table for every type. BFBS remains the
runtime wire schema. Existing store polymorphism remains its own concern.

## Slices and interim acceptance

| Work | Dependency and deliverable |
| --- | --- |
| Slice 1 | Capture schemas, consumer mappings, `IOBuilder` mechanics and existing baseline evidence. Use existing binaries if their provenance is suitable; no rebuild. |
| Slice 2 | Implement shared metadata and `Skip`; accept by source review of complete versus active fields and compile-time test cases prepared for M1. |
| Slice 3 | Implement one set of wire read/verification primitives; accept by source review of ingress boundaries and borrowed lifetimes, with tests queued for M1. |
| Slice 4 | Implement the downward writer and BFBS emitter; accept by layout, ownership, and growth review, with tests queued for M1. |
| Slice 5 | Integrate generic streams and direct I/O buffers; accept by review of allocation/transfer contracts and all-format `Skip` coverage. |
| Slice 6 — M1 | Build changed owning core/I/O libraries and the new isolated codec test/interop targets in one incremental batch. Run their queued checks and comparable core benchmarks. Do not rebuild existing dependent targets. |
| Slices 7–9 | Batch all database, store, telemetry, dashboard, and application source migrations. Each slice ends with consumer/ABI/ownership review; their builds/tests remain deferred to M2. |
| Slice 10 | Delete obsolete production code/dependencies and update build inputs; accept the dependency/reference audit without a separate build. |
| Slice 11 — M2 | Regenerate required build metadata with unchanged settings, incrementally rebuild affected dependents once, run affected suites/interop and final Valgrind checks, and confirm no regression. |

Record the affected source locations and evidence at each interim gate.
Review acceptance before M1/M2 means the source is ready for that milestone,
not that tests have passed. Reuse
the decision record rather than reopening established framework contracts.
Do not mark a gate complete on the strength of this plan alone.

## Slice 1 — Capture contracts, consumer mappings, and baseline

1. Refresh the inventory in `goal.md` across all module source roles, build
   inputs, installed headers, schemas, and current documentation. Include
   imported unqualified FlatBuffers APIs, generated `fbs::` names, adapted
   fields, and consumers of aliases.
2. Before deleting schemas, record each live root/type's field ID, wire kind,
   scalar default, inline layout, vector element representation, union tag,
   and discriminator slot. Preserve logical field names and SQL names.
   Include non-root nested types and empty tables.
3. Record the mismatched telemetry declaration/schema orders and define
   the new IDs consistently across metadata and consumers; retaining old IDs
   is not required. Include repeated union payload types:
   Replication and Recovery are distinct tags for the same Record type.
4. Map each reflection operation to its consumer, replacement descriptor or
   operation, and verification case. Record separate memory/PostgreSQL/SQLite
   type-support matrices rather than assuming equal support.
   Inventory the canonical `reflection::Schema` binary contract needed by
   these consumers. Retain BFBS as the portable reflection representation;
   do not replace it with only a private native descriptor format.
5. Capture independently generated canonical fixtures and their decoded
   expectations, including filtered rows, defaults, IP, nested buffers, and
   independently defined schema-evolution variants for codec coverage, with
   no requirement to read historical application data. Document canonical
   versions and fixture provenance in the interoperability harness. Align
   the canonical baseline with the current `Zfb` dependency (Q15):
   `flatbuffers >= 24` in `configure.ac`, using the installed C++ library/
   headers and matching `flatc`. The inspected workspace has version 25.12.19.
6. Record the active compiler/build configuration and representative baseline
   results for the existing `IOBuilder`: small rows, strings, scalar/struct/
   table vectors, telemetry envelopes, nested replication records, reflected
   reads/writes, and view printing. Record allocations, copies, size, and
   latency distributions with repeated measurements.
   Audit `Zfb::IOBuilder` together with `ZiIOBuf::alloc/realloc/free` and the
   current canonical `vector_downward` implementation. Capture initial
   capacity/headroom, exact growth requests, allocation count/size, copied
   head/tail bytes, alignment, and detach/rebind ownership transitions.
   The custom pooled builder is the baseline, not a default heap-backed
   `FlatBufferBuilder` benchmark. Reuse suitable current binaries/results;
   if a focused baseline target needs rebuilding, queue it in M1 before
   deleting the corresponding source. Do not reconfigure or build a second
   checkout merely to collect a baseline.

Keep baseline source snapshots/results as evidence so the final tree can
delete `zfb`; do not create a separately configured build. Existing `.fbs`
files may serve as independent interoperability fixtures while they are
being replaced; they must not remain production schema inputs.

**Interim acceptance:** every inventoried use has a planned replacement or
justified deletion; wire expectations are independent of the new codec.
Baseline provenance and reusable results are recorded; missing measurements
have a concrete M1 target before old implementation removal. Legacy
compatibility is not a gate.

## Slice 2 — Implement metadata and reflection together

1. Implement FlatBuffers field properties in `ZuFieldProp::FB`: `FieldID<N>`
   for the numeric wire ID, `ID<Name>` for a textual name override, and
   `Align<N>` for forced alignment. These spellings distinguish numeric IDs
   from the existing format-specific string-ID idiom. Derive ordinary wire
   representations from the existing field type and traits. Implement
   type-level table/inline-struct/union traits through the established
   ADL extension mechanism without another field-list declaration.
   Follow `ZfJSON`'s `JSON::ID<...>`/`GetID<Field>` pattern for the optional
   FlatBuffers field-name property, resolved from the selected facet's
   effective properties (Q7). Use an override verbatim; otherwise apply the
   exact current `ZtCase::camelSnake` conversion to `Field::id()`. Resolve
   names at compile time for BFBS generation. A name override changes neither
   the object member name nor its numeric wire ID. Use the same resolved
   names for BFBS, textual FBS, and reflected field associations.
   Map canonical `required` to the existing `Required` property rather than
   introducing an FB-specific required marker. Apply canonical restrictions
   on eligible wire kinds and diagnose unsupported property combinations.
   Express forced alignment through a `ZfFB`-specific field property;
   validate it at compile time and propagate it through layouts, buffer
   allocation, nested roots, reflection, and interoperable schema export.
   On a table's vector field, `FB::Align<N>` aligns vector data. On a member
   of an inline struct, it specifies the enclosing struct's canonical
   `force_align`: retain natural member offsets, increase the struct's
   alignment and trailing padding, and propagate its stride to vectors and
   parents. Resolve multiple such annotations to their maximum and reject
   a result below the struct's natural alignment. Export that annotation on
   the struct declaration/object metadata, not as a fictitious per-member
   alignment. Other table field kinds cannot express local forced alignment
   in the canonical schema and are diagnosed.
   Extend the `ZuStruct` accessor taxonomy with metadata-only `Skip` (Q6),
   inherited by `ZfStruct`. Retain field name, explicit type information,
   properties/default, and declaration position without requiring a C++
   member or generating object accessors. Existing accessor macros infer
   `T` from member/getter expressions; the skipped form must obtain its
   type from the declaration instead. Implement the compound accessor
   `(ID, Skip, T)` in `ZuStruct`; `ZfStruct` keeps its existing field type-code
   argument as well. For example, a metadata-only integer field has the form
   `((retired, Skip, int32_t), (FB::FieldID<2>), Int32)`. The explicit `T`
   supplies type information without member lookup, including for UDTs.
   Audit tuple/key extraction, facets, object printing/JSON, constructor and
   operation filters, and `ZfVField` factories: exclude `Skip` before they
   instantiate object accessors, including operations currently using `All`.
   Apply this at the shared framework level for every format's `load`/`save`,
   including JSON, CSV, ASN.1, URI, tree/config, CLI, and other field consumers.
   Keep a complete declaration typelist for schema generation and use a
   shared compile-time active-field filter for object operations. Apply it
   before tuple formation, matcher generation, or accessor/thunk instantiation.
   A `Skip` field
   is simply skipped and must never have `get` or `set` invoked. Retaining
   its metadata must not expose it as an active field to format loaders.
   Do not simulate absence with dummy getters or no-op setters. Existing
   property lists remain metadata; diagnose only combinations that cannot
   describe the supported canonical wire schema, not a new blanket blacklist.
   Keep skipped fields in the complete schema metadata. `ZfFB` emits them as
   deprecated in BFBS and textual FBS, omits them when saving objects, and
   ignores them when loading objects. Native object-field associations must
   tolerate schema entries with no corresponding object field.
2. Resolve IDs once from the complete wire declaration using canonical rules
   (Q5): all table fields use implicit declaration-order IDs or all declared
   fields specify explicit IDs. Count each union's hidden discriminator slot;
   an explicit union ID names its value slot, with the discriminator at ID-1.
   Resolved slots must be unique and contiguous from zero. Diagnose mixed
   declarations, collisions, gaps, and overflow at compile time, independently
   of whether schema export is requested. Facets, key projections, adapters,
   and save filters reuse the resolved original IDs without renumbering;
   filtered payloads may omit fields from that complete schema.
   `Skip` entries participate in numbering, including the all-explicit rule
   and a skipped union's two slots. These are declared deprecated fields,
   not unoccupied gaps; later fields retain their resolved IDs.
3. Keep wire IDs separate from constructor ordinals, field-list indexes,
   vtable byte positions, inline member offsets, and projected tuple indexes.
   Diagnose invalid metadata at compile time with concise field context.
4. Reuse existing Zf default-value logic (Q9): `Deflt` if set, otherwise the
   existing type-specific fallback used by other Zf adapters, through
   `Field::deflt()`. Do not add a separate FlatBuffers wire-default property or
   silently
   substitute zero/false for an existing Z default. Map representable defaults
   to canonical scalar defaults consistently across omission, absent-field
   reads, reflection, and both schema exports. Define physical presence
   separately from the value returned for an absent field. Include
   `UINT64_MAX` sequence defaults, version 1, absent/empty strings, inline
   missing values, and forced-present scalars.
   An absent inline value produces the defined null/default value using
   existing Zf default resolution (`Deflt` if set, otherwise the normal
   fallback); an absent `Required` field is an error (Q11).
5. Compute inline sizes, alignments, padding, and scalar endian handling
   from the resolved wire traits rather than native
   object size or the pointer-shaped API of an inline-struct vector.
   Support ordinary 32-bit FlatBuffers offsets; wider offset variants are
   excluded. Implement size-prefix selection as a compile-time parameter of
   `load` and `save`, propagated to root access, verification, and finishing.
   Provide both prefixed and unprefixed instantiations without runtime mode
   detection. This is root framing and is independent of field metadata.
6. Implement thin borrowed views for canonical BFBS types, objects, fields, vector
   elements, enums, union alternatives, and root information. Include name/ID
   lookup, defaults, slots/layouts, and child relationships. Generate direct
   associations to existing `ZfVField`/key fields in the stores' existing
   per-field records. Initialize them once at table/store setup using the
   resolved field name or ID. Do not duplicate the schema graph, create
   per-field heap objects, or generate unused object-access thunks. Native
   process pointers stay outside the portable schema bytes.
   Normalize Z and FlatBuffers metadata by semantic equivalence in both
   directions (Q8). Map equivalent meanings; omit metadata with no counterpart
   rather than preserving it through custom attributes or opaque extensions.
   Keep native metadata required by Z consumers locally, without promising
   its recovery from BFBS/FBS after translation. Do not equate Z multi-key/
   grouping semantics with a canonical sorted-vector key solely by name.
7. Implement the compile-time-specialized BFBS streaming generator below, using
   the same resolved traits as typed operations.
   Handle recursive type descriptions without a mutable runtime registration
   pass. Use BFBS object/enum indexes for schema references and the existing
   store ABI; do not introduce a cross-module type-identity subsystem.
   Supply qualified schema names through the ADL wire-type traits as
   compile-time strings, separate from field lists. For migrated types use
   their application namespace/type name, expressed with canonical dot
   separators; use `Zf` for core wire helper types and `Zi` for IP helpers.
   Declare distinct explicit names for distinct template wire types and
   reject collisions in the reachable schema graph. Never derive names from
   RTTI or compiler-mangled spellings. Existing Z field metadata coexists
   with an owned schema buffer; a span obtained from a buffer destination
   borrows that buffer. Template schema saving on `S` with `operator <<`
   as the destination contract; do not require every destination to expose
   buffer storage, capacity, seeking, or a span accessor (Q12).
8. Implement an erased API over the same wire primitives for root access,
   verification, field presence, scalar/span/vector reads, and dynamic table
   construction. Map all three stores' inventoried operations to that API.
   Use BFBS field views for wire operations and existing native
   associations only where Z fields/keys/accessors are required. Keep schema
   indexes, numeric wire IDs, and native field indexes explicit in those
   associations; `Skip` has schema metadata but no native object field. Do not
   template the stores on application row types.

Use compile-time-known field counts to size unavoidable metadata. Use
`ZtScratch` over a heap-identified Z array for temporary runtime field/offset
work; do not allocate another schema representation. Keep builder storage
workload-aware and tunable through existing Z allocators.

### In-memory BFBS generation

1. Describe the required canonical reflection wire types and IDs using
   ordinary Z metadata within `ZfFB`. Check these against the canonical
   reflection schema without introducing canonical generated headers or
   `flatc` into production.
2. Resolve the reachable type graph, object/enum indexes, keyed-vector
   ordering, names, defaults, layouts, union tags, and schema feature metadata
   at compile time. Represent recursive type relationships by indexes.
   Keep wire field IDs distinct from reflection-vector positions.
   Preserve the canonical ordering of BFBS metadata vectors for name lookup;
   this internal reflection requirement does not add sorted application
   vectors or their public sorting/search helpers to the feature scope.
3. Generate a specialized emission sequence using templates, `if constexpr`,
   and compile-time unrolling/layout computation. Stream that sequence into
   the supplied `S` using `operator <<`. Use a buffer-backed stream for the
   normal in-memory reflection path. Do not use runtime field/type switches, schema
   graph discovery, conditional metadata emission, or an interpreted generic
   descriptor walk to decide which schema bytes to emit.
4. Use compile-time size/layout information to emit offsets, tables, vectors,
   and strings in output order through `S << ...`; the stream need not seek
   or expose mutable storage. Pre-reserve only where the chosen buffer
   destination supports it. Factor wire primitives
   with the payload codec where useful. This is specialization of executable
   streaming code, not a requirement to constant-evaluate the entire output
   into an embedded blob. Buffer capacity/growth, error handling, byte-copy
   primitives, and all actual data-streaming operations may branch at runtime,
   including within the emission call path (Q2). Pre-reservation is an
   optimization, not a requirement to move all failure handling before
   emission. The schema generator's metadata decisions are compile-time fixed.
5. Finish a canonical `reflection::Schema` buffer with the `BFBS` identifier,
   root, object/enum references, field types/defaults, and inline layouts.
   For a buffer destination, expose its completed bytes as `ZuBSpan` and
   native schema views. A file/network/console destination need not return
   an in-memory span. Preserve exact bytes and lengths, including embedded
   zero bytes, through the existing span/printing machinery; do not format
   binary scalar values as text or rely on null-terminated strings.
6. Each `Table` generates its BFBS buffer during construction and owns that
   buffer for its lifetime (Q13). Stores and reflection consumers borrow spans
   and views from it. Complete all dependent store callbacks and close work
   before destroying the table and its buffer. Do not defer generation until
   store open, regenerate on row access, or share ownership through a schema
   registry, DB-wide cache, or process-global cache.
7. Pass the span directly to reflection consumers. The BFBS library function
   can write those same bytes to a caller-supplied file stream; no file generation,
   loading, or persistence participates in the normal reflection path.

Implement emission using the common wire primitives from Slices 3–4; the
generator contract and type graph are established here. Confirm with code
inspection and a representative compiled specialization that runtime schema
dispatch/conditional metadata emission is absent. Exempt actual streaming
operations, buffer management, error handling, and byte-copy primitives from
that inspection criterion (Q2). Runtime verification and access to
input-selected fields remain ordinary data-dependent operations.

**Interim acceptance:** source review covers numbering/filtering/layout
diagnostics and prepared compile-time cases; the BFBS stream/ownership
contract and erased store mapping are explicit. Execute the cases and inspect
a representative compiled emitter at M1, including canonical BFBS checks,
before broad dependent migration.

## Slice 3 — Implement core borrowed reads and verification

1. Implement little-endian scalar reads, root offsets, signed vtable
   displacements, field lookup/presence/defaults, strings, vectors, inline
   structs, nested tables, and tagged union access for the initial slice.
   Implement both compile-time-selected size-prefix modes on `load`; the
   external-data verification path checks the canonical prefix and buffer
   bounds before access. Trusted loads do not repeat those checks.
2. Provide typed views and erased tables over caller-owned buffer spans.
   Separate bounded verification entry points from trusted access. Verify
   all external data before loading. Data generated and parsed within the
   same process, including generated BFBS spans, uses trusted access without
   validation or re-verification. Trusted operations must not repeat checks
   already established at ingress for external data either (Q4).
3. Expose strings/bytes as borrowed `ZuCSpan`/`ZuBSpan`; retain lengths and
   presence separately. Supply indexed vector views and lazy transformed
   access instead of mandatory contiguous conversions or owning arrays.
4. Implement verification using overflow-safe bounds arithmetic before
   pointer formation. Cover root/vtable/table bounds, both vtable directions,
   scalar alignment, vector counts/strides, string termination, union
   relationships, and bounded nested traversal.
   Include missing `Required` fields, forced alignment, and size-prefix
   bounds in typed and reflected verification.
   Apply canonical deprecated-field behavior to `Skip` entries, without
   attempting C++ object access for them.
5. Offer both typed and descriptor-driven verification. Unknown fields and
   alternate valid physical layouts must remain readable. Keep application
   enum/tag restrictions outside structural verification where appropriate.
   Provide native BFBS schema views and verification over spans, including
   canonical-produced schema buffers, without reconstructing a second schema
   graph or depending on canonical reflection classes.
6. Prepare cases against independent canonical fixtures, including shared
   vtables, schema evolution, and malformed/truncated external input. Reuse
   the module's test harness; do not add a standalone fuzz target or a second
   validation framework. Execute these cases together at M1.

Do not claim that validating an external envelope's byte vector validates its
embedded row. Before loading that row, verify it against the selected nested
root descriptor unless the ingress verification already covered it. Locally
generated nested payloads need no redundant verification (Q4).

**Interim acceptance:** source review establishes shared typed/erased read
primitives, explicit ingress verification, and borrowed lifetimes. Fixture
and malformed-input cases are ready for M1; passing execution is required
there. No reader assumption depends on the new writer's chosen layout.

## Slice 4 — Implement the writer and isolated nested roots

1. Implement a backward builder over an explicit storage policy, with an
   ordinary Z-allocated implementation usable inside `zf`. Define growth,
   capacity limits, finish/reset behavior, and failure/cleanup contracts.
   Offsets survive growth; document raw-pointer invalidation.
   Reproduce the arena behavior currently supplied by canonical
   `vector_downward` to `Zfb::IOBuilder`: payload grows down from the end,
   scratch grows from the front, and growth preserves both live regions.
   In the inspected dependency, capacity increases by
   `max(requestedLength, oldCapacity / 2)` (initial capacity when empty),
   then rounds up to the buffer alignment. Carry that baseline policy into
   the native implementation and avoid a second independent growth factor
   in the storage adapter. Changes must demonstrate no performance regression.
   Expose save operations through the established `template <typename S>` /
   `operator <<` convention (Q12). Keep builder storage and any necessary
   staging internal to encoding; general stream destinations need no builder
   API, random access, or reserve operation. Use caller-scoped `ZtScratch`
   over a heap-identified Z array for temporary synchronous stream staging,
   with heap fallback and the same downward growth contract. Storage escaping
   the call must have an actual owner; do not return a span into scratch.
   Preserve the direct pooled-buffer path without an intermediate copy.
2. Build tables, vtables, strings, primitive vectors, inline structs/vectors,
   nested tables, and unions. Support default omission, forced presence,
   empty tables, and typed or erased prebuilt offsets.
   Enforce `Required` presence for eligible fields. Honor the forced-alignment
   field property and propagate the resulting buffer alignment requirement.
   Finish with or without the canonical size prefix according to `save`'s
   compile-time parameter; retain the chosen alignment in both modes.
3. Expose direct fill into uninitialized scalar/inline vector storage and
   callback construction for table/string vectors. Avoid intermediate
   owning objects and avoid default-initializing storage before overwrite.
4. Implement runtime descriptor-based table construction over the same wire
   primitives, supporting arbitrary field subsets and original field IDs.
   Complete the compile-time-specialized BFBS emitter over these primitives
   and exercise buffer-to-span-to-reflection consumption without file I/O.
5. Implement a nested-root scope for byte-vector FlatBuffers. Isolate its
   vtable reuse and any shared-object state from the enclosing builder;
   prevent references across the nested span boundary in either direction.
   Finish the nested root with its own alignment before resuming its parent.
6. Support copying a complete existing nested payload into new vector
   storage with the alignment required by its root contract. Require known
   alignment metadata where byte-vector contents alone cannot provide it.
7. Carry forward the current builder's vtable reuse behavior, keeping its
   state in the arena/scratch and clearing it on reset/reuse. Isolate that
   state across nested-root boundaries. Readers accept valid canonical
   shared-vtable layouts. Do not add alternative writer strategies or an
   independent deduplication cache.

**Interim acceptance:** review arena growth, offset arithmetic, nested-root
isolation, and detach/reset ownership against the captured implementation.
Queue bidirectional canonical checks and nested extraction after prior
content, repeated nesting, growth, vtable reuse, and copying for M1.

## Slice 5 — Integrate Z fields, transformations, and I/O storage

1. Adapt views to `ZuFields`, `ZuUnder`, `ZuStructKey`, and read-only field
   access. Preserve original metadata, facets, span-valued key types, and
   custom formatting properties.
2. Implement construction, load, allocation, and placement construction
   using ordered `Ctor` fields and post-construction `Init` fields. Retain
   additional caller-supplied constructor arguments and read-only rules.
   Apply Q11 consistently to typed and reflected loads: absent inline values
   produce their defined null/default unless `Required`, in which case
   absence is an error. Diagnose missing required fields at external-input
   verification and enforce required presence during object serialization;
   do not add a verification pass to trusted internal reads (Q4).
3. Implement `Save`, `Upd`, `Del`, and `All` behavior from existing filters.
   Apply these when serializing objects; preserve synthetic wire fields and
   partial-record semantics. For already serialized spans, the original
   serializer has already made inclusion/exclusion decisions. Preserve its
   result without rerunning field filters, default omission, or other
   object-to-wire selection logic. Unchanged payload copies use `memcpy` of
   a complete self-contained span (Q10), preserving all payload bytes without
   per-field reconstruction. `Skip` has no `get`/`set` operations in any
   format's object load/save (Q6); raw byte copies do not visit fields.
4. Connect views to `ZfStructPrint`, generic Z print streams, `ZuVStream`,
   and facet-aware `ZfJSON`. Verify span-to-stream output without an owning
   row or intermediate formatted string.
5. Complete transforms for 128-bit integers, fixed/decimal values, time/date,
   bitmap tables, strings/bytes and their vectors, nested objects, and
   custom vectors of tables. Keep primitive UDT support such as packed
   `ZuSemVer`; byte-vector vectors retain their wrapper-table encoding.
6. Implement `ZiFB` storage integration with `ZiIOBuf`, preserving tuneable
   pools, front reservation, size/skip/length, buffer owner, detach/reuse,
   growth, and exceptions/abandonment before and after finish.
   Reproduce the custom `Zfb::IOBuilder` mechanics below; a generic
   allocate-serialize-copy-to-IOBuf adapter does not satisfy Q16.
7. Implement `ZiIP` traits and transforms in `zi`: fixed IPv4/IPv6 byte
   arrays, union tags, and network-byte-order address handling. Generate
   their reflection through the same extension interface.
8. Move useful `zfb` test cases to their owning layers. Prepare checks
   for view printing/JSON, presence-sensitive copying, allocation-free key
   extraction, and snapshots that outlive the source builder.

**Interim acceptance:** source review covers a representative row, IP union,
nested message/vector, erased store-style read/write, and all-format `Skip`.
Core tests require neither `zi` nor canonical headers; queued `zi` tests
cover headroom, growth, detach/reuse, and cleanup. Execute together at M1.

### Required IOBuilder behavior

Use `zfb/src/Zfb.hh::IOBuilder` as the concrete implementation reference.
Split its codec/arena mechanics into `zf` and its `ZiIOBuf` integration into
`zi`, while preserving these contracts. These are prescribed implementation
constraints, not open requirements: honor canonical wire layout, relative
offsets, and alignment while reproducing the current allocation and ownership
behavior without regressions. Make the mechanics explicit to prevent
implementation pitfalls even where they follow from the required format or
existing buffer contract.

| Contract | Required behavior |
| --- | --- |
| Allocation | Build directly in the supplied `ZiIOBuf` storage, using its pool/jumbo allocation path. Derive initial usable capacity from aligned buffer size minus reserved `skip`; reset length for construction. Preserve existing tuning and avoid a second owning payload buffer or gratuitous initialization. |
| Growth | The codec selects the new arena size. As current `realloc<grow>` does, pass that size through the I/O adapter without applying `ZiIOBuf`'s default growth factor again. Preserve front scratch and back payload regions; use available storage where possible and copy only live regions when reallocating. Keep offsets valid and account for reserved headroom. |
| Ownership transfer | Detach the same `ZmRef<ZiIOBuf>` without copying payload bytes. Translate the arena's returned offset into `skip` and set `length` to the serialized size, clear builder state, and prevent later builder cleanup from freeing detached storage. Support reattachment/reuse and retain the existing owner metadata/lifetime contract. |
| Alignment | Preserve the current 8-byte baseline, aligned usable capacity, and aligned front reservation. Propagate greater alignment required by Q3's forced-alignment property through allocation, growth, root/nested placement, size prefixes, and copies. Do not accidentally align only the offset while leaving the storage base misaligned. |
| Cleanup | Reset/free the arena while the owning I/O buffer is still alive, including exceptions or abandonment before finish/detach. Free the correct allocation base after accounting for `skip`; maintain the ordering needed when rebinding a new buffer. |

Keep the templated stream contract (Q12) without introducing extra allocation
or payload copies into this existing direct-output path. Exercise exact-fit,
growth, reserved-headroom, detach/rebind, abandonment, and stronger-alignment
cases in the planned verification. Measure allocation/growth/copy behavior
alongside latency so an apparently equivalent API cannot conceal a regression.

## Slice 6 — M1: interoperability and performance

Prepare the new targets' owning `Makefile.am` inputs and regenerate only
what is needed, keeping the current configure settings. Collect any queued
baseline evidence before replacing its owning libraries. Then incrementally
build the changed `zu`/`zf`/`zi` owning libraries in dependency order and the
new isolated test/interop/bench targets in one batch. Run the cases queued
by Slices 2–5 here; existing application/store targets wait until M2.

1. Build a separately invoked canonical C++ harness for the complete
   supported feature set, aligned with the current `Zfb` FlatBuffers
   dependency (Q15). Use its C++ headers/library and matching `flatc`, record
   the actual version, and retain the existing `flatbuffers >= 24` dependency
   floor in the interop harness. Do not introduce an independent version pin
   or second-language acceptance gate.
2. Exercise both encoding directions using typed and reflected ZfFB reads.
   Include defaults/presence, both vtable directions, reordered fields,
   unknown fields, evolution, inline arrays/structs/vectors, duplicate union
   payload types, forced-present mutation, and filtered records.
   Cover `Required` rejection/acceptance, forced field alignment, and both
   size-prefix instantiations, including truncated or invalid prefixes.
   Cover `Skip` with no C++ member/accessors, retained IDs/types/defaults,
   canonical deprecated metadata in both exports, payloads containing or
   omitting skipped fields, and later live fields retaining their IDs.
   Cover facet-specific field-name overrides, the existing default name
   conversion (including acronyms), `Deflt`-derived scalar defaults, equivalent
   metadata mappings in both directions, and deliberate loss of unmapped
   metadata. Do not require lossless round trips for non-equivalent metadata.
   Apply Q3's scope consistently to typed operations, reflection, exports,
   and fixtures; excluded features are not gates.
3. Run independent nested-buffer extraction/copy tests. Add application
   contracts to this harness as their declarations migrate in later slices.
4. Implement both schema-output library functions in `ZfFB`, as specified below.
   Canonical test schemas are derived artifacts; retain independent fixtures
   and contract expectations so an exporter/metadata mistake cannot confirm
   itself. Do not add exporter/codegen steps to production builds.
   Test BFBS independently of textual export: stream a schema into a buffer,
   pass its span directly to canonical schema verification/accessors, and use
   canonical reflection to verify/read matching payloads. Feed canonical BFBS
   spans into native schema views and reflected operations. Compare schema
   semantics, names, IDs, defaults, indexes, union tags, and inline layouts
   with independent expectations, not byte identity. Include ownership tests
   and evidence that the generator has no runtime metadata branches.
5. Measure the new slice against the Slice 1 baseline using equivalent
   verification, field filters, default policy, and storage tuning. Include
   borrowed access, reflected operations, object construction, and printing.
6. Report message size alongside latency and allocation/copy counts, plus
   material code-size or compile-time effects from the milestone builds. First
   remove any regressions against the existing arena and consumer paths.
   Add no speculative reservation, caching, layout, or deduplication strategy
   merely because the codec is being rewritten.

### Schema-output library functions

Provide binary BFBS and textual FBS output as `ZfFB` library functions
templated on generic destination `typename S`, using `operator <<` (Q12/Q14).
The caller compiles in the relevant application's `ZfStruct` metadata and
selects the root type/facet. There is no standalone command-line exporter or
fixed application schema catalog in `zf`.

- Binary BFBS output uses the specialized generator to stream canonical
  schema bytes to `S`. The usual buffer destination exposes the completed
  span; a file destination can persist equivalent bytes as `.bfbs`. An
  already generated span can simply be written to the stream. Do not add a
  separate binary encoder or re-verify locally generated output.
- Textual FBS output renders the same resolved metadata as one self-contained
  schema per selected root, with all reachable types and namespaces. Preserve
  field names/IDs, wire order, defaults, enums, union tags/aliases, inline
  layouts, fixed arrays, deprecated fields, and referenced types. A file
  destination can persist this output as `.fbs`.

Document the library entry points and demonstrate instantiation from
application/test code. Application declarations remain in their owning
modules; `zf` must not depend on `zi` or application libraries. Selecting
types is a C++ call-site choice, not command-line root discovery. File output
uses the caller's stream; persistence is optional and no exported file is a
normal build or reflection prerequisite. Arbitrary external BFBS-to-text
conversion remains outside the requirement (Q4).

Exercise both functions in the interoperability harness. Canonical tooling
must accept binary output and compile textual output into a schema agreeing
semantically with the BFBS span and independent fixtures. Cover generic
stream destinations, their failure behavior, and reproducibility for the same
metadata. The functions use native `ZfFB`; canonical libraries and `flatc`
remain test-harness dependencies. Interop verification provides evidence of
correctness without adding verification to trusted production output paths.

**Exit gate:** required interoperability passes and the intended API serves
the difficult consumers. Resolve performance regressions before widespread
migration, particularly in allocation, growth, ownership transfer, and
alignment. Do not justify a regression by removing dependencies, omitting
validation or fields, or comparing against a generic allocator instead of
the current custom `Zfb::IOBuilder`.

## Slice 7 — Cut over database core and reflection dependents

This changes a shared store ABI and metadata declarations used by application
headers. Coordinate Slices 7–9 as one consumer migration series: intermediate
working states may require dependent edits before a full-tree build passes.
Do not add dual reflection APIs, generated-type aliases, or forwarding shims
to manufacture intermediate source compatibility. Defer rebuilding all
enabled stores and applications against the final interface to M2.

1. Replace `reflection::Schema` in `ZdbStore::open`, `AnyTable::rowSchema`,
   mocks, tests, and all backend implementations with the `ZfFB` BFBS span/view
   contract and field associations. During each `Table` constructor, stream
   its schema into its own heap-identified byte array and pass borrowed views
   directly across this boundary.
   Preserve binary schema reflection; do not substitute only a private native
   descriptor. Reuse existing per-field store records and associate native
   fields/keys once using resolved names or IDs; retain established SQL names
   and schema-buffer lifetimes. The store schema handle carries the borrowed
   BFBS view and a compiled native-ID-to-BFBS-field-index resolver. Use
   `ZuMatcher::exact` and `ZuSwitch` with the schema emitter's sorted slot
   typelist; index the BFBS field vector directly, without scanning either
   native names or wire IDs (GUIDELINES: Compile-time data and matching). Populate existing
   backend XField records through this resolver once at open, so `FB::ID`,
   explicit field IDs and `Skip` cannot be confused with SQL column naming.
   SQL names retain their existing native-ID derivation. Do not add a second
   mapping container merely to replace the existing one-time lookup.
2. Migrate `ZdbMsg` and its schema-only types to ordinary declarations and
   `ZfFB` views/building. Preserve Heartbeat, Replication, Recovery, Commit,
   nested row roots, union tags, and the custom four-byte framing.
3. Migrate `Zdb.hh`, `ZdbBuf.hh`, and `Zdb.cc`: row serialization/filtering,
   verified and trusted roots, placement construction, buffer-cache key
   adapters, selected key/row tuples, nested recovery copying, and direct
   `rowPrintFB`/record/buffer diagnostics.
4. Migrate the memory store's reflected reads and dynamic writes. Preserve
   tuple projection indexes, group prefixes, ordering, update/delete filters,
   buffer lifetimes, and recovery envelopes.
5. Migrate PostgreSQL field resolution and parameter/result conversion.
   Preserve OIDs, binary endian/array/bitmap/IP encodings, borrowed scalar
   text/bytes, query-send scratch lifetime, and complete serialization before
   releasing `PGresult` storage. Use explicit union metadata rather than
   calculating a discriminator with `field.offset() - 2`.
6. Migrate SQLite reflected access and construction. Preserve ordered blob
   encodings, text/blob emptiness versus SQL NULL, `SQLITE_STATIC` borrowing,
   scratch/statement guard ordering, and completion before column storage
   is reset. Test every mapping actually supported by this backend.
7. Migrate saga roots/catalogs and runtime-selected payload reconstruction,
   including the extra base constructor argument and owned payload copy.
   Update saga examples, mock stores, unit/integration tests, and benchmark
   consumers of the changed interfaces.
8. Migrate database telemetry declarations in their owning headers with
   explicit IDs/enum values as needed. Keep lower-layer header dependencies
   independent of `ztc` binaries; resolve shared metadata with an acyclic
   header arrangement.

**Interim acceptance:** review all memory/PostgreSQL/SQLite mappings and
shared ABI declarations, including queries, projected keys, updates/deletes,
recovery and saga replay. Cache lookup and diagnostics use borrowed buffers.
All enabled store libraries have matching source interfaces; build and
exercise their actual erased consumer paths at M2.

## Slice 8 — Cut over telemetry, routing, and dashboard

1. Replace `ZtcFB` declarations with ordinary `ZfStruct` metadata and the
   `ZfFB` traits needed for resolved wire IDs, defaults, and primitive UDTs.
   Use normal Z enum declarations/maps as the authoritative enum source.
2. Replace generated-only envelope/union/empty message types and direct
   builders. Compose prebuilt offsets in `saveMsg`/`saveTelemetry` through
   the new API. Remove `ZfbType` bindings and generated enum iteration/names.
3. Migrate `ZtcApp`, agent, and hub consumers: `All` serialization of synthetic
   telemetry fields, direct payload copying, optional IDs, semantic
   validation, sequence-slot mutation and forced presence.
   Replace `copyTelemetry`'s field-wise reconstruction with copying unchanged
   serialized bytes. `agentFrame` supplies new `subID`, `deviceID`, and
   `agentGeneration`, so construct the routing envelope separately. Pass the
   backing span/length and ownership through this path. Make the forwarded
   body a self-contained nested root, including its union tag, carried by a
   byte vector in the routing envelope. Propagate that schema change through
   senders, hub, and dashboard. Copy the complete nested span with `memcpy`,
   preserving required alignment and all internal offset targets. A table
   pointer or inline table size alone is not a copyable payload boundary.
4. Preserve framing and receive state handling, message-size limits, and
   distinction between body and framed spans. Retain owner-held buffers
   across Rx/Tx posts and asynchronous callbacks.
5. Preserve binary stream transmission (`tx << span`, binary opcode, flush)
   and alert-file persistence/replay. Do not introduce materialization or
   an extra serialization pass for stream output.
6. Migrate filter/group types and daemon headers that reference generated
   enums without directly mentioning `Zfb`.
7. Migrate dashboard request construction, enum lookup, typed dispatch,
   borrowed-key lookup, construction/update, and verified-buffer handoffs.
   Delete orphaned old dashboard schemas without reviving that protocol.
8. Update telemetry and dashboard tests, wire/load integration programs,
   examples, and application telemetry probes. Include an explicit dashboard
   target in M2 where its top-level traversal is disabled.

**Interim acceptance:** review schema/default/presence behavior, synthetic
fields, nested payload boundaries, routing, mutable sequence slots, streaming,
replay, and dashboard lookup. Their prepared tests and canonical checks of
the migrated declarations/envelopes execute at M2.

## Slice 9 — Finish application declarations and dependent coverage

1. Migrate `zum` rows, JSON facets, version defaults, administrative saga
   images, maintenance payloads, and custom vectors of deletion records.
   Replace ADL transformer bindings with the new extension contract.
2. Migrate `zdf` series/block declarations and stores, retaining grouped/
   descending keys and block byte spans. Exercise persistence round trips.
3. Update all dependent tests, examples, integration utilities and includes,
   including code using only a generated type or transitive declaration.
4. Verify retained image/payload ownership after builders and SQL results
   expire. Cover large vectors, empty payload cases, and nonzero defaults.
5. Reconcile the Slice 1 inventory against actual migrated consumers. Mark
   each entry with its new owner/API and the M2 check that exercises it.

**Interim acceptance:** source and ownership review covers every inventory
entry; no live consumer needs old Zfb declarations or reflection. Consumer
builds/tests remain queued for M2, with missing coverage recorded explicitly.

## Slice 10 — Remove zfb, canonical production dependencies, and codegen

1. Delete the `zfb` source/test module, installed headers/library definitions,
   generated-type bindings, unused file/sorted-vector convenience wrappers,
   and all compatibility remnants. Useful tests already have new owners.
2. Remove live production `.fbs` sources, externally generated headers and old
   embedded schema artifacts, `scripts/fbscodegen`, and their obsolete build/
   distribution prerequisites. Replace old BFBS artifacts with schema spans
   streamed in memory by specialized `ZfFB` generators. Provide the binary/text
   schema-output library functions for caller-selected streams. Keep independent
   canonical test oracles in the interoperability area.
3. Update top-level/module `Makefile.am`, `configure.ac`, includes, links,
   package metadata and distribution. Remove `FBS_CFLAGS`, `FBS_LIBS`, the
   global pkg-config requirement, and `zfb` traversal/configuration entries.
   Include link-only dependents such as `zgtk` and disabled dashboard rules.
4. Install/export the new core and I/O headers and shared-library symbols.
   Configure interop dependencies only for separately requested harnesses;
   missing canonical tools must not prevent ordinary production builds.
5. Prepare Autotools input changes for regeneration at M2 with the retained
   settings. Audit include dependencies, link inputs, installation lists,
   and generated prerequisites for stale `zfb` artifacts. No separate clean
   build or configuration without installed canonical tools is scheduled.
6. Update `README.md`, `CODEBASE.md`, module docs, test entry points, and
   obsolete `ZuStruct`/guideline references. Preserve historical documents
   where appropriate; distinguish historical mentions from live dependencies.
7. Search the entire tree again for `Zfb`, `zfb`, `flatbuffers`, `reflection`,
   generated-header patterns, codegen invocations and link flags. Classify
   hits; only documented history, the goal/plan, and explicit interop oracles
   may retain relevant legacy references.

**Interim acceptance:** the reference/build-input audit shows no production
dependency on `zfb`, canonical headers/library, external schema compiler, or
its generated artifacts. BFBS comes directly from an in-memory span. Confirm
this with M2 compile/link/dependency evidence; core `zf` tests link without
`zi`. Existing installed artifacts must not mask a retained dependency.

## Slice 11 — M2: final integration and acceptance

After all consumer and build-input edits are complete, regenerate only the
required Autotools products with the recorded settings. Incrementally build
the changed owning libraries in dependency order, then all affected enabled
dependents and tests in one batch using the job limits above (`-j4` for
Zum and beyond, and for top-level traversal). Include dashboard explicitly
where traversal omits it. Build affected `all` targets before `make test`;
do not compile unrelated targets or introduce configurations beyond the
debug correctness/memory build and required Clang release performance build.

Run the consumer matrix in `goal.md` and retain the following evidence:

| Gate | Evidence |
| --- | --- |
| Metadata and views | Numbering/layout diagnostics; projection and constructor independence; all-format `Skip`; direct keys, printing/JSON, and byte-preserving copies. |
| Runtime reflection | Streamed BFBS spans, borrowed schema views and existing field associations, erased access/build/verification, and actual memory/PostgreSQL/SQLite consumer results. |
| Schema generation | Compiled specialization without runtime metadata decisions (Q2 exempts streaming mechanics); direct buffer-to-span consumption; construction-time table ownership. |
| Interop exports | Binary output accepted canonically; textual FBS compiled canonically and compared with BFBS semantics and independent fixtures. |
| Canonical interoperability | Payload/BFBS in both directions against the current canonical dependency; recorded versions, evolution, mutation, independent nested extraction/copy, and migrated application schemas. Reuse M1 evidence where unchanged. |
| Ownership and I/O | Pool growth/headroom/detach/reuse, SQL borrowing, retained snapshots, framed binary sends, alert replay and teardown. |
| Application migration | Actual database queries/replication/recovery/sagas, telemetry/routing/dashboard, `zum` images/custom vectors, and `zdf` blocks. |
| Dependency removal | Current incremental compile/link/dependency and install-rule audit shows no production canonical or `zfb` inputs; core `zf` links without `zi`; classified final reference search. No second clean/configure/install matrix. |
| Performance | Release builds only. Compare native code and custom `Zfb::IOBuilder` under matching release flags/workloads: distributions, allocation/growth, ownership/alignment, encoded sizes, and copies. Earlier debug timings cannot satisfy this gate. Include migrated consumers. |
| Memory | Valgrind leak/corruption gate on the non-ASan debug build before the release transition, covering arena growth/detach, nested copies, table-schema teardown, and migrated store/transport lifetimes. Recheck subsequent lifetime/allocation fixes on the active non-ASan release build. |

Use existing owning-module `ZuTestUtil`/TAP targets and document the separately
invoked `interop` targets. Run Valgrind through `libtool exec`; use it before
this final gate only to investigate memory corruption when needed. Do not
run performance measurements under Valgrind. Review leaks and invalid accesses
from the exercised paths and resolve implementation defects; preserve and
explain existing external-library suppressions rather than broadening them
to hide new failures. Repeat focused checks for fixes, without repeated full
rebuilds or compiler/platform/configuration coverage beyond the authorized
debug correctness/memory and release performance builds.

Use the recorded Clang debug build for the initial correctness/memory gates,
then the required Clang release build for performance acceptance. Once that
transition is complete, retain the release configuration for subsequent
fixes and focused correctness/memory checks. An ASan switch and `make clean`
are permitted for diagnosis, not prescribed.
If that exception is used, document its impact on the final non-ASan Valgrind
run and artifact reuse before changing flags; ASan results do not discharge
the final Valgrind gate. Never mix objects with incompatible flags.

**Exit gate:** all functional criteria in `goal.md` have evidence under the
build scope prescribed here, including canonical interoperability, complete
consumer migration, no performance regression, and removal of `zfb`.
Unexecuted enabled consumers are outstanding checks, not passes. GCC,
MinGW/Windows and configurations beyond the prescribed debug/release pair remain outside
execution scope; maintain their source portability. Record findings and
commands/configuration actually used. Do not mark implementation complete
from source review alone.

### Acceptance scope reconciliation

The full-load 200 ms threshold in the historical telemetry harness is a
diagnostic threshold added during execution, not a requirement in `goal.md`.
Keep its overload failures recorded as failures. They establish a shared
single-host WSS/dispatch saturation limit, including direct echo without
the hub; they do not establish a codec regression or justify transport or
kernel changes in this implementation. No second host is available.
The user's subsequent single-message 200 microsecond concern was investigated
separately: the final one-in-flight run has median 96.311 microseconds and
p99 112.654 microseconds, with three of 10,000 samples above 200 microseconds
and a 665.868 microsecond maximum. This does not establish a hard maximum
latency guarantee. Preserve those tails and the HPET measurement overhead
in the final report. Codec acceptance follows `goal.md`'s pooled-buffer,
matching-workload performance comparison and its requirement to resolve
material regressions; do not turn the historical overload threshold into
an additional codec implementation requirement or relabel it as a pass.

## Milestone checklist

- [x] Contracts, reflection inventory, fixtures, and baseline captured.
- [x] Unified metadata, borrowed BFBS views, and existing store associations established.
- [x] Specialized BFBS streaming produces canonical-compatible schema spans without runtime metadata branching or file I/O.
- [x] Binary BFBS and textual FBS library functions stream to generic `S` destinations and their output is accepted by canonical tooling.
- [x] Canonical reads and typed/erased verification implemented.
- [x] Writer and independent nested roots implemented.
- [x] Field views, transformations, streams, and Zi integration implemented.
- [x] M1: queued core/I/O tests, canonical interoperability, and baseline performance checks passed.
- [x] Database/store ABI and reflection consumers migrated.
- [x] Telemetry/routing/dashboard consumers migrated.
- [x] Application declarations and remaining consumers migrated.
- [x] `zfb`, schemas/codegen, and canonical production dependencies removed.
- [x] M2: final dependent build, consumer/interop/performance checks, and Valgrind gate passed.

### Current execution evidence

#### Final repaired batch — 2026-10-09

The local-envelope repair and complete post-Ztc Zum refresh pass the current
Clang release build without diagnostics, including dashboard and all four
interop targets. Final functional checks pass 85 TAP files / 800 tests,
the telemetry Python lifecycle fixtures and the self-checking saga example.
All 15 Valgrind reports have zero errors and definite/indirect/possible
losses, without suppressions. The stopped freshness check and successful
continuation are preserved rather than counted as a functional failure or
silently discarded. The current dependency audit again covers 25 modules,
27 DSOs and 583 declared-object depfiles, with zero findings.

The final 32 release cases use canonical 25.12.19, matching release flags,
CPU 6, seven alternating rounds and matching pool policies, without
concurrent build/tests/debugger/Valgrind activity. Native/canonical medians
of sample means (ns unless stated) are:

| Workload | Reuse native/canonical | Fresh native/canonical |
| --- | ---: | ---: |
| 64-byte row save | 46.992 / 56.399 | 85.477 / 100.772 |
| 64-byte wide save | 81.601 / 97.527 | 124.348 / 148.696 |
| Large vector save (microseconds) | 71.044 / 119.741 | 106.773 / 150.674 |
| Replication | 112.100 / 121.394 | 131.675 / 135.415 |
| Nested telemetry | 156.239 / 155.027 | 204.270 / 206.263 |
| Serialized telemetry copy | 61.345 / 73.148 | 111.324 / 116.706 |

Configured pool-4 narrow and wide saves also improve. The nested reuse
residual is 1.212 ns / 0.8%, within the paired process variation recorded
for this repair; fresh construction improves. Narrow construction retains
the approximately 0.05–0.06 ns difference against the manually assigned
canonical row. Subsequent static review traced the extra initialization to
missing benchmark `Ctor<N>` properties; see the correction above.
Narrow own-schema reflected verification is 20.043 / 19.725 ns; with the
same canonical BFBS it is 18.925 / 19.683 ns, and with the same native BFBS
20.065 / 20.024 ns. This distinguishes schema layout cost from verifier
algorithm cost. These differences remain reported; they are not material
regressions requiring additional codec mechanisms. Performance acceptance
under `goal.md`'s material-regression criterion is satisfied. No constructor
contract changes, schema-layout redesign or new fast-path API are prescribed.
These sample-mean distributions are not per-message tail percentiles.

Across the ten captured dependent DSOs, text remains 130,193 bytes below
the pre-refresh baseline with no increases relative to that baseline. The
final scratch repair increases `libZtcApp` by 1,084 bytes versus the frozen
batch, although its isolated benchmark shrank 92 bytes. Preserve both
observations; do not generalize the private executable's size result to
production consumers. Allocation, growth, headroom, ownership, alignment
and heap tuning are unchanged by this final repair.

Raw CSVs, source snapshots, functional/memory summaries, paired prototypes,
rejected getter experiment, dependency/code-size results and runner records
are in [`m2-release-envelope`](zf/interop/evidence/m2-release-envelope/README.md).
The previous frozen batch is retained separately. After measurement,
`z.config -c -L /usr` regenerated the evidence distribution lists without
diagnostics; release flags and `config.h` are unchanged. The only changed
tracked build input after acceptance is the interop evidence list; tested
product sources are unchanged. Final distribution staging passed: 1,882
files, including all 405 byte-identical evidence files, exactly the two codec
headers and no obsolete `zfb`/`Zfb*`/codegen or non-interop schemas. Dashboard
is staged separately because root distribution traversal omits it. The final
consistency review preserves Q1–Q17 numbering and resolutions, current build
scope, explicit performance tradeoffs and historical network failures. All
milestone gates are complete; no additional source or configuration change
is needed.

#### Renewed implementation audit — 2026-10-08

**Pre-repair completion work (historical):** complete this plan with canonical performance
parity. The frozen repair batch completed its sequential dependent build,
functional checks, Valgrind and release matrix. Retain the current release
configuration and incremental artifacts. Nested telemetry construction still
shows a small regression; repeat measurements and a demonstrated repair are
needed. Earlier trials below are historical. Functional/memory success and
the measurement runner's zero exit status alone are not performance acceptance.

**Frozen-batch result — 2026-10-09:** the full incremental tree, separate
dashboard and four explicit interop builds passed without diagnostics.
The functional runner passed 85 TAP files / 800 tests, the three telemetry
Python lifecycle fixtures, and the self-checking saga example. All 15 final
Valgrind binaries report zero errors and definite/indirect/possible losses,
without suppressions. Final dependency inspection covers 25 modules, 27
production DSOs and 583 declared-object depfiles with zero findings. Across
the ten dependent DSOs captured before refresh, text shrank by 131,277 bytes
with no increases. This is not a whole-tree code-size baseline.

The 32 release cases completed seven alternating rounds each, using the
matching 25.12.19 canonical headers/reflection implementation and CPU 6 with
no concurrent build, correctness, debugging or memory activity. Narrow
64-byte saving is 46.782/85.274 ns reuse/fresh versus canonical
57.196/99.830 ns. Wide saving is 81.949/122.411 versus 99.654/148.884 ns.
Large vector saving is 71.527/107.084 microseconds versus
117.669/147.035 microseconds. Replication and serialized telemetry copying
are faster, and JSON output is at parity or faster in this matrix. Nested
telemetry construction remains 160.063/206.408 ns versus
153.758/203.614 ns. Some narrow read/load and reflected-verification
differences also need repetition before distinguishing small regressions
from measurement variation. These are medians of sample means, not
per-message tail percentiles. Raw reports and runners use
`/tmp/zffb-final-parity-*`; the focused three-process repeat uses
`/tmp/zffb-parity-repeat-*`. Complete parity and final evidence packaging
remain open.

**Focused follow-up — 2026-10-09:** three independent release processes
confirm narrow object construction at 2.034–2.060 ns versus canonical
1.973–1.990 ns. Borrowed reading is at parity and typed verification is
faster. The small construction difference is repeatable; it must not be
misreported as measurement noise. Native reflected verification is faster
when both implementations consume the canonical BFBS; the remaining own-schema
comparison includes differing schema layouts. Preserve that distinction.

An isolated telemetry prototype replaces the two fixed-envelope scratch
callbacks with local `ZtBuiltin<ZfFB::Offsets, N>` storage, where `N` is the
compiled envelope metadata extent. All slots are written by the existing
field/omission operations; no initialization pass is needed. This removes
callback machinery without changing the generic codec's workload-dependent
`ZtScratch` fallback, heap IDs, allocation, growth or ownership. It follows
GUIDELINES.md's [storage and capacity](GUIDELINES.md#storage-and-capacity)
and [audit flags](GUIDELINES.md#audit-flags) rules to prefer
existing framework storage and the smallest demonstrated simplification.
The private candidate passes the existing interop checks (8 tests across
baseline/candidate). Three paired release processes show nested reuse at
156.030–156.146 ns, against the baseline native 159.940–160.123 ns;
canonical samples range 153.885–156.274 ns. Copying also improves. Candidate
text shrinks 92 bytes. These are private diagnostic results, not acceptance
of a product change. Raw evidence is `/tmp/zffb-direct-envelope.40jlvwlh`.
The private offset-based scalar lookup experiment was rejected: three paired
processes show unchanged native loads (2.038–2.052 ns), with no benefit to
borrowed reading or verification. No `TableView` change is retained. The
extra present-field initialization store was subsequently traced to missing
benchmark `Ctor<N>` metadata. The former conclusion that this store was an
unavoidable adapter contract is withdrawn; existing constructor fields avoid
that initialization without changing the codec's default/construction rules.
The historical 0.06 ns measurement remains evidence for the old fixture only;
the corrected fixture has not been built or measured.

The completed frozen batch's raw release CSVs, source snapshot, compact
functional/memory summaries, dependency and code-size results and actual
host-specific runner scripts are preserved in
[`zf/interop/evidence/m2-release-frozen`](zf/interop/evidence/m2-release-frozen/README.md).
Their distribution-list addition and same-configuration `z.config`
regeneration are deferred until the current build/check batch finishes.

The local-envelope scratch simplification is now retained in `ZtcMsg.hh`.
Its storage extent is spelled directly from metadata, without new helper
names or redundant nested declarations. This is the only product source
change after the frozen batch. Refresh its dependents incrementally in one
sequential `-j4` batch, then rerun affected consumers, focused memory checks
and telemetry release measurements. The earlier full functional/memory
results establish the pre-change baseline, not acceptance of this repair.
The first repaired-batch freshness check stopped at `zum/itest`: root
traversal visits Zum before Ztc, whose later library rebuild invalidates
Zum's probe link and its `zumd-zumd.o` library prerequisite. This is a
build-order refresh, not a failed functional test. Preserve the stopped
runner, refresh the complete Zum module at `-j4` after Ztc, and resume the
remaining checks. Add this post-Ztc refresh to the recorded final build
sequence; do not accept stale dependent binaries.


- First-stage typed construction prototype (superseded by the later stages): a
  compile-time-selected table policy reuses child-offset scratch for final
  positions, including union tags, and deletes the separate tag scratch
  container. Generic composition retains arena records. A shared finalizer
  preserves table bounds, trailing-slot trimming and exact-layout vtable
  reuse; the typed policy fills every emitted slot once rather than zeroing
  then overwriting present entries. First-stage builds have no diagnostics;
  all 416 Zf tests and 49 pooled/core/telemetry interop tests pass. Three
  alternating process pairs show native nested telemetry at 168.72–169.15 ns
  reuse and 226.78–229.37 ns fresh, with canonical at 160.14–160.50 and
  211.55–212.80 ns respectively. Wide fresh saves are 148.78–150.54 ns
  versus canonical 146.03–146.94 ns. Empty vectors still regress. All 12
  paired workload files retain their encoded sizes and reported capacities.
  Commands/builds/tests and raw data use `/tmp/zffb-typed-*`.
  This improves the larger writer gap but is not parity or final acceptance.

  The second stage bounds scratch by the highest selected field slot, so
  key/update projections do not reserve the complete schema extent. Small
  scratch uses `ZtBuiltin` bounded to one platform cache line; larger schemas
  retain `ZtScratch` and the existing named heap fallback. This avoids new
  stack-availability accounting for scalar-only child tables. Typed table
  state also drops the redundant cache boundary: after each policy releases
  its records, the existing arena head is that boundary. The typed state is
  16 bytes on the retained platform. Its owning/isolated builds and 465
  functional checks passed (`/tmp/zffb-typed-small-*`). The large-vector
  comparison exposed extra per-child work, addressed in the third stage
  below; the second stage alone is superseded. Applies [Storage and capacity](GUIDELINES.md#storage-and-capacity),
  [Storage and lifetime](GUIDELINES.md#storage-and-lifetime),
  [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
  and [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

- Third-stage writer candidate keeps scalar-only singleton tables on the
  existing arena-record path: they have no child-offset scratch to consolidate.
  It also folds offset-vector alignment into the vector emission operation,
  removing a duplicate alignment pass while preserving forced alignment.
  Sequential incremental owning/isolated builds pass without diagnostics;
  465 focused checks plus eight existing large telemetry-schema checks pass.
  Six focused binaries pass Valgrind with zero errors or definite, indirect,
  or possible losses, without suppressions. Larger schemas exercise the
  existing `ZtScratch` fallback rather than increasing the fixed frame.
  Three release repetitions (seven alternating rounds each) give the
  following ranges of per-run medians, in ns, canonical/native:

  | Workload | Reuse | Fresh |
  | --- | --- | --- |
  | Narrow, 64 bytes | 56.48–62.45 / 46.51–46.64 | 100.82–105.50 / 88.60–89.97 |
  | Wide, 64 bytes | 99.11–100.64 / 95.19–95.32 | 142.10–143.21 / 135.33–136.63 |
  | Empty vectors | 65.42–66.07 / 72.96–73.59 | 111.88–112.45 / 115.96–116.36 |
  | Vectors, 64 bytes | 176.54–179.13 / 155.24–157.37 | 230.72–235.58 / 211.17–211.64 |
  | Vectors, 65536 bytes | 102866–118808 / 70892–71422 | 145118–151875 / 106820–107487 |
  | Nested telemetry | 156.46–157.76 / 155.26–155.61 | 203.04–203.60 / 211.94–214.47 |
  | Serialized telemetry copy | 73.33–74.47 / 63.60–64.15 | 119.92–120.36 / 105.02–106.59 |

  The preceding native large-vector baseline was 68634/104198 ns; the
  singleton path recovers the second-stage regression but the small remaining
  difference must remain visible. Large-vector debugger traces show identical
  physical allocation requests and growth copies: 294916 bytes/four calls
  reused and 884748 bytes/12 calls for three fresh saves, with identical
  245760-byte capacity. All stage-three byte counts, capacities and checksums
  match across codecs and rounds. Native's initial empty extension copies no bytes and
  adds no physical allocation. Evidence: `/tmp/zffb-typed-single-*`.
  Empty vectors and fresh nested telemetry still fail parity; reflected
  verification/printing and final consumer refresh remain open. These are
  intermediate results, not final completion. Next inspect unnecessary
  scratch setup for empty offset vectors. Applies
  [Storage and lifetime](GUIDELINES.md#storage-and-lifetime),
  [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
  and [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

- Empty offset vectors now bypass child-offset scratch setup, and vector
  emission skips zero-byte reserve/fill work. An attempted singleton stack
  offset was rejected: repeated paired large-vector native medians regressed
  from about 71/107 microseconds to 87/122 microseconds reuse/fresh, with small
  populated vectors also slower. Restored the scalar-only singleton arena
  path and removed that experiment's extra helper specialization. Evidence:
  `/tmp/zffb-empty-*`, `/tmp/zffb-scalar-*`.
  Telemetry's two envelope helpers now use the existing typed scratch policy,
  sized from their complete metadata. Initialize missing positions so manual
  composition remains safe for omitted fields. No new buffer owner or
  schema representation is added. Three CPU-6-pinned process pairs, seven
  alternating rounds each, measure nested telemetry canonical/native at
  158.81–160.81 / 157.34–157.42 ns reuse and 208.94–210.46 /
  209.83–214.01 ns fresh. Serialized-copy saves remain faster than canonical
  at 63.33–63.69 ns reuse and 112.14–114.16 ns fresh. All encoded sizes and
  checksums match, four telemetry interop checks pass, and executable text
  decreases by 1132 bytes. Fresh nested results are near parity, with one
  run still 2.4% slower; these measurements include unconfigured heap
  fallback. The preceding native copy-fresh medians span 106.19–122.68 ns;
  the candidate is 112.14–114.16 ns. This does not establish a uniform gain
  over the previous native path, despite beating canonical in every run.
  Sequential owning/isolated builds and 473 focused checks pass without
  compiler diagnostics (`/tmp/zffb-writer-check-*`); this is not a final dependent refresh or
  closure of the remaining verification/printing gaps. Empty-vector results
  from the subsequent current-writer run are recorded below.
  Applies [Storage and lifetime](GUIDELINES.md#storage-and-lifetime),
  [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
  and [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

- Current writer correctness milestone: 25 binaries / 473 checks pass under
  the retained release configuration; the source hashes are captured by
  `/tmp/zffb-writer-check-sources.txt`. A CPU-6 release pass measures empty
  vectors canonical/native at 67.96/67.14 ns reused and 113.92/109.16 ns fresh.
  Narrow reflected verification remains 20.83 versus 19.51 ns and narrow JSON
  printing 81.30 versus 74.47 ns; no complete parity claim is made.
  A targeted nullable-view branch hint reduces printing to 77.35–77.76 ns,
  versus canonical 75.36–75.54 ns, but does not eliminate the extra outlined
  unroll call. Restored the preceding header before the next experiment.
  Inspection finds `ZuUnroll` forces inline its implementation functions but
  leaves four forwarding entry points to the ordinary inliner. A candidate
  makes those entry points consistent with the existing implementation;
  it must earn retention through code-size and broad workload comparisons.
  Initial two process pairs remove that outlined native JSON unroll call:
  narrow printing is canonical/native 78.01/78.47 and 74.75/74.95 ns, versus
  native 80.38–80.39 ns before the change. Wide native printing improves
  from 346.62–348.17 to 327.65–331.58 ns. Benchmark text decreases by
  9432 bytes. Normalized instructions in the canonical narrow JSON routine
  are identical before/after, separating that code from placement/noise
  effects in timings. Broader writer/telemetry comparisons are in progress
  (`/tmp/zffb-unroll-broad.py`). Retention also requires the existing Zu
  unroll/metadata tests, focused adapter checks and the final consumer
  refresh; a framework-header change cannot inherit stale dependents' passes.
  The broader two-pair comparison completes with matching encoded sizes,
  capacities and checksums. Current native medians: narrow saves 46.79–46.82
  ns reuse / 86.13–86.49 fresh; wide 81.92–88.37 / 121.24–128.11; empty
  vectors 55.63–55.67 / 108.95–110.85; 64-byte populated vectors
  140.53–142.26 / 204.22–206.02. All beat canonical in these runs. Large
  vectors remain 70.59–72.17 / 106.91–107.28 microseconds, faster than
  canonical with no additional copied bytes identified in the earlier trace.
  Telemetry text falls a further 153788 bytes. Nested telemetry still has a
  small gap: canonical/native reuse 155.43–159.47 / 158.78–165.43 ns, fresh
  208.72–215.60 / 211.87–214.58 ns. Preserve that outstanding result rather
  than declaring complete parity. The existing Zu tests pass (two binaries /
  13 checks); refreshed owning/isolated adapter checks are running in
  `/tmp/zffb-unroll-check-*`. This remains a partial refresh, not final M2.
  No JSON formatter, number converter, or duplicate unroll algorithm is added.
  Evidence: `/tmp/zffb-writer-check-*`, `/tmp/zffb-view-hint-*`, and the
  upcoming `/tmp/zffb-unroll-*`. Applies
  [Type mechanics](GUIDELINES.md#type-mechanics),
  [Code structure](GUIDELINES.md#code-structure), and
  [Formatting](GUIDELINES.md#formatting).

- The metadata access candidate states two existing trusted-schema invariants
  through `ZuUnreachable`: schema/object views are present, and the required
  field-type view is present. External schema verification or local generation
  establishes these conditions before payload verification. No payload bound,
  offset, alignment, required-field or traversal-budget check is removed.
  Three paired runs use the same benchmark executable, selecting the preceding
  library with `LD_PRELOAD`; loader bindings confirm the intended old verifier
  is used. Current narrow reflected medians are 20.04–20.21 ns against
  canonical 19.64–20.39 ns; on the same canonical BFBS they are 18.94–19.02
  versus 19.59–20.44 ns. Wide reflected verification is 44.40–44.61 versus
  canonical 49.79–50.14 ns. The verifier shrinks by 208 bytes. Refreshed
  27-binary / 486-test checks pass without diagnostics; four focused Valgrind
  gates have zero errors and definite/indirect/possible losses, no suppressions.
  Evidence: `/tmp/zffb-metadata-*`. Final dependent/M2 refresh remains open;
  the small nested telemetry gap still needs resolution. Applies
  [Repair procedure](GUIDELINES.md#repair-procedure),
  [Assertions](GUIDELINES.md#assertions), and
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).

- Rejected forcing `withOffsets` inline: it removes one outlined forwarding
  symbol but outlines the containing telemetry helper instead, grows telemetry
  text by 68 bytes, and gives no consistent runtime benefit. Paired nested
  reuse medians change from 157.13–157.89 to 158.90–159.27 ns; fresh results
  vary. Restored `ZfFB.hh` byte-for-byte from `/tmp/zffb-offset-before.hh`.
  No rejected nullable-view hint, singleton-offset specialization or storage
  helper annotation remains. Evidence: `/tmp/zffb-offset-*`.
  The repair batch is frozen for full incremental dependent acceptance:
  `/tmp/zffb-final-parity-sources.json`, with sequential build/check/memory/
  performance runners prefixed `/tmp/zffb-final-parity-`. Reuse the current
  Clang release configuration/artifacts, build at most four jobs, and collect
  performance only after compilation and all functional/memory activity end.
  This refresh completed as recorded above; complete parity remains open.

  During the refresh, before traversal reached any telemetry consumer, the
  final style review replaced the two implicit captures in the changed
  `saveMsg` helpers with an explicit builder reference and fixed scalar
  values. This applies [Core language](GUIDELINES.md#core-language).
  Earlier-layer depfiles contain no `ZtcMsg.hh` dependency; the remaining
  traversal compiles the corrected header. The acceptance source snapshot
  was updated for that header alone, preserving the preceding snapshot as
  `/tmp/zffb-final-parity-sources-before-captures.json`. The pending functional,
  memory and release performance gates all use the corrected source.
  The same review deleted both offset-array zeroing loops in those helpers:
  every slot is subsequently assigned by `field`, `fieldOffset` or
  `fieldUnion`, including omitted optional fields. Failure paths return from
  `endTable` before reading positions. These concrete schemas have no gaps
  or uncomposed fields; no speculative future-schema initialization is needed.
  This applies [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).
  The source snapshot includes this removal before telemetry compilation.
  The final consumer review also added the existing `zdf/itest` SQLite
  persistence, `zgtk/test` value and `zdash/itest` OAuth/live lifecycle
  fixtures to the queued functional runner. Earlier passes do not establish
  their status after the current dependent refresh. Dataframe persistence
  uses a fresh disposable SQLite database; dashboard's live fixture supplies
  its own disposable database. No new test or configuration is introduced.
  The existing `ZmSchedTest` ordering regression is included as well, to
  retain direct final evidence for the transport investigation's scheduler
  repair instead of relying solely on passing consumer lifecycles.
  The inventoried `zdb/example/zdbsaga` also runs through its existing
  self-checking exit status, including the final expected account balances
  and teardown. Building the example alone would not establish its runtime
  migration; this reuses the example without adding a parallel fixture.
  After refresh, the functional runner first records current production DSO
  dependencies and non-interop object depfiles, verifies the two-header/core
  include boundary, and compares the ten captured dependent DSO text sizes.
  These reports precede tests and timing; they are not a second build or
  installation. Unchanged install manifests retain their earlier audit
  evidence; refreshed binary dependencies receive new evidence.
  The auditor obtains expanded object lists from current Makefiles in dry-run
  mode, excluding stale depfiles and generated Makefiles whose source
  `Makefile.am` no longer exists. Its precheck covers 25 modules, 27 DSOs and
  583 declared-object depfiles with zero findings, including Zf's direct and
  transitive Zi boundary. This precheck is superseded by the queued final
  report after dashboard/interop refresh; no obsolete artifact is deleted.

- Earlier completion work rejected direct BFBS-vtable-position dispatch:
  it added a low-level helper without reducing reflected-verification time.
  Restored the preceding header/source from
  `/tmp/zffb-parity-slot-before-{ZfFB.hh,ZfFBReflect.cc}` before the next
  experiment. Results: `/tmp/zffb-parity-slot-*.csv`; 67 focused checks passed,
  but functional success alone did not justify retaining the added API.
  The next simplification reads `Type.index` only for object-vector elements;
  scalar/string element layout does not use it. Three narrow reflected
  runs measure native 21.580–23.109 ns versus canonical 20.009–20.642 ns;
  wide native 47.521–48.068 ns versus canonical 53.344–56.068 ns. The small
  narrow improvement is not proof of parity; retain the removal of unused
  metadata access. Owning build and 67 focused wire/schema/interop checks pass
  (`/tmp/zffb-parity-index-*`). Applies
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).
- Rejected a targeted `Builder::field` inlining experiment. Three process
  pairs show fresh routing copies improving from 127.11–128.71 ns to
  111.08–115.37 ns, but nested construction has no consistent improvement
  and narrow fresh saves regress to 111.17 ns versus canonical 103.29 ns.
  Executable text grows by 2056 bytes. The forced-default builder and
  telemetry interop checks pass (two binaries/eight tests), which does not
  justify the measured tradeoff. Restored `ZfFB.hh` byte-for-byte from
  `/tmp/zffb-parity-field-before.hh`; the owning/isolated-target incremental
  restoration is running sequentially. Application/dependent refresh remains
  deferred until the performance batch is settled. Commands/results use
  `/tmp/zffb-parity-field-*`; old/new telemetry executables are compared
  through libtool wrappers with alternating process order. The next writer
  investigation must examine construction work rather than add more forced
  annotations: typed object saving retains child offsets in stack scratch
  and also records emitted fields in arena scratch for generic table finish.
  Any consolidation must preserve sparse IDs, omission/presence, generic
  reflected writes, nested-root isolation and pooled growth/ownership.
  Restoration of the owning Zf/Zi and isolated Zi/Ztc interop targets has
  completed without diagnostics. Before the subsequent writer consolidation,
  snapshot comparison confirmed that only
  `ZfFBReflect.cc` differs from the resumed 168-file implementation snapshot:
  local vector-helper inlining and conditional object-index access. No
  rejected header experiment remains in source.

  Historical proposal, subsequently implemented by the consolidation above:
  reuse typed saving's
  child-offset scratch for final field positions, deriving its extent from
  resolved wire slots (including union tag/value slots). Populate scalar and
  omitted-field positions as each field is processed, rather than emit a
  second `(offset, ID)` record into arena scratch. Factor the existing table
  finalizer's vtable-fill step so typed and runtime-reflected paths retain
  the same table-size checks, layout-cache matching and nested-root rules.
  Use existing `ZtScratch` storage and compile-time metadata; introduce no
  schema cache or second serializer. Acceptance requires the
  size/allocation/growth comparisons and timing recorded above. In particular, preserve trimming of trailing
  absent slots and exact-layout vtable reuse; reserving a full forward
  vtable before fields would defeat those properties and is not prescribed.

- Retained a focused `ZuInline` annotation on the local reflected
  `verifyVector` helper after three independent seven-round comparisons.
  Narrow reflected verification measures 22.020–22.131 ns (canonical
  19.988–20.099 ns), versus the resumed native 23.479 ns. Wide measures
  46.655–47.031 ns (canonical 51.620–52.298 ns), versus resumed native
  49.622 ns. This improves both workloads but does not close narrow parity.
  `libZf` text shrinks by 256 bytes; the same 67 focused wire/schema/interop
  tests pass. No check, metadata representation or public API changed.
  Evidence: `/tmp/zffb-parity-reflect-*`, `/tmp/zffb-parity-inline-*.csv`,
  `/tmp/zffb-parity-size-before.txt`. Applies
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)
  and [Repair procedure](GUIDELINES.md#repair-procedure).
- The next isolated writer experiment replaces `Arena::data()`'s redundant
  null conditional with `at(m_size)`. The arena invariant requires nonzero
  capacity to have storage; allocation is infallible after `canGrow` accepts.
  Empty storage has zero capacity and size, preserving its null result.
  Allocation requests, growth policy, field omission, wire layout and owner
  transfer are unchanged. Focused Zf/Zi and benchmark builds completed
  without diagnostics, and builder/stream/wire tests pass. Performance
  comparisons are pending in `/tmp/zffb-parity-arena-*`; retain this candidate
  only after evaluating them, then refresh affected dependents in the final
  batch. Earlier whole-tree results certify the preceding header revision.
- Rejected that arena experiment after controlled old/new executable
  comparisons. The baseline uses the exact recorded release compile/link
  flags with the saved preceding header included first; comparison artifacts
  are isolated in `/tmp`. Three alternating process pairs show native narrow
  fresh saves at 97.04–100.57 ns before versus 100.26–108.79 ns after.
  Empty-vector reuse improves slightly, but fresh-vector and wide results
  do not establish an offsetting gain. Restored `ZfFB.hh` byte-for-byte to
  `/tmp/zffb-parity-arena-before.hh`; the redundant-branch removal is not a
  retained repair. Raw pairs: `/tmp/zffb-parity-paired-*.csv`; baseline build:
  `/tmp/zffb-parity-arena-baseline-build.log`. The retained change from this
  batch is only reflected `verifyVector` inlining. Artifacts compiled with
  the rejected header are not final acceptance evidence and require normal
  incremental refresh before subsequent use.

**Benchmark provenance correction on resume:** installed canonical headers,
`flatc`, and pkg-config report 25.12.19, but the temporary reflection object
in `/tmp/zffb-audit-release-link.log` used the local 24.3.25 source tree.
Earlier reflected-verification comparisons using that object do not establish
same-version performance acceptance, regardless of matching optimization flags.
Retain those logs as historical diagnostics only. The resumed benchmark runner
compiles official `v25.12.19/src/reflection.cpp` against the installed headers
with the retained release flags before relinking; its source SHA256 is
`c4b7a46d0d65d8502ff9c1dd8d999b17722f8afeeeec3b314dec2d9c79fdabbf`.
Compilation, linking and measurements follow the functional and memory gates
sequentially. `/tmp/zffb-resume-perf-{compile,link}.log` records the commands.

The renewed audit covers all plan-owned changes, including tests and interop
fixtures, against every guideline category. It is in progress; earlier checks
do not establish acceptance of subsequent repairs. Retain the current release
configuration and batch repairs before rebuilding, with the existing `-j4`
limit for Zum and subsequent dependents.

- Allocation-contract review retracted the initial `ArrayStorage::grow`
  finding: its concrete `Buffer` uses `ZmVHeap`, whose nonzero allocation
  paths retry or invoke `ZmHeapFail`, rather than return null. Removed the
  speculative check added during the first pass. `Arena::grow_` retains its
  existing null handling for storage policies that can return null. This
  follows [Repair procedure](GUIDELINES.md#repair-procedure): establish the
  concrete allocator contract before adding validation.
- Initial storage review confirms pooled ownership remains in `ZiFB`, arena
  vtable records share the existing buffer, and `Zdb_::Schema` borrows BFBS
  with a compiled `ZuMatcher` resolver rather than retaining a second field
  index. Full caller/lifetime review remains outstanding.
- SQLite variable-length vector conversion now validates element headers
  while building, removing the separate `varVecData` traversal and repeated
  header decoding. Before allocating offset scratch, the count is bounded
  by the input's minimum length-word footprint. Each element still checks
  its length before borrowing bytes, and trailing bytes remain an error.
  No incomplete output escapes a failed conversion. This applies
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)
  and [Storage and capacity](GUIDELINES.md#storage-and-capacity).
  Removed a redundant SQLite unsigned-char-to-char span cast under
  [Bad casting](GUIDELINES.md#bad-casting). Verification is pending the batch.
- Replaced default lambda captures in the new reflected verifier and
  telemetry interop benchmark with explicit captures, per
  [Core language](GUIDELINES.md#core-language). Existing default captures
  in unrelated surrounding logic are outside this repair's scope.
- Core storage review: retain separate typed offset and byte-tag scratch in
  `saveObject`. Both are sized by selected metadata, and tags exist only for
  unions; a combined per-field record adds padding/tags to nonunion fields.
  A manually partitioned byte allocation would add alignment/aliasing
  machinery without removing either logical dataset. Vector offsets retain
  children across downward arena growth; scalar/inline vectors already write
  directly into their final reserved region. Vtable records share the arena
  head and are cleared/restored with root boundaries, so no separate cache
  or garbage collector is justified. Borrowed views/handlers contain pointers
  and metadata, not owning copies. Native materialization occurs only when
  requested by the destination type. `Handler::alloc` deliberately invokes
  the application's concrete `O` allocation, as other adapters do.
- Core schema review: `Graph`, sorted slots, layout tables, and dependency
  traversal are compile-time types/constants, not in-memory containers.
  BFBS streams forward without a runtime schema graph or staging copy.
  Numeric FBS defaults retain framework formatting; hexadecimal floating
  output uses the existing `ZtString` formatter because decimal precision
  cannot preserve subnormal defaults. Wire-representation casts in BFBS
  scalar emission are necessary (integer/endian-object pointers are not
  character-equivalent pointers). External-schema layout validation protects
  subsequent indexed/reflected access; trusted internal schema views bypass
  it. No justification found for deleting these checks or adding caches.
- Database consumer review removed the obsolete object-typed IP fallback
  from memory, PostgreSQL and SQLite field resolution. The current `ZiIP`
  metadata declares a union, handled by each resolver's union branch.
  Object UDT resolution now compares the single supported `Bitmap` token;
  removed the three unused two-token matchers and their includes. This
  follows [Scope and cleanup](GUIDELINES.md#scope-and-cleanup) and
  [Compile-time data and matching](GUIDELINES.md#compile-time-data-and-matching).
- `DBState::save` uses the local hash count only to provision offset scratch,
  appends offsets for actual visited states, and ends the iterator's scope
  before constructing the enclosing vector/table. This applies the count
  and iterator-lifetime flags under
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).
- Database storage decisions: retained memory-store native tuples are the
  mutable indexed row representation, not transient serialization copies.
  Their string/vector allocations predate the migration and cannot borrow
  a short-lived incoming buffer. New BFBS views replace schema pointers in
  existing field records; each Table's one named schema allocation is sized
  before streaming and follows Q13. The erased writers' offset scratch is
  needed while building children before their containing table. PostgreSQL
  network-byte-order and SQLite order-preserving values require their
  existing representation conversions; those are not interchangeable with
  FlatBuffers bytes. `SagaImage` retains the explicitly prescribed pooled
  buffer alias and copies into the pre-existing owning `Bytes` API; deleting
  that copy would require a different persisted-image ownership contract.
  Zdf schema migration introduces metadata declarations, not new runtime
  containers. These conclusions do not replace pending runtime verification.
- Telemetry/dashboard review removed the forwarding-only
  `Hubd::validAgentMessage` API and duplicate message-validity calls after
  `Ztc::msg` has verified the same message. Preserved route/authorization
  predicates and their standalone input contracts. The pre-authorization
  queue moves existing pooled buffer handles, is bounded by configured frame
  and byte limits, and releases them at disconnect; documented why pooled
  transport buffers cannot carry link-specific queue intrusion. Existing
  telemetry snapshots and dashboard model rows retain native data for later
  delivery/UI use and are not newly added codec staging containers.
- Admission-test review added a heap ID to byte scratch fallback and changed
  its allocated Link fixture to the concrete `Heap = ZuVoid`, first-base
  heap pattern with `ZuDerive` aliases. Fixed four overlong reflection lines.
  These apply [Storage and lifetime](GUIDELINES.md#storage-and-lifetime),
  [Heap allocation](GUIDELINES.md#heap-allocation),
  [Core language](GUIDELINES.md#core-language), and
  [Multiline layout](GUIDELINES.md#multiline-layout).
- Began one incremental top-level `make -j4` for the accumulated audit
  repairs, retaining the current Clang release configuration. Log:
  `/tmp/zffb-audit-build.log`. Build completion and runtime checks are pending;
  this entry is not a successful-build claim.
- During that build, the refreshed FlatBuffers/Skip suite passed 10 binaries
  and 88 TAP tests (`/tmp/zffb-audit-core-tests.log`). These cover metadata,
  builder, load, schema, stream, union, value, view, wire and Skip behavior;
  they do not establish consumer or memory-gate completion. Source review
  confirms adapter changes select `ZuActiveFields` before accessor/filter
  instantiation, including CLI options, CSV IDs, URI paths and DBUS signature
  generation. No additional runtime field container or parser was introduced.
- Reconciled direct foundational includes in both codec headers (including
  `ZuMatcher` in `ZfFB.hh` and `ZuBox`/`ZuFP`/`ZtEnum` in `ZfFBReflect.hh`)
  under [Header layout](GUIDELINES.md#header-layout). This declaration-only
  batch occurred after the running build passed Zf, so a final dependent
  refresh is required; the earlier core test pass does not claim to verify
  these updated headers. No separate concurrent build was launched.
- Added `Ztc.Load.Key` heap identification to the newly introduced direct
  load-test key string. The added-allocation/copy scan otherwise resolves
  production allocations to existing pooled buffers, named schema storage,
  or native row ownership. Saga payload serialization retains an owning copy
  into the pre-existing persistence field; telemetry routing retains opaque
  serialized spans instead of re-running object omission/default logic.
  The frozen canonical I/O builder remains confined to explicit interop
  fixtures and is intentionally excluded from production cleanup.
- SQLite consumer acceptance passed all seven integration drivers and 52 TAP
  tests (`/tmp/zffb-audit-sqlite-tests.log`), including the vector round trips,
  saga persistence, replication, schema and query-plan cases. Invoked the
  existing recipe directly with its residue lifecycle, without launching a
  second compiler build. This verifies the repaired conversion's successful
  consumer paths, not every malformed variable-vector input.
- Continued interop/test review replaced two default captures in the I/O
  comparison harness with explicit `f`/`bytes` captures and cached invariant
  vector lengths in the canonical save loop and truncation-verifier test.
  The compile-time `qualifier` name traversal needs no runtime cache.
  Applied [Core language](GUIDELINES.md#core-language),
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
  and [Style](GUIDELINES.md#style). Verification awaits the batch refresh;
  no performance measurements were run during compilation.
- Store serialization review found repeated `force_align` attribute lookup
  and integer parsing for every row's offset/inline fields. Memory, PostgreSQL
  and SQLite stores now resolve alignment once when populating their existing
  field records and pass the retained value to their writer primitives.
  The unsigned value fits existing padding on the current 64-bit target;
  no new container or allocation is introduced. Scalar defaults and IDs still
  borrow BFBS rather than duplicating the schema. This applies
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)
  and [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization).
  These repairs occurred after the build passed the stores and require the
  final refresh and renewed store acceptance.
- Added `ZdbPQTest::fieldAlignment` to exercise the cached-alignment contract:
  stream a schema from metadata, resolve its `force_align:32`, retain it in
  the existing field record, serialize a tuple through the PostgreSQL writer,
  then check the verifier, bytes and payload-address alignment. This focused
  acceptance test awaits the batch rebuild; existing unaligned/default
  fixtures did not cover this path.
- Boundary review confirms database/telemetry envelope verification does not
  traverse opaque nested byte payloads; their separate payload verification
  is necessary. Database local tuples/cache key access use trusted views;
  recovered external rows verify before native construction. Standalone
  diagnostic buffer printing accepts untrusted buffers and therefore retains
  its verifier. No extra verified-state flag, owning view or validation cache
  was introduced.
- Before that alignment repair, the database unit suite passed three binaries
  and 84 TAP tests (`/tmp/zffb-audit-zdb-tests.log`); the PostgreSQL codec unit
  suite passed nine tests (`/tmp/zffb-audit-pq-tests.log`). These are baseline
  results for this batch, not verification of the later alignment change.
- Remaining audit: complete codec/reflection and all dependent changes;
  account for each new container, transformation, copy and allocation;
  review all guideline flags and declarations/uses; repair demonstrated
  violations; then verify the complete repair batch and record its limits.
- Build-input review confirms `FB_INTEROP` flags occur only in the four
  explicit interop Makefiles; production traversal omits `zfb` and interop.
  No canonical includes, generated-schema includes, `ZfbStruct`, or `Zfb::`
  references remain in the affected production/test/example sources outside
  interop. Exactly `ZfFB.hh` and `ZfFBReflect.hh` form the codec's header API.
  This is source/build-input evidence; refreshed binary linkage still needs
  inspection after the build. Applied
  [Build system](GUIDELINES.md#build-system),
  [Metadata](GUIDELINES.md#metadata), and
  [Scope and cleanup](GUIDELINES.md#scope-and-cleanup).
- I/O review retains pooled buffers, complete-span copying for independently
  owned alert replay frames, and in-place sequence mutation within those
  copies. `ZfFB::save` reports codec errors and uses only the established
  `operator <<` destination contract; destination failure remains with the
  stream. Existing unchanged transmit/flush policies are outside this codec
  repair; no new stream abstraction or exception-catching layer is needed.
- Interop storage review consolidated the canonical/native field-handle
  arrays into one metadata-sized array of paired records in `zifbiobench`.
  Each record also retains resolved alignment, matching the repaired stores
  instead of parsing BFBS attributes inside the timed writer loop. Schema
  parsing/emission and field association remain outside timing. This change
  awaits interop rebuild; old benchmark numbers describe the prior kernel.
- The load harness's named timing array retains two timestamps per expected
  message until receipt, overwrites them with elapsed durations, then sorts
  the same array for each independent distribution after the run. It needs
  neither duplicate sample arrays nor scratch copies. Its size comes from
  the configured finite message count, and its lifetime spans asynchronous
  callbacks, so replacing it with local callback scratch would be incorrect.
- Repeated the identifier-length scan over changed/new C++ files, excluding
  strings/comments and the frozen canonical baseline. Matches beyond the
  28-character limit are external GTK/SQLite/picotls/Windows/canonical APIs
  or unchanged Zdf names; no migration identifier requires renaming. Wrapped
  two overlong lines in the I/O interop harness under
  [Naming](GUIDELINES.md#naming) and
  [Multiline layout](GUIDELINES.md#multiline-layout).
- Inspected ELF `NEEDED` entries for the 12 currently built shared libraries
  in Zf, Zi, Zdb, PostgreSQL, SQLite, Ztc, Zum, Zdf and Zgtk: none depends on
  canonical FlatBuffers or `libZfb`. This checks dynamic dependencies only,
  complements the source/build-input scan, and does not certify the pending
  repair rebuild or separate dashboard executable.
- After the running build completed `ztc/test` and entered `ztc/itest`, the
  rebuilt telemetry suite passed seven binaries and 29 TAP tests, including
  `ZtcHubTest` and `ZtcHubRxTest` (`/tmp/zffb-audit-ztc-tests.log`). The admission
  target also passed `make -q`. Invoked `prove` directly, without another
  build; these functional checks verify the telemetry repairs but do not
  certify the pending store-alignment rebuild or performance acceptance.
- The two rebuilt standalone telemetry integration drivers also passed all
  four TAP tests (`/tmp/zffb-audit-ztc-itests.log`). Full database-backed agent,
  routing/restart and cluster fixtures remain pending the store refresh.
- The rebuilt `ZtcHubRxTest` passed a focused `libtool exec valgrind` run
  with zero errors and zero definite, indirect or possible leaks, without
  suppressions. It retains 10,077 reachable bytes in 41 blocks. Logs:
  `/tmp/zffb-audit-hubrx.valgrind` and
  `/tmp/zffb-audit-hubrx-memory.tap`. This covers the repaired admission-test
  heap and pending-frame lifetime; it does not replace the pending codec,
  store and dashboard memory gates. No performance measurement overlapped it.
- The first incremental top-level audit build completed successfully with
  no compiler warnings/errors (`/tmp/zffb-audit-build.log`). Started one
  sequential refresh job for the later header/store/test repairs: top-level
  `make -j4`, dashboard, then explicit Zf/Zi/Zdb/Ztc interop targets at `-j4`.
  Logs are `/tmp/zffb-audit-refresh-build.log`,
  `/tmp/zffb-audit-dashboard-build.log`, and
  `/tmp/zffb-audit-{zf,zi,zdb,ztc}-interop-build.log`. Completion of that
  refresh and acceptance of its outputs are still pending. Retained Clang
  23.1.1 release configuration; installed FlatBuffers and `flatc` both report
  25.12.19. No clean, reconfiguration or parallel compiler batch was added.
- Refreshed core/I/O acceptance passed 11 binaries / 92 TAP tests
  (`/tmp/zffb-audit-refreshed-core-tests.log`); Zf tests and `ZiFBTest` both
  passed `make -q`. Ten runtime-focused binaries (builder, load, schema,
  stream, union, value, view, wire, Skip and pooled I/O) then passed
  `libtool exec valgrind` with zero errors and zero definite, indirect or
  possible leaks, without suppressions. Individual logs are
  `/tmp/zffb-audit-Z*Test.valgrind` and corresponding `-memory.tap` files;
  reachable process-lifetime allocations are reported rather than suppressed.
  The metadata-only test participates in functional/compile acceptance.
  These checks ran after the refreshed Zf/Zi artifacts were complete and
  while the same sequential build moved into database stores.
- After refreshed Zdb source/test/itest artifact checks passed `make -q`,
  unit acceptance passed 3 binaries / 84 tests and replication integration
  passed 4 binaries / 6 tests. Logs are
  `/tmp/zffb-audit-refreshed-zdb-{tests,itests}.log`. The rebuilt 79-case
  `ZdbSagaTest` passed `libtool exec valgrind` with zero errors and zero
  definite, indirect or possible leaks, without suppressions; 13,565 bytes
  in 105 blocks remain reachable. Logs are
  `/tmp/zffb-audit-zdb-saga.valgrind` and
  `/tmp/zffb-audit-zdb-saga-memory.tap`. These runs exercise the repaired
  memory-store field records; PostgreSQL/SQLite acceptance is still pending.
- Refreshed PostgreSQL source/test/itest artifacts passed `make -q`; unit
  acceptance passed 10 cases, including the new forced-alignment case, and
  integration passed 3 binaries / 16 tests. The codec and saga drivers also
  passed Valgrind with zero errors and zero definite, indirect or possible
  leaks, without suppressions (9,981 and 12,317 reachable bytes respectively).
  Logs: `/tmp/zffb-audit-refreshed-pq-{tests,itests}.log`,
  `/tmp/zffb-audit-pq-{codec,saga}.valgrind` and matching memory TAP files.
  Used the owned `/tmp/zffb-audit-pg.btXXED` PostgreSQL 16.3 fixture; verified
  `uint`/`libz` extension loading and stopped the server cleanly after checks.
- Refreshed SQLite artifacts passed recursive `make -q`; unit acceptance
  passed 5 cases and integration passed all 7 drivers / 52 tests, including
  variable-vector round trips. Both `zdbsltest` and `zdbsagatest` then passed
  Valgrind without suppressions or errors, with zero definite, indirect or
  possible leaks. The existing residue helper removed the successful private
  database fixture. Logs: `/tmp/zffb-audit-refreshed-sqlite-{tests,itests}.log`,
  `/tmp/zffb-audit-{zdbsltest,zdbsagatest}.valgrind` and memory TAP files.
  These results verify the store alignment-cache repair and one-pass SQLite
  variable-vector conversion; the remaining build/application/interop gates
  are not implied by these consumer results.
- The final incremental top-level refresh completed with no compiler
  warnings/errors (`/tmp/zffb-audit-refresh-build.log`). The same sequential
  job proceeded to dashboard and will then build the four explicit interop
  targets. Zum source/integration artifact checks passed `make -q`; its full
  integration target is running (`/tmp/zffb-audit-zum-itests.log`). No
  completion claim is made for the active dashboard/interop or fixture jobs.
- Zum's rebuilt full integration target passed all 13 drivers / 53 tests
  (`/tmp/zffb-audit-zum-itests.log`). Its checks include telemetry routing,
  restart persistence, vault/rekey, SSF, federation and cluster fixtures.
  Telemetry's complete SQLite-backed agent/routing/cluster target is now
  running after `ztc/itest` passed `make -q`; its result remains pending.
- The complete telemetry integration target passed its two standalone
  binaries / four tests plus all three SQLite-backed agent, routing/restart
  and cluster fixtures (`/tmp/zffb-audit-ztc-full-itests.log`). Zdf's current
  five test binaries passed six aggregate TAP cases
  (`/tmp/zffb-audit-zdf-tests.log`).
- Dashboard's refreshed functional fixture passed, followed by its mapped
  Valgrind fixture with 33 partial drains and 34 GTK events while telemetry
  remained queued. Valgrind reported zero errors and zero definite/indirect
  losses with the existing Fontconfig-only suppression. It still reports
  37,412 possibly lost bytes in 41 blocks: allocation stacks attribute 33,316
  to Rust/glycin, 2,608 to Mesa and 1,488 to glibc dynamic TLS. These are not
  suppressed or counted as proven leak-free; no possible-loss allocation
  originates in a Z allocator. Logs:
  `/tmp/zffb-audit-dashboard-tests.log`,
  `/tmp/zffb-audit-dashboard-memory.tap`, and
  `/tmp/zffb-audit-dashboard.valgrind`.
- Zum's refreshed unit suite passed 5 binaries / 35 tests
  (`/tmp/zffb-audit-zum-tests.log`). `ZumTest` then passed Valgrind with zero
  errors and zero definite, indirect or possible leaks, without suppressions;
  19,005 bytes in 200 blocks remain reachable. Logs:
  `/tmp/zffb-audit-zum.valgrind` and `/tmp/zffb-audit-zum-memory.tap`.
  Dashboard's build also completed without diagnostics; the sequential job
  has entered explicit interop compilation.
- The sequential refresh job completed successfully, including all four
  explicit interop targets. No compiler warnings/errors appear in its logs.
  Dry runs of those targets request only the intentional phony `fb-deps`
  pkg-config check, with no compilation or linking pending (`make -q`
  returns nonzero for that phony prerequisite). Current canonical interop
  passed all 3 binaries / 49 tests; log:
  `/tmp/zffb-audit-refreshed-interop-tests.log`.
- Rechecked the refreshed ELF dynamic dependencies of all 12 shared libraries
  across Zf, Zi, Zdb, PostgreSQL, SQLite, Ztc, Zum, Zdf and Zgtk, plus the
  dashboard executable: no canonical FlatBuffers or `libZfb` dependency.
  This complements the source/build-input audit; it does not purport to
  detect statically embedded code. `git diff --check` also passed.
- Refreshed reflected-writer release measurements completed after all builds,
  functional tests and Valgrind runs stopped. Each command was
  `zi/interop/zifbiobench 100000 SIZE 7 MODE`, with `MODE` equal to `write`
  or `widewrite` and `SIZE` equal to 64, 1024 or 65536. Both directions'
  correctness preflights passed. Logs are
  `/tmp/zffb-audit-{write,widewrite}-{64,1024,65536}.csv`. Field association
  and alignment parsing occur before timing in the repaired harness.
  Median round-mean nanoseconds per message (canonical / native):

  | Writer | Payload bytes | Reuse | Fresh |
  | --- | ---: | ---: | ---: |
  | Row | 64 | 73.925 / 48.142 | 115.822 / 105.705 |
  | Row | 1024 | 86.034 / 57.132 | 182.379 / 176.125 |
  | Row | 65536 | 1032.138 / 1005.137 | 27201.382 / 27147.474 |
  | Wide | 64 | 128.346 / 101.953 | 176.156 / 168.226 |
  | Wide | 1024 | 147.520 / 122.088 | 332.338 / 309.692 |
  | Wide | 65536 | 2365.407 / 2003.334 | 70284.892 / 69845.717 |

  Native medians are lower in this batch; sub-percent fresh-buffer differences
  at 64 KiB are not evidence of a meaningful speedup. These are schema-driven
  writer kernels under the retained release configuration/default pool
  fallback, not SQL throughput, per-message latency tails, full allocation
  parity, or closure of the complete performance matrix. No source changes
  were made during these measurements.
- The complete current Zf suite passed 21 binaries / 415 TAP tests after
  its `all` target passed `make -q`, including the other format adapters
  affected by `Skip` (`/tmp/zffb-audit-all-zf-tests.log`). Current scheduler
  and TLS ownership regressions also passed 2 binaries / 22 tests
  (`/tmp/zffb-audit-scheduler-tls-tests.log`). These extend the earlier focused
  codec checks; they are functional results, not performance measurements.
- The subsequent load-fixture review found repeated `Ztc::msgBody` extraction
  and full `AppTelemetry` construction where only packed version and start
  time are consumed. Retain the borrowed union/payload views for the callback
  and read those two fields directly. Their backing receive frame remains
  owned throughout `messageEnd`; no field escapes the callback. This removes
  unused field conversions without adding storage, allocation, validation
  or a cache. Applies [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow), and
  [Metadata](GUIDELINES.md#metadata). The isolated `ztc/itest` incremental
  build is pending in `/tmp/zffb-audit-load-view-build.log`; this fixture-only
  change requires direct and hub-path acceptance before being counted as
  verified. Earlier load timings describe the prior instrumentation.
- That fixture-only build completed without diagnostics and the target is
  current. Direct echo and SQLite-backed hub acceptance both passed using
  2 clients, 2 subscriptions per client, 4 publishers, 1 round, pacing 1000
  messages/sec and one message per turn; the hub also used 2 agents. Each
  delivered all 16 declared samples, covering direct telemetry and hub
  request/ACK/generation/telemetry paths. Logs:
  `/tmp/zffb-audit-load-view-{direct,hub}.log`. This verifies the borrowed
  reads and callback lifetime under both routes, not the full-scale SLO.
  Successful fixture processes and owned temporary databases were cleaned
  up through the existing residue lifecycle. No dependent library changed.
- The final added-line storage/transform pass confirms PostgreSQL's migrated
  vector decoding writes endian/length-prefixed values into its existing
  parameter buffer; the size pass provisions that required contiguous
  representation and does not create a duplicate converted array. Zum's
  custom `UDTArray` transformer and five derived vector transformers were
  deleted; native metadata-driven vector handling replaces them. Their
  long-lived saga arrays still own deletion records across asynchronous
  steps and are not callback-local scratch. The load fixture retains its
  timing storage through receive completion and sorts it only after client
  stop and link release. These are explicit lifetime/representation reasons
  to retain storage, under [Storage and lifetime](GUIDELINES.md#storage-and-lifetime)
  and [Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
  rather than assuming every array or copy can be deleted.
- A controlled current-state performance pass rebuilt the missing temporary
  canonical reflection object from the local source tree (24.3.25, initially
  misidentified as 25.12.19; see the provenance correction above) with the retained
  Clang release flags, then relinked the existing pooled benchmark object.
  Commands/output: `/tmp/zffb-audit-release-link.log`. CPU-6 runs used
  1,000,000 iterations and 7 alternating rounds at 64 bytes, with default
  pool fallback and no concurrent builds/tests. Logs:
  `/tmp/zffb-audit-current-{ops,wideops,save,wide}.csv`. They reproduce
  narrow typed verification at 7.072 ns versus canonical 6.301 ns, narrow
  reflected verification at 23.692 versus 19.498 ns, wide reflected
  verification at 52.996 versus 49.613 ns, and fresh wide saves at 151.923
  versus 140.371 ns. Fresh narrow saves now measure 94.567 versus 99.350 ns.
  These are round-mean medians, not per-message percentiles. The reflected
  comparisons use mismatched canonical source/header versions and cannot
  establish acceptance.
- Simplified the verifier's two end-bound checks with widened sums:
  `uint64_t(at) + n <= length` and `uint64_t(slot) + width <= objectSize`.
  Each operand is at most 32 bits, so the sum cannot overflow 64 bits; the
  checks retain identical empty-span, out-of-range and field-width semantics.
  Alignment, minimum field position, table boundaries and external-data
  validation are unchanged. This removes compare/subtract chains without
  new state, allocations or validation, under
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow).
  Added a wire-test case for exact-end empty spans, past-end spans and
  `UINT_MAX` endpoints/widths. The owning Zf/Zi, test and interop batch
  passed without diagnostics; the late test addition required only its
  target refresh. Zf passed 21 binaries / 416 tests, canonical interop 41
  tests, and the 7-case wire test passed Valgrind with zero errors and zero
  definite/indirect/possible losses, without suppressions (32 reachable bytes).
  Logs: `/tmp/zffb-audit-range-*-build.log`,
  `/tmp/zffb-audit-range-tests.log`, `/tmp/zffb-audit-range-interop.log`,
  `/tmp/zffb-audit-range-wire.valgrind` and its memory TAP file.
  Matched release after-runs and fresh-process repeats are in
  `/tmp/zffb-audit-range-{ops,wideops}*.csv`. They do not yet close the
  full performance matrix. This header repair requires final dependent
  refresh; earlier application passes describe the preceding header.
- Three fresh-process repeats completed for that bounds simplification.
  Narrow typed verification medians range 6.458–6.782 ns (canonical
  6.286–6.959); narrow reflected verification remains slower at
  22.997–23.039 ns (canonical 19.464–19.713). Wide typed medians range
  13.891–14.114 ns (canonical 15.289–15.371), and wide reflected medians
  48.687–49.606 ns (canonical 49.694–49.915). Retain the simpler equivalent
  arithmetic with this evidence: narrow typed and wide reflected results
  improve, while wide typed results are slower than the preceding native
  13.119 ns snapshot but still faster than canonical. This is a measured
  tradeoff, not an across-the-board speedup or complete performance signoff.
  All repeat processes exited successfully; `git diff --check` passed.
- A call-site guard avoiding reflected alignment lookup when the BFBS field
  has no attribute vector was measured and rejected. It left narrow
  reflected verification at 23.084 ns versus the preceding 22.997–23.039 ns,
  while adding a duplicate attribute probe when metadata is present. Wide
  reflected verification measured 48.288 ns, but that limited benefit does
  not justify extra work on the unchanged narrow path. Restored
  `ZfFBReflect.cc` byte-for-byte to its pre-experiment source. Logs:
  `/tmp/zffb-audit-attrs-*.log` and `/tmp/zffb-audit-attrs-{ops,wideops}.csv`.
  The experiment's Zf 416-test and canonical 41-test passes establish its
  functional behavior only. No public API, cache or descriptor was added.
- Started the final dependent refresh for the retained bounds arithmetic
  and load-fixture repairs: one sequential top-level `make -j4`, dashboard,
  then the four explicit interop directories at `-j4`, retaining the current
  release artifacts/configuration. Logs are `/tmp/zffb-audit-final-build.log`,
  `/tmp/zffb-audit-final-dashboard-build.log` and
  `/tmp/zffb-audit-final-{zf,zi,zdb,ztc}-interop-build.log`.
  Completion and refreshed consumer checks remain pending; no performance
  acceptance measurements may overlap this build.
- During the dependent refresh, current Zf/Skip and pooled-I/O artifact
  checks passed `make -q`, and their 11 binaries / 93 TAP tests passed
  (`/tmp/zffb-audit-final-core-tests.log`). Later consumer/interop build
  outputs are still pending; this is not a whole-tree refresh claim.
- Writer guard review retained the apparent duplicate span/vector capacity
  checks. `ZuSpan::length()` is `uint64_t`, while `Builder::vector` accepts
  an `unsigned` count. The outer check rejects oversized lengths before
  narrowing; its inner byte-product check cannot recover truncated bits.
  `saveValue` additionally checks before allocating offset scratch and
  traversing child values. `copyRoot` likewise must bound its 64-bit span
  length before delegation. Removing these checks would introduce an
  overflow bug, not simplify equivalent validation. This follows
  [Repair procedure](GUIDELINES.md#repair-procedure) and
  [Storage and capacity](GUIDELINES.md#storage-and-capacity); no code change
  was made from the initial duplicate-check suspicion.
- Reopen-path review found a remaining cache-consistency issue introduced
  by the alignment repair: memory, PostgreSQL and SQLite `StoreTbl::schema`
  replace their borrowed BFBS field handles but leave the cached `align`
  unchanged. The previous writer read alignment from the rebound field on
  each use. Refresh alignment alongside each rebound handle (including the
  memory store's separate key records), preserving setup-time computation.
  Repaired all three rebind paths; memory row/key records share a local
  binding lambda, and SQL key projections reference the rebound field
  records. No row-time parsing or extra storage is added. Focused reopen/
  alignment verification is pending; the active dependent build had already
  passed affected memory-store targets when these headers changed. A
  sequential incremental refresh after that job must catch the late repairs.
  After the active job exits, update the modification times of the three
  repaired store headers before that refresh: a compiler can read an old
  header yet finish its object after the edit, making an ordinary timestamp
  check insufficient. This forces only their dependent rebuilds, without
  cleaning, changing configuration or overlapping compiler batches.
  Keep its result distinct from final acceptance. Applies
  [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)
  and [Storage and lifetime](GUIDELINES.md#storage-and-lifetime).
- The final-build tool handle was lost, but process inspection confirmed
  its original `make -j4` (PID 186878) remained live with four compiler jobs.
  Its wrapper shell had exited, so the dashboard/interop tail could not be
  assumed queued. A separate temporary supervisor (PID 233715) waits on
  that make's Linux process descriptor before touching the repaired headers
  and running the sequential tree/dashboard/interop refresh. It does not
  compile while the original make runs. Progress and terminal exit status
  are `/tmp/zffb-audit-rebind-refresh.{stage,status}`; individual logs are
  `/tmp/zffb-audit-rebind-*-build.log`. Confirm the live process and terminal
  results rather than treating either a missing tool handle or a stage file
  alone as completion.
- Current release assembly of the Wide fresh/reuse timing loop calls the
  same `ZiIOBuf` heap-cache instance/allocation functions for both codecs.
  The selected timed-region call sites are retained in
  `/tmp/zffb-audit-wide-save-calls.txt`. This complements the earlier actual
  allocation/growth traces; it does not measure call frequency or explain
  the remaining fresh-buffer timing difference. No allocator, buffer-size,
  heap-ID or ownership change is justified by this inspection.
- Queued the existing acceptance suites behind the refresh supervisor's
  actual process exit and successful status. Each owning directory must
  first pass `make -q all` (pooled I/O checks its specific target), so the
  test phase cannot silently start another source build. Coverage is full
  Zf/Zdb/PostgreSQL/SQLite/Ztc/Zum/Zdf unit targets, pooled I/O, database
  integration for all three stores, telemetry and Zum integration, dashboard
  and the three canonical interop binaries. PostgreSQL uses the existing
  isolated 16.3 fixture and is stopped in a `finally` block after its checks.
  The queued runner is `/tmp/zffb-audit-rebind-check.py`; stage, terminal
  status and per-suite logs use `/tmp/zffb-audit-rebind-check*`. These are
  queued checks, not passing results. No performance measurement is queued
  to overlap them.

- Runtime naming regression checks passed after `make -C zt/test -q
  ZtArrayTest ZtStringTest` confirmed current artifacts: two binaries / 328
  tests, including their existing camel/snake conversion cases. Log:
  `/tmp/zffb-audit-case-tests.log`. Source comparison against `HEAD` confirms
  the runtime transforms are unchanged; compile-time `camelSnake` is a
  separate overload. The repository baseline itself counts output length
  before emitting, so describe this as restoration of the baseline, not as
  a one-pass runtime algorithm. This evidence covers naming behavior only,
  not the pending dependent refresh or performance gates.

- Follow-up reflection lookup audit: `Reflect::field(ObjectView, unsigned)`
  scans name-sorted BFBS fields only for schema-layout union/tag validation
  and explicit lookup callers. Payload union verification addresses the tag
  by its wire ID directly; database construction/rebind uses `Schema`'s
  compiled native-ID resolver and retains the resulting field handles.
  No row-time numeric-ID scan or justification for a second runtime index
  was found in these paths. Name lookup of externally supplied schema
  metadata uses `ZuSearch`; its token set is not known at compile time.
  Applies [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow)
  and [Compile-time data and matching](GUIDELINES.md#compile-time-data-and-matching).
- Rebind verification scope: `ZdbPQTest::fieldAlignment` tests forced
  alignment through the retained writer descriptor; existing store saga
  fixtures exercise reopening. SQLite's namespace reopen case reuses the
  same schema spans. These tests do not directly exercise changing forced
  alignment across a reopen and must not be cited as doing so. Source review
  confirms each rebind now refreshes both the borrowed handle and alignment,
  including memory-store key records; final refreshed runtime results remain
  pending.

- Further [Liveness](GUIDELINES.md#liveness) review found that the new
  pre-authorization frame queue was drained in one Rx callback. Its frame
  limit is configurable up to `INT_MAX`, so the memory bound does not bound
  work per scheduler turn. Replaced the loop with one frame per turn and a
  continuation only while frames remain. Newly authenticated arrivals stay
  behind pending frames and remain subject to the existing queue limits;
  ordinary authenticated traffic with an empty queue retains its direct
  path. No new production container, counter or allocation layer is added.
  `ZtcHubRxTest` now models deferred Rx callbacks with one pending function
  and checks yielding plus arrivals during draining. Verification is pending
  the already queued refresh, not established by earlier six-case results.
  A process-descriptor waiter (PID 241793,
  `/tmp/zffb-audit-drain-refresh.py`) touches the two edited telemetry sources
  after original make PID 186878 exits, before the queued dependent refresh
  reaches telemetry. Confirm its status and artifact currency before tests.
  These two source files supersede the corresponding entries in the earlier
  168-file frozen snapshot; all other entries remain applicable.

- Admission continuation review keeps its sole capture as the moved link
  handle; `drain` is static and obtains the application through the existing
  link/server relationship. `ZiMultiplex::rxRun` uses `ZmScheduler`'s
  `ZmRingFn`, which moves a closure directly into ring storage and allocates
  only on its existing overflow path. It does not use ordinary `ZmFn`
  closure allocation; wrapping this callback in `ZmFn::mvFn` would add an
  unnecessary adapter. Added a deferred-drain disconnect case to confirm
  the closed-state guard prevents processing or reposting after queue cleanup.
  Both new cases await the queued build/test pass. Applies
  [Callbacks](GUIDELINES.md#callbacks),
  [Framework fit](GUIDELINES.md#framework-fit) and
  [Teardown](GUIDELINES.md#teardown).

- Original make PID 186878 has exited after its log reached the final
  top-level directory; that log contains no warning/error diagnostics.
  The telemetry timestamp waiter completed with status 0, and the queued
  refresh supervisor is now running its `tree` stage (observed compiling
  `zdb/test`). The three store headers and two telemetry sources have the
  post-original-build modification time, so this pass cannot silently reuse
  objects produced after reading their earlier contents. Acceptance runner
  PID 234620 still waits for refresh PID 233715; no refreshed whole-tree
  acceptance result is claimed yet.

- Added-line framework scan and source triage found no new production
  malloc/free path, STL owning container, or hand-written numeric parser in
  the recorded implementation set. Remaining casts are actual representation
  boundaries: PostgreSQL endian words, aligned arena addresses, BFBS scalar
  bytes, and placement-storage tests. FBS hexadecimal floating defaults use
  the already reviewed framework `ZtString::sprintf` path. The default
  capture on Zum's `ZuActiveFields` replacement line predates this change;
  only the selected field list changed. Interop's direct buffer free belongs
  to the canonical old-builder baseline, not a new production allocator.
  This targeted scan complements the source review; it is not a substitute
  for whole-scope acceptance or a claim that arbitrary casts are acceptable.

- The naming scan found no identifiers exceeding 28 characters in the six
  new codec/I/O source files. Arithmetic-style review did find unresolved
  [Style](GUIDELINES.md#style) violations: power-of-two integer arithmetic
  and spaced division in the core headers, pooled-buffer tests, interop
  checks, and load fixture. Collect these into one remaining source batch;
  preserve type-width, signedness and grouping when using shifts. Existing
  expressions merely adjacent to a changed line are outside that repair.
  The current rebuild can establish intermediate functional results only;
  these style repairs and their final artifact refresh remain outstanding.

- Applied the arithmetic-style batch in nine files: use shifts for literal
  power-of-two integer operations and remove spaces around division.
  Grouped the unsigned interop checksum product before shifting, and retained
  generic `sizeof(T)`/element-size divisions rather than hard-code their
  widths. Bitmap limb widths remain derived from `sizeof(unsigned long)`
  for LP64/Windows portability. `git diff --check` passes; runtime
  verification is pending.
  Superseded acceptance waiter PID 234620 before it ran any tests, since
  shared headers changed during the current refresh. Final refresh PID
  251167 waits for refresh PID 233715 to exit, touches the nine repaired
  files, then runs tree/dashboard/interop sequentially with `-j4` and the
  retained configuration. Acceptance PID 251168 waits for that final
  refresh's success. Logs/status use `/tmp/zffb-audit-style-refresh*`,
  `/tmp/zffb-audit-style-*-build.log`, and `/tmp/zffb-audit-final-check*`.
  No compiler batches overlap; prior test evidence remains historical.

- Focused admission-drain verification passed after `make -C ztc/test -q
  ZtcHubRxTest` confirmed its executable was current: one binary / eight
  TAP cases (`/tmp/zffb-audit-drain-tests.log`). This includes yielding one
  frame per deferred Rx turn, queueing new arrivals behind pending frames,
  and a no-op continuation after disconnect, alongside the six original
  admission cases. This is functional evidence for the repair; it is not
  the pending final whole-tree refresh, memory gate or load-performance gate.

- Queued the final focused memory gate after functional acceptance PID
  251168: supervisor PID 256457 first requires acceptance status 0 and an
  exact match to `/tmp/zffb-audit-final-sources.json`, then checks target
  currency and runs 15 existing binaries sequentially through `libtool exec
  valgrind`. Coverage is eight runtime FB codec binaries, Skip, pooled I/O,
  memory-store saga, PQ/SQLite units, admission draining, and Zum. All
  definite/indirect/possible leaks count as errors, without suppressions.
  Progress/results use `/tmp/zffb-audit-final-memory*`. This is a queued
  focused gate, not a passing result or replacement for the separately
  documented dashboard/external-library memory findings. No performance
  run may overlap it.

- The store/admission refresh completed with terminal status 0: top-level
  tree, dashboard, and all four explicit interop builds finished without
  warning/error diagnostics in `/tmp/zffb-audit-rebind-*-build.log`.
  Final refresh PID 251167 has now started its `tree` stage and reached
  Zf tests after touching the arithmetic-batch files. Functional acceptance
  PID 251168 and memory supervisor PID 256457 remain waiting in sequence.
  This successful intermediate build does not replace their pending results.

- Queued release performance supervisor PID 298393 behind successful final
  memory verification. It requires the final source snapshot, relinks the
  refreshed pooled benchmark with the existing canonical reflection object
  built using matching Clang release flags, and runs the existing seven
  benchmark modes on CPU 6 at 0, 64, 1024 and 65536 payload bytes. Each uses
  seven alternating rounds; iteration counts are respectively one million,
  one million, 100000 and 10000 to bound large-payload run time. Logs/status
  use `/tmp/zffb-audit-final-perf*`. These are queued codec microbenchmarks,
  not performance acceptance results, network-latency evidence or a claim
  that earlier performance gaps have been closed.

- Resumed after the explicit stop and user's completed build. The cancelled
  supervisors are no longer live. Current-state dry runs show the main tree
  only needs `zmcpd` compilation/linking; dashboard has three residual target
  compilations. Nine audited files changed while paused, including the
  storage-policy `canGrow` contract and removal of allocation-null checks.
  Preserve those changes: `ZiIOBuf::realloc` uses infallible Z allocation,
  while `ensure` still reports capacity/output-status rejection. The arena
  now checks capacity/ownership through `canGrow` before calling `grow`.
  Old snapshots/results do not certify this source revision.
  Recorded `/tmp/zffb-resume-sources.json` and started an incremental-only
  residual refresh (PID 391633), then functional acceptance (391634),
  focused memory checks (391635), and release benchmarks (391636), all
  sequential. Logs/status use `/tmp/zffb-resume-*`; no timestamps are forced,
  configuration changed, artifacts cleaned, or source repairs made on resume.

- Resumed incremental refresh completed with status 0, including dashboard
  and the four interop directories, without compiler warning/error diagnostics
  in `/tmp/zffb-resume-*-build.log`. All 168 snapshot entries remain unchanged.
  `readelf -d` inspection of 27 production shared objects found no canonical
  FlatBuffers or `libZfb` NEEDED entry; results are recorded in
  `/tmp/zffb-resume-dependencies.log`. This checks dynamic dependencies, not
  statically embedded code. Functional integration remains in progress.
  Replaced the waiting performance supervisor with PID 395815 to compile
  canonical reflection from version 25.12.19 before running comparisons,
  correcting the source-version mismatch described above. No measurement
  overlaps functional or memory verification.

- Resumed functional acceptance completed with status 0: 78 TAP binaries /
  777 tests, plus the three telemetry agent/routing/cluster Python fixture
  checks. Coverage includes Zf, pooled Zi, Zdb, both SQL backends, Ztc, Zum,
  Zdf, dashboard and all three canonical interop binaries. Logs are
  `/tmp/zffb-resume-check-*.log`; each owning build target was checked current.
  The subsequent 15-binary focused Valgrind gate also completed with status
  0, zero reported errors and zero definite/indirect/possible losses, without
  suppressions (`/tmp/zffb-resume-memory-*.valgrind`). Reachable framework
  allocations remain; this does not supersede the separate dashboard
  external-library memory findings.
  The benchmark currency check initially stopped on the phony `fb-deps`
  prerequisite. Dry-run output contained only its pkg-config check, and
  `make -q -o fb-deps zifbiobench` confirmed actual artifacts current.
  Performance supervisor 407300 restarted after that runner-only correction;
  canonical 25.12.19 reflection compilation/linking succeeded and measurements
  started. No production code or build configuration changed.

- The resumed release matrix completed with status 0: seven modes at each
  of 0/64/1024/65536 payload bytes, seven alternating rounds on CPU 6, with
  no concurrent builds/tests. All correctness preflights passed. Commands
  are retained in `/tmp/zffb-resume-perf.py`; raw CSVs and medians are
  `/tmp/zffb-resume-perf-*.csv`. At 64 bytes, canonical/native median
  round-mean nanoseconds are:

  | Operation | Canonical | Native |
  | --- | ---: | ---: |
  | Narrow save, reuse | 57.933 | 47.118 |
  | Narrow save, fresh | 102.562 | 93.089 |
  | Wide save, reuse | 97.810 | 97.337 |
  | Wide save, fresh | 139.822 | 150.763 |
  | Narrow reflected verification | 19.728 | 23.479 |
  | Wide reflected verification | 49.770 | 49.622 |
  | Narrow print | 73.504 | 78.635 |
  | Vectors, reuse | 179.985 | 155.088 |

  Follow-on consumer runs used 1,000,000 iterations/seven alternating rounds
  and the same CPU/configuration. Replication canonical/native reuse is
  121.803/111.887 ns and fresh is 135.213/128.021 ns. Nested telemetry is
  153.922/178.517 ns reuse and 216.466/239.977 ns fresh; unchanged-payload
  routing copies are 73.442/63.582 ns reuse and 122.775/124.319 ns fresh.
  With `ZIFB_BENCH_POOL=4`, wide fresh saves become 108.942/109.055 ns;
  narrow fresh saves are 68.873/58.110 ns. Logs and commands:
  `/tmp/zffb-resume-consumers*`. The wide fresh gap depends on allocator
  regime in this run, but that observation does not establish its root cause
  or excuse fallback performance. Narrow reflected verification, narrow
  printing and nested telemetry remain performance findings. These are
  round means, not latency percentiles or network measurements. All 168
  source snapshot entries remain unchanged; aggregate acceptance is open.

- A post-measurement telemetry CPU profile completed through `libtool exec
  gprofng collect app` (`/tmp/zffb-resume-telemetry.er`, function report and
  run log alongside it). The report samples field emission, offsets, strings
  and table finalization; it does not establish a unique cause of the nested
  gap. Optimized disassembly confirms Request's field-expansion wrapper and
  primitive field/offset calls remain out of line
  (`/tmp/zffb-resume-request-unroll.asm`). The low sample count limits
  attribution. No forced-inlining change, extra metadata cache, validation
  removal or new allocation policy is justified by this profile alone;
  retain the measured source revision pending a controlled targeted fix.

Implementation is at M2. Unchecked aggregate gates are not completion claims.
Detailed historical evidence remains in the [core report](zf/interop/README.md),
[database report](zdb/interop/README.md), [telemetry report](ztc/interop/README.md),
and [dashboard memory report](zdash/test/README.md).

#### Build and current source

- Follow-up refresh after the TLS rxInvoke correction and benchmark/evidence
  changes: `./z.config -c -L /usr` completed, with unchanged config.h and
  retained Clang `-march=x86-64-v2 -O3 -g -DNDEBUG`, prefix /usr. Log:
  `/tmp/zffb-rx-final-config.log`. The single incremental top-level `make -j4`
  completed successfully without diagnostics (`/tmp/zffb-rx-final-build.log`).
  The separate dashboard and explicit Zf/Zi/Zdb/Ztc interop refresh also
  completed successfully without diagnostics, sequentially at -j4
  (`/tmp/zffb-rx-*-build.log`). After `make -C zum/itest -q all` confirmed
  its binaries were current, the 13-fixture Zum integration target started
  (`/tmp/zffb-rx-zum-itests.log`) and passed all 13 fixtures / 53 tests.
  The refreshed dependency audit covers 25 modules with zero findings
  (`/tmp/zffb-rx-depaudit.log`). Functional tests
  overlapped only the independent dashboard compilation, not performance
  measurements or another compiler batch. Telemetry integration then passed
  the two standalone binaries (four tests) and all three SQLite-backed agent,
  routing/restart and cluster fixtures (`/tmp/zffb-rx-ztc-itests.log`). This is
  functional coverage, not the still-failing full-load latency gate.
  Canonical core/database/telemetry interop passed 3 binaries / 49 tests;
  Zf passed 21 binaries / 415 tests (`/tmp/zffb-rx-*-interop-tests.log`,
  `/tmp/zffb-rx-zf-tests.log`). Dashboard module and OAuth checks passed
  (one test each; `/tmp/zffb-rx-dashboard-{tests,itests}.log`). The initial
  live fixture skipped because ZDB_MODULE was absent; that skip is not
  acceptance. Its explicit SQLite-backed rerun FAILED waiting for the first
  real agent to reconnect after late-publisher discovery
  (`/tmp/zffb-rx-dashboard-live.log`, residue
  `zdash/itest/ztchubtest.xfk2zhm_`). Hub callbacks report ServiceError::OK
  (-1) on already-closed links. A corrected GDB attachment to the restarted
  hub confirmed the one-pending-message admission guard fired on all 19
  reconnect attempts (`/tmp/zffb-admission-hits.txt`,
  `/tmp/zffb-admission-attach-run.log`, residue `ztchubtest.vueypzrk`).
  The collector sends two publisher inventories before asynchronous
  authorization completes. The earlier attachment targeted the initial hub
  and its absence of hits was not contrary evidence.
  The focused fix retains pre-authorization frames by moving their existing
  pooled handles into a lazy `ZmQueue`, bounded by the existing controlFrames
  and controlBytes settings (including an incomplete current frame in the
  byte limit). Successful authorization consumes them in order through the
  existing external-payload verifier; the partial current frame is preserved.
  Authenticated traffic bypasses the queue. Disconnect releases pending
  handles, and an already-closed link no longer logs successful authentication
  as a failure. Focused release compilation passed without diagnostics
  (`/tmp/zffb-admission-build.log`). The uninstrumented SQLite-backed live
  GTK fixture then passed, including two-publisher reconnect, aggregate
  snapshots, surviving-publisher delivery and publisher shutdown
  (`/tmp/zffb-admission-live.log`, 1 fixture / 1 TAP test). This pass does not
  establish the separate load-performance gate.
  `ZtcHubRxTest` covers admission cases through the actual daemon
  callbacks with an inert timer/transport stub and real Hubd/verifier. It
  covers exact frame and byte limits, an in-progress frame during successful
  authorization, rejected authorization/cleanup, and reuse of a drained
  receive buffer. It also checks malformed queued payload rejection by the
  real verifier. Failed authorization and rejected input now mark the link
  closed before a late authorization callback can act; draining preserves an
  existing partial frame and otherwise retains a receive buffer for reuse.
  The new target is documented in `ztc/test/README.md`.
  `./z.config -c -L /usr` regenerated the target with unchanged config.h
  (`/tmp/zffb-admission-test-config.log`). Sequential focused source/test
  release builds at -j4 passed without diagnostics
  (`/tmp/zffb-admission-edge-{src,test}-build.log`). The telemetry unit suite
  passed 8 binaries / 34 tests, including all six admission cases
  (`/tmp/zffb-admission-edge-tests.log`). The final production hub also passed
  the live GTK fixture (`/tmp/zffb-admission-edge-live.log`); that functional
  test overlapped only the test-target compilation, not performance work.
  Focused Valgrind verification of ZtcHubRxTest passed with zero errors and
  zero definite/indirect/possible leaks, without suppressions
  (`/tmp/zffb-admission-edge.valgrind`,
  `/tmp/zffb-admission-edge-memory.tap`). The initial fixture omitted logger
  shutdown and exposed a logger/global-teardown race; adding the explicit
  logger lifecycle used by neighboring tests fixed that test-owned failure
  (`/tmp/zffb-admission-edge.initial.valgrind`). This focused pass does not
  close the broader final memory gate or its earlier saga shutdown finding.
  Additional coverage for the TLS owner-dispatch change passed all five
  WebSocket binaries / 15 tests (`/tmp/zffb-rx-zws-tests.log`) and five HTTP
  binaries / 47 tests: H1 TLS messages, H2 messages, client cancellation,
  pooling and hub lifecycle (`/tmp/zffb-rx-http-tests.log`). These used the
  retained release artifacts from the completed dependent build.
  No alternate build, clean, install, or configuration change was performed.
  Source manifest review finds all 208 evidence files listed, no missing or
  unlisted paths, and exactly ZfFB.hh/ZfFBReflect.hh as the core FB headers.
  A production-source scan (excluding interop/archive trees) finds no
  canonical FlatBuffers includes/namespaces or old Zfb APIs. Removed 47
  ignored .bfbs build artifacts mapped exactly to deleted fbs/*.fbs sources;
  `/tmp/zffb-obsolete-bfbs-cleanup.json` records paths and sizes. A later
  filesystem audit found another 15 obsolete production outputs and one
  obsolete test output whose historical names were not covered by that
  deleted-source mapping. Their BFBS identifiers, ignored status and absence
  of current references were checked before removal. Paths, sizes and hashes
  are in `/tmp/zffb-legacy-schema-audit.json`; original bytes are retained in
  `/tmp/zffb-legacy-schema-backup.tar`. Only seven historical BFBS outputs in
  the archived `old/zcmd` tree remain; none are current build/distribution
  inputs. No current source or compiled artifact was removed.
  After this build, refresh outstanding Zum integrations and TLS-dependent
  consumer checks; performance and final memory gates remain outstanding.

- Current refresh (2026-10-08): `./z.config -c -L /usr` succeeded with
  unchanged `config.h` and retained Clang release flags. Incremental top-level
  `make -j4` completed successfully without warnings/errors; logs are
  `/tmp/zffb-current-refresh-{config,build}.log`. This includes the current
  saga catalog header change and all top-level dependents through GTK.
  Dashboard and explicit Zf/Zi/Zdb/Ztc interop builds also completed with no
  diagnostics (`/tmp/zffb-current-dashboard-build.log` and
  `/tmp/zffb-current-{zf,zi,zdb,ztc}-interop-build.log`).
  The heavy ZumTest compilation reached an observed peak RSS of 9533056 KiB
  (`/tmp/zffb-current-compile-resources.jsonl`); this is a resource observation,
  not a baseline compile-time comparison or performance acceptance result.
- Exactly two headers remain: `ZfFB.hh` follows the other adapters' organization;
  `ZfFBReflect.hh` owns reflection/schema output. Twelve helper headers were
  deleted, includes/install declarations migrated, and reflection definitions
  moved to `ZfFBReflect.cc`. `./z.config -c -L /usr` regenerated release metadata.
- The two-header build passed all owning source modules through dashboard.
  Its first dependent failure was ZiFBTest's unqualified local `save` helper
  colliding through ADL with the public codec entry point; qualifying those
  test calls fixed it.
- After concurrent Zu commit `b4c51b3e`, the build reached Zdf and exposed
  Zf templates inspecting `ReadOnly` on metadata-only `Skip`. `ZfFieldWritable`
  now selects writable adapters only for active fields; Load/Init reuse it.
  No getter, setter, member or synthetic ReadOnly flag is added to Skip.
  Zf source rebuilt cleanly (`/tmp/zffb-skip-matcher-zf-src.log`).
- The subsequent sequential `-j4` batch passed the top-level build, dashboard
  and core interop compilation. Pooled benchmark compilation then failed on
  four missing dependent `template` qualifiers; those are corrected, along
  with warnings for captures used only in the native instantiation. Original
  logs are `/tmp/zffb-skip-matcher-{all,dashboard,zf-interop,zi-interop}.log`.
  The final incremental top-level/dashboard/interop refresh passed without
  compiler diagnostics; logs are `/tmp/zffb-final-refresh-*.log`. No
  overlapping compilation was used.
- `ZtCase::camelSnake` runtime code is restored exactly to the repository
  original. Compile-time sizing/emission are local to the consteval overload;
  shared sizing/writing helpers were deleted. The original runtime body
  itself counts uppercase letters before emission. The final incremental
  refresh picked up this later header edit in the earlier core objects.
- Release fixes retain both telemetry egress-hash removals outside debug-only
  assertions and the Zu harness's release finalization registration/checks.
  Earlier debug-only forced-inlining changes were removed.

#### Lookup and allocation audits

- `ZdbStore::Schema` uses `ZuMatcher::exact` and `ZuSwitch` to select the
  emitted BFBS vector index directly from the sorted slot typelist. Neither
  native names nor numeric field IDs are scanned. Skip slots and union tags
  remain in schema indexing; native field lookup excludes Skip.
- Chained UDT-name dispatch in all three stores, pooled benchmark modes and
  the Zum telemetry probe now use exact matchers. The audit covered changed
  C++ and new migration sources across production, tests, integration,
  examples and interop. Generic erased BFBS name lookup already uses ZuSearch
  on canonical sorted runtime vectors; unrelated application parsing was not
  changed. The dependent rebuild passed; refreshed store functional checks
  remain required below.
- Two saga allocation regressions were corrected: `IOBufAlloc<SagaData>`
  retains `Zdb.Saga.Buf`; `SagaImage::Buf` aliases the original default-size,
  default-max `ZiIOBufAlloc` with `Zum.Saga.Image`. Both retain their final
  owned copy. Default ZiIOBuf tuning, Zdb row/heartbeat hooks and telemetry
  pools are unchanged. Each table separately owns its new `Zdb.Schema` BFBS.
- Production object/ELF/header audits are retained under
  `zf/interop/evidence/m2-release-{dependencies,executables,headers}.txt`.
  At the dependent-refresh snapshot, the audit passed all 25 production
  modules with zero findings. Later core edits require another final refresh
  and dependency audit; this snapshot does not establish current completion. It checks declared/native object existence,
  install header/executable manifests, generated-header patterns, ELF
  dependencies and Zf's forbidden Zi includes. The earlier missing
  `ZdfSeries.lo`/`ZdfStore.lo` findings are now discharged by this full scan.

#### Executed checks and their limits

- Current release BFBS emitter inspection is retained in
  `zf/interop/evidence/m2-release-emitter.txt`, including source/binary hashes.
  The recursive Root/Child emitter uses literal schema offsets/counts and
  direct specialized calls. Visible branches implement destination capacity/
  storage handling or stack protection (Q2 exemptions). Source review found
  graph/layout/field decisions resolved by consteval/if constexpr/ZuUnroll;
  padding loops are byte streaming. This representative disassembly does not
  claim every transitive stream helper is branch-free. FBS/attribute formatting
  uses existing framework helpers and scratch storage. No exporter source
  change or rebuild was needed for this review.
- Refreshed dashboard module and OAuth checks each passed 1/1, logged in
  `/tmp/zffb-final-dashboard-{module,oauth}.log`. The live GTK lifecycle
  fixture failed before its initial-dashboard marker with a WebSocket
  disconnect (`/tmp/zffb-final-dashboard-live.log`); retained fixture logs
  are under `logs/ztchubtest.hsqd2wh7`. Debugger capture showed closure before
  the first dashboard application reply. Source inspection found its manual
  request writer omitted alertDate/alertSeqNo, yielding unsigned null defaults
  where inventory validation requires zero. Both fields are now written as
  zero and the existing module fixture checks them. The focused dashboard
  rebuild passed without diagnostics (`/tmp/zffb-dashboard-request-defaults-build.log`).
  Both the module regression and full live GTK lifecycle fixture passed 1/1
  (`/tmp/zffb-dashboard-request-defaults-{module,live}.log`), confirming the fix.
- After that dependent refresh, Zf 21/413, focused Zi 2/10, core canonical interop
  1/41, database interop 1/4, telemetry interop 1/4, Zdf unit 5/6 and GTK 1/1
  passed (`/tmp/zffb-final-functional.log`). Zdf's SQLite persistence fixture
  passed with both required environment variables set
  (`/tmp/zffb-final-zdf-persistence.log`). These include the restored ZtCase
  runtime implementation and current Skip/matcher changes.
- After the Skip trait fix: Zf 21 binaries / 413 tests and six relevant Zu
  binaries / 238 tests passed. Logs: `/tmp/zffb-skip-matcher-core-tests.log`
  and `/tmp/zffb-skip-matcher-zu-tests.log`. These precede the later ZtCase edit.
- The preceding two-header release snapshot passed ZiFB/ZiMxLoop 10 groups;
  Zdb unit 3/84 and integration 4/6; PostgreSQL unit 1/9 and integration 3/16;
  SQLite unit 1/5 and integration 7/52; Zum unit 5/35; telemetry unit 7/28
  and standalone integration 2/4. Logs use `/tmp/zffb-two-headers-*`.
- Zum integration passed all 13 fixtures / 53 tests across original/resumed
  runs. Discovery initially ran before the zumping example linked; resuming
  there passed the remaining fixtures, including OIDC HTTP, federation and
  cluster failover. Logs: `/tmp/zffb-two-headers-zum-itests.log` and
  `/tmp/zffb-two-headers-zum-itests-resume.log`. Telemetry's corrected unit
  manifest run is `/tmp/zffb-two-headers-ztc-tests-corrected.log`; the first
  invocation incorrectly named nonexistent targets and was not a passing run.
- Focused release PostgreSQL/SQLite saga and ZumTest Valgrind checks reported
  zero errors and no lost blocks. The first memory-store saga run reported
  32 bytes definitely and 864 bytes possibly lost at scheduler shutdown.
  Three unchanged reruns, including before/at-exit debugger inspection, were
  clean. At that point the failing shutdown schedule remained uncaptured and
  unproved fixed; no suppression or speculative scheduler edit hid it.
  A fresh check of the current retained release memory-store saga binary
  passed all 79 cases with zero Valgrind errors and zero definite, indirect,
  or possible leaks (`/tmp/zffb-final-saga.tap`,
  `/tmp/zffb-final-saga.valgrind`). `make -C zdb/test -q all` confirmed the
  artifacts were current before that run. This fourth clean rerun does not
  explain the earlier shutdown schedule.
  A subsequent controlled non-stop GDB run established the lifetime window:
  the final recoveryPages call returned from mx.stop(), destroyed its
  multiplex, and main reached ZmGlobal_atexit while the detached mx:timer
  thread was held immediately after invoking its stop completion callback.
  The thread still owned that callback and its local completion queue.
  Its callback vtable identifies the exact ZmBlock<bool>/ZmEngine<ZmScheduler>
  stop lambda in the original 32-byte leak stack. Releasing that thread
  completed cleanup; main then exited normally. Evidence:
  `/tmp/zffb-stop-window-callback.log`,
  `/tmp/zffb-stop-window-live.gdb`,
  `/tmp/zffb-stop-window-inferior.tap`, and the retained-release instruction
  inspection `/tmp/zffb-scheduler-stopped-asm.txt`.
  This deliberately widened a permitted scheduling window; it was not a
  spontaneous reproduction of the original Valgrind byte totals. It proves
  global teardown can overtake control-thread cleanup.
  The focused fix retains a joinable scheduler control-thread handle until
  owner destruction. A successor owns its predecessor's handle and joins it
  before exiting, allowing synchronous restart from a stop callback without
  losing the old thread's cleanup. Existing spawn locking protects handle
  publication, and failed creation restores the predecessor handle. Normal
  owner destruction stops an active scheduler and joins the latest thread;
  the existing self-destruction exception detaches via ZmThread::detach().
  No work-path polling, new queue, or production delay was added.
  ZmSchedTest holds the first control thread in TLS teardown. Both ordinary
  destruction and destruction following callback-driven restart failed the
  early-return check before the fix (`/tmp/zffb-control-before.log`). After
  the fix those checks pass, together with implicit stop during destruction,
  worker self-stop, the existing scheduler cases and ZmEngineTest (2 binaries /
  9 top-level tests, `/tmp/zffb-control-after.log`). Focused source/test builds
  passed without diagnostics (`/tmp/zffb-control-{src,tests}-build.log`).
  Focused scheduler Valgrind passed with zero errors and zero definite,
  indirect or possible leaks, without suppressions
  (`/tmp/zffb-control.valgrind`, `/tmp/zffb-control-memory.tap`).
  The changed spawn template requires dependent recompilation before further
  consumer acceptance. One retained-release top-level incremental `make -j4`
  completed successfully (`/tmp/zffb-control-dependent-build.log`) with no
  warnings or errors; completed consumer checks are recorded below, with
  remaining dashboard/interop/performance checks pending against that rebuilt
  state. The subsequent sequential `-j4` batch also completed dashboard,
  the four explicit interop directories and the matching-release benchmark
  link without warnings or errors. Logs are
  `/tmp/zffb-control-{dashboard,zf-interop,zi-interop,zdb-interop,ztc-interop}-build.log`
  and `/tmp/zffb-control-bench-link.log`. No clean,
  configuration switch, GCC or MinGW execution is part of this batch.
  During this rebuild, the interop evidence distribution list was checked
  against the filesystem: all 208 evidence files are listed, with no missing
  or unlisted files (`/tmp/zffb-control-evidence-audit.log`). This checks the
  distribution manifest, not whether historical results establish current
  acceptance. The interop README now distinguishes the earlier core snapshot
  from the pending scheduler-dependent refresh.
  After traversal completed Zf, Zi and TLS, `make -C MODULE/test -q all`
  returned zero for each, confirming that their current artifacts included
  the changed scheduler template. Functional checks then passed: Zf 21
  binaries / 415 tests, ZiFB/ZiMxLoop 2/10, and ZtlsBufHookTest 1/16. Logs:
  `/tmp/zffb-control-{zf,zi,tls}-tests.log`. These checks ran during the
  higher-level build and establish no timing or full-consumer acceptance.
  The complete rebuilt TLS suite subsequently passed 11 binaries / 66 tests
  (`/tmp/zffb-control-tls-suite.log`). Once HTTP test compilation finished,
  its `-q all` check also returned zero; H1 TLS/H2 messaging, client cancel,
  client pool and hub lifecycle passed 5 binaries / 47 tests
  (`/tmp/zffb-control-http-tests.log`). No compilation was launched by these
  checks and no performance measurements ran concurrently with the build.
  The rebuilt representative Root/Child BFBS emitter has identical complete
  objdump text to the retained release review; its generator/fixture source
  hashes also match. `evidence/m2-release-emitter.txt` records the rebuilt
  executable hash while retaining the original provenance. The comparison
  is `/tmp/zffb-control-emitter-review.txt`; it supports the same limited
  representative-function Q2 review, not every application instantiation.
  WebSocket's rebuilt test artifacts subsequently passed `-q all`, then all
  5 binaries / 15 tests passed (`/tmp/zffb-control-zws-tests.log`), covering
  protocol, H1/H2/H3 hub and source-boundary checks. The top-level build
  remains active.
  A focused final memory recheck of the rebuilt ZiMxLoopTest also passes all
  six cases, including the 131,072-callback wake burst and shutdown, with zero
  Valgrind errors and zero definite, indirect or possible leaks. No suppressions
  were used; 9,821 bytes remain reachable at process exit. Artifacts were
  confirmed current with `make -C zi/test -q ZiMxLoopTest` before invoking
  Valgrind through `libtool exec`. Evidence:
  `/tmp/zffb-control-mx.valgrind` and `/tmp/zffb-control-mx-memory.tap`.
  This exercises the scheduler lifetime fix in I/O teardown.
  After their artifacts passed `-q all`, the rebuilt memory-store database
  unit suite passed 3 binaries / 84 tests and its integration suite passed
  4 binaries / 6 tests (`/tmp/zffb-control-zdb-{tests,itests}.log`).
  ZdbSagaTest then passed all 79 cases under Valgrind with zero errors and
  zero definite, indirect or possible leaks; no suppressions were used and
  13,565 bytes remain reachable. Evidence is
  `/tmp/zffb-control-saga.valgrind` and `/tmp/zffb-control-saga-memory.tap`.
  Unlike the earlier unchanged clean reruns, these results exercise the
  rebuilt scheduler lifetime fix, backed by its failed-before/passed-after
  deterministic regression.
  PostgreSQL then passed unit 1/9 and integration 3/16 after all three owning
  artifact checks (`src`, `test`, `itest`) returned current. The private
  `/tmp/zffb-pg.r84cgV` fixture was started only for these checks and stopped
  successfully by `/tmp/zffb-control-pq-check.sh`. Logs are
  `/tmp/zffb-control-pq-{run,tests,itests}.log`. SQLite's complete module
  subsequently passed `-q all`, unit 1/5 and integration 7/52, using its
  existing temporary-database test fixture; logs are
  `/tmp/zffb-control-sqlite-{tests,itests}.log`.
  Final focused saga memory checks of these rebuilt PostgreSQL and SQLite
  consumers each passed all 12 cases with zero Valgrind errors and zero
  definite, indirect or possible leaks, without suppressions. Reachable
  allocations are 12,221 and 12,253 bytes respectively. Evidence is
  `/tmp/zffb-control-{pq,sqlite}-saga.valgrind` and the corresponding
  `-saga-memory.tap` files. The PostgreSQL fixture stopped cleanly and the
  SQLite residue helper cleaned its successful temporary fixture.
  These checks ran during the existing Zum compilation batch. Remaining
  application/interop checks and performance acceptance are still pending.
  Zum's rebuilt unit artifacts subsequently passed `-q all`, followed by
  all 5 binaries / 35 tests (`/tmp/zffb-control-zum-tests.log`). ZumTest's
  focused final memory check passed all 24 cases with zero Valgrind errors
  and zero definite, indirect or possible leaks, without suppressions;
  19,005 bytes remain reachable. Logs are `/tmp/zffb-control-zum.valgrind`
  and `/tmp/zffb-control-zum-memory.tap`. Before running Zum integrations,
  require current artifacts in both `itest` and `example`: discovery uses
  `zumping`, whose example target follows integration compilation in traversal.
  Both targets subsequently passed `-q all`, and `make -C zum/itest test`
  passed all 13 fixtures / 53 tests against these rebuilt artifacts
  (`/tmp/zffb-control-zum-itests.log`). This includes discovery, telemetry
  lifecycle, SQLite restart/recovery, HTTP, Vault/rekey, SSF, OIDC transport,
  federation and cluster failover. The OIDC transport fixture completed in
  60.04 seconds; an attempted diagnostic attachment occurred after its exit
  and captured no stacks. These are functional checks during the ongoing
  Ztc compilation, not release performance measurements.
  Ztc's test target then passed `-q all` and its unit suite passed all
  8 binaries / 34 tests (`/tmp/zffb-control-ztc-tests.log`). After both its
  integration and example targets passed `-q all`, the integration target
  passed two binaries / four tests and all three live fixtures: agent Vault
  provisioning/restart, OAuth/WSS routing/overflow/restart, and cluster mode.
  `/tmp/zffb-control-ztc-itests.log` records the SQLite-backed run; load and
  dashboard options were disabled. No earlier interop or dashboard result is
  treated as validation of the scheduler lifetime change.
  The rebuilt ZtcHubRxTest's focused final Valgrind check passed all six
  admission/partial-frame cases, with zero errors and zero definite,
  indirect or possible leaks, without suppressions; 10,077 bytes in 41
  blocks remain reachable. Logs are `/tmp/zffb-control-hub.valgrind` and
  `/tmp/zffb-control-hub-memory.tap`.
  Zdf then passed rebuilt unit 5/6 and SQLite persistence 1/1, after each
  target passed `-q all`. Logs are `/tmp/zffb-control-zdf-{tests,itests}.log`.
  The persistence fixture used a private temporary database managed by
  `zi-test-residue.sh`, which cleaned it after success.
  GTK's rebuilt unit target passed `-q all` and its value test passed 1/1
  (`/tmp/zffb-control-gtk-tests.log`).
  Once dashboard's production target was current, the dependency/install-rule
  auditor was rerun across all 25 production modules: zero findings
  (`/tmp/zffb-control-depaudit.log`). The refreshed reports are
  `/tmp/zffb-m2-{dependencies,headers,executables}.txt`; this checks current
  declared objects/depfiles, ELF dependencies and installation manifests,
  including exactly two installed ZfFB headers and no Zi dependency from Zf.
  Dashboard's rebuilt module target passed `-q all` and its GTK module test
  passed 1/1 (`/tmp/zffb-control-dashboard-tests.log`), including queued
  telemetry during GTK drains. The separate live GTK lifecycle fixture also
  passed 1/1 with SQLite enabled (`/tmp/zffb-control-dashboard-live.log`),
  exercising telemetry, late discovery and generation replacement. Both
  checks used the rebuilt dashboard/module. Once its integration target was
  current, OAuth also passed 1/1 (`/tmp/zffb-control-dashboard-oauth.log`).
  Dashboard's complete build passed without diagnostics. The refreshed
  canonical interop suites then passed core 1/41, replication 1/4 and
  telemetry 1/4 against FlatBuffers/flatc 25.12.19. Logs are
  `/tmp/zffb-control-{zf,zdb,ztc}-interop-tests.log`. The core `all` query
  remains nonzero solely because its package-version check is phony;
  `make -n all` showed no compilation or link work before testing.

#### Earlier release consumer checks — 2026-10-08

Before the scheduler lifetime fix and its dependent refresh above, the
release batch passed Zf 21/415, ZiFB/ZiMxLoop 2/10, canonical
core/database/telemetry interop 3/49, memory-store unit 3/84 and integration
4/6, SQLite unit 1/5 and integration 7/52, PostgreSQL unit 1/9 and integration
3/16, Zum unit 5/35, telemetry unit 7/28 and Zdf unit 5/6. Logs use
`/tmp/zffb-current-*-tests.log` / `*-itests.log`. These include refreshed saga
checks for all three stores after removal of redundant catalog verification.
The owned PostgreSQL fixture was restarted only for its checks and stopped
successfully afterward. GTK 1/1, dashboard module 1/1, dashboard OAuth 1/1,
live GTK lifecycle 1/1 and Zdf SQLite persistence 1/1 also pass, with required
store environment enabled. Logs are `/tmp/zffb-current-{gtk,dashboard}-tests.log`,
`/tmp/zffb-current-dashboard-itests.log` and `/tmp/zffb-current-zdf-itests.log`.
Zum full integration, full telemetry load, performance and final memory gates
remain separate. The full telemetry workload ran against the rebuilt release
artifacts with default 2048/32/32/256/3 sizing and a fresh SQLite store and
FAILED. `/tmp/zffb-current-full-load.log` and residue
`ztchubtest.g1g_hb01/{load,hub-0}.log` preserve the result: 10336 samples,
max_latency_us=107585, fanout_us=82, incomplete delivery. The first peer
failure reports Error code 4 (`AgentGone`); hub logs show four TLS
`bad_record_mac` errors immediately before agent disconnects. This is not a
passing latency result because delivery failed. Investigate the transport
failure before repeating the full gate; no queue-limit or SLO relaxation is
justified by these logs.

The production dependency/install-manifest audit was rerun after the complete
build: 25 modules, zero findings. Reports are `/tmp/zffb-m2-dependencies.txt`,
`/tmp/zffb-m2-headers.txt` and `/tmp/zffb-m2-executables.txt`; the auditor inspects
configured object depfiles, native objects, ELF dependencies and installation
lists. This is not an installation into `/usr`.

#### Remaining acceptance work at earlier snapshots (historical)

- Direct-versus-hub loopback comparisons are now recorded in
  `ztc/interop/README.md` and `zf/interop/evidence/m2-loopback/`. The reusable
  `ztcdirecttest.py` fixture uses the existing load peer's new echo/direct
  modes and production WSS, with identical pacing/sample validation and
  explicit application byte counts. No production path was altered.
  High-rate queues persist with the hub removed. Connection topology matters:
  at 131072/sec, hub median changes from 2.541 ms with two producer sockets
  to 5599 ms with 32. Direct echo also saturates, including two-socket controls.
  This is evidence for shared local transport/dispatch limits, not proof that
  the kernel alone is responsible. The user confirmed no second host exists;
  cross-host performance remains unmeasured and is not asserted.
  GUIDELINES: reuse existing Z formatting, metadata and pooled I/O buffers;
  keep Rx state on Rx; teardown through existing asynchronous stop/drain paths.
  All 786432 samples arrive in each comparison, but high-rate SLO failures
  remain failures. Source, binary hashes, per-run results and command order
  are retained; distributions are actual per-message measurements.
  Same-configuration `z.config -c -L /usr` regeneration passed; generated
  manifests contain all 20 loopback evidence files and the direct fixture.
  Release flags remain unchanged. All comparison processes have exited.

- The isolated-message diagnosis requested after the status review is recorded
  in `ztc/interop/README.md` and `zf/interop/evidence/m2-e2e-latency/`.
  Single-message production WSS delivery (including hub forwarding and ingress
  verification) has median 97–116 µs and p99 116–157 µs with exactly one message
  in flight. It does not meet a strict every-message 200 µs maximum. A controlled
  32768/sec comparison reduces median 409→70 µs by changing only generator
  batching; the generator's scheduled timestamps can be in the future.
  At 262144/sec, actual handoff-to-receive medians remain 3.11 s with two agents
  and 5.30 s with 128 agents. The backlog is real and after handoff. Sampled peer
  CPU is dominated by socket I/O, synchronization and clock reads, not codec
  primitives. This host switched from unstable TSC to HPET; measured realtime
  clock reads cost about 1.2 µs. Keep that measurement overhead explicit and
  remove the redundant diagnostic clock read; do not change the host clock or
  treat sampled CPU proportions as per-message latency decomposition.
  GUIDELINES references: General principles (latency and thread ownership),
  Heap allocation (named, preallocated test storage), and Code structure
  (eliminate repeated work). The new arrays are application-Rx owned and sized
  from the declared sample count. No production transport change is prescribed
  from this evidence alone. Full-load acceptance remains open.
  The final three-clock-read isolated recheck has median/p99 96.311/112.654 µs,
  3/10000 samples above 200 µs, and maximum 665.868 µs. The two-agent highest
  rate still fails: actual max 1073.089 ms and publication lag 2499.327 ms.
  Instrumentation affects overload behavior; preserve both snapshots and do
  not describe the difference as a production fix. Test-only release rebuild
  has no diagnostics, all messages were delivered, and diff whitespace checks
  pass. The new evidence files are listed in EXTRA_DIST. Same-configuration
  `z.config -c -L /usr` subsequently refreshed the generated manifests for
  this evidence and the direct fixture; final distribution staging remains
  part of the outstanding implementation acceptance batch.

- Verification-boundary source audit removed one redundant same-process
  check: saga catalog TupleData is serialized locally by every store backend,
  so sagaCatalogScan now loads it directly. TupleData documents that contract.
  Nested opaque persisted saga payloads, replayed images, replication/network/
  cross-process-ring ingress and corrupt-buffer diagnostics retain their
  verification boundaries. Details are in `zdb/interop/README.md`. This header
  edit has now rebuilt and all three stores' refreshed saga checks pass in
  the current release batch above; earlier runs were not used to validate it.
- Release evidence is now listed explicitly in `zf/interop/Makefile.am`:
  the initial batch, focused hotpath batches and production dependency/header/
  executable reports. The core report distinguishes current release status
  from historical debug/consolidation results. The latest vector-padding
  source/binary hashes and commands are retained with its data. The current
  `z.config` refresh regenerated build metadata successfully. The generated
  `zf/interop` distdir target then staged all 196 evidence files, each identical
  by SHA-256 to its source, under `/tmp/zffb-dist-evidence-nuqnu0g8`.
  Command/output: `/tmp/zffb-current-dist-evidence.log`. Source-manifest audit
  also finds no unlisted or missing evidence paths. This closes the core
  evidence-packaging check. The first top-level distdir attempt stopped on obsolete `naming.md`
  in `Makefile.am` (`/tmp/zffb-current-dist.log`); that migration plan is now
  archived under `arc/`, with current naming rules in `GUIDELINES.md`.
  Read-only expansion of DISTFILES in 122 generated manifests found this
  as the only missing input (`/tmp/zffb-current-dist-inputs.txt`).
  The obsolete distribution entry was removed after the top-level build
  completed; same-configuration `z.config` regeneration succeeded. The
  repository distdir target then passed with a relative staging path (its
  recursive Automake path handling rejected the initial absolute path).
  `/tmp/zffb-dist-tree-703lkh7p/z-10.0.0` contains 1648 files, including all
  196 byte-identical evidence files and the current two adapter headers and
  their source files. No `zfb`, `Zfb*`, fbscodegen, or non-interop `.fbs`/`.bfbs`
  files remain in that staged tree. Logs/audit are
  `/tmp/zffb-current-dist-fixed.log` and `/tmp/zffb-current-dist-audit.json`.
  Dashboard remains outside the existing top-level DIST_SUBDIRS. Its separate
  distdir check staged 22 files with no old schemas or `Zfb` artifacts
  (`/tmp/zffb-current-dashboard-dist.log`); build/consumer gates remain separate.
  No second configuration/build
  matrix or distcheck build was performed. The other interop modules
  have no separate evidence directories.
  The scheduler-dependent refresh also reran distribution staging:
  `/tmp/zffb-control-dist-ds1w7luw/z-10.0.0` contains 1662 top-level
  distribution files, including all 208 byte-identical evidence files,
  exactly two ZfFB headers, and the current scheduler and hub regression
  sources. No obsolete zfb artifacts or non-interop schemas were found.
  Dashboard was staged separately beneath that tree (22 additional files),
  since root traversal still omits it. Its subdirectory had to be created
  before invoking its standalone Automake distdir target; the retry passed.
  Logs and audits are `/tmp/zffb-control-dist.log`,
  `/tmp/zffb-control-dist-audit.json`, and
  `/tmp/zffb-control-dashboard-dist{.log,-audit.json}`. This verifies current
  packaging inputs; subsequent source/evidence edits require a final refresh.
- Execute the remaining consumer checks against the final refreshed binaries.
  Newer matcher and Skip changes must not inherit earlier binaries' passing
  results for consumers not yet rerun.
  Dashboard module, OAuth and `zdashlivetest.py` with SQLite enabled now pass;
  the live GTK publisher/agent/hub lifecycle fixture covers late discovery and
  generation replacement, following the request-default fix recorded above.
  Zdf's SQLite persistence fixture and GTK value test now pass as recorded
  above; skipped optional fixtures are not passes.
- The complete post-scheduler-refresh release matrix has now run to completion
  on CPU 6, with `ZIFB_BENCH_POOL` unset and no concurrent compilation,
  debugger or functional fixtures. All 30 cases completed their correctness
  prechecks and 11 alternating rounds, yielding 110 comparisons. Raw CSVs,
  commands, runner, matching-release link log, hashes and min/median/max
  sample means are preserved in `zf/interop/evidence/m2-release-refresh/`
  and explicitly listed in `zf/interop/Makefile.am`. Regeneration through
  `z.config` and final distribution staging must include this new evidence.
  The run log is `/tmp/zffb-control-release-measure.log`. Remaining median
  regressions include narrow reflected verification (about 20%), narrow typed
  verification (8–19%), small fresh saves (6–10%), empty-vector fresh saves
  (15%), narrow empty JSON printing (13%), and nested telemetry construction
  (14% reuse, 21% fresh). Replication is now 9–10% faster; telemetry's
  byte-preserving copy path is 8–16% faster. Large vector saves and reflected
  writes also improve. This narrows the unresolved work but does not satisfy
  the no-regression or end-to-end telemetry latency gates. Sample-average
  distributions are not per-message tail-latency percentiles.
- A focused release experiment retained `ZuInline` on only
  `Arena::scalar` and `Arena::offset`. The nested telemetry profile sampled
  these out-of-line primitive writes (`/tmp/zffb-control-telemetry-profile.er`,
  `/tmp/zffb-control-telemetry-functions.txt`); profiling timings are not
  acceptance evidence. No allocation, growth, alignment, ownership or wire
  logic changed. Release measurements justify the narrow-save improvement;
  telemetry executable text grew by 308 bytes (2898127 to 2898435).
  Baseline sizes/hashes are
  `/tmp/zffb-scalar-inline-before-{size,hash}.txt`. The focused build batch
  uses `z.config -c -L /usr` (also incorporating the new evidence manifest),
  then owning Zf/Zi sources and the telemetry/I/O benchmark targets with
  `-j4`. This batch passed without diagnostics, and telemetry canonical
  interop passed all four tests (`/tmp/zffb-scalar-inline-interop.log`).
  Measurements in `/tmp/zffb-scalar-inline-measurements/` include five
  initial cases and three fresh-process repeats of narrow/wide saves and
  telemetry, using CPU 6, 11 alternating samples and the default pool.
  Narrow 64-byte fresh saves now take 95–96 ns versus canonical 101–110 ns
  across repeats; reuse is about 17% faster. Wide fresh saves remain 11–12%
  slower (169–172 ns versus canonical 152–153 ns), slightly worse than the
  preceding snapshot, so this is not a general performance signoff.
  Nested telemetry still regresses about 10–11% reuse and 19% fresh.
  The annotation change does not explain that remaining construction cost.
  This header edit requires final dependent refresh and checks after the
  performance-fix batch; the completed consumer results above apply to its
  preceding snapshot. Do not rebuild all dependents for each experiment.
- A second focused experiment moved the existing reflection `alignment()`
  accessor inline into `ZfFBReflect.hh`, with direct ZuBox/ZuSearch includes.
  Release disassembly showed a PLT call on every vector's verification path,
  including absent attribute vectors. The body is unchanged: sorted attribute
  lookup and existing ZuBox parsing remain, with no added cache, metadata or
  validation. Zf/Zi sources, the I/O benchmark and core canonical interop
  rebuilt sequentially with `-j4`, without diagnostics, and all 41 core
  interop tests passed. Logs use `/tmp/zffb-align-inline-*`. However three
  fresh-process release comparisons worsened narrow reflected verification
  to 24.6–24.7 ns from the preceding 23.4 ns; wide results did not improve
  consistently. Raw runs are `/tmp/zffb-align-inline-measurements/`.
  The experiment was rejected and the accessor restored byte-for-byte to the
  preceding reflection header/source. Restoring the owning libraries and
  benchmark completed (`/tmp/zffb-align-revert-*-build.log`). The two
  separately measured scalar/offset annotations remain. A missing function
  call is not itself evidence of a faster verifier.
- The next focused change factors `Verifier::offset_` for visitors whose
  offset word was already bounded and aligned by `field()` or `vector()`.
  All six typed/reflected table, union and vector-element call sites meet
  that precondition. The public checked `offset()` still checks the word,
  including roots; both paths retain nonzero/range/forward-target validation.
  This removes repeated checks rather than omitting an external-data boundary.
  No metadata cache or additional state is introduced. Owning Zf/Zi sources,
  all Zf tests, core interop and the release benchmark rebuilt in one
  sequential `-j4` batch without diagnostics (`/tmp/zffb-offset-*-build.log`).
  All 21/415 Zf tests and 1/41 core interop tests passed
  (`/tmp/zffb-offset-{tests,interop-tests}.log`). Three fresh-process
  narrow/wide release runs are in `/tmp/zffb-offset-measurements/`: narrow
  typed verification is about 7.26 ns, reflected verification 23.6–23.8 ns
  versus canonical 19.4–19.7 ns. This does not close the reflected gap.
  Wide typed verification improved in the repeated runs (13.2–15.8 ns).
  Retain the removal of checks whose prerequisites are already established;
  no offset-value or destination validation was removed. Focused Valgrind
  rechecks of ZfFBWireTest, ZfFBViewTest, ZfFBUnionTest and ZfFBStreamTest
  all passed with zero errors and zero definite, indirect or possible leaks,
  without suppressions (`/tmp/zffb-offset-ZfFB*.valgrind` and `.tap`). Dependent
  rebuilding remains deferred until the performance-fix batch is settled.
- Release performance acceptance is not met. The first idle-machine run used
  11 alternating samples per mode/size with matching release flags. Raw data,
  commands, build/link evidence and sample min/median/max summaries are in
  `zf/interop/evidence/m2-release-initial/`. JSON printing is near parity;
  typed verification is roughly 3x canonical latency, reflected verification
  roughly 1.7x, small wide saves roughly 1.2–1.3x, replication roughly 1.25x,
  and fresh large table-vector saves roughly 2.25x. The last case also has
  different final capacities (native 297256 versus canonical 245760 bytes),
  requiring growth/allocation investigation. Sample-average distributions
  are not per-message latency percentiles. These regressions remain open.
  A release allocation trace isolated first-growth divergence: canonical
  requests 65536/98304/163840/245760 bytes while native requested
  66576/132112/198168/297256. Native now matches canonical first-allocation
  sizing when the arena contains no payload or scratch; later growth keeps
  the existing policy. A pooled regression check covers the first allocation.
  Assembly also showed repeated external verifier calls with constant field
  metadata; their five small definitions now reside inline in `ZfFB.hh` for
  normal compiler optimization, with no forced-inline attributes or removed
  validation. Focused build passed without diagnostics
  (`/tmp/zffb-release-hotpath-build.log`); Zf 21/414 and ZiFB 1/4 passed.
  Repeat measurements in `zf/interop/evidence/m2-release-hotpath/` now give
  identical 245760-byte capacities for large table vectors; native median
  fresh/reuse times are 0.92x/0.88x canonical. Typed verification improves to
  about 1.45x and reflected verification to 1.4x canonical, still regressions.
  Small wide saves remain about 1.33x. Defer the final dependent refresh until
  these remaining performance corrections are settled.
  A further simplification removed duplicate field-buffer checks already
  covered by verified object bounds, duplicate scalar/inline field checks,
  and per-element inline-vector checks already covered by vector bounds.
  Zf 21/414 passes (`/tmp/zffb-verify-once-tests.log`). Repeat data in the
  hotpath evidence directory shows wide reflected verification at about
  1.28x canonical; narrow typed verification remains about 1.65x and wide
  typed verification about 1.38x. Verification acceptance remains open.
  Separating Arena's capacity fast path from its slow growth body allows
  normal release inlining without forced attributes. Focused build and
  Zf 21/414 plus ZiFB 1/4 checks pass. The 64-byte, million-iteration,
  11-sample results are retained as `arena-growth-*.csv` in the hotpath
  evidence directory. Native/canonical median ratios for reused buffers are
  0.86x narrow save, 0.97x wide save, 0.72x narrow reflected write and 0.92x
  wide reflected write. Fresh-buffer ratios remain 1.15x/1.10x/1.05x/1.04x
  respectively; fresh-buffer acceptance is still open. These measurements
  supersede the earlier small-wide-save result, not other workload gates.
  Typed field verification now uses ZuUnroll's existing boolean accumulator
  rather than capturing mutable result state by reference. Vector verification
  also avoids a second region check after its validated count bound; empty
  vectors still bypass element alignment. The focused release build passed
  without warnings and all Zf 21/414 tests passed. `verify-reduce-*.csv` and
  `verify-reduce-tests.txt` retain the evidence: 11 alternating million-call
  samples at payload size 64 give native/canonical median typed verification
  ratios of 1.21x narrow and 0.92x wide. Reflected verification remains
  1.41x narrow and 1.29x wide, requiring further work. No dependent acceptance
  is inferred from these focused tests or measurements.
  Reflected scalar verification now dispatches directly to constant-width
  field bounds checks, avoiding per-scalar dynamic shape calls and unused
  schema-index reads. The trusted-schema contract already excludes required
  scalar fields; schemaObject continues enforcing that contract. The attempted
  object-shape helper split did not improve release results and was removed.
  Focused release build has no diagnostics and Zf 21/414 passes. Retained
  `reflect-scalar-*.csv` samples give median reflected verification of
  canonical/native 19.51/26.21 ns narrow and 49.99/59.68 ns wide (1.34x/1.19x).
  These improve the preceding measurements but do not meet acceptance.
  Refreshed canonical interoperability passes 1/41. Added reflected scalar
  mutations cover unaligned offsets, fields extending beyond object bounds,
  and valid scalar omission; the refreshed Zf suite passes 21/414. Logs are
  retained as `reflect-scalar-{interop,regression}-tests.txt` alongside timings.
  Reflected table verification now resolves object metadata only for object
  fields, reuses that handle for recursion, and verifies string/vector offsets
  with their fixed wire width. This removes dynamic shape calls and repeated
  object lookups from those branches (GUIDELINES: algorithmic inefficiency,
  switch dispatch and avoiding redundant work). Zf 21/414 and canonical
  interop 1/41 pass. `reflect-dispatch-*.csv` records narrow reflected medians
  of canonical/native 19.87/25.78 ns and wide medians of 50.04/55.50 ns;
  remaining gaps are about 1.30x/1.11x. No verification condition was removed.
  Release assembly identified a dynamic integer division in vector bounds
  verification. The check now compares the 64-bit product of the 32-bit count
  and stride with the available span, preserving overflow-safe bounds without
  division. Added cases cover a product wrapping at 32 bits, UINT32_MAX count
  and stride, exact fit, and overrun. Focused release build and refreshed Zf
  21/414 plus canonical interop 1/41 pass; test logs confirm the modified wire
  test was rebuilt. `vector-product-*.csv` retains 11 alternating samples:
  typed canonical/native medians are 6.50/7.43 ns narrow and 15.58/14.41 ns wide;
  reflected medians are 19.64/24.13 ns narrow and 49.79/57.36 ns wide. Narrow
  reflection improves; the wide result is slightly slower than the preceding
  dispatch-only sample and still fails parity. Performance acceptance remains
  open; these are sample-average timings, not tail-latency measurements.
  Fresh-buffer investigation used gprofng against the existing release
  binary (10 million iterations, 64-byte payload, 11 alternating rounds).
  Its coarse 338-sample function report is retained as `fresh-profile.txt`;
  allocator work and scalar writes are visible, but it does not isolate an
  ownership-transfer regression. Instrumented elapsed times are diagnostic
  only. Uninstrumented follow-ups are `current-save-short.csv` (1 million),
  `current-save-long.csv` (10 million), and `current-wide.csv` (1 million),
  each with 11 alternating samples and payload 64. Narrow canonical/native
  fresh medians are 102.22/108.94 ns for short samples and 101.72/99.09 ns for
  long samples; reuse medians are 57.60/49.75 ns and 56.94/49.36 ns. Wide
  fresh medians are 173.44/170.97 ns and reuse 107.73/102.06 ns. Short native
  fresh samples cluster near both 100 and 109 ns, while long samples are
  more stable. This sample-length/process-state sensitivity remains
  unexplained; neither the faster long sample nor earlier slower snapshots
  alone establish current fresh-buffer acceptance. Pool tuning and ownership
  code have not been changed in response to this inconclusive profile.
  Offset emission now writes directly after its existing alignment step,
  eliminating scalar()'s second alignment. Reflected table/string vectors
  retain the resolved child schema for traversal instead of resolving it for
  every element. A new reflected-vector regression includes tables, strings,
  inline structs and corruption of second-element offsets. The incremental
  release batch passes without diagnostics; Zf 21/415, ZiFB 1/4 and canonical
  interop 1/41 pass. `offset-vector-*.csv` retains 11 alternating samples at
  payload 64 and one million iterations. Canonical/native medians (ns) are
  narrow save reuse 57.26/51.66, fresh 103.81/105.04; wide save reuse
  98.24/101.45, fresh 148.58/164.71. Reflected verification is 19.92/23.57
  narrow and 49.65/54.58 wide; typed is 6.76/7.56 and 15.59/14.60. Save
  results remain variable across batches, and wide fresh saves still regress
  about 11% in this batch. Neither change establishes full acceptance.
  Table/inline-vector and reflected-write preflights pass at payload sizes
  0/64/1024/65536 (`offset-vector-preflight.txt`); these one-iteration runs
  establish correctness only.
  Two further release repeats pinned the process to allowed CPU 6 (core 3,
  socket/node 0), with unchanged binaries and allocator settings. Each used
  one million iterations, payload 64 and 11 alternating samples; raw data is
  `affinity-{save,wide}-{1,2}.csv`. Narrow fresh canonical/native medians are
  103.45/102.36 and 103.77/103.53 ns, near parity in both repeats. Wide fresh
  remains 147.97/163.81 and 152.75/163.11 ns, about 7–11% slower. Wide reuse
  varies from 97.94/99.89 to 107.69/100.40 ns. Thus CPU migration alone does
  not explain the variation or remove the wide fresh regression. Release-loop
  assembly inspection shows both fresh branches call the same ZiIOBuf heap
  allocator and perform the same new-owner increment/old-owner decrement
  sequence; it does not support changing pool identity or reference ownership.
  No source or build-configuration changes were made for these repeats.
  Scratch review confirmed primitive offsets/tags are already uninitialized
  under ZtScratch; no replacement container or allocator was introduced.
  Vector padding now aligns once to max(offset alignment, element alignment),
  equivalent for the supported power-of-two alignments. Offset-vector saving
  checks each produced child immediately, eliminating the later null-offset
  scan and avoiding further serialization after a failed child. Regression
  coverage now confirms a null table element returns Required without
  publishing output. The release batch passes without diagnostics: Zf 21/415,
  ZiFB 1/4 and canonical interop 1/41. CPU-6 runs in `vector-padding-*.csv`
  retain 11 alternating samples. At payload 64, canonical/native medians are
  narrow reuse 56.19/51.52 ns, fresh 99.33/113.26; wide reuse 100.62/98.58,
  fresh 152.56/171.48. Small fresh saves remain regressions. At payload 65536,
  table-vector reuse is 119.70/74.44 us and fresh 152.67/111.27 us, with both
  capacities exactly 245760. This validates the tested growth outcome; it
  does not establish allocation/copy counts or acceptance for other workloads.
  An ordinary-inline alignment-helper experiment retained an out-of-line
  compiler call and did not improve reflection: CPU-6 medians were
  canonical/native 19.69/25.42 ns narrow and 50.27/56.17 ns wide. Zf 21/415
  and canonical interop 1/41 passed. The source move was reverted; no extra
  public helper or forced attribute was retained. `reflect-align-rejected.txt`
  records experimental hashes, with raw samples and tests alongside it.
  The restored reflection header matches its pre-experiment SHA-256. The
  restored library and pooled benchmark rebuilt successfully; narrow/wide
  operation preflights pass (one iteration, correctness only). All 177 retained
  evidence paths are accounted for in EXTRA_DIST.
  A release-only ZuInline annotation on the local reflected shape helper
  removes its hot-path call with 120 bytes of libZf text growth (601201 to
  601321; data/BSS unchanged). Matched CPU-6 before/after samples retain an
  essentially unchanged narrow result (native 23.47 to 23.49 ns), and a small
  wide improvement (52.26 to 51.63 ns). Corresponding canonical after medians
  are 19.77/49.42 ns; parity remains unmet. Zf 21/415 and interop 1/41 pass.
  The single annotation is retained with its limited evidence and code-size
  cost in `shape-inline-review.txt`; `shape-{before,after}-*.csv` preserves
  raw samples. Unlike prior debug-only forced-inlining experiments, this
  change is based on release assembly and release timing, but it does not
  establish a broad speedup or close the performance gate.
  The pooled operations benchmark now additionally verifies identical
  canonical BFBS and identical native BFBS with both implementations, with
  handles prepared outside timing. Original own-schema measurements remain.
  Preflight verification requires true rather than merely equal outcomes.
  The focused release benchmark build passes without diagnostics. CPU-6,
  million-iteration, 11-sample comparisons (`schema-cross-*.csv`) give
  canonical/native narrow medians 19.62/22.70 ns on canonical BFBS and
  19.68/23.71 on native BFBS; wide medians are 50.19/51.37 and 50.21/51.94.
  Thus metadata layout accounts for only a small part of the remaining gap;
  no more elaborate BFBS compaction/layout design is justified by this result.
  `schema-cross-review.txt` records commands, interpretation and hashes.
  An empty-attribute early return in reflected alignment lookup was tested
  and rejected: narrow own-schema verification changed 23.67 to 23.38 ns,
  but wide changed 51.83 to 52.67 ns, with mixed shared-schema results.
  The extra branch is removed; `empty-attrs-{ops,wideops}.csv` and
  `empty-attrs-review.txt` preserve the result. Zf 21/415 and canonical
  interop 1/41 passed for that experiment. No acceptance gate is closed.
  The restored reflection source matches its pre-experiment SHA-256 and
  libZf rebuilt without diagnostics. Release GDB allocation tracing now uses
  the actual 64-byte cache entry and excludes variable-allocator frames from
  fixed-pool counts. Three-iteration narrow/wide/vector traces match between
  codecs: reuse/fresh pool requests 1/4; large-vector variable allocation
  sequences 65536/98304/163840/245760 once/thrice. Wide row-copy setup accounts
  for identical 41/64-byte requests outside timing. `allocation-review.txt`
  and three raw traces record scope, commands and hashes. Growth-call/copy
  counters were initially unmeasured and omitted, replacing misleading zeroes
  from breakpoints unavailable in optimized code. A subsequent x86-64 release
  trace now counts actual memcpy/memmove call-site size registers and growth
  entry state, requiring all three expected copy sites in each growth
  function. `copy-{row,wide,vectors}.txt` and `copy-review.txt` retain raw
  results, commands, scope and hashes. Row/wide 64-byte cases have zero
  growth copies; large vectors match at 294916 copied bytes/four copy calls
  in reuse and 884748 bytes/twelve calls across three fresh iterations.
  Nonempty extensions match at three/nine. Native reports one extra empty
  initial extension per buffer (canonical uses its zero-capacity allocation
  path), without an extra physical allocation or copy. Allocation requests
  still match. Nested/full-application allocation and timing gates remain
  open. The four new evidence files are in EXTRA_DIST; defer regeneration
  through z.config and distribution refresh to the next final evidence batch.
  The trace driver now also requires an observed successful inferior exit
  and all four codec/mode rows, exiting GDB nonzero on incomplete capture or
  setup errors. This prevents a debugger-level success from masking failed
  diagnostic execution. The guard check passed after the top-level build:
  valid `3 64 1` completed with all counters, while invalid `0 64 1` returned
  status 1 and emitted no counters. Logs:
  `/tmp/zffb-trace-guard-{success,failure}.log`. This diagnostic ran during
  the independent dashboard build and provides no timing evidence.
  The benchmark now accepts optional ZIFB_BENCH_POOL to configure the fixed
  ZiIOBuf pool through existing ZmHeapMgr, equally for both codecs. Default
  behavior and production heap settings are unchanged. GDB confirms every
  fixed request falls back with the unconfigured default, and none sees an
  empty pool with four blocks. On CPU 6, 11 alternating release rounds of
  one million 64-byte rows yield canonical/native fresh medians 67.454/63.051
  ns (configured pool) versus 101.049/105.717 ns (fallback); wide medians are
  108.984/109.959 versus 152.890/165.013 ns. Configured pooling removes the
  narrow fresh gap, but does not excuse the fallback regressions or establish
  why equal heap request sequences differ. Preserve both regimes in final
  acceptance. `pool-review.txt`, four CSVs, two allocation traces and build
  log are in the evidence manifest; no production code changed for this
  diagnostic. Values are medians of round means, not message percentiles.
  Existing debug timings are diagnostic history. The pooled benchmark is in
  `zi/interop`, keeping `zf` independent
  of `zi`; `/tmp/zifb-release-bench` links the matching-release canonical
  reflection object. One-iteration correctness preflights exposed rejection
  of canonical empty 64-bit vectors: their empty data area need not have
  element alignment. The verifier now skips element alignment only when the
  verified count is zero, retaining header/bounds checks. A focused wire test
  covers empty, truncated and nonempty unaligned cases and passes after a
  focused incremental build without diagnostics
  (`/tmp/zffb-empty-vector-{build,wire}.log`). All seven
  pooled modes pass preflights at 0/64/1024/65536 bytes, as do replication and
  telemetry benchmark modes (`/tmp/zffb-release-benchmark-preflight-v2.log`).
  These one-iteration checks are not performance acceptance measurements.
- Newly built benchmark modes cover inline/table vectors (`vectors`), cached
  reflected writer primitives (`write`, `widewrite`), and nested replication
  with database pooling/framing (`zdbmsginterop --bench`). Telemetry also has
  nested-union/routing-copy comparisons (`ztcmsginterop --bench`). Setup and
  schema association are outside timing; cross-codec preflight checks precede
  samples. Reflected kernels exclude native-value conversion/DB scheduling.
  Canonical bytes use the old CreateVector path and table-vector offsets use
  the old ZmScratch policy. Both codecs expose full output buffers to identical
  zero-instruction compiler barriers. Compile and inspect optimized loops,
  then collect idle-machine latency distributions, allocation/growth/copy
  counts, sizes and material code-size/compile-time effects.
- Full-scale release telemetry load failed in the refreshed run recorded
  above with TLS `bad_record_mac` followed by AgentGone and incomplete delivery.
  A 128-agent diagnostic retained all 1024 subscriptions and publication rate
  and reproduced AgentGone/bad_record_mac after 5158 samples; see
  `/tmp/zffb-diag-load128.log` and `ztchubtest.duzjxpc4`.
  Source review found scheduler callbacks could bypass pending overflow work
  by entering newly available ring space. A deterministic `ZmSchedTest`
  regression holds the worker while a large callback overflows and a later
  small callback fits the ring: ordering fails before the fix and passes
  afterward (`/tmp/zffb-scheduler-order-{before,after}.log`). `tryPush_` now
  refuses direct ring admission while queueCount is nonzero, preserving the
  existing overflow queue and worker drain without adding locks or storage.
  The focused libZm/test build has no diagnostics. With the fix, the same
  128-agent diagnostic delivered all 786432 samples with no bad_record_mac
  or load-failure messages, but FAILED the 200 ms latency gate:
  max_latency_us=12874810, fanout_us=13. Evidence is
  `/tmp/zffb-diag-order128.log` and `ztchubtest.76g1zvxf/{load,hub-0}.log`.
  This supports callback reordering as the integrity failure's cause while
  leaving a substantial latency deficit. The full 2048-agent workload then
  delivered all 786432 samples with no bad_record_mac/load-failure messages,
  but FAILED latency at max_latency_us=14943687 (fanout_us=81). Logs are
  `/tmp/zffb-full-order.log` and `ztchubtest.abruwd9v/{load,hub-0}.log`.
  Integrity improved at full scale; the 200 ms gate remains unmet. The first
  gprofng diagnostic wrapped the Python fixture and aborted with
  `free(): invalid pointer` (`/tmp/zffb-hub-profile.log`); it yielded no usable
  profile and is not an application-test result. A temporary launcher now
  leaves Python uninstrumented and profiles only the C++ peer through libtool
  (`/tmp/zffb-profile-peer.py`, `/tmp/zffb-peer-profile.er`,
  `/tmp/zffb-peer-profile.log`). The completed peer profile delivered all
  786432 samples, with instrumented max_latency_us=12972529. Of 26.849 s
  sampled CPU, send/recv account for 32.65%, syscall for 15.13% (callers are
  scheduler ring wait/wake), scheduler work/push for 10.29% exclusive, and
  overflow-array moves for 4.62%. posix_memalign accounts for 12.86%
  inclusive; its principal allocation callers are variable I/O buffers and
  TLS Tx buffers, with App::tick contributing only 2.07% of the sampled
  aligned-allocation caller total. Reports are
  `/tmp/zffb-peer-{functions,callers,alloc-callers}.txt`. These observations
  identify transport, scheduling and allocation costs for investigation;
  they do not establish the hub bottleneck or canonical codec parity. A
  separate hub-only profile uses `/tmp/zffb-profile-hub.py`, retaining the
  same 128-agent diagnostic and full publication rate. It completed with all
  786432 samples and max_latency_us=12531618 (fanout_us=13), again failing
  latency. Of 21.355 s sampled hub CPU, send/recv account for 32.52%,
  scheduler overflow-array moves for 10.87%, clock reads for 8.76%, and
  syscall for 7.59%. Caller attribution places all sampled overflow moves
  in ZmQueue::extend, rather than shift/unshift; source inspection confirms
  those latter operations use circular indices. Do not change scheduler
  draining on the unsupported premise that every shift moves the queue.
  Reports: `/tmp/zffb-hub-direct-{profile.log,functions.txt}` and
  `/tmp/zffb-hub-moves.txt`; fixture `ztchubtest.o8qhdd5f`. The next diagnostic
  must separate publication lag from hub delivery lag and quantify queue
  growth before selecting a fix. The load peer now also reports
  max_publish_lag_us, measured immediately before handing each encoded frame
  to the Tx stream, relative to its scheduled publication time. The existing
  end-to-end SLO is unchanged; the new metric adds no per-message storage.
  A focused Clang release build completed without diagnostics
  (`/tmp/zffb-publish-lag-build.log`). The unprofiled 128-agent diagnostic
  delivered all 786432 samples with max_latency_us=18319306,
  max_publish_lag_us=2540373 and fanout_us=15, failing the 200 ms gate
  (`/tmp/zffb-publish-lag.log`, fixture `ztchubtest.23ebor5g`). Thus at least
  15778933 us of latency occurred after producer handoff for some message;
  this includes local Tx scheduling, transport, hub work and receive-side
  scheduling, not just time inside the hub. These maxima must not be treated
  as additive component measurements or attributed solely to the codec.
  Queue growth uses ZmGrow's 64 KiB steps above 64 KiB; the observed moves
  are capacity extensions. Further diagnosis must locate backlog ownership
  before changing buffering or scheduling. A subsequent GDB capacity-growth
  trace located the hub backlog on scheduler sid=1 (Rx), with 1046528
  pending callbacks at capacity 1048576. Stacks show Ztls::recvRecord
  dispatching back to the same Rx thread and ZiMultiplex::wakeRx adding
  further callbacks. No other scheduler queue reached the trace's 1024-entry
  reporting threshold. Evidence: `/tmp/zffb-hub-queue-growth.txt`, launcher
  `/tmp/zffb-queue-hub.py`, debugger script `/tmp/zffb-queue-growth.gdb`, and
  `/tmp/zffb-queue-hub.log` (all 786432 delivered; debugger timings excluded
  from acceptance). Source inspection confirms hub I/O and application Rx
  both use sid=1. Ztls::connected_0 and recvRecord now use existing rxInvoke
  instead of rxRun, preserving queued cross-shard dispatch while avoiding
  self-queueing on the owner. Both handoffs change together so connection
  initialization precedes records. ZiRx saves trailing bytes before invoking
  the body, retaining the buffer-lifetime contract. This follows GUIDELINES
  General principles (thread-affined state and low-latency dispatch) and
  existing Z framework dispatch; no new buffers, locks, or queue algorithms.
  Focused retained-release builds completed without diagnostics;
  `/tmp/zffb-rx-invoke-build.log` records the sequential -j4 batch. All 16
  ZtlsBufHookTest checks passed (`/tmp/zffb-rx-invoke-tls.log`). The same
  unprofiled 128-agent workload delivered all 786432 samples with
  max_latency_us=8016079, max_publish_lag_us=2542299 and fanout_us=11
  (`/tmp/zffb-rx-invoke-load.log`, fixture `ztchubtest.ob6q5vxf`). This is a
  substantial improvement over the preceding 18319306 us run, with nearly
  unchanged publication lag, but still FAILS the 200 ms gate. Retain the
  dispatch correction; it does not establish complete performance acceptance.
  A peer-side GDB trace then located 260096 pending I/O Tx callbacks
  (sid=2, from application Tx sends and socket-ready dispatch) and 63488
  pending application Rx callbacks (sid=3, from I/O Rx record dispatch).
  Evidence: `/tmp/zffb-peer-queue-growth.txt` and `/tmp/zffb-queue-peer.log`.
  This diagnostic delivered all samples but reported 8660190 us latency,
  which is a failure. GDB initially masked the inferior exit status and the
  outer fixture printed a false passing summary; the diagnostic launchers
  now use --return-child-result. Do not count that summary as acceptance.
  The load generator uses eight scheduler threads (four unused) and separate
  I/O/application shards, unlike the hub's shared Rx/Tx ownership. Investigate
  unnecessary dispatch in the generator while retaining all workload counts,
  scheduled timestamps and SLOs; shared-shard and separate-shard TLS behavior
  must both remain covered.
  Source inspection confirms that TLS send already uses txInvoke and that
  established sockets use EPOLLET. Do not infer a level-triggered readiness
  storm from the queue-growth producer stacks alone. The prepared diagnostic
  `/tmp/zffb-peer-queue-mix.gdb`, launched by `/tmp/zffb-queue-mix.py`, reads
  queued callback invocation addresses at geometrically spaced growth points
  and groups them by symbol. It checks queue bounds/counts and capture errors;
  the post-rebuild run captured five valid I/O Tx queue snapshots without
  capture errors (`/tmp/zffb-peer-queue-mix.jsonl`). At the largest snapshot,
  248809 of 260096 callbacks were ZiConnection::send dispatches, with 11287
  readiness dispatches. Both ZiConnection::send and TLS send already use
  txInvoke; the load peer deliberately separates application Tx from I/O Tx.
  This establishes queue composition, not the reason its consumer cannot
  keep up. All 786432 samples arrived, but the instrumented run failed the
  latency gate (9027117 us, publish lag 2854389 us, fanout 9 us) and correctly
  propagated exit 1. Logs are `/tmp/zffb-control-queue-mix.log` and
  `ztchubtest.ene68s13/load.log`. These are queue snapshots, not total
  dispatch counts or performance acceptance measurements.
  A subsequent syscall-counting diagnostic tested whether ZiTx's send attempt
  on each enqueue caused repeated EAGAIN retries. The 128-agent fixture
  issued 789184 sendto calls with zero failures
  (`/tmp/zffb-send-syscalls.txt`, launcher `/tmp/zffb-send-syscalls.py`).
  This rejects that explanation for this run; no ZiTx change was made.
  All 786432 samples arrived, but ptrace greatly perturbed publication and
  latency (about 75 seconds), so its timings are not acceptance evidence.
  The expected failing SLO propagated exit 1, and the fixture cleaned up
  its processes. Logs are `/tmp/zffb-send-syscalls.log` and
  `ztchubtest._t24crjb/load.log`. The application binaries precede the latest
  codec-header experiments; this is transport diagnosis, not refreshed
  consumer acceptance for those edits.
  Host scheduling review found all 16 logical CPUs allowed, no finite CPU
  quota in the process's cgroup ancestors, and zero recorded throttling
  (`/tmp/zffb-host-cpu-review.json`). The peer uses separate application/I/O
  Rx/Tx shards; `isolated` excludes scheduler worker selection, rather than
  imposing CPU affinity or permanent busy spinning. No topology change was
  made. TLS currently provisions 16 KiB storage in allocTxBuf_ before writing
  even small messages. Local zpicotls source confirms in-place encryption
  calls reserve_aligned and can emit KeyUpdate output before application
  records; resizing during that operation can invalidate aliased plaintext.
  Do not remove this storage provision merely from allocation counts without
  proving sufficient stable capacity for all supported cipher/control paths.
  No TLS allocation/default/heap change was made from this inspection.
  The dependent refresh for the Ztls header change is complete. Instrumented
  timings cannot satisfy acceptance. Earlier debug attempts
  encountered TLS setup failure, premature subscription pipelining (fixed by
  first-ACK gating), then a reproducible Rx self-wake pipe deadlock after all
  786,432 samples were sent. Linux semaphore-mode eventfd preserves counted
  wakes; dropping pipe writes on EAGAIN had failed. The 131,072-callback
  regression and its Valgrind check passed, including shutdown. UDP TOS
  control padding was also fixed from that memory-check evidence. These
  focused passes do not establish the full-load latency/delivery gate.
- Complete the requirement-by-requirement final audit; do not close the goal
  while consumer, performance, memory interpretation or final consistency
  evidence remains outstanding.

## Permanent questions and decision record

Question IDs are permanent. Append new questions using the next unused
number; never renumber, reuse, or delete an ID. Resolve a question in place
by changing its status and recording the decision and affected plan sections.
If a question is superseded, retain it and link its successor. References to
these IDs remain valid after edits and resolution. The next unused ID is Q18.

Q1–Q17 are resolved or superseded below; no requirement question is pending.
Future questions must identify a genuinely missing requirement, rather than
an existing Z contract or a routine implementation choice. This record does
not reopen `ZfFB` placement in `zf`, deletion of `zfb`/`ZfbStruct`, canonical
interoperability, in-memory BFBS streaming, compile-time schema specialization,
or the two required schema-output library functions (Q14).

### Q1 — Compatibility with existing application data and peers

**Status:** Resolved. **Affects:** Outcome and working decisions, Slice 1,
all consumer migrations, Q7, and `goal.md`.

Must the replacement preserve every current live application's wire schema
for existing persisted rows, saga payloads, alert files, and deployed peers,
or are any named contracts allowed to change in a coordinated migration?
Canonical format interoperability is required either way; it does not decide
compatibility with a particular earlier application schema.

**Decision:** backwards/legacy compatibility is a non-goal. There is no
requirement to preserve compatibility with existing persisted data, deployed
peers, application schemas, or C++ APIs. Propagate changes through current
consumers without legacy readers, shims, or data-migration tooling for old
formats. Canonical FlatBuffers payload and BFBS interoperability remains
required for the supported features and new schemas. The earlier proposal
to preserve all existing wire contracts is rejected.

### Q2 — Boundary of the no-runtime-branching requirement

**Status:** Resolved. **Affects:** Outcome and working decisions, Slices 2
and 4, compiled-code acceptance, Slice 11, and `goal.md`.

Is the requirement satisfied by a fixed schema-emission sequence with all
metadata decisions resolved at compile time, while ordinary buffer capacity/
error handling and byte-copy primitives may branch, or must even capacity
checks be absent from the emitter's call path?

**Decision:** buffer capacity, error handling, byte-copy primitives, and
anything relating to actually streaming the data are exempt from the
no-runtime-branching requirement, including within the emitter's call path.
Schema/type/field decisions and metadata inclusion remain compile-time
resolved. Pre-reservation and moving failure handling before emission are
optional implementation choices, not conditions of compliance. Inspect the
metadata decisions rather than requiring branchless streaming primitives or
an entirely branchless call tree.

### Q3 — Supported canonical schema features

**Status:** Resolved. **Affects:** Outcome and working decisions, Slices 1–6,
the interoperability matrix, Q6, and `goal.md`.

Is initial support limited to the features used by this repository plus the
canonical reflection schema itself, or must `ZfFB` support additional canonical
features from the outset? Examples needing an explicit scope decision include
nullable scalars (`= null`), required/deprecated fields, sorted keyed vectors,
forced alignment, size-prefixed roots, union vectors, and wider offset variants.

**Decision:** limit support to features used by this repository, including
the canonical BFBS reflection contract, with these explicit decisions:

**Scalar clarification (resolved):** existing `zfb` support for missing
optional scalar fields that return defaults is required. The exclusion below
applies only to nullable scalars declared with `= null`. Do not interpret it
as excluding optional fields with ordinary scalar defaults.

| Feature | Scope and implementation contract |
| --- | --- |
| Required fields | In scope; map canonical `required` to existing `Required`. Respect canonical restrictions on eligible wire kinds; diagnose unsupported combinations. |
| Sorted application vectors | Out of scope, including sorted-table construction/search convenience APIs. Canonical BFBS metadata ordering and reflection name lookup remain required. |
| Forced alignment | In scope; express it through a `ZfFB`-specific field property and carry its effect into writing, buffer alignment, verification, reflection, and interoperable exports. |
| Size-prefixed roots | In scope; select prefix presence via a compile-time parameter of `load`/`save`, with matching verification and root finishing. Support both modes without runtime mode detection. |
| Wider offset variants | Out of scope; use ordinary 32-bit FlatBuffers offsets. |
| Missing/defaulted scalar fields | In scope, as supported by existing `zfb`: absent fields read as their wire default, including implicit zero/false and explicit nonzero defaults. Default omission, field-presence access, and forced-present default values remain supported. |
| Nullable scalars (`= null`) | Out of scope under the repository-use rule: no use found in repository schemas. This means a distinct absent/null result rather than a scalar default; it does not exclude missing/defaulted scalar fields. |
| Deprecated fields | In scope by the subsequent explicit Q6 decision: a metadata-only `Skip` accessor in `ZuStruct`/`ZfStruct` implies canonical `deprecated` for `ZfFB`. This supersedes the earlier exclusion based on repository use. |
| Union vectors | Out of scope under the repository-use rule: no use found in repository schemas. Support for individual union fields remains required. |

A union vector is a heterogeneous sequence whose elements independently
select an alternative of one union type. For example, `union Body { Ping,
Alert }` with `items:[Body]` can hold Ping, Alert, then Ping. Canonical encoding
uses two corresponding vectors: discriminators and payload offsets. This
requires paired-length/tag/payload verification and per-element dispatch.
The repository's `body:Body` message fields hold one union value each and do
not require this vector feature. See the canonical
[union-vector example](https://github.com/google/flatbuffers/blob/master/tests/union_vector/union_vector.fbs).

Diagnose unsupported metadata/features at the appropriate boundary. Reading
BFBS schema fields describing unsupported features does not commit the codec
to constructing or interpreting payloads that use those features.

### Q4 — Reflection from an external BFBS span alone

**Status:** Resolved. **Affects:** Outcome and working decisions, Slices 2–4,
Slice 6 schema-output functions, the store/reflection API, Q14, and `goal.md`.

For a canonical-produced BFBS span with no matching C++ type or native
companion descriptors, must `ZfFB` provide generic payload verification,
field access, construction, and schema export, or only schema inspection and
the subset of operations currently exercised by local consumers?

**Decision:** payload verification is in scope, including reflection-driven
verification already used by the repository. Verify all external data before
loading it. Data generated and parsed within the same process requires no
validation or re-verification; consume generated payloads and BFBS spans
through trusted access. Previously verified external data does not require
repeated checks during later access. External nested payloads must be covered
before loading; verifying only their enclosing byte vector is insufficient.

Binary schema generation streams canonical BFBS bytes into its templated
destination. The usual buffer destination exposes the resulting span.
Binary schema export simply writes those bytes, optionally
to a `.bfbs` file; no additional schema conversion or verification is needed.
The required textual FBS library function renders the same generated schema
metadata. The earlier proposal for generic textual conversion of arbitrary
external BFBS is not a requirement and is withdrawn.

Retain reflected access/construction needed by audited consumers and canonical
BFBS interoperability within Q3's scope. Store operations retain their Z
field/key information and native associations. Q4 does not add a general
external-schema transformation toolkit. Verification in implementation and
interop tests remains necessary evidence of correctness, without adding
verification passes to trusted production paths.

### Q5 — Implicit IDs when explicit IDs are mixed in

**Status:** Resolved. **Affects:** Slice 2 numbering, both exporters, Q6,
and `goal.md`.

What exact rule should resolve an omitted ID after an explicit one? For
example, with `a` implicit, `b` explicitly 7, and `c` implicit, should `c`
receive 8, its declaration ordinal 2, or should that mixed declaration be
rejected? How should a later explicit ID below the cursor affect subsequent
implicit numbering?

**Canonical behavior:** without explicit IDs, table fields receive IDs in
declaration order, counting the hidden union discriminator slot. If any
field specifies `id`, all declared fields in that table must specify it.
Resolved IDs must form a contiguous range starting at zero; declaration
order may differ from ID order. A union's explicit ID names its value slot,
and its hidden discriminator uses the preceding ID. Consequently the mixed
`a` implicit / `b` 7 / `c` implicit example is rejected by `flatc`.
See the canonical [field-ID rules](https://flatbuffers.dev/schema/#attributes).

**Decision:** follow the canonical all-implicit or all-explicit rule above.
Diagnose mixed declarations, duplicate IDs, gaps, and overflow at compile
time for the complete wire declaration, regardless of whether schema export
is requested. Explicit union IDs identify value slots; discriminator slots
participate in uniqueness and contiguity checks. Filtering does not renumber
IDs or require every field to be present in a payload. This replaces the
earlier mixed-ID cursor proposal. Q6's declared `Skip` entries participate in
these rules and keep the complete schema contiguous.

### Q6 — Retired field metadata and gaps in exported schemas

**Status:** Resolved. **Affects:** `ZuStruct` accessor taxonomy, `ZfStruct`
field machinery, Slice 2, typed/reflected operations, interop tests, both
schema exports, Q3, and `goal.md`.

How should a retired wire field remain declared after its C++ member/accessor
is removed? A reserved number alone does not retain its former type/default
for schema export, and sparse explicit IDs need a defined representation in
canonical textual schemas.

**Decision:** extend the `ZuStruct` accessor taxonomy with `Skip`, inherited
by `ZfStruct`. `Skip` declares metadata without a live C++ member or object
getter/setter. The underlying C++ struct/class need not declare any
corresponding data member or member function; declaring a `Skip` field must
not require member lookup or create storage in the object. For `ZfFB` it
implies canonical `deprecated`. Retain the name,
declared type, properties/default, and wire ID in the one declaration so BFBS
and FBS can describe the actual field. This explicit feature addition
supersedes Q3's earlier exclusion of deprecated fields and Q6's prior closure
without metadata-only fields. Q1's legacy compatibility non-goal is unchanged.

Include skipped fields in complete schema numbering and reflection/export
metadata; exclude them from object construction, load/save/update/delete,
tuple/key extraction, object print/JSON, and object accessor factories.
This is a format-independent `ZuStruct`/`ZfStruct` contract: every format's
`load` and `save` simply skips these fields and never calls their `get` or
`set`. Metadata remains available solely for metadata uses such as schema
generation, reflection, and wire-ID accounting; it does not represent an
object value to load or save.
Apply exclusions at compile time before getter/setter instantiation; a
read-only flag alone is insufficient. Skip object-use properties without
instantiating accessors; diagnose only unsupported canonical combinations
(such as `Required` plus deprecated). Generic BFBS inspection exposes the deprecated
field, with no corresponding native object accessor.

Q5 remains in force: all implicit or all explicit IDs, unique and contiguous
from zero including declared `Skip` entries and union discriminator slots.
A skipped field occupies its real schema slot; do not invent placeholder
types or permit unoccupied gaps. An ordinary `Skip` field consumes one ID
even though it has no C++ accessor and is omitted from new payloads. A
skipped union consumes its discriminator and value IDs, as any union does.
Use `(ID, Skip, T)` as prescribed in Slice 2; `T` supplies the metadata type
without referring to a C++ member.

**Implementation evidence:** current `ZuField_*` accessors infer `T` from
an object member/getter, `ZuStructTupleT_` and tuple extraction instantiate
`Fields::get`, and `ZfVField` construction instantiates `getFn`/`setFn`.
`ZfFieldFilter::All` currently includes every field and object printing only
filters `Hidden`. The change therefore spans both layers' field machinery,
not just a `ZfFB` serialization filter.

### Q7 — Naming of exported types and fields

**Status:** Resolved field requirement; type naming prescribed as an implementation choice.
**Affects:** Slice 2, schema-output functions, reflection consumers, and `goal.md`.

What rule derives qualified BFBS/FBS type names, namespaces, and field
spellings from `ZfStruct` metadata, and where should explicit wire-name
overrides live? Q1 removes any requirement to retain legacy schema names
such as `Zfb.Fixed` and `Zfb.IP`.

**Decision for fields:** follow the `ZfJSON` mechanism: an optional
format-specific field property supplies an alternative textual ID, and facet
property overrides select facet-specific names. Resolve it from the selected
facet's effective field properties, following `JSON::ID<...>` and
`GetID<Field>`. Use the explicit alternative verbatim. With no override,
derive the FlatBuffers field name from `Field::id()` using the existing
`ZtCase::camelSnake` behavior: each uppercase character becomes an underscore
followed by its lowercase form, leaving other characters unchanged. For
example, `roleIDs` becomes `role_i_ds`; do not substitute a different acronym
normalization algorithm.

This textual ID is distinct from the numeric FlatBuffers field ID. Resolve
names at compile time, use them consistently in BFBS/FBS and reflection, and
keep native field associations valid when the name is overridden. JSON and
FlatBuffers name overrides remain format-specific. This also applies to
metadata retained by `Skip`.

**Evidence:** `zf/src/ZfJSON.hh` defines the optional ID property and its
fallback resolver; current store reflection joins use `ZtCase::camelSnake`
on field IDs.

**Type naming prescription:** supply qualified names as compile-time strings
through ADL wire traits, using the application namespace/type name with dot
separators, `Zf` for core helpers, and `Zi` for IP helpers. Give distinct
template wire types explicit names and reject reachable name collisions.
This routine implementation choice closes the former naming ambiguity; it
does not change the user's field-name requirement or create a schema catalog.

### Q8 — Z-specific information in portable BFBS/FBS

**Status:** Resolved. **Affects:** Slice 2 reflection, metadata translation,
interop exports, and `goal.md`.

Should exported schemas carry Z metadata such as multiple keys, descending/
group flags, `Mutable`, facets, or UDT identities as custom attributes, or
should those remain only in native generated companions? Does an external
BFBS consumer need to recover any of that information without C++ metadata?

**Decision:** normalize Z <-> FlatBuffers metadata by semantic meaning. If
an equivalent meaning exists on both sides, map the metadata accordingly;
otherwise that information is lost in translation. Do not add custom
attributes, opaque payloads, or a side-channel format to preserve metadata
with no equivalent. Apply the same rule to BFBS and textual FBS.

Mappings include `Required`/canonical `required`, `Skip`/`deprecated`, and
representable `Deflt` values/canonical defaults. Similar terminology alone
does not establish equivalence: multiple Z keys, grouping, or descending
order must not be relabeled as a canonical sorted-vector key. Native Z
metadata can still serve local object/store operations; its existence does
not imply it survives export and import. This does not expand Q3's supported
feature set or introduce a generic schema-import toolkit.

### Q9 — Selection and meaning of wire defaults

**Status:** Resolved. **Affects:** Slice 2 properties, typed/reflected
load/save, both schema exports, migration fixtures, and `goal.md`.

For new ordinary `ZfStruct` fields, should an explicit existing `Deflt<>`
automatically become the wire default when representable, or must wire
defaults always use a separate property? What should an unannotated scalar
use when the Z field's default is a null sentinel rather than zero?

**Decision:** reuse existing Zf default-value logic: `Deflt` if set, otherwise
the same type-specific fallback used by other Zf adapters. Consume that
resolved value through the existing `Field::deflt()` machinery; do not
reimplement fallback selection in `ZfFB`. Do not introduce a separate FlatBuffers default property or
replace resolved Z defaults with an independent zero/false policy.

Map representable defaults consistently to canonical scalar defaults for
writer omission, missing-field reads, reflected access, BFBS, and textual
FBS. Annotate application declarations with `Deflt<>` where their intended
default needs to be explicit. Apply Q8 when a default has no equivalent in
canonical schema metadata; do not invent a noncanonical encoding or a custom
attribute to preserve it. Field presence remains distinct from the default
value, and `Required` still disallows absence for eligible fields. This
supersedes the earlier separate-wire-default proposal.

### Q10 — Presence when serializing a borrowed view

**Status:** Resolved. **Affects:** Slices 3–5, Slice 8 telemetry forwarding,
and `goal.md`.

**Meaning:** read field values directly from an existing serialized buffer
and serialize them into a new buffer, without first constructing an owning
application object. The "view" is the accessor object pointing into the
source buffer; "borrowed" means it does not own that storage. The source
buffer must remain alive while its fields are read. Existing
`ztc/src/ztchub.cc::copyTelemetry` does this with
`ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>(builder,
*source->value_as_Name())` when rebuilding a telemetry envelope.

Should saving a borrowed view preserve source field presence, or should it
read logical values and apply the normal object-saving omission/default
rules? These differ for absent versus empty strings/vectors and explicitly
present default-valued scalars needed for mutation.

**Original motivation:** a scalar physically present with value equal to its default
can become absent when serialized using normal default-omission rules.
Reads still return the same value, but subsequent in-place mutation needs
the physical slot. The resolution below avoids repeating the original
serializer's decisions altogether.

**Decision for unchanged copies:** use `memcpy` of the complete serialized
span instead of reading and saving each field. This preserves physical field
presence, default-valued slots, and unknown fields automatically. Do not
justify a separate field-wise presence-preserving serializer solely by the
current `copyTelemetry` implementation.

**Current implementation constraint:** `copyTelemetry` copies unchanged
telemetry into a new builder, while its caller `agentFrame` changes routing
fields (`subID`, `deviceID`, `agentGeneration`) in the outer `Msg`. The
telemetry argument is a table pointer within the original buffer, not a
standalone buffer with a known length. Its referenced strings, payload
tables, and vtables need not form an independently copyable slice beginning
at that pointer. Carry a complete source span/owner and arrange the payload
as an independent nested root including its union tag, carried as a byte
vector in the routing envelope. Propagate this layout through all consumers
so raw copying retains every offset target and alignment. Build only the
changed routing envelope separately. Q1 permits
the necessary schema/consumer changes. No re-verification is needed for the
trusted copy (Q4).

**Decision for already serialized spans:** field inclusion/exclusion was
decided by the original serializer. Preserve that result without repeating
field selection, save/update/delete filters, default omission, or other
object-saving rules. Present default-valued scalars and empty strings/vectors
remain present; absent fields remain absent. Do not materialize missing
values from `Deflt` or add a new selection pass over the fields. Raw span
copying preserves the result, including fields unknown to the current schema,
without needing to decide their inclusion individually.

Ordinary object serialization continues to use Q9's default logic and Q6's
`Skip` contract. Raw copying invokes neither `get` nor `set` and does not
reinterpret or remove existing bytes for a field now marked `Skip`. The
earlier statement that field filters should be reapplied when copying a
serialized view is withdrawn.

### Q11 — Missing inline values during object construction

**Status:** Resolved. **Affects:** Slice 2 defaults, Slices 3 and 5,
typed and reflected loads, and `goal.md`.

When a structurally valid table omits an inline struct used for a Z value
such as `ZuTime`, `ZuDecimal`, or a 128-bit integer, should construction
produce a defined null/default value, fail at the typed conversion boundary,
or require the schema field to be marked required? Views can expose absence;
ordinary value members cannot all represent it in the same way.

**Decision:** produce a defined null/default value when an inline value is
absent, unless the field is `Required`, in which case absence is an error.
Use Q9's existing default-value logic: `Deflt` if supplied, otherwise the
normal Zf fallback. Apply the same rule to typed and reflected object loads,
including transformed time, decimal, and 128-bit values. Preserve absence
information in views; returning a default during object loading does not
insert a field into the serialized span or change Q10's copying rule.

Required-field errors belong to external-input verification and enforcement
of required presence during object serialization. This does not introduce
re-verification of trusted same-process data (Q4). Do not dereference an
absent inline-value pointer or introduce a separate per-transform policy
choice for this case.

### Q12 — Generator destination and failure contract

**Status:** Closed — existing codebase contract; incorrectly raised as an
open requirement. **Affects:** Slices 2 and 4, public save/schema APIs,
schema-output functions, and `goal.md`.

Should the BFBS generator require an existing builder, accept a general Z
buffer/stream destination, or provide both? Must it accept fixed caller-owned
storage as well as growable storage, and should insufficient capacity be
reported without allocation or handled by the destination's growth policy?

**Existing contract:** save is templated on stream
destination `S`, which implements `operator <<`. Destinations include
`ZiTxStream`, `ZiFileTxStream`, `std::cout`, `ZuStream`, and other compatible
types. Use the same convention for BFBS schema generation and interop export;
do not introduce a separate mandatory buffer-writing interface.

This was already established across the codebase and should have been
determined from the implementation before asking a requirements question.
Retain Q12 only to preserve its permanent reference and this correction;
it records no new API choice for the user to make.

The normal reflection use case selects a buffer-backed stream and obtains a
borrowed binary span from the completed buffer. Other streams need not own
or return a contiguous buffer, implement reserve/seek, or expose a builder.
Honor each destination's existing capacity, allocation, error, and streaming
behavior; those operations remain exempt under Q2. Encode exact binary bytes
through `operator <<` using established Z span/printing facilities, including
embedded zero bytes. Keep `zi` destination dependencies above `zf`.

The BFBS generator has a compile-time-known layout and streams its resolved
sequence in output order. A payload encoder may use internal builder storage
where its wire construction requires it, while preserving existing direct
pooled-buffer output. Neither makes a new storage interface a requirement on
`S`. This supersedes the proposed buffer-only contract and universal
span-return requirement.

### Q13 — Schema ownership and sharing among stores

**Status:** Resolved. **Affects:** Slice 2 generation/lifetime contract,
Slice 7 `Table` construction and store ABI, and `goal.md`.

Should each logical table own its emitted BFBS buffer, should a DB instance
share one buffer per schema across its tables/stores, or should schema
registration own the buffer independently? Can store callbacks retain schema
views until asynchronous close completes, and can supplying modules unload?

**Decision:** each `Table` owns its emitted BFBS buffer, generated during
`Table` construction. Even tables with the same schema own their respective
buffers. Store `open` and subsequent reflection operations receive borrowed
spans/views into that table-owned storage. No separate schema-registration
owner or shared schema-buffer cache is needed.

Retain the table and its buffer through all dependent store operations,
callbacks, and completion of close; release the buffer with the table.
Generation is once during construction, without a later lazy-generation or
per-access serialization step. Existing native accessor pointers retain
their code-lifetime requirements; the existing `ZiModule` contract keeps
loaded store code resident. Add no callback registry or unloading mechanism.

### Q14 — Schema-output inputs and output organization

**Status:** Resolved. **Affects:** Component boundaries, Slice 6 schema-output
functions, application instantiations, acceptance gates, and `goal.md`.

Q4 fixes the required inputs: export schemas derived from `ZfStruct` metadata;
arbitrary external BFBS-to-text conversion is not required. Should textual
output be one self-contained schema per root, or a collection with includes
and shared definitions? Are the proposed tool names part of the requirement
or implementation choices?

**Decision:** provide library functions for binary BFBS and textual FBS
output, templated on generic `typename S` destinations using `operator <<`.
Each caller compiles in the relevant `ZfStruct` metadata, which varies by
application. Select root types/facets at the C++ call site; no standalone
command-line tool or fixed compiled catalog is a deliverable.

Generate BFBS from that metadata and render textual FBS from the same
resolved metadata/generated schema representation. Emit one self-contained
textual schema per selected root, including reachable types and namespaces.
The caller can supply a file stream to write `.bfbs` or `.fbs`, or any other
compatible stream. Keep application types above `zf`. Arbitrary external
BFBS-to-text conversion is not required.

This supersedes earlier command-line tool descriptions, executable names,
and root-selection command-line interfaces. The original proposal's
metadata-derived output and self-contained schema organization are retained
as library capabilities; interop harnesses instantiate them with test types.

### Q15 — Canonical versions and second language

**Status:** Resolved. **Affects:** Slices 1 and 6, release acceptance,
and `goal.md`.

Which canonical FlatBuffers versions constitute the compatibility baseline,
and which second language should the interop suite exercise? Is support
required against a pinned release, a minimum/latest range, or particular
existing external consumers? The second language must cover its supported
subset without weakening the complete C++ checks.

**Decision:** align with the original `Zfb` FlatBuffers dependency.
The original configuration required `flatbuffers >= 24` and its now-deleted
`scripts/fbscodegen` invoked the installed `flatc`. The explicit interop
targets retain that dependency threshold; production has no such dependency.
The inspected environment reports 25.12.19
for both `pkg-config --modversion flatbuffers` and `flatc --version`.

Use that canonical C++ dependency and matching compiler for the complete
supported-feature interop suite. Record the actual library/compiler versions
with fixtures and results; the observed version is baseline evidence, not a
new hard-coded dependency pin. Keep canonical dependencies in the separate
interop harness after deleting `zfb`. No independent minimum/latest release
matrix or second-language gate is added. This replaces the earlier proposed
separate version/language selection.

### Q16 — Performance acceptance thresholds

**Status:** Closed — prescribed implementation constraints, incorrectly
presented as open choices. **Affects:** Slices 1, 4–6, and 11, core arena and
`ZiFB` ownership/allocation contracts, and `goal.md`.

Which workloads and latency percentiles determine acceptance, and what
regression in latency, encoded size, allocations, or code size is material?
May dependency removal justify unchanged speed or a measured regression in
a secondary workload, and who accepts that tradeoff?

**Required implementation:** align buffer allocation, growth, ownership transfer, alignment,
and downward-writing behavior with the current custom `Zfb::IOBuilder` so
performance does not regress. `ZfFB` and its `ZiFB` integration must reproduce
much of this logic; it is a concrete baseline requirement, not an optional
optimization after the codec is functional.

Audit and reproduce direct pooled allocation, head/tail-preserving downward
growth with one growth policy, reserved headroom, aligned capacity/storage,
zero-copy detach and rebind, and cleanup while the buffer owner remains
alive. Follow the Required IOBuilder behavior section in Slice 5, including
stronger alignment where explicitly requested by field metadata. Preserve
allocation/copy efficiency through the templated stream interface.

Use repeatable comparisons against that custom implementation, covering
representative production rows, telemetry, nested buffers, and reflection.
Record allocation counts/sizes, growth and copied bytes, ownership transfer,
alignment, and latency distributions. No new steady-state allocations or
copies may be introduced into existing direct-buffer/borrowed paths.
Investigate measurement noise, but resolve actual regressions; dependency
removal is not an allowance to regress. No speculative speedup target is
required.

These mechanics are consequences of satisfying canonical wire-format
interoperability together with the existing buffer/performance contract.
They should have been prescribed from the implementation audit, not raised
as requirements questions. Retain Q16's ID for reference; implementation
and measurements must establish compliance, not seek a new policy decision.

### Q17 — Required release configurations and available environments

**Status:** Resolved. **Affects:** Slice 11 build/test and diagnostic scope,
portability requirements, and `goal.md`.

Which compiler/architecture/OS combinations and optional consumer builds
must have executed results before this implementation is accepted? The plan
names gcc/clang, x64/ARM, Linux/MinGW, both SQL backends, and the dashboard,
but does not identify the available environments or owners of those runs.

**Original decision (superseded):** require Clang/debug/ASan and use Valgrind
only if needed. The user's later build instructions replace that execution
scope: retain the current configuration, batch changes, build incrementally
at major milestones, defer dependent rebuilding until the end, and use
Valgrind as a final leak/corruption gate as well as for needed debugging.

**Current prescription:** the initial Clang debug correctness/Valgrind gates
precede the release transition required for performance acceptance. That
transition through `z.config` and clean has now occurred. Retain the current
release artifacts for subsequent fixes and focused correctness/memory checks;
never mix incompatible objects or treat debug timings as performance evidence.
ASan/clean remains an optional diagnostic exception. Other compilers/platforms remain outside build/test scope. Code for
GCC and MinGW/Windows without claiming execution.

Exercise the affected enabled consumers and optional backends within this
scope and record genuinely missing checks. No broader execution matrix is
required. This record retains Q17's history and permanent number; the
Authority, scope, and build discipline section governs execution.

## Repair2 execution — 2026-10-09, first source batch in progress

Execution follows `repair2.md` against the existing migrated worktree, with
`6e9a1252` retained as the comparison baseline. The current Makefile records
`CXX = clang++` and `CXXFLAGS = -march=x86-64-v2 -O3 -g -DNDEBUG`.
No configuration change, rebuild, test or benchmark has been performed in this
batch. Source snapshots for the initial Zdb and Zi edits are retained in
`/tmp/z-repair2-start`; the existing worktree changes were preserved.

Source changes so far, pending milestone acceptance:

- `ZdbMemStore.hh`: construct non-vector decoded values directly in Tuple
  storage, normalize `CStringVec`, and select Float32 scalar/vector read and
  reverse-write widths from cached BFBS field views. Rebuild row/key bindings
  together on reopen; route binding exceptions through the open completion and
  discard a dormant table on failed rebind.
- `ZdbMemStore.cc`: move last-use index keys; use cached schema alignment for
  select output. `saveRow` combines reflected row and outer-message alignment.
- `ZdbSaga.hh`: replace the pooled temporary builder and manual copy with
  generic scratch-backed stream save into the retained payload; preserve
  replace semantics and propagate codec errors to the existing submission catch.
- `Zdb.hh`/`Zdb.cc`: select primary-key fields at original row slots for delete;
  index tombstones only by primary key, and invalidate secondary buffer entries
  from retained row/buffer state on recovery. Supply typed row/message alignment
  to the replication builder.
- `ZiFB.hh` and `ZfFBReflect.hh`/`.cc`: permit actual runtime alignment below
  the supported ceiling in pooled storage and arena construction; derive
  reflected alignment transitively, including forced vectors and union members.

This is incomplete implementation. Recursive UDT decomposition has not yet been
added: the setup error boundary currently rejects values outside the existing
bindings and must be extended before Phase 1.3 can be accepted. The next work
must retain component ownership and original nested wire layout, using native
metadata and representation mappings rather than a Bitmap/IP whitelist.
The operation/type review, remaining cold-find/recovery copy audit, projected
record verification (including Required non-key fields), complete delete-cache
consumer review, milestone compilation, focused fixtures, dependent refresh,
canonical interoperability, allocation/performance comparisons and final
Valgrind gate remain outstanding. No static or executable phase acceptance is
claimed by this entry.

### Repair2 milestone recovery — 2026-10-09

The first source batch now includes recursive memory-store binding. `Schema`
provides compiled object/field metadata; the store caches the matching native
components and BFBS views during setup. Row-owned contiguous component storage
retains nested table/vector/inline/union boundaries for reverse construction
and comparison. This is pending executable and full operation acceptance.
The typed `ZfFB` codec already supported recursive UDTs before this repair;
HEAD's memory-store Tuple binding also lacked generic recursive UDT support.
This work expands that backend binding, rather than restoring lost codec support.

The milestone builds exposed and corrected dependent-template/cache-node type
errors, a fixture's omitted `ZtString<>` template arguments, and a borrowed
bitmap representation whose runtime metadata tried to instantiate a writable
vector setter. The bitmap helper is now read-only constructor metadata.
Runtime UDT text-scan detection also matched unconstrained array assignment;
array UDTs now retain structured binding without instantiating that scalar
text scanner. Index initialization reads the retained key metadata after the
constructor has transferred its parameter.

Projected verification now accepts a root field filter. Excluded Required
fields may be absent, but any present field still undergoes ordinary wire
verification; nested values retain complete verification. Zdb chooses the
primary-key, update, or full-row requirement from the record version at its
existing external boundary. The ordinary unfiltered verifier remains strict.

Executed in the unchanged Clang `-O3 -g -DNDEBUG` configuration, incrementally
with `-j4`, with source libraries refreshed before dependent tests:

- `zf/src`, `zi/src`, and `zdb/src` completed without warnings/errors after
  milestone fixes; logs `/tmp/z-repair2-{zf,zi,zdb}-build4.log`.
- `zf/test` built without warnings/errors; log
  `/tmp/z-repair2-zf-tests-build.log`.
- `./zf/test/ZfFBTest` passed all 20 top-level TAP subtests, including new
  projected Required-field and malformed excluded-field cases; log
  `/tmp/z-repair2-zffb-test.log`.
- Earlier Zdb test builds failed during compilation. Their logs are
  `/tmp/z-repair2-zdb-tests-build{,2}.log`; they are not passing test evidence.
  The corrected test build is currently pending in
  `/tmp/z-repair2-zdb-tests-build3.log`.

Outstanding work includes recursive Tuple round trips and operation coverage,
mapped/native absence defaults, nested operation-filter semantics, setup/rebind
negative cases, cache behavior for projected updates/deletes, actual alignment
in SQL output callers, the complete operation/type matrix, canonical interop,
matched allocation/performance measurements, final dependent refresh and the
Valgrind gate. No phase is accepted solely from the builds above.

### Repair2 first runtime milestone and second source batch — 2026-10-09

The corrected Zdb test build completed without diagnostics. Its first saga
execution crashed during `findFail`, before reaching the recursive fixture.
The retained core showed an empty first XFields in the temporary XKeyFields:
brace initialization selected its initializer-list constructor. Switching to
the capacity constructor fixed that implementation error. GDB could read the
retained core without live ptrace; no Valgrind run was needed for diagnosis.

After incrementally refreshing `zdb/src` and `zdb/test` (`-j4`), the existing
suite passed 3 binaries / 85 top-level tests. `ZdbSagaTest` passed all 80
subtests, including 15 assertions in the first `repairBindings` fixture:
nested tables, table arrays, a forced-16-byte inline struct, Float32 vectors
and CStringVec survived insert/find and reverse encoding. Logs are
`/tmp/z-repair2-zdb-build5.log`, `/tmp/z-repair2-zdb-tests-build4.log`,
`/tmp/z-repair2-zdb-saga-test2.log` and `/tmp/z-repair2-zdb-suite.log`.
The Zf suite also passed 14 binaries / 363 tests.

The second source batch is not covered by those results. It adds:

- Complete nested value selection after a row key/update filter has selected
  the containing field; explicit `All` still includes synthetic child members.
- Setup-time canonical inline defaults for mapped native fields, retained with
  compiled field metadata and reused without reconstructing native UDTs per row.
- Scalar-width dispatch outside loops for generic inline arrays/vectors, and
  reuse of nested field construction in root Tuple output.
- Cached actual row/key alignment in PostgreSQL and SQLite output/rebind paths.
- Primary-node cache invalidation, which also marks the same nodes stale in
  secondary hashes without reading omitted fields. Projected update/delete
  buffers are indexed by primary key only; cold reads skip incomplete update
  buffers and fall through to the existing store path. A select callback now
  borrows its result handle instead of making another retaining handle copy.
- Expanded recursive fixtures for secondary UDT lookup, whole-field updates,
  union table/inline variants, mapped-inline and Float32 defaults, empty vectors,
  recovery, successful rebind, primary-only deletion and failed rebind completion.

The next milestone library build has started with `zf/src`; this entry does
not claim compile or runtime acceptance for the second batch. Final integration,
complete type/operation coverage, canonical interop, matched performance and
allocation evidence and the final Valgrind gate remain required.

### Repair2 second runtime milestone and alignment follow-up — 2026-10-09

The second batch built Zf, Zi, Zdb, PostgreSQL and SQLite libraries without
diagnostics under the retained Clang release configuration. ZdbSagaTest passed
80 top-level subtests; its expanded repairBindings case passed 135 assertions
for nested table/vector/inline values, secondary UDT keys, mapped inline and
Float32 defaults, union variants, updates, recovery, schema-owner replacement,
primary-only deletion and invalid-schema open completion. Logs:
`/tmp/z-repair2-zdb-build6.log`, `/tmp/z-repair2-zdb-tests-build6b.log` and
`/tmp/z-repair2-zdb-saga-test6.log`. The first test build in this batch failed
on fixture type/dependent-name errors; that failed build is not acceptance.

The Zf suite again passed 14 binaries / 363 top-level tests
(`/tmp/z-repair2-zf-suite6.log`). ZiFBTest passed seven top-level cases, including
16 assertions each at runtime alignment 8, 16 and 32 for pooled growth,
scratch head/tail preservation, nested/frame alignment, detach and reuse
(`/tmp/z-repair2-zifb-test6.log`). ZiIOBufTest passed five cases
(`/tmp/z-repair2-ziiobuf-test6.log`). Both SQL libraries and PostgreSQL test
binaries built, but no SQL runtime acceptance is claimed for this batch.

The follow-up batch adds read-only runtime object metadata for native array
helpers, IPv4/IPv6 persistence coverage, actual-schema residue when relaying
framed buffers, and typed key/group/select alignment based on their projected
fields while retaining original wire IDs. It also adds an eight-byte projected
key check from a row requiring 32-byte alignment. These changes await the next
milestone builds; earlier results do not certify them. Canonical interop,
complete operation/type coverage, matched allocation/performance evidence,
final dependent refresh and Valgrind remain outstanding.

### Repair2 recursive interop compile fix and ownership checks — 2026-10-09

The projected-alignment/IP follow-up built changed libraries and unit binaries
without diagnostics. The Zf suite passed 14 binaries / 363 tests, ZiFBTest
passed its seven cases, and Zdb passed 3 binaries / 85 top-level tests; logs
are `/tmp/z-repair2-{zf-suite,zifb-test,zdb-suite}7.log`.

Canonical interop then exposed a compile-time recursion defect in the repair's
nested operation-filter implementation: each recursive table edge acquired a
new ValueFilter template alias. This exhausted template instantiation depth for
the existing Root/Child recursive schema. Nested save now selects the stable
Save or All template directly. The interop fixture additionally exercises a
finite Root/Child/Root/Child value chain in both producer directions. Its build
and test pass (`/tmp/z-repair2-zf-interop-{build,test}8.log`). A subsequent Zdb
interop compile found a char-vector/byte-vector fixture mismatch; the canonical
producer now receives an explicit ZuBSpan. Its corrected build is pending.

New unit ownership checks use existing heap telemetry, without production
instrumentation or another allocator. They check nonempty/empty bitmap decode,
integer/Float32/string vector destination allocations and long string element
ownership, including the absence of temporary frees before destination teardown.
The existing all-types SQL integration fixture also gains cold whole-field
updates and secondary-key deletion, and can run against either SQLite or the
memory module. Its zero comparison permits SQLite REAL's normalization and
memory's preserved sign. These new ownership/integration checks await the
milestone builds and executable acceptance; none is accepted from source alone.

The first ownership execution reached six passing bitmap allocation assertions,
then crashed in ZuTestMgr::end_: the direct helper call opened an unregistered
nested dynamic test scope. The retained core is
`/tmp/z-repair2-zdb-ownership.core`, with analysis in
`/tmp/z-repair2-zdb-ownership-core.log`. Removing the helper's scope leaves its
checks under the registered repairOwnership parent. The corrected build is
pending in `/tmp/z-repair2-zdb-tests-build8c.log`; the crashed run is not suite
acceptance, and `/tmp/z-repair2-zdb-saga-test8.log` ran the preceding binary
during linking and does not certify the new checks.

Further static review identified two generic binding cases still to address:
missing table objects need an absence terminator so recursive optional schemas
stop at a null edge and preserve that absence on reverse output; generic UDT
tapes currently grow while decoding and need workload-sized final storage to
avoid repeated outer reallocations. The fast bitmap and primitive/string-vector
allocation checks do not cover those generic cases. Also inspect propagation
of typed ingress-verification failure before replication writes/caches and
recovered sequence advancement. These are outstanding source work, not accepted
invariants or measured performance regressions.

### Repair2 continuation — 2026-10-10

The preceding milestone eventually passed Zdb's 3 binaries / 86 top-level tests,
including 81 saga subtests and 48 ownership assertions; PostgreSQL and SQLite
unit suites passed 10 and 5 groups respectively. Zdb message interop passed
four groups. Zdb integration compilation initially failed because its manifest
still named two already-deleted config files; removing those stale entries
allowed the incremental build to pass. Its runtime attempt was denied loopback
sockets by the sandbox. The escalation call was interrupted; it is not a
passing integration run or an automatic-review rejection.

On resumption, process handles 83080 and 56646 are missing and the previous
`/tmp` logs are unavailable. Those counts are historical execution observations;
fresh final acceptance remains necessary. Source and configured artifacts are
retained. The SQLite expanded integration build is not proven complete.

The next source batch adds shared counting/construction traversal for generic
UDT tape allocation, terminating absent table nodes, and reverse serialization
of that absence. Cached object binding setup is factored once and reused by
ownership tests. Fixtures add a finite recursive pointer chain, null-edge
updates, and one-allocation tape checks for recursive values and table arrays.
Native pointer metadata is tested through borrowed wire views, consistent with
the typed codec's explicit restriction on owning pointer reconstruction.

Recovery now propagates typed verification failure before persistence/caching
and recovered-sequence advancement. Cached rows with only a primary index also
receive mutable-field recovery updates; buffer-cache iteration ends before row
cache mutation. These source changes await milestone acceptance. The first
library compilation failed on dependent lambda capture diagnostics referencing
unnamed forward-declaration parameters; the declaration now names its parameters
and the corrected build is pending. No runtime acceptance is claimed for this
batch yet. Full dependent refresh, complete operation/type coverage, matched
performance evidence and the final Valgrind gate remain outstanding.

### Repair2 ownership/integration milestone — 2026-10-10

Batch 9 incrementally built Zdb src/test, PostgreSQL and SQLite src, and Zdb
and SQLite integration programs sequentially with `-j4`, retaining the configured
Clang release flags. No compiler diagnostics remained. The Zdb suite failed
one of 86 top-level tests: repairBindings had two failed chain-presence assertions
among 153 checks. repairOwnership passed all 66 assertions, including a single
final tape allocation for absent nodes and table arrays. Its intended nonempty
chain case was also absent because the helper's default root Save filter omitted
the read-only chain field; that run does not establish chain allocation coverage.
The recursive fixture's read-only `next` had no constructor/key membership and
was therefore omitted by nested Save filtering. Its metadata is corrected to a
writable field; the corrected round trip awaits batch 10. This failure is not
accepted recursive-store coverage.

The expanded `zdbsltest` passed all five top-level tests separately against
libZdbMem and libZdbSL, including cold whole-field update and secondary-key delete.
The memory run used `-c unused`; SQLite used `/tmp/z-repair2-types9.db`. These
standalone tests did not require network escalation. Replication integrations
remain outstanding. Batch 9 logs and raw TAP are retained under
`zdb/interop/evidence/repair2-20261010/`.

Batch 10 moves typed nested-record validation to Cxn ingress, before debug
printing and peer replication-state advancement. Recovery on the shard consumes
the verified record without a second verification pass. Its private callers were
audited; the existing buffer-handle handoff and dispatch ordering are retained.
The recursive fixture correction is in the same batch. A top-level incremental
`make -j4` is running to refresh current dependents consistently. No clean,
compiler/configuration switch, timing run or Valgrind run was performed.

Before the aggregate reached Zdb, the same source batch also removed recovery
relay temporaries: SagaData/SagaStep page entries are placement-constructed in
their final array slots directly from the callback result Tuple. This removes
the local Tuple and temporary row moves, including repeated builtin payload
copies. Required copies remain at wire emission, backend ownership, typed
selection materialization and ownership transfer into the retained recovery
page. Heap-backed containers move; builtin storage must copy when its owning
object changes. Saved definitions are verified before native reconstruction.
Typed select also constructs its Tuple directly in the callback result union,
removing the temporary Tuple before delivery. Recovery-page capacity/growth and
heap IDs are retained. The recovery changes await this batch's runtime checks.

The final defaults trace found that generic absent native inline UDTs used
component defaults instead of their containing field's resolved default. Schema
metadata now retains wire defaults for native as well as mapped inline values
when a native default exists. Pointer-only borrowed metadata is excluded from
native default reconstruction. The existing round-trip fixture now omits an
explicitly defaulted native inline field on insert, alongside the mapped-inline
and Float32 default cases. These amendments precede Zdb compilation in the same
aggregate batch and have no runtime acceptance yet.

Grouped-key review identified a further mismatch: `rowKeyFields` presents group fields before member
fields, while memory-store `extractKey` still walks row declaration order. Keys
whose group fields are declared after members can therefore disagree between
index insertion and query decoding. Existing fixtures declare group fields
first and did not establish this case. The required repair caches the
row-component index in each key binding during setup and extracts in binding
order, without per-row textual matching or another key representation.

The aggregate remained ahead of Zdb long enough to include that repair in batch
10: key XFields now cache row-component indexes resolved once from the same
BFBS field views. Index extraction follows binding order. The existing recursive
fixture adds a grouped secondary index whose Required group field follows its
member in declaration order, plus lookup and group-count checks. Key-only select
output also serializes directly from stored row components through those cached
indexes, removing its owning temporary key Tuple. Retaining index entries and
update/delete snapshots still construct independently owned keys. These changes
await the current milestone; the earlier concern is resolved in source only.

The user explicitly approved establishing matched HEAD store/saga baselines on
2026-10-10 after repository searches found codec records but no store/saga
performance records. Preserve the current compiler/configuration and pool
settings, equalize semantic schemas/defaults/field presence, and report codec,
store and saga allocation counts, encoded sizes and distributions separately.
This approval does not constitute measured acceptance; baseline construction
and measurements remain outstanding.

Batch 10 corrects that allocation-fixture gap by explicitly saving all active
fixture fields and asserting final tape lengths for the finite chain, absent
node and table array. The added assertions await execution; the earlier 66-check
result retains the narrower scope described above.

### Repair2 aggregate interruption — 2026-10-10

The retained-configuration incremental aggregate build stopped in `zdb/src`
before refreshed Zdb tests or downstream acceptance. Clang reported apparent
NULs in `ZuFixed.hh`, duplicate system-header declarations, an unavailable
`ZfFieldIDs`, and frontend SIGBUS/SIGSEGV. Subsequent direct reads found no NULs,
`ZuFixed.hh` matched HEAD, and `ZfFieldIDs` was present in `ZfStruct.hh`.
`ZuStruct.hh`, `ZfStruct.hh`, and `ZdbStore.hh` all had write timestamps at
13:02:44 BST during compilation. No source repair is justified from those
transient diagnostics alone. The current `ZuLib.o` identifies Clang 23.1.1,
matching `/usr/bin/clang++`; older Clang 22 dependency paths do not establish
a compiler change during this build. The failed log is preserved as
`zdb/interop/evidence/repair2-20261010/aggregate-build10-failed.log`.

The approved isolated HEAD baseline is an explicit exception to the earlier
Slice 1 prohibition on alternate checkouts solely for baseline collection.
It retains the current configuration, runs builds sequentially, and must not
reuse any object whose source/header dependencies differ from HEAD. No baseline
timing result has yet been established.

A matched benchmark mode has been added to the existing ZdbSagaTest fixture,
with its shared workload in `zdb/bench/ZdbRepairBench.hh`. The isolated HEAD
snapshot receives only that header and the command-line test branch. This is
source preparation, not a build or measurement result. It covers warm single-row
insert/find/select/update/delete with bitmap, vectors and three indexes, and
the fixture's complete three-step saga. Codec construction, asynchronous store
completion and full saga completion are reported separately. A dependency audit
was saved to `head-artifact-audit.json`; reuse remains conditional on matching
all inputs after baseline configuration, not only matching `.cc` files.

The old canonical Order/saga schemas use zero scalar defaults, while the native
metadata uses integer null sentinels in those fields. An unmodified HEAD wire
layout therefore cannot meet RQ4's equal-default/field-presence comparison.
The isolated comparator's Order/LiveSaga and saga bookkeeping `.fbs` defaults
have been normalized to the native metadata defaults; enum null defaults have
explicit baseline-only enumerators. The exact substitutions are recorded in
`head-schema-normalization.json`. Label resulting measurements **matched-schema
HEAD baseline**: C++ library algorithms remain at HEAD, but canonical schema
inputs are intentionally normalized. Production native defaults are unchanged.
Generated schema and field-presence checks remain outstanding before timing.

The resumed aggregate build was deliberately interrupted after the shared
matcher declarations changed again during execution (`ZfFieldIDs` became
`ZfFieldMatcher` in `ZfStruct.hh`/`ZdbStore.hh`). No acceptance is claimed from
that partial refresh. The isolated baseline is independent of those continuing
worktree edits. Its initial configuration failed because HEAD lists an absent
`zi/interop` directory in `DIST_SUBDIRS`; an empty directory restored that
distribution topology without adding sources or changing flags. Configuration
then succeeded and its `config.status --config` exactly matched the worktree.
Thirteen existing Clang 23.1.1 objects were reused after dependency-content
checks, with hashes/input paths in `head-reused-objects.json`. Remaining baseline
libraries are building sequentially with `-j4`; no timings have run.

Baseline libraries through `zf/src` built successfully. `zi/src` exposed a
second incomplete HEAD build-manifest entry: it names `ZiFB.cc`/`ZiFB.hh`,
neither of which exists in HEAD. They are untracked direct-adapter files in the
current worktree and are unused by the old Zfb dependency path. Those two
manifest entries were removed only in the isolated baseline, retaining all
HEAD C++ library sources. Reconfiguration retains the exact same configuration
and existing baseline objects. This is another recorded comparator-preparation
change, not a production repair or a measurement result.

The matched-schema HEAD libraries and the default `zdb/test` target now build
without diagnostics. The first explicit `ZdbSagaTest` target lacked its generated
schema header; building the directory's default target generated it normally.
A `--repair-bench 2 1 32` smoke run exited zero, checked insert/update/delete
field masks against metadata, validated reconstructed updated fields, and
completed the three-step sagas. Raw CSV/stderr and baseline artifact hashes are
preserved in the evidence directory. These short smoke samples are correctness
checks, not performance acceptance. The candidate was reconfigured through
`z.config -c -L /usr` only for its new distribution inputs; `config.status
--config` still exactly matches the retained arguments. The full incremental
aggregate refresh is resumed in `/tmp/z-repair2-build10c.log`, after the shared
headers remained stable.

Matched-schema HEAD baseline samples are now established for widths 32 and
4096: 7,000 samples per operation/width across seven rounds, with the warm cycle
excluded. Both processes exited zero and passed their field-mask, decoded-row
and saga-completion checks. Raw CSV, distributions, conditions and artifact
hashes are retained; `head-results.md` describes the measurement boundaries.
These establish a comparator, not candidate performance acceptance.

The candidate aggregate refresh stopped again in `zi/test`: a concurrently
edited `ZiTxStream.hh` has an unterminated `flush()` block and was transitioning
its buffer-send helper. Its write timestamp changed again after the failure.
That unfinished stream refactor has not been overwritten. A stable completed
worktree is required before resuming the candidate build, correctness gates,
paired performance comparisons and final Valgrind gate.


### Repair continuation — parameter limits and diagnostic reconciliation

Resumed the active `repair2.md` objective against the current worktree. The
preceding turn made authoritative plan changes for the user-confirmed 100 MiB
Zdb parameter-buffer policy; it was progress, not executable acceptance.

Postgres fixed-width array sizing now derives the count bound before multiplying,
variable-width sizing uses the existing cap for each accumulated payload, and
bitmap sizing bounds its word count before arithmetic. Borrowed parameters and
aggregate conversion scratch use the same cap. Size rejection routes through
existing operation failure handlers. sqlite3 inspection found 64-bit sizing and
connection-length checks but no Zdb-cap enforcement. Its planner now bounds
fixed-width counts, variable-width payload budgets, borrowed text/blob lengths
and aggregate conversion scratch, retaining stricter connection limits and the
existing SQLITE_TOOBIG handling. Postgres arithmetic fixtures were adapted to
the Zdb cap and distinct encoded element widths without large allocations.

Reconciled the earlier postgres edits against their saved pre-edit snapshots,
preserving other migration work. Removed the newly added binding rejection,
constructor/open catches dependent on that rejection, staged schema-rebinding
validation and missing-OID throw. Static OID-name coverage is complete for concrete
Value alternatives; missing conversion coverage must be repaired. The user
clarified that necessity and redundancy, rather than scope alone, determine
whether edits persist; repair2.md now states this criterion. The existing-table
failure diagnostic now retains a separate failing-field index and uses a general
fallback, preserving the column counter, matching and completion behavior.

These source edits are uncompiled and unrun. sqlite3 size-limit fixtures,
postgres callback/diagnostic fixtures, remaining type-coverage work, candidate
build, operation gates, matched performance and the final memory gate remain
pending. No milestone build or executable acceptance is claimed.


### Repair continuation — preserve nothrow paths

The preceding turn changed parameter sizing and diagnostic implementation and
was progress. The user clarified that new exceptions must not be introduced
where old code did not throw. Removed the newly added postgres sizing throws
and the dependent Store::send catch. Sizing helpers and planTuple now return
UINT_MAX on policy rejection; each affected count/select/find/write send path
calls its existing operation failure handler and returns Unsent before conversion
scratch allocation or encoding. Removed the redundant submission-length checks:
the parameter planner already establishes the stricter Zdb cap for borrowed and
converted values, and bookkeeping parameters have fixed small widths. Updated
arithmetic fixtures to inspect the failure sentinel rather than catch exceptions.
Removed the now-unused checkedSize helper. sqlite3's existing planner already
throws SQLITE_TOOBIG and continues to use that pre-existing behavior. Added the
nothrow requirement to repair2.md §5.2. Diff whitespace inspection passed; no
build or executable check was run. Full plan acceptance remains pending.


### Repair continuation — Float32/CStringVec and milestone build 11

The preceding turn inspected sqlite3's public error-code definition and was
read-only evidence. Continued the active repair against current sources.
Postgres and ZdbSL now resolve CStringVec alongside StringVec and Float32 alongside
Float64. Input scalar/vector conversion dispatches on the existing BFBS width
outside element loops; result scalar/vector emission uses the schema's Float32
width while preserving the existing double-width store representation. Postgres
saveOffset now receives its existing field metadata, propagated to callers;
no owning conversion container was introduced. Added a postgres native Float32/
CStringVec wire round-trip fixture, including an omitted nonzero scalar default,
and extended the existing sqlite3 AllTypes fixture with float scalar/vector and
CStringVec controls. These fixtures have not run.

ZdbSL parameter planning now returns UINT_MAX for size-policy failure, and its
callers report a local parameter diagnostic through their existing callbacks.
Removed the size-policy throw and its dependence on sqlite connection error text.
The combined update/key scratch buffer also obeys the Zdb cap. Writes use a
returning transaction scope so rollback precedes failure completion. Existing
non-sizing ZdbSL exceptions are unchanged; no claim that all hot-path exceptions
have been eliminated is made.

Milestone build 11 started with the unchanged config.status arguments, reusing
existing artifacts. Sequential directory builds use -j4: zf/src, zi/src, zdb/src,
zdb_pq/src, zdb_sqlite/src, followed by relevant test and sqlite3 integration
directories. Log: /tmp/z-repair2-build11.log. Unified exec session: 82423. The
session was confirmed live after launch; zf/src required no work and zi/src
was refreshing ZiFBReflect. Do not start another build until this handle is
polled to completion. No executable acceptance or performance result yet.


Milestone build 11 completed its library directories (zf, zi, zdb, postgres,
sqlite3) and zi/test without reported diagnostics, then failed in zdb/test.
The first compiler error was the earlier repair fixture's unprotected comma in
Deflt<RepairInline{97, 2.5f}>; wrapped that property in parentheses using the
existing metadata macro convention. The subsequent missing Props/KeyIDs errors
were downstream of that invalid declaration. Session 82423 exited 2 after its
remaining jobs drained. Incremental continuation started from zdb/test, then
postgres tests, sqlite3 tests and sqlite3 integration programs, with -j4 and
unchanged configuration. Current log: /tmp/z-repair2-build11b.log; unified exec
session: 52590. Do not restart while that handle remains live. Library compile
success is not operation/type acceptance; no fixture has run yet.


Milestone build 11b completed zdb/test, then failed linking the new postgres
fixture because inline OIDs::init calls the out-of-line OIDs::name, which lacked
export visibility. Corrected the declaration's library visibility and changed
the fixture's CStringVec pointer comparisons to ZuCSpan content comparisons,
eliminating three compiler warnings. Session 52590 exited 2. Continuation 11c
rebuilt postgres src/test and sqlite3 test successfully without reported
diagnostics; sqlite3 integration programs remain building. Current live unified
exec session: 50636; log: /tmp/z-repair2-build11c.log.

Milestone executable checks completed: ZiFBTest (20 top-level tests),
ZiFBReflectTest (7), ZiFBIOTest (8), ZdbSagaTest (81), ZdbPQTest (12), and
ZdbSLTest (5), all exit zero with no TAP failures. ZdbSagaTest's repairBindings
passed 227 assertions and repairOwnership passed 69. Postgres floatWire covers
omitted nonzero Float32 defaults, Float32 vector widening/narrowing and
CStringVec round trips; paramBounds exercises fixed-width count and variable
payload/aggregate arithmetic against the Zdb cap. sqlite3's codec unit suite
is not its store integration acceptance; the extended AllTypes fixture has
not run. Logs and binary/source hashes are preserved under
zdb/interop/evidence/repair2-20261010/milestone11/.

Source review confirms both external stores still resolve fields using the
native ZfFieldTC directly instead of Schema::fieldMeta's mapped code. Mapped UDT
consumption, nested UDTs/arrays, additional parameter-policy and mismatch
callback fixtures, store integration, paired performance, final dependents and
the memory gate remain outstanding. Do not infer full plan completion from the
passing native/memory suites.


### Repair continuation — sqlite3 integration and mapped UDT metadata

The prior goal turn made implementation changes and passed executable gates;
it was progress. Build 11c exited zero with no reported diagnostics. The sqlite3
integration target ran seven programs/52 top-level tests: operation/type (including
new Float32/CStringVec fields), saga, configuration, inspection, schema and query
planning programs passed. Replication initially failed assertion 6 because the
sandbox denied both listen/connect socket calls (Operation not permitted), leaving
the follower's persisted order table empty while the leader had one row. The
requested permission rerun was interrupted before execution: no rerun log or
live replication process existed. After the user granted full access, reran only
replication with fresh databases under /tmp/z-repair2-sqlite-repl11.c65l6r0y.
All eight assertions passed, including post-failover lookup. No production repair
was made for the sandbox failure. Logs retained in milestone11 evidence.

Started the next source batch: both external-store resolvers now consume
Schema::fieldMeta's mapped code instead of assuming the native field code is the
store representation. Cached inline default wire pointers are propagated through
initial binding and supported schema refresh, and inline loaders use those bytes
before the existing native-default fallback. Added mapped UInt32 and mapped Time
fields to sqlite3's AllTypes fixture, with a nonzero mapped inline default. These
new mapped-binding changes and fixture extensions are uncompiled/unrun; milestone11
hashes/results certify the preceding batch only. Generic nested UDTs/arrays,
additional size-policy and mismatch callback fixtures, postgres integration,
matched performance and final memory/dependent gates remain pending.


### Repair continuation — PostgreSQL installation and milestone12

Resumed after the explicit pause. The immediately preceding path acknowledgement
was no progress; current source/build state was re-inspected before proceeding.
Use /home/count0/postgres/bin, as requested. That installation is PostgreSQL
16.3 and already contains uint/libz extensions and libZu. Created a disposable
cluster at /tmp/z-repair2-pg12.mksk4bg6, with a private Unix socket directory,
port 55432 and TCP disabled. Database repair2 has both extensions enabled.
No persistent user database was changed.

Started milestone12 incremental builds of postgres/sqlite3 libraries, unit tests
and integration programs, sequential directories with -j4 and unchanged build
configuration. Unified exec session 48125; /tmp/z-repair2-build12.log. This is
the first build of the mapped terminal UDT source batch recorded above. Do not
restart while that handle is live. No milestone12 executable result yet.

Added an execution reconciliation to findings3.md: its original audit is
historical; full concrete OID mapping makes conversion repair the requirement,
and the multi-gigabyte arithmetic example exceeds Zdb's existing 100 MiB policy.
It does not prove reachable corruption for a policy-compliant message.

Milestone12 session 48125 exited 2: libraries, postgres unit/integration and
sqlite3 unit programs built, but sqlite3 AllTypes compilation exposed an
undefined ZiFB::Rep<ZuTime, Time> for MappedTime. The application mapping's
output was fed directly to Rep without applying the existing Time mapping.
Repaired Value's compile-time mapping composition through ValueBase; mapped
outputs are composed before layout/encoding and decoded in reverse order.
Final prvalue mapping results remain values, avoiding references to temporary
mapping output. No new runtime dispatch, validation or exception was added.
Added codec scalar/vector/absent-default coverage for application-to-Time
mapping. Incremental continuation 12b includes zi and affected dependents;
session 75323, /tmp/z-repair2-build12b.log. No executable acceptance yet.

Continuation12b exited 2 in ZdbMemStore: ZiIP's existing mapping deliberately
retains the same type/code as its union wire adapter. Recursive composition
must terminate for that pair. ValueBase now distinguishes such terminal
adapters from mappings to another type/code. Continuation12c (session11189;
/tmp/z-repair2-build12c.log) rebuilds the same affected directories, with -j4.
The initial composition change is not yet accepted by executable evidence.


### User correction — field-code conversion versus recursive UDT metadata

The prior mapping-composition work addressed an artificial fixture, not the
required recursive UDT contract. Removed ValueBase and restored Value's prior
representation selection and conversion. Fields declared Time use native C++
conversion to/from ZuTime. Replaced sqlite3's application-to-Time transformer
with those conversions and a Time field declaration. The codec fixture now
uses a metadata-defined Stamp containing a Time member, nested in a UDT field
and a UDT array, with no transformer. Extended the memory-store recursive
fixture with terminal Time members under nested scalar/array UDTs, including
secondary-key queries, updates, recovery and reopen checks.

repair2.md now specifies the exact nested-MappedTime example supplied by the
user and explicitly excludes chained mapping machinery as its implementation.
The previous build12c handle no longer exists; process inspection found no
remaining build/compiler processes. Its log stops during postgres compilation
and does not establish successful completion. Its partial artifacts are reused
only as build inputs, not acceptance evidence. No rebuild/test of this corrected
batch yet; external-store generic nested UDT consumption remains outstanding.


### Store column-contract realignment

The user clarified that recursive codec UDT metadata does not extend the Zdb
store column contract. Inspected HEAD 83d96641: postgres/sqlite3 construct one
XField per field and resolve only supported scalar/vector representations plus
explicit Bitmap/IP cases; memory Value has no generic composite alternative.
The prior composite-store requirement and its acceptance claims were incorrect.
repair2.md now withdraws that requirement throughout Phase1.3, the conversion
matrix, Phase5, RQ2 and SQLQ1. repair3.md and findings3.md carry the correction.

Removed generic memory UDT/Atom/Count storage, comparison and recursive load/save
machinery, object-binding graph, schema object/default/mapped-code plumbing,
composite-store fixtures and their new validation/catch path. Restored native
field-code resolution in all stores. Kept Float32/CStringVec widths/conversions,
bitmap direct ownership construction, alignment, last-use key moves, supported
reopen view refresh, size-policy checks and narrow mismatch diagnostic.
Replaced the memory acceptance fixture with supported scalar/vector columns.
Removed the unnecessary added recursive codec fixture; existing codec recursion
remains unchanged. Retained a sqlite3 scalar Time conversion fixture with ordinary
C++ conversions and a Time declaration, without a transformer. Source snapshots
before removal: /tmp/z-repair2-realign.al_hma9e.

The interrupted build13 later exited2 in zi/test because the now-removed fixture
needed a wire type declaration and macro parentheses. Continuation13b is also
terminal (no build/compiler processes remain). Neither provides acceptance for
this realigned source. Rebuild/test only at the next milestone.

Milestone14 incremental build started for the realigned batch, unchanged
configuration, sequential directories with -j4; session28673 and
/tmp/z-repair2-build14.log. Memory/postgres libraries compiled without reported
diagnostics so far. No current executable result; old composite-fixture passes
are historical and do not certify the revised supported-column fixture.

Milestone14 session28673 exited0, with no reported compiler warnings/errors.
Executable session43114 also exited0: six unit suites/133 top-level tests, all
seven sqlite3 integration programs/52 tests, and all three postgres integration
programs passed. The revised supported-column memory fixture and sqlite3 Time
conversion/default checks passed. PostgreSQL used the requested ~/postgres/bin
16.3 disposable cluster with uint/libz. Logs and source/binary hashes saved in
zdb/interop/evidence/repair2-20261010/milestone14/. These results certify the
realigned source, not generic composite-column support. Final performance,
remaining parameter/mismatch fixtures, dependent/interop gates and final memory
checks remain pending under repair2.md.


### Parameter and OID diagnostic acceptance batch

The preceding goal turn was progress: column-contract realignment compiled and
passed milestone14. Continuing pending acceptance against that corrected scope.
Moved sqlite3's existing fixed-vector sizing helper into its codec header and
factored variable-vector prefix/payload sizing through the same remaining-budget
helper. Both StringVec/BytesVec production planners use it; no new limit or
exception was added. Added count/max+1 checks for widths1/2/4/8/12/16, small
connection budgets, prefix rejection before traversal, payload/aggregate limits
and UINT64_MAX inputs. Postgres unit fixture now exercises planTuple borrowed
String/Bytes bounds, fixed-count rejection and aggregate expansion using small
buffers with synthetic length words; no large allocation or encoding is run.
Also elided unnecessary union-tag lookup on memory scalar/vector fields.

Added postgres integration cases opening existing tables with genuine OID
mismatches: one field, last recognized field, shuffled physical order, missing
application column and reserved-column mismatch. These check callback completion
and actual diagnostic field identity/general fallback. Production mismatch
comparison and counting are unchanged. This batch is not yet compiled or run.

Milestone15 build started, unchanged config, sequential -j4 directories;
session60738, /tmp/z-repair2-build15.log. Reusing the disposable PostgreSQL
cluster /tmp/z-repair2-pg12.mksk4bg6 with ~/postgres/bin (16.3), private socket
and TCP disabled. The cluster is running for the new diagnostic fixtures.
Do not restart a live build or count this source batch as accepted yet.

Milestone15 session60738 exited2: the small postgres planner fixture used a
borrowed Int8 span that its erased metadata setter cannot assign. Replaced the
fixture members with tiny owning ZtArray<int8_t> values; no production change.
Continuation15b session2132 exited2: the new mismatch helper lacked its ZuTest
scope. Continuation15c session38646 exited2 because that static scope was
incorrectly captured. Added the helper scope and used it directly from the
lambda. Continuation15d session47947 is rebuilding the remaining directories,
unchanged configuration; log /tmp/z-repair2-build15d.log.
Also corrected the scope table's postgres binding wording: concrete OID coverage
is already complete; the repair concerns supported wire conversions.

Milestone15d session47947 exited0 without reported compiler warnings/errors.
Acceptance session80076 exited0: three unit suites/100 top-level tests, all
seven sqlite3 integration programs/52 tests, and all three postgres integration
programs passed, including the genuine existing-table OID-mismatch diagnostics.
Logs and source/binary/configuration hashes are preserved in
zdb/interop/evidence/repair2-20261010/milestone15/.
This completes the requested column-contract realignment. The broader repair2
performance, interoperability, dependent and final memory gates remain pending;
these passes do not assert completion of the entire repair plan.

### Matched performance and shared conversion fixture batch

Previous goal turn was verified progress: milestone15 accepted the corrected
column contract and diagnostic/parameter fixture batch. Ran three sequential
paired HEAD/candidate processes at count1000 rounds7 widths32/4096, reversing
order between pairs; sessions58282 and45431 exited0 for all12 processes.
Raw CSV/stderr, conditions and summaries are in paired15, paired15-repeat2 and
paired15-repeat3 under zdb/interop/evidence/repair2-20261010/. No builds/tests
ran concurrently. Same release configuration, semantic schemas and default pools.
Initial saga median regression12–15%, repeats4–8%; acceptance remains open.
Allocation reductions are measured independently of that unresolved latency.

The coverage audit found postgres operation-level native Float32/CStringVec
coverage still missing. Extracted the existing memory supported-column fixture
into test/ZdbRepairTest.hh for reuse by postgres integration, retaining lifecycle
ownership in callers. Added double/StringVec controls and singleton vectors to
its existing multi/empty/default/key/select/update/recover/reopen/delete cases.
No production conversion or validation changes. Source batch not yet accepted.

Milestone16 shared-fixture build session51993 is running, log
/tmp/z-repair2-build16.log. Only zdb/test and zdb_pq/itest are rebuilt; source
libraries are unchanged. Automake regenerated the changed test Makefile via
config.status without changing configure flags. Memory test compilation found
an unused local type alias left by extraction; removed it and its unused
namespace directive after that translation unit completed. A sequential
incremental continuation will refresh that object before acceptance.

Milestone16 session51993 and warning-clean continuation32035 build16b exited0.
The first execution then crashed after30 of81 top-level memory tests; postgres
was not run. GDB through libtool exec reproduced it in ZuTestMgr::end_ at the
extracted helper's scope teardown (/tmp/z-repair2-gdb16.log). Its direct call
opened an unregistered nested dynamic scope. Replaced it with ZuTestCallRT;
continuation session70591 rebuilds the corrected caller and then runs memory and
postgres suites sequentially. No production change was needed for that crash.

Static nothrow audit found a remaining migration-added explicit exception in
MSaga::save (ZdbSaga.hh), absent in the original serializer implementation. The
caller already has a submit(false) failure path. Next production source batch
must return the codec error and use that existing failure path without throwing;
retain pre-existing catches for application/allocation exceptions. This remains
unfinished until implemented and accepted. Also pending: actual size-failure
callback fixtures, full enabled/dependent/interop gates, transmit/cold-recovery
performance scope and final memory certification; saga latency regression is
unresolved. Goal remains active.

Milestone16c build and memory suite passed:81 top-level tests. Initial postgres
run failed to connect because my cluster restart omitted its original explicit
socket/port/TCP-disable options and used postgres defaults. This is harness
setup failure, not a code result. Terminated its waiting saga child; session70591
is terminal exit2. Restarted only the disposable cluster with explicit private
socket, port55432 and empty listen_addresses. psql confirmed PostgreSQL16.3.
Postgres-only rerun session48117 has a60s harness timeout and logs to
/tmp/z-repair2-postgres16b.log; no rebuild was needed for this setup correction.

Postgres16b reached the new fixture, then crashed. Offline GDB/core evidence
(/tmp/z-repair2-pg16.core, /tmp/z-repair2-pg-core16b.log) showed a default/null
OpenData after earlier select errors. A buffered TAP run and server log traced
the first error to prepared statement arepair_selKII_2: the fixture requested
inclusive=true for selectNext=false. The existing producer API uses false for
this non-continuation case. Changed the shared fixture to that valid combination;
no production query or error-handler change. The invalid request produced an
error followed by an end callback, causing extra wakeups in the fixture's shared
ZmBlock semaphore and misleading downstream results. Fresh-table reproduction
proved the initial Float32/CStringVec/IP/default and control round trips passed
before the select error. Do not count the failed run as operation acceptance.

Also reset only this postgres fixture's public.repair table and arepair MRD rows
before each run; otherwise prior crash state makes low UN writes intentionally
idempotent no-ops. Wrapper owns this setup, shared fixture remains store-neutral.
Milestone16d session94432 incrementally rebuilds both affected test directories
sequentially with-j4, then runs memory and postgres acceptance. Source libraries
and configuration remain unchanged. Initial GDB runs waited on debuginfod;
terminated those debugger sessions and used DEBUGINFOD_URLS= plus explicit
set debuginfod enabled off for authoritative offline core/run traces.

Milestone16d session94432 exited0 for both incremental builds, memory unit81
and postgres integration18 top-level tests. No compiler warnings/errors reported.
This accepts end-to-end native Float32/CStringVec conversion, controls,
defaults, primary/secondary keys, group selection, multi/empty/single vectors,
updates, recovery, schema-view replacement on reopen and deletion in both stores.
Evidence is in milestone16/; current test source advances in the next batch, so
only unchanged shared fixture/PG source and exact executable hashes are recorded
for this milestone. The active goal is not complete.

Batch17 removes the migration-added MSaga::save codec-error exception. Save now
returns ZiFB's integer error; saga submission logs the same diagnostic and uses
its existing submit(false) path, retaining old application/allocation catches.
Propagated the result contract to direct callers in Zdb tests/bench and Zum unit
and restart fixtures. Added RequiredSaga/requiredAdmission coverage for missing
required serialization, cleared retained payload, failed admission, no journal
rows and no effect/completion callback. repair2.md explicitly requires this
nothrow propagation. This batch is source-only pending milestone17 acceptance.

Milestone17 build started, session96227, /tmp/z-repair2-build17.log. Sequential
changed source libraries and owning unit/integration directories, -j4 unchanged
configuration. Source libraries completed; zdb/test compilation is live now.
Zum's direct test callers have been updated but its heavy dependent rebuilding
remains deferred to the final integration milestone as required by repair2.md.
Do not interpret the source-only RequiredSaga check as an executable pass yet.

### Milestone 17 acceptance — 2026-10-10

Incremental build session 96227 completed with exit 0; no compiler warnings or errors in `/tmp/z-repair2-build17.log`. Session 95933 completed owning unit suites and sqlite3 integration with exit 0. Session 27268 completed postgres integration (3 programs, 18 top-level tests) with exit 0, then stopped the disposable cluster. Logs and current source/binary hashes are retained in `zdb/interop/evidence/repair2-20261010/milestone17/`. Saga codec errors now follow the existing failed-submission callback without a newly introduced throw; the required-field failure fixture passed. Zum caller compilation is still pending the final dependent build. Performance acceptance remains open because repeated matched complete-saga timings regressed despite lower allocation counts.

### Profile and saga coverage source batch 18 — 2026-10-10

The previous goal turn made verified progress: build17 and 171 top-level tests passed. Both diagnostic gprofng runs completed with exit 0 (session 68231). Aggregate sampled CPU is dominated by scheduler/semaphore activity; no causal performance conclusion is established. Raw logs and top-function reports are in `zdb/interop/evidence/repair2-20261010/profile17/`. Eight additional matched processes (session 59012, all exit 0) restricted equally to CPUs 0,2,4 still show complete-saga p50 regressions of 12.4%, 5.2%, 10.4%, 13.5%, while store insert improves 1.8–4.5%. Reports are in `paired17-affinity/`; original unrestricted evidence remains valid and acceptance remains open. No scheduling ordering branch was removed or build flags changed.

Source batch 18 extends the shared supported-column header with native saga insert/update/compensation using Float32 scalar/vector and CStringVec fields, mutable Float32 secondary lookup and delete, required immutable fields, reverse store emission, and journal cleanup. It is registered in the existing memory and both data-store saga entry points. This addresses a coverage gap rather than adding validation policies. Build and executable acceptance are pending.

Build18 session 82740 failed at a test-only access to protected `storeTbl()`. Replaced that fixture shortcut with the public uncached `selectRows` operation, checking both reconstructed values and row count. No production API or behavior changed. Incremental continuation is required.

Build18b session 84836 completed all three directories with exit 0 and no compiler diagnostics. The first executable run failed before the new memory saga fixture started: the wrapper passed the nested Zdb configuration while the shared helper looked for sibling `mx` inside it. Corrected the fixture interface to receive Zdb and multiplex configuration nodes separately. Existing setup exception behavior remains unchanged; neither store integration was run after the unit failure. Logs `/tmp/z-repair2-build18b.log`, `/tmp/z-repair2-test18.log`.

Incremental continuation build18c is live as session 82164 (`/tmp/z-repair2-build18c.log`), currently in `zdb/test`; no new executable acceptance is claimed. Postgres remains stopped. Next action: poll this same build handle to terminal, then rerun owning units, sqlite3 integration and postgres integration against the disposable cluster. The shared fixture configuration fix must be proven before updating acceptance summaries.

### Milestone18 accepted — 2026-10-10

Build18c session82164 completed all three owning directories with exit0 and no warnings/errors. Session94734 passed 103 unit and54 sqlite3 integration top-level tests. Session92160 passed20 postgres integration tests, then stopped the disposable cluster. All177 top-level tests passed. The shared native Float32/CStringVec saga fixture now proves insert, mutable Float32 secondary-key update, compensation, uncached store-to-native selection, deletion and journal cleanup in memory, sqlite3 and postgres. Exact accepted sources, binary hashes and all failed/final logs are retained in `zdb/interop/evidence/repair2-20261010/milestone18/`. This does not establish new crash/recovery coverage for these particular native fields or resolve performance acceptance.

### Source batch19 — typed envelopes and remaining focused coverage

`saveMsg` and `saveRecord` now reuse the existing `withOffsets`/TypedTable path. Every slot is assigned or omitted by its typed field helper; no preinitialization, new allocator, validation or copy policy is introduced. This removes the generic table per-field `(offset, ID)` scratch records for these compile-time-known envelopes. Commit emission composes its already-built typed Commit directly with saveMsg, avoiding a temporary native union/root. Wire format and error behavior must be established by owning tests and canonical interop; no timing benefit is claimed yet.

The matched shared benchmark adds scheduler-only 2/8/32-post controls on the same configured threads, after existing codec/store/saga phases. Baseline and candidate benchmark headers are identical. The baseline library algorithms remain unchanged. These controls are diagnostic and cannot substitute for complete-saga latency acceptance.

The shared store fixture adds synthetic over-cap Int8Vec length words to exercise count/select/find and insert/update failure callbacks in postgres/sqlite3, without allocating a large payload. It requires exactly one completion per operation after close drains work. Memory does not execute these store parameter-policy checks. This tests the already-authorized parameter policy rather than adding production validation. Executable acceptance is pending.

Build19 session3841 completed changed Zdb/postgres/sqlite3 libraries, then failed test-only base/derived buffer reference conversions in repairSize. Its local buffer handles now explicitly use the existing Zdb IOBuf reference type, matching prior fixtures. No production API changed. ZiFBIOTest additionally observes completed-span insertion bytes for the concrete transmit type and proves grown-owner detach compaction; expected generic reference encoding and observer copies are kept outside that byte counter. These source changes require continuation acceptance.

Build19b session68586 completed zi/test and zdb/test; remaining sequential owning directories and the matched HEAD test are live. The canonical commit fixture now follows the production typed Commit plus saveMsg composition, and checks an empty union through saveMsg to cover omitted typed-table slots. That interop directory has not yet been reached by this build continuation. No executable acceptance is claimed for batch19 yet.

### Milestone19 acceptance — 2026-10-10

Build19b session68586 completed all candidate owning directories, canonical message interop and the isolated matched HEAD test with exit0 and no diagnostics. The first unit run failed only the new span-byte assertion because the counter included the preceding six-byte prefix. Corrected that accounting; no production transmit behavior changed. Session58412 rebuilt only the affected Zi test and passed138 unit top-level tests, then passed20 postgres integration tests and stopped the disposable cluster. Session9046 passed4 canonical message interop groups and54 sqlite3 integration tests. All216 top-level tests passed. Exact accepted source copies, source/binary hashes and failed/final logs are retained in `milestone19/`.

The supported-column saga fixture now exercises existing parameter-policy failure callbacks in both postgres and sqlite3: count, select, find, insert and update complete once with an error after synthetic over-cap vector length words; close drains callbacks before counts are checked. The transmit fixture proves a grown owner is handed off with zero completed-span insertion bytes; its measured compaction is928 bytes within the same owner. The splitting fallback inserts3968 object bytes; the preceding6 span bytes are accounted separately. These observations do not time the transmit paths or count every growth relocation. Performance and broader final integration/memory gates remain open.

### Paired19 results and dependent milestone20 — 2026-10-10

The previous status turn made progress by establishing terminal completion of all four paired19 benchmark processes (each exit0) and generating the matched summary. At widths32/4096, codec insert p50 improves29.0%/24.9%; complete-saga p50 regresses5.8%/1.4%. Saga observed allocations fall from120.937/120.406 to112.136/112.723. Scheduler-only controls move+2–3% at width32 and-2–4% at width4096; these controls neither explain nor cancel the saga regression. Conditions, raw samples and distributions are in `zdb/interop/evidence/repair2-20261010/paired19/`. No blanket performance acceptance is asserted.

Deferred dependent integration build20 started as session17545, `make -j4`, log `/tmp/z-repair2-dependent20.log`. Current configuration and artifacts are retained; no benchmark runs concurrently. The build must reach a terminal result before its acceptance is claimed. Supported-field saga crash/recovery, full enabled suite and final memory gates remain to be established.

Milestone20 source coverage adds controlled restart of the shared native-field saga at step2, after the Float32/CStringVec update. It preserves memory-store rows using its existing preserve() facility and reopens both external stores through their normal lifecycle. Replay-success and replay-compensation variants are registered in all three owning saga programs. New execution has not yet occurred. This is controlled DB failure/stop/reinitialization, not process-crash coverage. The top-level build remains live as session17545; its source pass must include these changes before any acceptance claim.

The same milestone20 test batch strengthens repairSize error assertions to require the existing parameter-size diagnostic for each callback, not merely any Event. No production diagnostic, validation or completion behavior changes. These new assertions and restart variants remain unaccepted until rebuilt and run.

Milestone20 also closes two workload omissions in the shared benchmark: store.recover result materialization and a distinct single-vector-element mutation after the existing seqNo-only update. Both benchmark headers remain identical. Versions advance0→1→2→delete-3; no production or baseline library algorithm changes. New measurements require both fixtures rebuilt, and will be recorded separately from paired19. The isolated HEAD test must be incrementally rebuilt after the top-level candidate build to retain the aggregate -j4 cap.

Continuation checkpoint: session17545 is authoritatively live (write_stdin returned the same session id); top-level build has reached zhttp/test without reported compiler diagnostics. Do not restart it. Re-poll this handle to a terminal result, then build the isolated HEAD zdb/test with -j4 sequentially. Run milestone20 owning saga units and both store integrations before claiming the new controlled-restart/diagnostic assertions accepted. The benchmark also needs a small semantic smoke run before fresh paired timings. PostgreSQL remains stopped; use the existing disposable cluster and ~/postgres/bin. No new test or timing result is claimed for source batch20.

Milestone20 source byte accounting extends the existing ZiFBIOTest with GrowStorage, a test-only forwarding observer over unchanged IOStorage<32>. It counts backing reallocations, preserved head/tail relocation, in-place tail movement and detach compaction for fitting/splitting payloads, and compares bytes/layout with the actual transmit save path. Initial pooled owner allocation and emitted-buffer allocations are kept distinct; reference/output observer copies are excluded. No production instrumentation or growth policy changed. zi/test was already traversed by the live aggregate build, so its changed fixture needs a sequential incremental continuation after session17545 finishes. These counters remain unaccepted pending execution.

Continuation progress: added the test-only I/O growth observer and reconciled zi/interop documentation with the current retained SagaPayload path and release-only acceptance. No production source changed this turn. The previous turn made source/evidence progress, not merely a status restatement. Session17545 remains live while four compiler processes execute the retained HTTP test build; no diagnostics were found. Do not start another aggregate build or timing run. After terminal success, incrementally refresh zi/test, zi/interop and the isolated HEAD zdb/test sequentially with -j4; then execute the owning suites and canonical interop plus both stores. Source batch20 remains unaccepted.

Milestone20 adds tx mode to the existing zi/interop/zifbiobench. It compares direct native transmit construction with current native generic scratch/span insertion through the same synchronous sink. Alternating rounds report emitted bytes, owner allocation requests, sends, completed-span bytes and elapsed time. Both outputs must be identical and independently verified before measurement. This is a mechanism control, not a replacement HEAD comparator; timed sinks avoid observer copies. No new benchmark framework, production instrumentation, output policy or build configuration. The mode is uncompiled/unmeasured pending the sequential zi/interop continuation.

User steering: stop building zhttp and focus on the saga performance regression. Stopped aggregate build20 root359239 and descendants; session17545 reached terminal143. Its compiler descendants subsequently exited. Do not resume the aggregate/dependent build while this steering applies. Milestone20 source tests/bench changes remain unaccepted. Focus now is causal saga profiling using the previously built paired19 executables; no unrelated rebuild or acceptance suite.

Saga-focused diagnostic20: both current binaries hash-match paired19 acceptance despite newer uncompiled source fixtures. A smoke run with current libZm preloaded into HEAD exited0, and LD_DEBUG confirms the current library was initialized. ZmScheduler object layout is unchanged; changed thread declarations add methods without member-layout changes. The substitution isolates compiled Zm-library differences, not solely the queueCount branch. Six sequential runs (forward/reversed order: HEAD, HEAD+currentZm, candidate; count10000 rounds7 width32) are live as session53369, evidence `scheduler20/`. Original baseline files/algorithms are untouched, and these substituted runs are diagnostic, not replacement baseline acceptance. No compiler is running.

Scheduler20 terminal acceptance: session53369 exited0, all six processes exited0. Current-Zm substitution into HEAD improves complete-saga p50 by2.8%/2.9% in the two orders; candidate remains9.7%/4.3% slower than original HEAD (and slower than HEAD with current Zm). Compiled Zm changes do not reproduce the regression. This rules them out as its sole/main observed cause in these runs; it does not attribute all remaining cost. Full raw distributions and scope are in scheduler20/summary.json/results.md. No production ordering branch was changed.

Existing profile17 thread6 contains the native DB saga commit/replication work; its candidate CPU1.346s vsHEAD1.254s includes+0.059s syscall and+0.027s AnyRow::replicate. It still profiles pre-milestone19 generic envelopes, so it cannot establish current envelope costs. Fresh diagnostic CPU profiles of paired19 executables are running sequentially as session94230, profile20/; do not interpret instrumented timing as acceptance. Aggregate/dependent building stays stopped per user instruction.

Profile20 completed both processes with exit0. Worker identity was resolved by sampled call stacks, not assumed thread-number order: HEAD DBthread8/store9, candidate DBthread9/store8. Candidate DB sampledCPU1.473s vsHEAD1.315s; AnyRow::replicate0.206s vs0.163s and commitSend0.115s vs0.094s. Instrumented scope and raw reports retained in profile20/.

Focused envelope21 source change: small withOffsets slots now use exact metadata-sized ZuArray instead of a growable owning ZtBuiltin. All slots retain explicit assignment/omission and uninitialized primitive storage. Larger schemas retain existing ZtScratch fallback. Zero-slot tables use one unused backing slot because ZuArray requires nonzero capacity. No serializer/layout/error/growth policy change. Before native Zi/Zdb libraries and header are preserved in envelope21/ for same-binary library controls. Only owning zi/src and zdb/src libraries will incrementally rebuild with -j4; no zhttp/dependent/test fixture rebuild. Performance benefit remains a hypothesis until measured.

Envelope21 owning-library build session11671 completed zi/src and zdb/src with exit0 and no compiler diagnostics. Existing paired19 ZdbSagaTest then passed all84 top-level tests (exit0), using the rebuilt libraries; batch20 source fixtures remain uncompiled. Four same-binary, forward/reversed before/after runs are live as session82801 (count10000 rounds7 width32). Before controls preload only the preserved pre-change Zi/Zdb libraries. zdb/src also rebuilt libZdbMem; that current library is common to both control and after runs, so this experiment isolates the Zi/Zdb core-library substitution rather than reproducing the entire original candidate library set. No unrelated/dependent build is running. No benefit is claimed before the comparisons finish.

Envelope21 comparison completed: session82801 terminal0, all four before/after processes exit0. Saga p50 falls5.8%/5.5%, p95 from40.2–40.4us to29.0–29.8us; scheduler medians remain within about1%. Scope remains core-library substitution with common current ZdbMem, not matched HEAD acceptance.

Paired21 session2269 terminal0, all four matched HEAD/candidate runs exit0 (count10000 rounds7 width32; accepted paired19 executables). Saga p50 remains9.5%/11.7% slower than HEAD, despite individual store median improvements and fewer allocations. Reported scheduler controls move roughly0% in set0 and3% in set1; no subtraction or blanket acceptance. Full distributions/conditions retained in paired21/. Further saga-specific profiling is required; aggregate and zhttp builds remain stopped.

Fresh post-offset-change candidate CPU profile21 is running as session55199 with gprofng, using the same accepted binary and workload. Instrumented timing is diagnostic only. No concurrent compiler, integration suite or timing-acceptance run.

User requested scrutiny of additional saga copies/allocations. Completed source tracing and recorded the audit in findings3.md: redundant completion-wrapper closure, existing driver/action closures, explicit retained payload copies, shared memory-store tuple/key materialization, const-reference delete-key copies, and recovery inline-payload relocation. HEAD comparison distinguishes longstanding costs from migration changes.

User requested the SagaCompleteFn fix. Both MSaga::run overloads now use completeFn: exact SagaCompleteFn inputs forward by copy/move without a capturing adapter; other callable inputs retain the prior wrapper. Current build session31469 sequentially rebuilds zdb/src, candidate ZdbSagaTest, and matched HEAD ZdbSagaTest only, each -j4; retained configuration unchanged. This also compiles the previously pending memory restart fixtures and identical benchmark additions, so forthcoming results must be distinguished from paired19/21. No external-store, HTTP or aggregate builds.

Callback22 build session31469 completed all three owning/fixture targets with exit0 and no compiler diagnostics. Refreshed candidate ZdbSagaTest passed all86 top-level tests, exit0, including the two previously source-only memory restart cases. Full build log and TAP/errors retained in callback22/. Matched count10000 rounds7 width32 measurements now use both refreshed fixtures, identical shared benchmark source and unchanged HEAD algorithms; run order HEAD/candidate then candidate/HEAD. Broader store/HTTP/dependent build remains deferred.

Callback22 matched timing session39477 is live; poll the same session to terminal before claiming timing or allocation results. No compiler is running.

Callback22 session39477 terminal0: all four matched runs exit0. Summary session79024 terminal0; raw CSV, conditions, distributions and source/build evidence retained in callback22/. Saga observed allocation totals109.765/109.996 versusHEAD121.040/121.745; prior paired21 candidate112.915/112.899 is consistent with removal of approximately3 wrapper allocations, though source workload refresh/background destruction prevent exact causal telemetry attribution. Saga median remains22.162/22.151us versusHEAD19.406/19.657us (+14.2%/+12.7%). Callback fix is accepted by source analysis, build and86 tests; broader performance regression remains unresolved. No HTTP or aggregate build resumed.

Goal continuation: previous turn made verified progress (callback22 source fix, owning builds,86 saga tests and matched benchmark evidence); no-progress threshold does not apply. User steering still confines current work to saga performance, with HTTP/aggregate builds stopped.

Delete23 removes a shared memory-store cost exercised five times by warm three-step saga cleanup: negative-VN writes decode primary-key fields directly using compiled key metadata. They no longer allocate a transient full-cardinality Value Tuple plus MemRow or copy their primary key into another Tuple. Metadata updates, missing-row diagnostic, secondary-index removal, UN index and callback paths retain their previous semantics. Removed dead loadDelTuple helper. This cost also existed at HEAD; optimizing it is not attribution of migration regression.

Only libZdbMem was incrementally rebuilt, session75137 terminal0, no diagnostics. Existing callback22 saga executable passed all86 tests with the new library, exit0. Before memory-store library preserved in delete23/; an LD_PRELOAD smoke exited0 and LD_DEBUG confirmed its initialization. Six sequential before/candidate/HEAD runs now compare unchanged executables and width32,count10000,rounds7 with reversed order. No benchmark runs concurrently with compilation or acceptance programs.

Delete23 timing session36893 terminal0, all six processes exit0; summary session18530 terminal0. Same-executable before/candidate saga p50 worsens4.2%/5.9% despite observed allocations falling about109→100. Matched HEAD regression remains10.5%/13.3%. This is not a latency success. Reverted only the delete23 source experiment using exact preserved before copies; other worktree repairs retained. Owning memory library will rebuild before further executable evidence. User steering now focuses ZiFB versus canonical FlatBuffers, including direct serialized reads instead of C++ temporary materialization; no further shared store optimization planned in this batch.

Read24 source batch extends the existing zdbmsginterop entry point with --reads: canonical generated direct field access versus native borrowed view access over the exact same canonical-produced Record, plus an explicitly labeled owning-consumer control with SagaPayload storage. Both readers consume identical fields; owning mode materializes all Record fields. Timing excludes input construction, validation and heap snapshots. No new framework or production instrumentation. Build session16513 sequentially restores the pre-delete-experiment libZdbMem and builds only zdbmsginterop, -j4, current configuration. New diagnostics remain unaccepted until compilation and execution.

Read24 session16513 and diagnostic session27635 completed exit0; four interop TAP groups passed. Recorded original read and envelope controls in read24/. Found observer callback allocation and ZmVHeap jumbo telemetry exclusion. Changed only the diagnostic observer to a noncapturing callback; owning-target incremental build session53947 completed exit0. Corrected read32/read4096 commands both exit0; borrowed readers show zero tracked heap requests. Small accessor timing difference changes sign between builds; no stable read regression attribution. Owning materialization remains substantially slower. Added direct serialized read review/measurement requirements to repair2.md; no production read rewrite or HTTP/aggregate rebuild. Full saga latency acceptance remains open.

Growth25 extends existing zdbmsginterop with canonical matched SagaData wire layout and local storage/allocator growth observers, not production instrumentation. Build session25731 completed exit0. Both payload32 and4096 diagnostic processes in session38428 completed exit0; outer and inner canonical/native verification plus field/payload checks run before timing. Small SagaData messages emit232bytes: both grow once208→312; native relocates208bytes versuscanonical204 and has higher median cost. Large4096payload emits4296bytes: canonical grows twice to6456, native once to4304 and relocates far fewer bytes. Reused owners grow zero times in both. Therefore excessive backing allocations are not demonstrated for this row.

Assembly confirms native inline-field helpers contain full repeated growth slow paths, while canonical calls separate vector_downward::reallocate. Experimental one-line production optimization marks Arena::grow_ ZuNoInline; it changes no growth, sizing, error or ownership semantics. Original header and runnable interop fixture preserved in growth25/. Only interop target is rebuilding as session22964 to compare this mechanism before propagating to owning libraries or saga binaries. Experimental source remains unaccepted pending evidence; no HTTP or aggregate build.

Growth25 isolated outlining experiment: diagnostic build22964 and all12 processes in session89659 completed exit0. Native row medians improve roughly10–13% in most pairs; allocation/movement totals unchanged. Owning milestone session33834 reached terminal0 overall, but its combined shell did not fail fast: inspection found zi/test/ZiFBIOTest failed due mixed unsigned/uint64_t initializer-list lengths in pending batch20 observer. Zi/Zdb libraries and ZdbSagaTest built without diagnostics. This is not a successful whole milestone. Corrected only test length deduction; fixture build session13838 runs fail-fast for zi/test codec targets and zdbinterop (also final diagnostic declaration/indentation cleanup). No executable acceptance or saga timing is claimed before it completes. Preserved before saga executable and three owning libraries for full before/after comparison.

Growth25 corrected fixture build session13838 completed both targets exit0 without compiler diagnostics. Prove acceptance session completed exit0 for ZiFBTest, ZiFBReflectTest, ZiFBIOTest (including pending relocation observer), ZdbSagaTest (all86 top-level tests), and zdbmsginterop. Preserved full before fixture preload smoke exit0; LD_DEBUG confirms initialization of preserved Zi/Zdb/ZdbMem libraries. Six matched HEAD/before/after processes are running sequentially as session78643, with reversed order,count10000 rounds7,width32. Await terminal state and distributions before accepting production outlining.

Growth25 session78643 terminal0: all six matched before/after/HEAD processes exit0. Summary session21572 terminal0. Retain Arena::grow_ outlining based on isolated codec improvements and126 passing top-level TAP tests. Saga p50 after21.079/21.290us versusbefore21.831/21.901us (-3.4%/-2.8%) andHEAD20.298/19.997us (+3.8%/+6.5%). Scheduler controls improve~3% in order0 and~1% in order1; do not wholly attribute full-saga delta or infer acceptance. Full distributions/conditions and exact before libraries retained in growth25/. Broader saga regression remains open, no HTTP or aggregate build.

Profile26 session20537 completed both HEAD/current candidate profiles exit0. Worker identity confirmed by call stacks (HEADDB8/store7,candidateDB6/store7); reports retained in profile26/. Current candidate replicate,commitSend andmemoryloadRow sampledCPU are belowHEAD; excessCPU mainlysyscalls/waits/posts, not a demonstrated new codec read/materialization cost. Existing orderingguard retained. Shared benchmark snapshots allocate a capturing observer and scan allheaps outside timing; diagnostic source batch27 removes observer closure and adds --repair-latency to omit snapshots with empty unmeasured allocation CSV columns. Identical shared header and CLI branch applied to matched HEAD fixture only; no library algorithm changes. Next owning fixture build/controlled comparison remains required.

Posts28 static audit completed in response to user steering: inspect ring capture packs and redundant invoke/invoked. Recorded five pre-existing SagaFn erasures allocating stateful action closures, and one definitely redundant same-shard invoke in follower recovery eviction. No production changes or performance claim; existing release validation and queued ordering retained. Scheduler Linux getTID is TLS load, not syscall. Audit: zdb/interop/evidence/repair2-20261010/posts28.md. Observer27 session71658 completed both candidate and matched HEAD fixture builds exit0; latency experiment remains pending. No HTTP/aggregate rebuild.

Capture29 implements user-requested removal of the five SagaFn action wrappers: preserve ZuDecay<L> through SagaInsert/Update/Delete, remove now-unused erased alias and two obsolete T aliases. Completion forwarding and all error/assertion paths unchanged. Also applies proven same-shard direct follower-recovery eviction from posts28; no queued ordering changes. Before sources, executable/wrapper and Zi/Zdb/ZdbMem libraries preserved in capture29/. Incremental owning build session40386: zdb/src libZdb.la and libZdbMem.la exit0, zdb/test ZdbSagaTest still running. No acceptance yet.

Capture29 build40386 terminal0 for both owning targets, no diagnostics; prove all86 saga tests passed. Measurement16528 terminal0: preload smoke confirms preserved Zi/Zdb/ZdbMem; all8 full heap/latency before/after/HEAD processes exit0. Summary35017 terminal0. Retain explicit user-requested action allocation elimination: mean108.7232→105.7341 tracked requests/3-step saga (~3 lower). Latency0 p50 before20.919us→after19.757us (-5.55%) versusHEAD19.155us with stable scheduler medians, but p95/p99 worsen; reverse order p50 improves10.23% with significant scheduler/environment variation. No overall saga performance acceptance. Small CSV parsing overlapped part of latency0HEAD; recorded limitation. Pending broad integration/store fixtures/final memory gates remain. HTTP/aggregate builds remain stopped.

Profile30 collection32626 completed both candidate/HEAD exit0; per-worker reports all exit0. Candidate binaries are capture29; detected subsequent source-only drainShards template refactor and preserved it, not represented by runtime evidence. Sampling confirms loadRow0.073→0.064s, commitSend0.083→0.080s, replicate0.151→0.170s and excess syscall/wake CPU; no extra post or codec copy established. Reports diagnostic only. Paired31 session47707 terminal0, all24 alternating unprofiled processes exit0; summary90284 terminal0. 12pairs,count2000,round1,width32,latency-only. Pooled saga p50 HEAD19.156us/current19.968us (+4.24%), p95+2.95%,p99+4.85%; median paired ratios+4.06/+3.57/+1.13%. Scheduler controls slightly faster currently and generic saga-save median241→151ns. Remaining complete-saga discrepancy corroborated; cause/performance acceptance open. No source changes, builds, HTTP/aggregate work this turn.

Profile32: user explicitly directs z.config -p, authorizing profiling configuration exception. Created isolated fresh source snapshots /tmp/z-repair2-profile32-{candidate,head}, recorded source manifests; existing release configuration/artifacts untouched. Both ./z.config -L -p /usr configure commands exit0 with PATH including ~/postgres/bin; optimized flags retained plus-pg, existing enabled features. Initial snapshot omitted configure.ac; first build terminal2 before compilation, preserved attempt1 logs and corrected source copy. Retry session63726 is live, sequential dependency-order owning targets at-j4, candidate thenHEAD then saga fixtures. No HTTP/aggregate build. Initial shared header hash check found a subsequent current-fixture change preserving concrete schedHop completion types. Copied that fixture-only change into the profiling HEAD snapshot before its build; original HEAD header and matched header retained in profile32/. Shared profiling headers now identical; HEAD library algorithms unchanged. Instrumented timings are diagnostic only.

Profile32 session63726 progress: candidate owning dependency chain and ZdbSagaTest all completed exit0. HEAD completed through zfb/zv, now building zdb. Actual object/library nm confirms mcount instrumentation. Clang emits unused-pg warnings only on shared-library links (candidate18); retained logs, do not claim warning-free diagnostic build. Root release executable/library hashes still match capture29. Profiling-header match includes the already-present templated schedHop fixture change; library scheduler algorithm unchanged.

Profile32 session63726 completed all 19 build commands with exit0, retaining documented unused-pg shared-link warnings. Runner83252: candidate 86 tests, loader, instrumented benchmark and gprof exited0. Historical HEAD full suite failed findFail and stopped advancing in abandonRollback after a null-UN recovered-step diagnostic. Confirmed process455659 live and waiting in futex; sent SIGINT to that test only, runner terminal1. Failure and interruption record preserved; cause unproven. Resume98923 independently completed HEAD loader, benchmark and gprof with exit0. Both gmon files retained. Analysis44065 terminal0: default gprof wrongly attributes over130000 calls to startup up(); explicit nm-derived address symbol table resolves attribution. Single executable histogram has only48 candidate/63 HEAD samples, with hundreds of DSO child arcs outside its range; glibc mcount is not thread safe. These profiles cannot establish whole-process CPU or the cause of the4% latency gap. Reproducible analyze.py, address maps/reports and README retained in profile32/. No production change, no HTTP/aggregate build; full performance acceptance remains open.

Paths33: previous turn made profiling/evidence progress, not a blocked wait. Current source traced against HEAD for AnyRow::replicate, endNested, saveRecord/saveMsg, detach/prepend and RepBuf borrowed adapters. Generated8 targeted reports from profile30; session24604 terminal0, all reports exit0; hot-PC stderr only reports the requested60-entry print limit, other stderr empty. This profile attributes replicate increase to SagaData/SagaStep rather than Order, but prior profile26 differs; no causal claim. Direct nested offset construction and borrowed field reads remain intact; no new redundant copy demonstrated. Conditional prepend/growth must be measured rather than assumed absent. Evidence and limitations retained in paths33/README.md. No source repair or new build, HTTP and aggregate builds remain stopped; saga performance acceptance remains open.

Buffers34 actual saga diagnostics: previous turn produced narrowed profile evidence. No build or production change this turn. Confirmed release candidate hashes still match capture29. Initial gdb observer53164 counted prepends but missed template copies; corrected all-copy observer25743 exposed100 debug-type parse failures, preserved. Typed observer3095 and canonical observer36041 both terminal0 with zero observer errors. For101 sagas both versions perform1111 row replications and2222 header prepends; neither relocates on prepend. Candidate100 I/O arena growth calls versus HEAD101, recurring208→312 in both; moved208 versus204 bytes per recurring growth. Extra native growth allocations are not established. Header lengths expose native commit envelopes8 bytes larger than canonical; source order table/un/shard differs from canonical un/table/shard, providing a concrete padding investigation. Raw stacks, addresses, hashes, corrections and limits in buffers34/. No debugger timings are acceptance; saga latency regression remains open, no HTTP/aggregate build.

Layout35 experiment: buffers34 demonstrated commit messages8 bytes larger with metadata-order emission. Typed saveObject now sorts only final table member emission by descending natural wire alignment using ZuTypeSort; slots/defaults/child construction/validation unchanged. Existing message commit interop test compares equal-width native/canonical frame lengths. Preserved before sources, release executables and owning libraries in layout35/. Synchronized the already-present concrete schedHop benchmark helper into matched release HEAD fixture before rebuilding; old header retained. Batch rebuild limited to Zi/Zdb owning codec libraries and relevant existing codec/interop/saga fixtures, sequential -j4; no HTTP/aggregate build. Experimental change is not accepted pending milestone correctness and before/after measurements.

Layout35 general-order experiment: build75932 terminal0 without diagnostics; all171 top-level checks passed. Runner75927 completed all latency/heap/buffer/debugger commands exit0. Summary12875 terminal0. Generic ordering reduces commit size, but increases SagaData by8 bytes, adds one warm growth and doubles4096-byte diagnostic growth calls/backing allocations from1 to2 per fresh row, with about38% worse diagnostic time. Rejected the generic WriteAlign sort despite promising saga medians; restored ZiFB.hh byte-for-byte to its preserved before header. Narrow replacement adds saveCommit beside existing envelope composers and uses it only in commitSend and its existing interop test; UN precedes table offset, slots/defaults unchanged. Rejected sources/results retained, narrow owning rebuild and acceptance pending.

Layout35 narrow milestone: build71245 terminal0, seven owning/fixture commands, no diagnostics. Runner16080 and summary59638 terminal0; all171 checks passed, loader verified preserved before libraries, all latency/heap/buffer/observer commands exit0. Generic ZiFB header hash exactly restored. Actual commit bodies shrink75896→68640 bytes over1111 messages (-9.56%), non-commit total199552 unchanged. Growth and prepend counts match before;4096 diagnostic again one backing allocation per fresh row, no generic layout regression. Retain narrower source candidate for this demonstrated padding removal. Saga p50 after18.916/20.018us versus HEAD19.086/18.936us (-0.89%/+5.71%); p95/p99 still worsen against HEAD, with scheduler controls within1.3%. No blanket timing acceptance or causal claim. Narrow README records full distributions, telemetry limits, before-helper difference and current source scope; broader performance/integration/final memory gates remain open.

Paired36 no-build comparison: runner94765 all24 alternating processes exit0; summary58368 terminal0. Current artifacts match layout35 narrow, HEAD/current shared helper identical. Saga median19.166→20.068us (+4.71%, median paired+5.39%); p95+0.31%,p99-2.48% pooled. Scheduler8/32-post medians nearly identical, generic saga-save faster. Gap remains, no full acceptance. Reads37 native primary-key getter is385 bytes and repeats nested-vector empty tests; attempted matching HEAD standalone symbol is absent, no paired assembly claim. Source establishes IOBuf_::fbo_ calls data_ with an added empty-vector test versus HEAD direct trusted root access. Trusted38 candidate directly constructs the nested view in that one accessor, preserving verified/public helper behavior and HEAD's trusted precondition. Before artifacts saved; owning build and acceptance pending.

Trusted38 experiment: build8939 terminal0, all3 owning/fixture targets, no diagnostics; runner89117 all171 checks and all36 short measurements exit0; summary61676 terminal0. Native standalone key getter no longer emitted, but candidate is slower versus preserved preload-before by3.33% median paired. Matched-loading follow-up6212 preloads before/current/HEAD equally, all loaders and36 measurements exit0; summary97072 terminal0. Candidate is still5.26% slower than before at median paired, despite lower source/accessor branch count; reject experiment. Preserved candidate header, assembly and all measurements. Restored ZdbBuf.hh byte-for-byte to before; restoring owning build and executable acceptance before further work. No new validation or cached state introduced, narrower commit repair retained.

Trusted38 restoration: build83300 terminal0, all3 owning/fixture targets. accept.py exit0, all171 top-level checks pass. ZdbBuf source, saga executable, libZdb, libZdbMem and libZi hashes match preserved before artifacts exactly. No rejected accessor code remains; narrow commit writer retained. Applicable normal-load current result is paired36 (+4.71% pooled saga median), not the rejected candidate's lower HEAD ratios. No new accepted optimization this turn; completed matched-loader comparison and rejected a measured median regression. Broader goal remains active, no HTTP/aggregate build; remaining performance/integration/memory gates unchanged.

Usage39 diagnostic milestone: opt-in round-boundary getrusage(RUSAGE_THREAD) snapshots added to the shared saga fixture, byte-identical in matched HEAD/current. Samples main, DB and memory-store owners via existing run/inline completion; four extra posts per whole round, outside operation timers. No production code edits or library rebuilds. Existing retained-release saga fixtures compiling; outcomes pending. Goal remains active; this diagnoses CPU/context-switch costs, not exact post counts or full performance acceptance.

Usage39 completed: both incremental fixture builds exit0 without warnings, candidate86 saga checks pass. Six alternating pairs, runner54183 terminal0, all12 processes0;180000 retained samples/version. Round CPU paired median current-minus-HEAD main-0.006us, DB-0.176us, store-0.200us per saga; DBvoluntaryswitches+0.057/saga, not an exact postcount or proven cause. Diagnostic p50HEAD/current19.026/18.685us, p9523.444/24.295us, p9928.974/30.678us; median direction differs from uninstrumentedpaired36 while tails worsen. ProductionZi/Zdb/Memhashes unchanged. Opt-in benchmark diagnostic retained, previousbinaries/header preserved. No productionoptimization accepted on these counters; performance/fullrepair acceptance remainsopen.

Lookup40 static milestone: matched current/HEAD Order, SagaData and SagaStep
primary-key findBuf and iterKey disassembly retained with executable hashes.
Native iterKey calls separate borrowed tuple getters; HEAD inlines its field
reads. All three native getters have zero call instructions, no allocator or
memcpy. No owning row materialization found. Native findBuf also excludes
positive-version projections, a required cold-load correctness difference.
No speculative inlining/ownership change; trusted38 already demonstrated that
changed inlining alone does not establish a latency improvement.
Paired41 now runs the existing fixtures without ZDB_REPAIR_USAGE or heap
snapshots, two alternating long pairs each at widths32/4096, count10000 rounds7.
No rebuild or production changes; runner81636 live at launch, outcomes pending.

Paired41 runner81636 terminal0, all8 processes0; summary45790 terminal0.
Two alternating pairs each width32/4096, count10000 rounds7, no diagnostic
sampler/heap snapshots, unchanged production library hashes. Width32 HEAD/current
p50=18.946/18.314us, p95=22.953/23.805us, p99=28.093/27.762us.
Width4096 p50=19.106/18.945us, p95=24.966/23.354us, p99=30.427/28.182us;
scheduler controls also improve at4096, no pure codec attribution. Width selects
separate codec/store payload, not the LiveSaga application's vector size.
Current median faster, width32 p95 still worse; no blanket acceptance. Fixture
binary layout changed with usage39 diagnostic even when disabled; previous
paired36 executable results remain distinct. Evidence paired41/README.md.

Layout42 runner57688 terminal0:4 loader smokes and16 rotating workload processes
all0,60000 saga samples/version. Identical explicit library loading for both
executable generations; pre-diagnostic hashes exactly paired36. Pre-diagnostic
native/HEAD p50=18.895/19.326us, p95=25.768/24.556us, p99=29.776/29.766us;
paired median changes-2.47%/+3.98%/-1.98%. Diagnostic-generation medians/tails
also improve in both implementations; no isolated layout cause or stable median
regression established. Explicit loading differs from earlier normal-wrapper
acceptance; no gate waived. Restored both benchmark source/executables exactly
to paired36 hashes, preserved diagnostic binaries and source in evidence.
Diagnostic object files invalidated by newer header mtimes for next incremental
build; no production library/configuration changes or rebuild. Restored current
86 saga checks pass. Subsequent acceptance uses fixed ordinary wrapper fixture.
Tail discrepancy and wider repair2 acceptance remain open.

Rootcache43 implementation milestone: buffers34 directly observed100 recurring
208→312 growths at the final message vtable-cache append (tail204, cache4,
next scratch4). All native saveMsg call sites immediately finish the envelope.
Added compile-time endTable<Cache=true> policy; only saveMsg usesfalse, retaining
existing vtable deduplication but omitting the unused final-root cache entry.
finish releases only current-root cache scratch before alignment/prefix writes;
nested parent scratch remains bounded by m_cache. No growth algorithm, buffer
pool, limits, validation, callback or wire-field changes. Both steps are needed
for the208-byte frame to finish without copying to312-byte storage. Before
sources/libraries/executables preserved in rootcache43/. Owning incremental
Zi/Zdb and existing codec/interop/saga fixture build99498 live; all outcomes and
performance acceptance pending. No HTTP/aggregate build or alternate config.
Scheduler audit also confirms ZmRingFn has variable-size messages: no fixed
capture-capacity threshold; heap ownership comes from move/defer or overflow.

Rootcache43 first build99498 terminal0 without warnings. Acceptance runner
stopped immediately at171-test milestone:169 pass, two I/O groups fail because
clearing cache in genericfinish removes their deliberately exercised growth /
compaction. Original test assertions preserved. Narrowed API to endTable<Final>
(defaultfalse); only saveMsg usestrue, deduplicates against existing entries,
then releases current-root cache instead of adding the final unused entry.
Genericfinish restored exactly. This eliminates only the observed final-message
cache cost and preserves generic scratch/lifecycle coverage. Initial logs retained
in attempt1/. Corrected owning incremental milestone now rebuilding; no HTTP.

Rootcache43 corrected build42086 terminal0, all6 owning/fixture commands succeed
without warnings. Runner3751 passed171 existing checks and all3 loader smokes;
matched explicit Zi/Zdb/Mem loading for HEAD/before/current. Timing, heap,
buffer diagnostics and final actual-growth observer running; no acceptance claim
yet. Original I/O fixture assertions preserved and now pass. Genericfinish exact
before semantics; only saveMsg selects final-table scratch release.

Rootcache43 rejected after actual observer, despite corrected171 checks passing.
Runner3751 terminal0 all22 commands0; summary27219 terminal0.100 arena growths
removed but100 header-prepend relocations introduced, moving20800 bytes: before
400 scratch+20400 payload bytes, candidate20800 payload bytes. Body208 plusHdr4
exceeds original208 allocation, so a backing allocation is still necessary.
1111replicate/2222prepend and268192bodybytes unchanged, zero observer errors.
Broader payload fixtures still one fresh allocation; no allocation elimination.
All source/libraries/fixtures restored exactly to before.json via atomic binary
replacement; candidate artifacts saved. Header mtimes invalidate candidateobjects
for next incremental build. Restored171 checks pass. No retained Final API or
scratch changes; accepted earlier optimizations unchanged. Read rootcache43/
README.md before attempting similar growth/cache/header changes. Goal active.

Wire44 static/debugger milestone: native71181 and HEAD79704 terminal0, zero
observer errors,11 sagas each,121replicate/242prepend. Recurring SagaData bodies
are208 bytes with112-byte nested rows in both; first bodies200. ExistingHdr4
requires more than original208 allocation. Decoded nativeRecord/root/row layouts
and enumerated120 SagaData field orders plus allRecord/rootorders for ten
observed shapes, reproducingactualsizes then findingcurrentalreadyminimal under
fixedchildren/alignment/presence. No simpleorderingfix for warmgrowthbody; no
source/build changes, original43beforehashesallmatch. Othernativebodies often
smallerHEAD (Order update216/240, delete136/192), notlatencycausation. Evidence
wire44/README.md, rawhex/layouts/packing.py andpacking.json. Goal remainsactive.

Ring45 scheduler ownership audit completes: both matched whole benchmark
processes exit normally,982 successful enqueues each, identical target counts
(SID1=6,SID3=535,SID4=441), zero failed pushes and zero heap-owned Fn objects at
enqueue. Direct m_ptr OnHeap checks supplement allocation-entry probes that
could miss inlined code; zero observer errors. Captures range8–144 bytes; no
fixed inline-size threshold. Referenced capture objects may independently
allocate, and deferred closures never queued are outside direct proof. Scope is
whole process (11 sagas plus store/controls/setup/teardown), not recovery or
saturation. No extra aggregate posts or overflow demonstrated; no new scheduler
optimization justified. All11 accepted source/artifact hashes still match.
No production edit/build; debugger timings not acceptance. Evidence ring45/;
performance and full repair2 gates remain open.

Paired46 longer fixed-fixture comparison completes: runner71183 terminal0,
all12 alternating workload processes0; summary70678 terminal0. Sixpairs,
count10000 rounds3 width32,180000samples/version/operation, normal libtool
wrappers and no heap snapshots. Both fixture sources and recorded artifacts
exactly match paired36; all11 accepted source/artifact hashes still match.
Saga HEAD/currentp50=19.006/19.206us (+1.05%),p95=23.313/24.356us (+4.47%),
p99=29.295/29.485us (+.65%). Median paired changes+.47/+4.19/+.25%; p95 slower
in5/6 pairs but ranges-8.59% to+17.05%. Scheduler controls slightly faster and
codec saga-save231→151ns; no control subtraction/causal claim. Same fixed
artifacts show smaller median but larger p95 gap thanpaired36. No production
edit/build/configuration change. Evidence paired46/README.md; no-material-
regression and full repair2 gates remain open.

Hw47 instruction-sampling diagnostic rejected as unusable. User-only gprofng
counter probe completed, but real candidate --repair-latency10000 7 32 ran82s
without a completed benchmark phase; SIGINT child-2, runner11511 terminal254.
HEAD/later pairs not run. Report99.14% instructions in one libc static routine
under store record<Upd>/builder.string; interrupted experiment not closed.
No cause, corrupt length or saga cost inferred. Read-only gdb attach failed
ptrace permission; preserve reports/logs inhw47/. All11 accepted hashes still
match; no source/build/configuration changes. Warm write audit uses trusted
msg_/record_ and direct reads; no repeated verification demonstrated. No removal
of verification justified. Paired46 remains latest ordinary latency evidence;
full repair2 remains active and open.

Profile48 refreshes clock sampling on accepted fixed release artifacts after
narrow commit packing: four workloads0 (runner77453 terminal0), both orders,
count10000 rounds7 width32. All report commands0 (report81172 terminal0), generated
only after collection. Every header warns997→0 timer interval; preserve reliability
limit, not timing acceptance. DB total HEAD/current1.258/1.229s then1.270/1.313s;
replicate inclusive.170/.155 then.151/.157; commit.082/.103 then.100/.081.
Saga store loadRow inclusive.085/.076 then.082/.068; store syscall.208/.239 then
.206/.265. No consistent extra reader/builder CPU attribution or extra-post claim.
Callback audit retains per-step SagaStepDriver→SagaCompleteFn allocation because
copied erased delegates share consumed mutable state; simply passing copied
concrete drivers changes behavior. Source mechanism atHEAD too. Evidence
profile48/README.md andcallbacks.md. All11 accepted hashes match; no production
edit/build/configuration change. Paired46 remains ordinary latency evidence;
performance/full repair2 gates remainopen, HTTP/aggregate stillstopped.

Key49 candidate removes repeated envelope/nested-root navigation per key field:
localized RepBuf_<T> tuple renderer extracts one borrowed view, shares existing
view-field getters; no node size/ownership/validation/callback changes. Before
sources and artifacts preserved. Initial build82248 failed metadata adapter
layer; attempt1 retained. Corrected build55965 terminal0 all6 commands0 without
diagnostics. Runner32860 terminal0:171 checks pass,3 verified loader smokes,
9 latency runs and2 heap runs0. Separate getters disappear; iterKey sizes
305→506/355→605/366→787 (Order/SagaData/SagaStep), calls5→3, not timing proof.
First3 rotations have mixed medians and p99 worse all3 versusbefore. Candidate
not accepted. Longer six balanced-order triples now running session66841,
count10000 rounds3 width32, explicit matched loaders. No HTTP/aggregate,
configuration change or profiler. Evidence key49/README.md; inspect session
before continuation and decide retain/reject from evidence. Current source and
artifacts contain candidate until that decision; before/ supports exact restore.

Key49 controlled milestone completed: longer runner66841 terminal0 all21
commands0 (3 verified loader smokes+18 workloads), summary18301 terminal0,
180000samples/version/operation. Median paired current/before saga p50/p95/p99
changes-1.294/-3.683/-2.746%; current/HEAD-2.738/-2.027/-5.934%. Scheduler
medians within+.5% versusbefore; generic saga-save unchanged. Compiled
Record.data slot guards Order2→1,SagaData2→1,SagaStep3→1 corroborate one
borrowed view per extraction; not dynamic instruction counts. Retain focused
key-read change after171 passing checks and this controlled comparison; short
trial's tail concern superseded by stronger but still variable measurements.
Ordinary-wrapper matched HEAD/current acceptance now running session12508,
paired50/,6 alternatingpairs count10000 rounds3 width32. No further source
change/build/profiler; exact HEAD artifacts/shared benchmark match prior.
Full repair2 gates remain open; HTTP/aggregate stay stopped.

Paired50 ordinary-wrapper comparison completes: runner12508 terminal0 all12
workload processes0; summary70048 terminal0. Six alternatingpairs,count10000
rounds3 width32,180000samples/version/operation. HEAD/shared fixture unchanged;
current artifacts exactly match key49/after.json. Saga HEAD/current p50
19.286/19.145us (-.73%),p95 24.636/25.157us (+2.11%),p99 29.695/32.140us
(+8.23%); median paired changes-1.70/+2.50/+2.69%. Scheduler p95 pooled
2/8/32-post-.69/+3.60/+5.64%, no subtraction or causal attribution. Retain
focused single-view key extraction based on key49's controlled before/after
improvements, unchanged ownership/node layout and171 passing checks; full
no-material-regression gate remains open because ordinary saga tails stillworse.
No further production edit/build/configuration change. Evidence paired50/README.md;
full integration/store/final-memory gates stillopen, HTTP/aggregate remainstopped.

Read51 source batch (unbuilt): pass existing Msg::RecordView/shard/UN from
StoreTbl::write into its sole private loadRow caller; fetchVN once while retaining
same owning Tuple/MemRow/filter/error/callback behavior. Move terminal captured
buf refs into DB::replicate in both AnyTable::write closures; necessary leader
capture copy and allposts remain. Move final-use update/delete temporarykeys
into existing missing-row diagnostic lambdas (coldpath, no warm latencyclaim).
Before accepted key49 sources/artifacts saved and verified; all owning binaries
remain unchanged. No build/test/profiler thisturn; candidate source not accepted
by earlier171 tests orpaired50 timings. Evidence read51/source.diff,README.md,
unbuilt.json. Continue static batch review before owning-j4 milestone; no HTTP,
aggregate, alternate configuration, exceptions or validation changes. Goalactive.


Read51 built milestone and paired52 (2026-10-10): scoped sequential owning-j4
build13556 terminal0, all3commands0/no diagnostics. Runner97641 terminal0,
all24commands0,171 existing codec/interop/saga checks pass. Six balanced
before/current/HEAD orders: median paired saga current/before p50+1.54%,
p95-3.14%,p99-4.21%; store insert/update/delete p50-2.85%/-2.05%/-2.25%.
Mixed saga timings do not prove a full-saga gain. Compiled loadRow shrinks
1351→1193bytes; both replication closures lose the final-use atomic increment,
with3 rather than4static locked instructions; no heap-elision claim.
Ordinary-wrapper paired52 runner41898 terminal0,12commands0,summary60120
terminal0:180000samples/version/operation. Saga HEAD/current pooled
p50 19.266/19.357us (+.47%),p95 25.978/25.638us (-1.31%),p99
31.128/31.208us (+.26%); median paired-.84/-2.31/-6.71%. All14current
source/artifact hashes match read51/after.json after completion. Retain narrow
read51 source repair; current ordinary pooled timings close to HEAD but prior
tail variability and full gate remain open. Evidence read51/README.md and
paired52/README.md. Broader stores/integration/full-suite/final-memory gates
remain incomplete; HTTP/aggregate builds remain stopped. No new configuration,
validation, exceptions, scheduler posts or callback-order change.


Key53 source batch: change Saga::findDel and both Table_::findDel overloads
to owning by-value keys and move onward into existing lookup/private paths,
matching findUpd. Temporary/converting owning keys no longer require the
const-reference-to-by-value deep copy. Reused lvalue keys retain owned lookup
state. Applies to ordinary/secondary/intended-UN deletes, saga forward/reverse
and journal cleanup; no callback/post/UN/error/validation change. Cost also
exists at HEAD, not a proven migration regression. Saved verified read51
sources/artifacts in key53/before; scoped sequential-j4 owning/fixture build
44934 is running. Source candidate not yet accepted. HTTP/aggregate stopped.


Key53 focused milestone: build44934 terminal0, all3 scoped sequential-j4
commands0/no diagnostics. Runner69348 terminal0/all24commands0,171checks
pass; summary60813 terminal0. Six balanced before/current/HEAD orders,180000
samples/version/operation, verified14current source/artifact hashes. Median
paired current/before saga p50/p95/p99-1.286/-3.624/-4.147%; current/HEAD
-2.629/-1.968/-4.032%. Scheduler medians versusbefore within+.16%, no control
subtraction. Allocation means105.375/105.943 are noisy; short builtin strings
still physically copy on move, no heap-elision claim. Retain the focused owning
findDel key transfer; no universal performance acceptance. Evidence key53/README.md,
source.diff,summary.json,paired-ratios.json. Ordinary paired52 predates this edit.
Continue pending scoped store/saga integration, not HTTP/aggregate.


Store54 scoped integration build started session74708 after key53 benchmark
runner and summary reached terminal0. Sequential-j4 sqlite3/postgres owning
libraries, unit and integration fixtures only; exact targets in store54/. No
HTTP/aggregate/configuration change. Poll same handle; no executable store
acceptance yet. Existing disposable PostgreSQL cluster confirmed stopped and
original socket/port/listen options inspected; ~/postgres/bin remains selected.


Store54 completed: build74708 terminal0, six sequential scoped-j4 directory
commands0, no compiler diagnostics. Runner31777 terminal0: sqlite units6,
postgres units13, sqlite integration56, postgres integration22 top-level tests
allpass (97total, no whole-program skips). This accepts pending external-store
batch20 native Float32/CStringVec controlled restart-after-update success and
compensation cases, plus strengthened parameter-size callback assertions.
Controlled DB restart is distinct from existing process-crash coverage.
Disposable PostgreSQL16.3/uint/libz used with original private socket55432,
TCP disabled, ~/postgres/bin; stopped cleanly and status rechecked. Linked
client libpq18.6 and sqlite runtime3.53.4 recorded separately. All26source/
artifact hashes verified; conditions/commands/logs retained in store54/.
No production edit/newtests/configuration/HTTP/aggregate work in this milestone.
Full dependent consistency, enabled suite, remaining matched workload acceptance
and final memory gate remain open; goalactive.


Paired55 final ordinary-wrapper width32/4096 comparison started session92292,
six alternating matched HEAD/current pairs perwidth,10000x3 samples/process.
Current artifacts match key53; shared fixture identical, HEAD hashes unchanged.
No concurrent build/profiler. audit55.md records current partial requirement
audit and remaining proof. Prepared dependent56 scoped-j4 targets but not
launched during timing; HTTP/root aggregate remain excluded. Goalactive.


Paired55 completed: runner92292 terminal0/all24 workloads0, summary72330
terminal0. Six alternating HEAD/current pairs perwidth32/4096,180000samples
perversion/operation/width. Ordinary wrappers/shared fixture/current release;
all14key53 hashes verified, identical artifacts at both widths. Saga pooled
p50/p95/p99 versusHEAD width32-1.72/-4.33/-6.69%; width4096-1.15/-7.31/
-13.18%; median paired-1.94/-4.78/-5.66% and-2.00/-4.62/-4.96%. Scheduler
controls also improve; no subtraction/causal attribution. Width affects store
vectors/standalone payload save, not live-saga target size. Small-row store.update
still slower: median paired+1.35/+3.98/+3.33%, p50slower6/6; large-vector
update-8.95%median. Record residual instead of blanket performance acceptance.
Evidence paired55/README.md and bothwidth summaries. Next scoped dependent56
build refreshes Zf/Zi/Zdb tests/interop/examples and zum/ztc/zdf consumers; no
zhttp module/root aggregate target or configuration change.


Dependent56 launched session11200 after paired55 runner/summary terminal0.
Nine sequential scoped-j4 directory targets; no zhttp module/root aggregate
command, install, clean or configuration switch. Existing HTTP libraries can be
linked by Zdb-dependent consumers without rebuilding the zhttp module. Exact
commands/progress in dependent56/. Poll same handle to terminal; full dependent
and executable suite acceptance not yet established. Goalactive.


Dependent56 attempt1 session11200 terminal2: Zf/Zi test builds passed; Zi
interop benchmark TxSink retained old one-argument TxStream construction.
Preserved attempt1 logs and fixture source; updated only initializer to
Base{ZiIOBuf_DefltSize,0,0}, retaining existing size/no reservations under
current three-argument API. No production shim/configuration change. Retry
session71557 resumes seven remaining directories at Zi interop. Poll this
handle; final dependent/suite acceptance not yet established. Paired55 source/
artifacts unchanged by this interop-only constructor propagation. Goalactive.


Dependent56 retry71557 terminal2: Zi interop and Zdb test/itest/example
builds pass, Zum stops at missing libZrest.la. Owner runner93142 terminal2:
Zf344top-level tests pass; ZiTxStreamTest loses valid prefix after rejected
printable, then fixture dereferences absent sent[0] and SEGV. GDB reproduces.
Current flush early-returns on m_failed before sending retained prefix; this
contradicts current documented and HEAD behavior. Stale-object explanation was
considered but not established; source contains defect. prefix57 restores
HEAD send-nonempty/clear-owner/return-sticky-failure sequence, no new validation
or throws. Preserved before header/current key53 artifacts; source candidate
not yet accepted. Retry was already terminal before intended interruption; no
process killed. Prepare22 sequential-j4 owning/dependency/consumer directories,
including required Zrest before Zum; no zhttp/rootaggregate/config switch.

Prefix57 owning/consumer refresh launched session15060;22sequential-j4
commands, current configuration. No concurrent test, profiler or benchmark.
Poll same handle to terminal; then existing ZiTxStream/codec/Zdb/store and
applicable dependent acceptance. No post-fix runtime result yet. Goalactive.


Prefix57 full Zi suite session79163 terminal0:25programs/156top-level
tests pass, including restored oversized-printable prefix flush, move/failure
and layered behavior. Current Zi loaded SHF_ALLOC sections/VMAs equal preserved
key53 except ignored GNUbuild-id note (zi-loaded-sections.json); this alone
does not certify all benchmark dependencies unchanged. Build15060 remainslive
with lower/owning stages passing; no compiler diagnostics. Prepared final
seven-program Memcheck runner but it requires all22build commands0 before
launch; no Valgrind run yet, no source changes thisturn. Goalactive.

Prefix57 owner runner39665 terminal0, all4commands0: Zi canonical interop
passes; full Zdb units3programs/91top-level tests, message interop4groups and
Zdb integration4programs/6top-level tests pass. No skips claimed as passes.
All relevant owning build directories preceded execution; later store/Zum
compilation does not alter these owning libraries. owner-artifacts.json records
20source/executable/library hashes. Full22command build15060 remainslive,
no compiler diagnostics; external/dependent suites and final memory stillpending.


Prefix57 external runner completed all eight commands with expected exits
(PG stopped status3, all others0). sqlite3 unit6/integration56 and postgres
unit13/integration22:97top-level checks pass. All26recorded source/artifact
hashes rechecked unchanged before callback58; disposable PG confirmed stopped.
Dependent build15060 subsequently exposes ten Zum template errors: removing
SagaFn erasure leaves Table::findUpd missing-row callback argument as ZmRef,
which cannot deduce generic auto* action parameters. callback58 passes row.ptr()
at both findUpd/findDel missing-row sites, preserving raw-row callback contract
without restoring heap-backed erasure. Successful ownership and all failure
semantics unchanged. Source candidate pending batch rebuild/acceptance; original
build still live, no overlapping retry. Evidence callback58/README.md.


Scheduler comparison clarification: the pinned matched baseline is6e9a1252,
while current repository HEAD is83d96641. Production scheduler does differ
from the pinned baseline: overflow queueCount ordering guard and coordinator
thread retain/join lifecycle changes. None was edited in callback58. The shared
templated schedHop fixture remains identical on both sides; control results
measure each complete runtime stack. Do not claim identical production scheduler
or attribute observed changes solely to ZiFB. Exact diff/hashes recorded in
paired59/scheduler-baseline.diff and scheduler-sources.json.


Callback58 owning acceptance: runner7665 terminal0, threecommands0;
Zdb units91, messageinterop4 and integration6 top-level checks pass after
corresponding owning rebuilds. Source/artifact hashes retained. Incremental
build44634 remains live through sqlite/store/Zum/Ztc/Zdf; no compiler diagnostics
so far. Broader consumer, final memory and refreshed performance pending.
No production scheduler source changed; pinnedbaseline distinction recorded
separately in paired59. Goalactive.


Callback58 external runner93210 terminal0, alleightcommands expected exits:
sqlite unit6/integration56, postgres unit13/integration22:97checks pass.
All26source/artifact hashes verified unchanged; disposablepostgres stopped
(status3). Combined current Zdb/store acceptance198top-level checks. Zi201
checks remain prefix57 evidence with unchanged Zi source/artifacts; no blanket
broader-consumer or final-memory acceptance. Build44634 remainslive in Zum,
first11directories0, no compiler diagnostics so far. Next: poll same build;
after14successfulcommands run callback58/accept.py (consumer suites and final
memory), then paired59/both.py and perwidth summaries with no build overlap.


Callback58 Zum saga translation unit compiled successfully; object and Zdb
header hashes in callback58/zum-saga-object.json. Zumsrc linked and build
advanced to Zumtest, resolving the ten prior generic-pointer deduction errors.
This is compile evidence, not runtime acceptance. Build44634 stilllive; no
compiler diagnostics recorded. Paired55 allsix width32 shapes confirm update
524bytes HEAD versus516native with same fieldmask475, so a larger native message
does not explain the indexed-update residual. Full enabled-scope inventory now
explicit in enabled-suite-inventory.json/audit55.md; lower/network/Gtk/Zi-itest
checks are still outstanding beyond current scoped consumer batch. No extra
source/configuration change or unlaunched gate claimed as passing.


Callback58 Zum unit runner50831 terminal0: fiveprograms/35top-level checks
pass, including the existing Zum HTTP-dependent fixture without building the
zhttp module. Zumtest build completion was confirmed before this run; no
compilation occurred in the test command. Source/executable/library hashes in
zum-unit-artifacts.json. Remaining consumer runner now omits Zumtest and final
accept.py requires its recordedexit0. Build44634 remainslive at Zumrestart
integration compilation; no compiler diagnostics. No memory/perf gate launched.

Callback58 build44634 observed terminal2 after draining Ztc compiler jobs:
13commands, first12exit0 (including Zum1322.46s), Ztc failed because libZws.la
is missing. Consumer61 prerequisite inventory finds only that external missing
library; missing libZdf.la is produced by its planned owning src stage. No
source edit/configuration change. Retry will build Zws library then Ztc/Zdf;
accepted-build.json is generated only after actual first12+three successful
commands, preserving original failed build.json and source hash verification.
Final acceptance/memory/performance guards consume that successful15command
manifest; they do not erase or waive the prior build failure.

Consumer61 retry59371 confirmed live after callback58 terminal2. Required
Zws library command completed0 in13.83s, no source/configuration edit; Ztc has
advanced into test compilation, no compiler diagnostics. Zdf remains next.
No zhttp module/root aggregate command. Final acceptance and timing runners
still unlaunched; guards require generated accepted15command manifest, which
is absent until retry's three actual commands all succeed. Poll59371, do not
restart from observation timeout. Existing Zdb/store198 and Zumunit35 results
remain separately recorded for unchanged callback58 source/artifact generation.

Consumer61 build59371 terminal0, allthreecommands0: Zws library13.83s,
Ztc532.29s, Zdf complete. No compiler diagnostics. accepted-build.json contains
15actualsuccessful commands (callback58 first12+consumer61 three), preserving
the earlier failed attempt separately. Current production header hash unchanged.
Readonly make-q freshness queries for zu/zm/zt/ze/zf/zi/zv/zdb source targets all
return0; owner-freshness.json records commands/results. Consumer prefix12982
is live through Zum integration after Zuminterop0; no final memory or refreshed
performance run yet. Remaining full enabled scope stays explicitly incomplete.

Fixture62 retry48022 terminal0, threefixture directories0, no compiler
diagnostics. Only RepairRow and AllTypes CStringVec native element ownership
and standard AliasFn accessors changed; production sources/libraries untouched.
Current Zdb/store acceptance and final memory rerun started after allthreebuilds
passed. The prior120invalidreads are retained in callback58; no suppression or
new Row buffer-reference storage added. Four Zi Memcheck passes are reused only
with unchanged binary/library hashes; the failed old saga result is not reused.
Consumer61 build and callback58 sixconsumer suites remain separately accepted
for unchanged production source. Current-generation performance not yet run.


### Fixture62 and paired59 repair acceptance — 2026-10-10

Fixture62 corrects persistent native CStringVec fixture ownership without
changing the production borrowed decoder contract. Incremental affected builds
and all198 Zdb/store checks passed; final nine Memcheck programs report zero
errors and zero definite/indirect/possible lost bytes. Four unchanged Zi results
were reused with exact artifact hashes. Consumer61 build and callback58 consumer
acceptance passed separately (Zum35 unit checks and104 other consumer checks).

Paired59 refreshed matched release timing with180000samples/version/operation
per width. Saga median paired p50 improves2.24% in both runs; tails are mixed,
including width4096 median paired p99 +3.25% despite pooled p99 -2.66%.
Small indexed-update median paired p50 +0.81% splits three pairs each way.
Keep broad performance acceptance open; no measured new copy/post cause has
been established. Exact data and runtime-scheduler caveat are in paired59.

Enabled63 started the remaining enabled-module incremental build batch only
after workloads and summarizers completed. No zhttp or root aggregate build,
installation, alternate compiler/platform/configuration or production edit.
Its build/tests and full enabled acceptance remain outstanding. Repair2 remains
active and incomplete; see audit55.md for requirement-level limits.


Enabled63 build milestone: all44incremental build commands completed0 without
compiler diagnostics. Verified804source/artifact hashes and unchanged final
memory-gate library/fixture hashes. The26remaining enabled test targets are
running sequentially; no test completion or full-suite acceptance is claimed.


Enabled63 runtime acceptance stopped at an existing release-mode QUIC fixture
error: LinkTxDiag counters compile out, but three expected values assumed debug
counters. Fixture/counter definition match pinned6e9a1252. Crypto65 changes only
those expected values to DiagCounter{3/6/2}; behavior/rejection assertions remain.
Its incremental fixture rebuild passed; source/executable are the only two
changed entries in the804entry manifest. Failed log retained. First14targets
passed; failed target and remaining11targets are now running with hash guards.
No production change or debug configuration was introduced.


Crypto65 corrected QUIC acceptance passed all20programs/161checks. WebSocket
units and REST units passed; public Binance interop reported its explicit opt-in
skip. REST integration then failed because required zrest/example peers were
absent. The initial enabled63 plan omitted them; rest66 now builds those two
peers incrementally, linking existing HTTP libraries. No new production edit.
Original failure/observation logs retained. Remaining acceptance is stopped
pending that prerequisite build, not reported as running or complete.


### Remaining enabled acceptance and allocation refresh completed

Rest66 peer build passed, then all26remaining enabled targets passed with811
source/artifact hashes verified:173prove programs/2094top-level checks, plus
TOML/Go/Python/shell checks. Initial public Binance skip was followed by an
explicit successful run. Earlier QUIC and missing-peer failures remain recorded;
no new production change. Final memory manifests remain unchanged and the
disposable postgres cluster is stopped. zhttp/root aggregate remain excluded.

Counts64 allfourmatched counter runs passed with unchanged paired59 binaries/
libraries. Tracked live-saga allocations average121baseline versus105–106current,
about15–16fewer per three-step saga. Store insert drops3, indexed update1,
update-one unchanged. ZmVHeap jumbo and cross-snapshot async activity limit
interpretation; no allocation-instrumented latency used for acceptance.
Broad performance acceptance remains open because paired59 tails are mixed.


Paired67 completed12processes0 and same-artifact verification, repeating
width4096 after acceptance. Saga median paired p50/p95/p99 improves
2.48/2.83/5.49%; pooled3.19/1.08/4.53%. Combined12pairs across paired59/67
favor current median12/12, median paired changes-2.27/-2.59/-3.01%.
Host snapshots recorded performance governors and global CPU/load; no causal
attribution or adjustment. Paired68 now repeats width32's remaining small
indexed-update median residual, retaining prior and new pairs, same fixture/
artifacts/configuration. No further production or benchmark-source change.
Removed only two generated executable .o files from completed isolated
profile32 trees to free112808344bytes; all recorded profiling binaries/libraries,
sources and raw profiles verified retained. Current release/baseline objects
untouched. Broad final acceptance remains pending performance/scope assessment.

## Paired68 current performance assessment

Paired68 completed all12 width32 workload processes with unchanged matched
artifacts. Across all12 paired59+paired68 pairs, saga median paired p50/p95/p99
improves3.95/2.97/3.44%; indexed update remains+1.16/-2.88/+0.51% (only4/12
medians favor current). Update-one improves4.08/2.06/2.63%, all12 medians favor
current. Width4096 combined saga improves2.27/2.59/3.01%. The saga median
regression is absent in these current comparisons; tails vary and the small
indexed-update discrepancy persists. No cause is established and broad repair
completion is not claimed. Raw paired59/67/68 CSV evidence was losslessly gzip
archived with verified original/archive SHA256, recovering806869290bytes;
summary values/sample content unchanged. Full details:paired68/README.md,
combined-paired.json and csv-archives.json. External-store/transmit timing and
the user-excluded zhttp scope remain outstanding limits.

## Direct transmit measurement after paired68

Existing already-built zifbiobench tx mode completed four processes0,11
alternating rounds each. Source/executable/library hashes unchanged; no rebuild
or configuration change. Payload64/928 emits104/968bytes: direct/staged
completed-span insertion0/104 and0/968, pool requests1/1, paired median round
time-5.92/-4.90%. Payload944/4096 emits984/4136bytes and takes the existing
splitting fallback: full completed-span insertion on both paths, one extra
direct pooled-owner request (3/2 or6/5), paired timing+62.81/+15.60%.
Counts are pool requests, not backing reallocations; in-owner growth relocation
and detach compaction remain separate. This is a current-native mechanism
control, not canonical HEAD/network/external-store/saga latency evidence.
See tx69/README.md,commands.json,summary.json,artifacts-verified.json.

Static indexed-update reinspection: loadRow borrows the RecordView then builds
its required owning mutable Tuple once; updTuple moves mutable Values. Before
mutation, independent old secondary-key snapshots are retained; changed-key
insertion moves the new Tuple. Pinned baseline uses the same snapshot/delete/
reinsert algorithm. No new extra copy/post is established by the small median
residual. No speculative code edit or numerical acceptance threshold introduced.

## External70 backend acceptance preparation

Existing shared RepairBench now optionally selects sqlite3/postgres through
ZDB_MODULE/ZDB_REPAIR_CONFIG. Default memory operations/config unchanged;
backend start/stop occurs outside direct-store timing. Both versions use the
same helper. Isolated baseline stores built incrementally, then both fixtures;
all4commands0. Candidate saga86checks and refreshed final Saga Memcheck0,
zeroerrors/leaks; four external smoke processes0, intended loader paths verified,
postgres stopped cleanly. No production library edit. Earlier811remaining-enabled
hashes unchanged; owning fixture is separately guarded and has changed.

External70 runner1458 now collects six alternating pairs/backend/width32/4096,
count1000rounds3 with fresh owned storage. No overlapping build/test/profiler/
summary. Results pending; earlier memory/saga performance evidence retains its
original binary generation. zhttp scope question remains pending; no zhttp build.

## External70 failure and store71 isolation

External70 incremental builds4commands0, current saga86checks and refreshed
Saga Memcheck0 (zeroerrors and definite/indirect/possible lostbytes). Four small
HEAD/current sqlite3/postgres smoke processes0; dynamic-loader records verify
intended tree libraries/modules. Full matched baseline sqlite3 workload then
reported primary-key-missing SagaData/SagaStep deletes and became INACTIVE.
Specific failed child609450 terminated withSIGTERM; runner1458 exit1, own
postgres stopped0. Database/partialCSV/log/interruption record retained.
Candidate same1000x3 width32 complete external workload exited0. This establishes
an observed baseline-only failure in these runs, not its cause or general
correctness. No latency acceptance from the failed batch.

Store71 adds a shared ZDB_REPAIR_STORE_ONLY selection to stop after existing
direct store operations, isolating their performance from baseline saga failure.
No production/scheduler/library algorithm edit. Default full workload unchanged.
Both fixtures rebuilding incrementally under same configuration,-j4. Baseline
full external-saga latency remains unestablished; the failure is not hidden by
the store-only comparison. Prior memory-saga and functional external-store
acceptance keep their recorded scope. zhttp remains stopped; scope question
pending. Broad completion still unproven.

## Latest acceptance checkpoint — store71

All48 direct-store latency processes and16 allocation-counter processes passed
on sqlite3/postgres atwidth32/4096; six alternating latency pairs per workload,
18000samples/operation/version. Source/library hashes reverified and own postgres
stopped. Current shared fixture is optional-backend/store-only; production
libraries remain identical to paired59/67/68/counts64. Refreshed Saga fixture
passed86checks and final Memcheck with zeroerrors/definite/indirect/possible lost
bytes. Other owning/consumer/remaining-enabled acceptance remains scoped to
unchanged source/binary/library manifests; all811remaining-enabled hashes rechecked.

Small external find/recover saves one tracked Z-heap allocation on both backends.
Large-row counts are unchanged; full-row/delete envelopes are8bytes smaller,
key-select byte counts unchanged. SQLite large-row find/recover/select paired
medians improve18.00/23.98/11.37%. Most other store medians differ by0–2%;
postgres large select is+2.04%. Some large-row tails worsen and paired ranges
are broad. Store72 repeats only width4096 with the same artifacts, retaining all
prior/new pairs. Counter-mode timings are not acceptance, and heap telemetry
excludes SQLite/libpq/system and ZmVHeap allocations.

Memory saga current-generation combined medians improve3.95/2.27% atwidth32/4096;
small indexed update remains+1.16%. Direct transmit below the aligned emission
limit avoids completed-span copies; oversized splitting adds an owner request
and retains its measured overhead. Full baseline external sqlite3 saga failed
with missing-primary-key bookkeeping deletes; candidate same full workload
passed. Its cause and external-saga latency remain unestablished. zhttp remains
excluded under user steering; no scope reply yet. Broad completion is unproven.
Evidence:store71/README.md,latency-summary.json,counts-summary.json,
terminal-verification.json; store72/ pending. Earlier pending statements below
are historical checkpoints and do not replace this current assessment.

## Current assessment after store72

Store72 completed24same-artifact large-row repeat processes0 and stopped own
postgres; all813current remaining-enabled/owning guard entries and both tree's
measured artifacts reverified. Combined12pairs/backend,width4096: sqlite3
find/recover/select median improves18.03/23.25/12.07%,all12medians favor current.
SQLite update/update-one/delete median near parity,p99+0.46/+0.76/+1.05%, with
original extreme pairs retained. Postgres select remains+3.32/+2.33/+5.81% by
median paired p50/p95/p99; only2/12medians favor current. Other postgres medians
are-0.41% to+0.49%, but several tails still worsen. Do not dismiss select as noise.

Static select trace found borrowed PGresult Value overlays and direct pooled
builder output, no new owning Tuple/staging/post/invoke. Large select bytes32928
and tracked requests5 are unchanged; common saveTuple also serves find/recover.
Next investigation targets the select conversion/builder/growth path against
pinned baseline. No speculative production change. Scope/conditions/allpairs:
store72/README.md,combined-paired.json,terminal-verification.json.

Current owning Saga86checks/final Memcheck pass atstore71; production libraries
remain unchanged since paired59. Earlier memory-saga combined medians improve;
small indexed update+1.16%, external70 baseline full-saga failure and user-stopped
zhttp remain separately reported. Broad repair2 completion remains unproven.

## Grow73 postgres select diagnostic

Both existing release fixtures under libtool exec gdb completed0 with zero
observer errors. Four width4096 cycles/version including warmup; all create/
drop/stop commands0 and artifact hashes unchanged. Each select grows twice,
528→16912→33296, carries0front and32+16420tail bytes. Eight observed growths
and65808tail bytes per version, identical. No extra native growth or relocation
volume for this exercised row. Native saveArray and canonical pvectorIter both
write borrowed postgres elements directly into uninitialized output. No owning
array relay found. Debugger timings do not establish latency/CPU equality.

The repeated postgres select discrepancy remains+3.32% median; growth is now
ruled out as an extra-cost mechanism in this scope. Next investigation targets
conversion/finalization CPU versus backend/scheduler waits. No speculative
production change, no new validation/exception/configuration/HTTP build.
Evidence:grow73/README.md,head/candidate observations.json,installed.json,
commands.json,artifacts-verified.json. Goal remains active and incomplete.

## Current investigation — vector75

Profile74 completed four matched postgres client sampling runs. All experiments
warn about an unreliable sampling interval; CPU samples are diagnostic leads.
Generated assembly independently shows native saveArray reloading referenced span
and captured count state per element, whereas the pinned baseline retains its
loop state in registers. The narrow vector75 repair snapshots the borrowed input
span and loop bound locally inside the fill callback. Rebuilt assembly confirms
register state; no payload copy, owning vector, allocation, validation or format
change was introduced.

The postgres library and its unit/benchmark/integration consumers rebuilt
incrementally in the retained release configuration with -j4 and no diagnostics.
Existing postgres unit13 and integration22 top-level checks passed. Six matched
uninstrumented large-row postgres pairs are running; latency effect and refreshed
final memory gate remain pending. The store71/72 artifact generation is now
historical, not certification of this changed postgres library. Memory-store and
sqlite3 production libraries are unchanged. Broad repair2 acceptance remains
unproven; the baseline external-saga failure and user-stopped zhttp remain open.
Evidence: zdb/interop/evidence/repair2-20261010/profile74 and vector75.


## Current result — vector75

The postgres scalar-vector fill loop now uses a local borrowed input cursor and
loop bound; rebuilt assembly eliminates the per-element referenced-state loads.
No payload copy, allocation, validation or format change. Four incremental -j4
build commands, existing unit13/integration22 top-level checks and final postgres
unit/actual-I/O Memcheck passed (zero errors or definite/indirect/possible leaks).

All12 uninstrumented postgres processes passed: six alternating pairs,
count1000rounds3width4096,18000samples/op/version. Select remains slower than
pinned baseline: median paired p50+4.75%,p95+2.04%,p99+3.14%,only1/6 faster
medians. No latency benefit is proven for this repair; it has not yet been
compared directly with the prior native module. Shared memory-store/sqlite3
production libraries and baseline artifacts are unchanged. The remaining813
artifact guard also matches but excludes the postgres module; separate runtime
manifests and new postgres source/fixture records cover this changed generation.
Owned databases dropped and cluster stopped. Broad acceptance remains open.
Evidence: zdb/interop/evidence/repair2-20261010/vector75/README.md and
terminal-verification.json. Profile74 warnings and all individual pairs retained.


## Current action — revert78

Cursor76 compared vector75's local-cursor change against the pre-change native
postgres module, using the same current executable/runtime. Archived prior
module hash exactly matches store72. All12 processes passed,18000samples/op/
version; paired select p50+6.10%,p95+3.86%,p99+6.81%,only1/6 favors the change.
This isolates the module generation, not canonical HEAD. Reduced assembly loads
did not justify retaining the change. The three-line saveArray experiment has
been reverted; postgres library/consumers are rebuilding incrementally at-j4.
No other repair is reverted. Results/manifests/allpairs: cursor76/README.md.

Vector75 timing/memory evidence applies to the now-rejected generation. Once
restored artifacts are checked, earlier acceptance can be reused only for exact
matching artifacts. Saga77's full postgres comparison is prepared but not run;
no overlap with rebuild/diagnostics. Broad repair2 performance acceptance remains
open, including the earlier native/canonical select discrepancy.


## Current checkpoint — restored postgres generation

Vector75's local-cursor experiment is reverted after cursor76 isolated A/B
reported select+6.10/+3.86/+6.81% median paired p50/p95/p99,5/6 pairs slower.
Four source-first incremental postgres builds passed. The restored library/header
and every postgres source/binary in fixture62's external/final-memory manifests
match exactly; prior correctness and Memcheck acceptance is reused by artifact
identity. Both full store72 runtime manifests also match. No other repair changed.

Saga77 is running the existing FULL postgres workload atwidth32,count1000,
rounds3,six alternating canonical-HEAD/current pairs, with STORE_ONLY explicitly
cleared, fresh database per process and90second timeout. No result yet. This is
separate from the earlier failed full sqlite baseline and store-only71/72 results.
Broad repair2 performance acceptance remains open, especially repeated postgres
select and mixed tails. Evidence: revert78/README.md and
reused-acceptance-artifacts.json; cursor76/allpairs and artifacts retained.


## Current checkpoint — saga77 and overflow80

The narrow vector75 loop experiment remains reverted. Revert78's library,
header and recorded postgres source/binaries match accepted fixture62 and
store72 exactly; earlier correctness/final-memory results apply by identity.

Full postgres saga77 established a baseline validity problem: pinned HEAD
exited0 and emitted all16operations/3000samples, but logged72 ERROR/FATAL
missing-primary-key/deactivation diagnostics AFTER DB::stop_ began. Two current
full runs exited0 with all samples and no such diagnostics. Reject matched
latency acceptance from this batch; the next known-live HEAD process was
terminated(-15), runner1, finally database cleanup/cluster stop0. Artifacts
reverified. This is distinct from external70's full sqlite baseline failure.

Overflow80's corrected debugger observer required a positive pending count at
successful ring publication, rejecting one zero snapshot. It observed an
event-loop wake published with queueCount1 at the same destination thread as a
prior store-write overflow fallback. This proves wake overtaking, NOT store-write
reordering or the cause of missing-key errors. The debugged workload had no
persistence errors and perturbed the failure; all timings are diagnostic only.
Existing current scheduler ordering guard is retained; no scheduler/config edit.

Both failed-witness overflow79 and corrected overflow80 evidence are retained.
All processes are terminal and the owned postgres cluster is stopped. Broad
repair2 acceptance remains open: repeated postgres store-select/mixed-tail
performance, valid matched external-saga baseline, and user-stopped zhttp scope.
Evidence: saga77/terminal-verification.json, overflow80/terminal-verification.json,
revert78/reused-acceptance-artifacts.json. No clean build/install/config change.


## Current checkpoint — short81/82

The existing full saga workload completes cleanly on both external stores when
run for a shorter process length: count100rounds3width32,303actual/300measured
sagas per process. Six alternating pairs/backend,12processes each, all exit0 and
no ERROR/FATAL diagnostics;1800samples/op/version. Same schemas/defaults/pools,
original scheduler queue131072 and respective complete runtime stacks; no source,
build or configuration change. Postgres saga median paired p50/p95/p99 improves
12.60/12.69/0.90%; sqlite3 improves11.67/14.76/2.37%. All ratios retained.

These timings measure the existing logical saga completion callback, not durable
commit latency. Persistence/shutdown validity is checked separately after timing.
The shorter run does NOT replace saga77/external70's failed3000-measured-saga
baselines. Their backlog/shutdown behavior remains unaccepted and unexplained;
no broad full-external-saga timing claim. Only1800samples/version limits tail
precision. Scheduler controls compare different runtimes and are not subtracted.

Separate four-process counter batches/backend pass,600samples/op/version. Saga
mean tracked allocations fall postgres55.902→48.010 and sqlite3 50.462→43.372.
Telemetry excludes dependency/system and ZmVHeap jumbo allocations; asynchronous
store work can overlap adjacent logical-completion snapshot windows. These are
observed window deltas, not exact isolated per-saga costs. Instrumented timings
are excluded from acceptance. Artifacts reverified; postgres stopped(status3).
Five relevant baseline production files independently match git6e9a1252 exactly.

The vector75 experiment remains reverted and accepted artifact identities match
revert78. Repeated large-row postgres select/mixed-tail performance, long external
baseline validity and user-stopped zhttp scope keep broad repair2 acceptance open.
Evidence: short81/82 README, latency/counts-summary.json and terminal manifests;
short81/pinned-production-source-check.json. No new production change.


## Current narrowing — runtime83

Profile74's archived libpq/libc/loader/libck/hwloc files have identical hashes
between baseline/current; module compiler/optimization/link flags also match.
Current TableView/VectorView source uses trusted borrowed accessors, with ingress
verification separate; no owning decode or additional per-field verification was
found in these accessors. These checks do not prove equality of all runtime work.

Refreshed existing same-process schema-writer controls (write/widewrite,32 and
16384-byte payloads,100000iterations,11alternating rounds,fresh/reuse) all pass,
with exact output bytes/checksums/capacities and unchanged artifacts. Native
median paired batch means favor native in all cases. Mixed/vector32904-byte
output: fresh1248.86→1180.40ns/op(-5.54%),reuse642.33→565.09(-12.80%).
Byte-only16424-byte fresh is near parity(-0.93%); reuse improves11.81%.
These are batch means, not latency quantiles; conversion from postgres, query
execution and scheduler are excluded. They do not explain or clear the repeated
large postgres select regression. No source fix is justified by these controls.

Short81/82 clean full-saga comparisons and separate counter coverage remain as
recorded below; longer baseline failures remain invalid. No production change,
build, new test fixture or queue tuning. All processes terminal, postgres stopped.
Evidence: runtime83/README.md,writer-summary.json,terminal-verification.json;
zi/interop/README.md refreshed to point to these controls. Broad repair2 remains
incomplete, preserving long-baseline/default-queue failures and store-select gaps.


## Current diagnostic checkpoint — control85/86

Ordering84's stricter write-over-write observer did not save a confirmed witness;
HEAD hit a missing-key SagaStep delete, failed leader election and INACTIVE state
before shutdown. The bounded150s debugger timed out124; cleanup and postgres
stop0. Final observer accounting is missing, so no negative ordering conclusion
or valid timing follows. Raw events/logs retained.

A separate canonical scheduler module control adds ONLY the existing current
queueCount guard to pinned HEAD tryPush_. Same compiler/generated Makefile/flags,
original objects/headers/Zu dependency; one source compile and relink0, no
production/configuration/queue-size change. Original baseline artifact hashes
remain identical. LD_DEBUG confirms only the isolated scheduler DSO is loaded.
Two full3000-measured-saga processes/backend (postgres and sqlite3) all exit0,
all16operations/3000samples each and no ERROR/FATAL diagnostics. Guard-only
replays support an ordering explanation, without proving every failure's cause.

These are explicitly PATCHED canonical diagnostics, not unmodified HEAD results
or a matched current/control performance comparison. The one-line patch is
reviewable at control85/scheduler.patch. User preference for long-run timing
against this labeled control has been requested; original HEAD failures remain
recorded. No dependent timing work proceeds on an assumed answer.

Short81/82 valid unmodified-HEAD comparisons, runtime83 writer controls and
revert78 original accepted artifacts remain as recorded. Broad repair2 remains
open, including original long-baseline failures and large postgres select.
Evidence: ordering84/terminal-verification.json; control85/86 README and terminal
manifests. All replays terminal; no new current production edit.


## Current diagnostic checkpoint — affinity87

The original-HEAD/current store-only postgres CPU-placement diagnostic is
terminal: six alternating pairs,1000iterations×3rounds,width4096. All12processes
exit0 with empty stderr,11operations/3000samples each; all12 task snapshots are
nonempty with the expected main/rx/tx/zdb/store CPU masks0/2/4/6/8. Owned postgres
was placed on CPU10. Artifacts are unchanged; database cleanup and stop succeeded.

Select remains slower: median paired p50/p95/p99 +2.29/+1.50/+11.65%,18000samples
per operation/version. Find/recover medians are essentially equal; this does not
establish the cause of select's discrepancy. Runtime placement differs from the
unbound store71/72 runs, whose +3.32/+2.33/+5.81% select result remains separate.
Do not pool the conditions or infer affinity-independent causality.

Evidence: zdb/interop/evidence/repair2-20261010/affinity87/README.md,
latency-summary.json and terminal-verification.json (reproducible verify.py).
No production edit/rebuild. Long external-saga baseline preference remains
pending: control85/86 contains only the existing queueCount guard, and is a
patched canonical diagnostic. Original HEAD failures and short81/82 valid
comparisons remain recorded. Broad repair2 acceptance is still open.


## Current diagnostic checkpoint — convert88

A standalone matched postgres Int32Vec result conversion diagnostic calls the
existing saveOffset helper from original6e9a1252/current headers and libraries,
including the actual binary-array length prefixes and endian conversion. Two
fixture-only compile/link commands reused each generated Makefile/configuration;
no production rebuild/edit. Loader controls confirm each version's own Z DSOs.
All24processes pass, all elements/output sizes checked before/after batches,
production runtime hashes unchanged. Six alternating pairs/width,CPU8,
5000conversions/batch,seven rounds,fresh/reused pooled buffers.

Median paired batch-mean differences:32elements fresh-10.08%,reuse-12.53%;
4096elements fresh+4.46%,reuse+3.19%. Every pair agrees in direction. Large-vector
HEAD/current process-median batch means are5920.18/6186.77ns fresh and
5527.99/5708.70ns reused. This is roughly0.2–0.3microseconds per vector; by itself
it does not explain the tens-of-microseconds whole-row select discrepancy.
The separate diagnostic can differ in compiler inlining/register allocation;
it measures one vector root, not full schema/table finalization or store latency.
Do not use it to reinstate rejected vector75 or claim acceptance.

Evidence: zdb/interop/evidence/repair2-20261010/convert88/README.md,summary.json,
terminal-verification.json,build.json and loader-verification.json. Affinity87
select remains+2.29/+1.50/+11.65% p50/p95/p99 under its distinct placement;
unbound store71/72 remains separately recorded. Long-saga baseline preference
is pending. Broad repair2 is open; all these diagnostic processes are terminal.


## Current diagnostic checkpoint — syscall89/server90

Client syscall tracing (original6e9a1252/current store-only,width4096,two pairs,
20iterations×3rounds) completes4processes0 without errors. All252select requests
receive the entire65932-byte result in one positive socket read; no added current
receive boundary was observed. Completion wakes match the blocked caller's futex
address. Last-read-to-wake medians HEAD/current61/62µs and62/64µs; send-to-first-read
medians1056/1079µs and1032/1078µs. These are instrumented intervals, not native
latency acceptance or isolated codec costs; they narrow the larger discrepancy
toward pre-read work without proving causality. Find includes3untimed checks.

A separate server-duration diagnostic uses existing per-connection logging
options, suppresses parameter text, and leaves global configuration unchanged.
Four originalHEAD/current processes0,100iterations×3rounds,width4096. Exact read
statement text matches.300measured samples/op/process after warm/semantic-check
exclusion. Select Bind medians HEAD/current94/93µs and87/87µs; Execute929/943µs
and949/934µs. Client select medians1106.34/1117.24µs and1115.88/1099.94µs.
Execution/client direction reverses between pairs; no stable cause is established.
Do not subtract medians as client CPU or use these instrumented runs to clear the
unbound/fixed-placement native select discrepancy.

Both diagnostics preserve exact production artifacts, drop owned databases and
stop postgres. No source repair or rebuild. Evidence in syscall89/server90
README.md,summary.json,terminal-verification.json and reproducible analyze.py.
Current conversion88/affinity87 and original store71/72 remain separately scoped.
Long-saga patched-control baseline preference is still pending; original failures
and valid short81/82 results retained. Broad repair2 acceptance remains open.


## Current diagnostic checkpoint — row91

The complete reflected postgres Order row writer is now measured in isolation,
using the same schema and borrowed PG binary representations as store71/72,
actual Order pooled allocator, schema-derived native alignment and original
canonical IOBuilder. Two final fixture-only compile/link commands0; three
fixture preparation failures retained separately (names/type span/enum definition).
No production edit/rebuild/reconfiguration. Loader controls verify the respective
Z DSOs. All24measured processes0, empty stderr, production hashes unchanged;
full-row values/bitmap/all vector elements verified outside each timed batch.
Six alternating pairs/width,seven rounds,5000conversions/batch,CPU8.

Widths32/4096 output416/32928bytes and capacities528/33296 match both versions.
Median paired batch-mean differences:small fresh+7.25%,reuse+5.73%;large fresh
+4.67%,reuse+4.06%, every pair favors canonical. Large HEAD/current absolute
medians11546.49/12084.99ns fresh and11171.57/11637.57ns reused:about0.47–0.54µs
extra. Small difference15–20ns. This complete writer cost still does not explain
the20–40µs whole-select discrepancy. Separate compilation/inlining caveats apply;
no additional owning copy/allocation was demonstrated and no new source repair
is justified by timing alone. Do not reapply rejected vector75.

Evidence: zdb/interop/evidence/repair2-20261010/row91/README.md,summary.json,
terminal-verification.json,build.json,loader-verification.json. Prior syscall89,
server90,conversion88 and affinity87 remain separately scoped. Original native
store71/72 select discrepancy and long external-saga baseline failure remain
unresolved; patched-control preference is pending. Broad repair2 remains open.
All diagnostic processes terminal; no benchmark/server/build left running.


## Current requirement checkpoint — audit92/reads93

A fresh requirement/artifact audit verifies850unique current files, including
all811remaining-dependent entries,26store source/artifact entries and the
current interop reader executable. The old saga executable in fixture62's memory
manifest is explicitly superseded by store71's current86-check/Memcheck result;
the other14memory entries match. All9applicable memory logs show zero errors and
zero definite/indirect/possible lost bytes.26remaining-enabled accepted targets
retain exit0 with matching artifacts. Five exported baseline production sources
still match git6e9a1252;23retained memory-benchmark artifacts/library identities
are verified separately, including saved pre-extension executables/headers.
No correctness/Valgrind suite, production rebuild or source repair was repeated.

The audit found that direct-reader timing predated the final rebuild. Existing
zdbmsginterop--reads was refreshed on current artifacts,1000000reads×7alternating
rounds at32/4096bytes. Both processes0,empty stderr,matching checksum/owner,
source/binary/config/library hashes unchanged and loader control0. Canonical/
native/explicit-owning median batch means:32bytes4.061/3.986/28.831ns;4096bytes
4.073/3.966/64.785ns. Median paired native/canonical changes-1.66/-3.14%.
Borrowed readers have zero tracked requests/materialized payload bytes; owning32
has one tracked request/read, owning4096 uses jumbo allocation excluded from
telemetry. These are scoped batch means, not complete-saga latency acceptance.

Current zdb/interop README acceptance text was corrected to point to this audit
and distinguish completed scoped correctness/memory checks from incomplete
performance and excluded zhttp coverage. Requirement dispositions and remaining
gates are explicit in audit92/README.md; executable identities/logs reproduced
by audit92/verify.py. Reader details in reads93/README.md and summary.json.

Completion is still unproven: original long external saga baselines fail,
large postgres select/other timing residuals remain unresolved, and zhttp is not
certified under the stop-building instruction. The patched-control baseline
preference remains pending; original failures and short comparisons are retained.
No scope/baseline redefinition. All diagnostic processes terminal, postgres stopped.


## Fixed-placement long-baseline outcome — affinity94

The first original6e9a1252 SQLite full saga workload failed with missing-primary-key
and fatal SagaStep diagnostics despite verified placement of all7named threads.
The verified child was terminated(exit-15); runner exit1, owned postgres stopped,
original/current runtime hashes unchanged. Partial output contains968saga samples,
not the required3000; it is invalid for performance comparison. The batch stopped
before any current/postgres timing process. Fixed placement does not establish a
valid original long baseline. Existing guard-only controls remain diagnostic;
long matched timing against them still awaits the requested baseline preference.

A first setup attempt defaulted to external70 and overwrote earlier setup/command
metadata before launching any workload. Misdirected records are preserved under
affinity94/attempt1. external70 raw failure CSV/stderr/database/config/interruption
and frozen benchmark artifacts remain intact; artifact/condition manifests were
recovered from retained same-generation copies. Original command durations and
host snapshots could not be recovered and are explicitly marked unavailable,
not reconstructed as original observations. See external70/latency-metadata-recovery.json.
The corrected runner fixes output to its own directory and refuses existing
command records before writing or starting postgres.

No production source/configuration changed. Completion remains unproven; this
failure and provenance loss do not supersede successful short/native store results
or satisfy the full saga gate. Evidence: affinity94/terminal-verification.json,
README.md and external70/latency-metadata-recovery.json.


## Actual compiled writer checkpoint — module95

The existing original/current libZdbPQ saveTuple<XFields> functions were measured
through their exact function-pointer signatures and recorded local ELF offsets,
using hashed already-loaded modules. No production export/source/configuration
change or rebuild. Only two diagnostic fixtures were compiled with the existing
generated flags; both commands0 without diagnostics. Six alternating pairs/width,
CPU8,7rounds×5000conversions,all24processes0/empty stderr,allvaluesverified outside
batches; loader controls0 confirm each version's own Z DSOs. Production artifact
hashes match before/after, including after loader controls.

Width4096 actual-module fresh HEAD/current11621.29/12124.14ns,reuse11185.10/11686.04ns;
median paired changes+4.41/+4.42%,all6pairs favor canonical. Width32fresh281.37/308.69ns,
reuse254.89/294.73ns,paired+10.28/+15.48%,all6pairs canonical. Large extra cost is
about0.50µs,small27–40ns. Thus separate saveTuple compilation in row91 is insufficient
to explain the prior whole-select20–40µs discrepancy. Allocation/finish are still
fixture-side; the common indirect call is diagnostic, not product code. Do not
subtract these medians from whole-store timings or claim new allocation/copy proof.
No production repair follows from this measurement; rejected vector75 stays reverted.

Evidence: module95/symbols.json,build.json,comparison.json,summary.json,
loader-verification.json,terminal-verification.json and rawCSV. Long saga baselines,
remaining postgres timing residuals and broader acceptance remain unresolved.


## Select request equivalence checkpoint — packets96

Reanalysis of retained syscall89 traces proves all252runtime select requests are
byte-for-byte identical across the four original/current processes:72bytes,
Bind/Describe/Execute/Flush, one binary8-byte limit(value1), binary results. The
128-byte trace capture includes these entire packets; truncated initial Parse
text is excluded(query text separately checked in server90). Input/packet hashes,
hex, decoded fields and reproducible parser are retained. No workload/build/server
restart or production edit. A first parser pass rejected resumed-call whitespace;
that syntax was corrected while keeping exact length/protocol checks.

Exact source comparison confirms unchanged StoreTbl::select dispatch and
Store::sendPrepared. This workload binds no group/key/vector data: group count0,
only limit1. Parameter row decoding/vector conversion cannot explain this select's
pre-response difference. TableView root construction and scheduler/runtime effects
remain distinct; identical request bytes do not prove identical timing. No new
copy/allocation or warranted source repair found. Performance acceptance remains
open. Evidence: packets96/analyze.py,summary.json,input-hashes.json,
source-comparison.json and README.md; original raw traces unchanged.


## Process order/round diagnostic — order97

Raw store71/store72/affinity87 postgres4096 measurements were reanalyzed against
recorded alternating process order, without new workloads. Three pairs/order/batch:
HEAD-first/current-first paired median select changes are+1.02/+3.07%(store71),
+5.59/+3.82%(store72),+0.11/+3.83%(affinity87). Dataset scopes remain separate.
Current is slower in all9current-first pairs; order direction/magnitude varies
across batches, so order alone is not an established explanation. Per-process
select round medians sometimes drop60–80µs,other processes staynear1.1ms.

CSV samples are11-operation sequence indices, not equal per-operation indices.
A first parser pass rejected that mismatch; the corrected parser verifies exact
operation offsets and matches iteration(sample//11)/round. Sequential select-minus-
recover median gaps vary substantially(HEAD-12.5to75.2µs,current18.0to79.4µs),but
these distinct queries do not isolate codec CPU. Variation exceeds module95's
~0.50µs writer overhead. No invented materiality/significance target or acceptance
claim. Raw input hashes,per-pair/per-round values and parser:order97/README.md,
summary.json,input-hashes.json,analyze.py. Live relation-stat observation(stats98)
is a separate diagnostic with unchanged settings,not proof of a maintenance cause.


## Live relation-maintenance diagnostic — stats98

Four existing original/current store-only postgres4096workloads,1000×3,two
alternating pairs,fixedplacement,allprocesses0/noerrors and11operations3000samples
each.146externalstatsobservations0/emptystderr;runtimehashes unchanged;DBcleanup/
ownedpostgresstop0. Actualsettings showautovacuumon/default60snaptime. No source/
build/configurationchange. Observers addload;these are not performance acceptance.

FirstHEAD/currentrunsobserve no automaticmaintenance;latercurrent/HEAD eachobserve
onevacuum/analyze. Atfirstobservedmaintenance,currenthad1613deletes,dead23,17pages;
HEAD803deletes,dead30,10pages. Delayedstats/plannerestimateschangefrom-1/0to1/actual
pages. LaterHEADselectroundmedians1093.34/1046.61/1015.58µsfallaftermaintenance;
latercurrent1091.47/1098.88/1092.72µsdonotshowthesamereductiondespitemaintenance.
Thus unequal maintenance histories are demonstrated,but vacuumalone is not an
establishedcause. Freshdatabases do not establishequal planner/relationstate
throughout a workload. summary.json,settings.csv,146rawJSONobservations and
terminal-verification.json retain evidence. No additional copy/allocation shown.

restart99separately testsmatchedserveruptime by restartingtheownedinstancebefore
eachprocess,with unchangedsettings/workload/artifacts and no timedobserver.
Post-runcountersmustbechecked;do notpoolthese timingswithshared-uptimeresults or
claimrestartisolatesvacuumalone. Originalfullsagabaseline/patchedcontrolpreference
remain separate and unresolved. No completionclaim or baseline substitution.


## Matched server-uptime result — restart99

The owned postgres instance was restarted before each existing original/current
store-only process, retaining settings, build artifacts, workload, schema/defaults,
pools and CPU placement. Six alternating pairs, width4096,1000×3: all12processes0,
noERROR/FATAL,11operations×3000samples each (18000/op/version). Every base-table
post-run record reports3003inserts,6006updates,3003deletes,live0,zero automatic
vacuum/analyze. All7named masks and serverCPU10 verified. AllDBdrop/stop0,final
status3;production/config hashes unchanged. No timed observer/build/source edit.

Median paired p50/p95/p99 changes: select+0.15/+0.42/+0.28%,find+0.81/+0.92/+0.78%,
recover+0.59/+0.84/+0.53%,insert+0.19/+0.37/+3.47%,update+0.11/+0.38/+3.05%,
update-one+0.14/+0.48/+1.51%,delete+0.27/+4.44/+2.44%. Prior shared-uptime
select+3.32/+2.33/+5.81% does not reproduce under this control. Select paired p50
ranges-1.53to+1.68%;p99-21.62to+15.92%,with similar large-read tail swings. This
is evidence of condition sensitivity, not a unique vacuum cause or an individual-
run tail guarantee. Restart also changes caches/background state; base counters
do not cover allTOASTactivity. Do not pool scopes or discard earlier measurements.

The same unmodified artifacts/workload can run near parity with matched server
uptime. No newly demonstrated redundant codec copy/allocation warrants a source
edit. Full performance completion remains unproven, particularly the long original
saga baseline and residual/tail variability. Patched-control preference remains
pending; this run uses unmodified HEAD and does not substitute a saga baseline.
Evidence:restart99/README.md,summary.json,input-hashes.json,analyze.py,
terminal-verification.json,rawCSV and maintenance/placement records. No process or
server remains running. Current correctness/memory artifacts were not rebuilt.


## Consolidated performance report and remaining baseline decision — report100

report100/README.md now presents all recorded memory codec/store/saga quantile
changes at both widths, allocation requests and payload sizes, large external
store results in their separate server-uptime scopes, short external saga results,
component measurements and failed-run/provenance limits. No inconvenient operation
or tail is filtered out. Its generator reads14existing JSON evidence files; input
hashes and evidence links verified. No new measured workload, build or production
change. A fresh audit92 identity/log review verifies850current files,9matching
clean memory logs,26accepted remaining-enabled targets and retained memory fixture
identities. Compiler currently reportsClang23.1.1,x86_64Linux; configuredrelease
flags and postgres16.3 unchanged. No application test/Valgrind suite was rerun.

The plan sets no numerical sample minimum. Failed chosen long original-baseline
runs are unusable evidence; passing short runs remain scoped evidence rather than
a guarantee of longer behavior. The canonical comparison target remains6e9a1252.
The previously requested decision about a labeled guard-only canonical long saga
control remains unanswered across module95,packets96,restart99 and this checkpoint.
Its concrete one-line patch and clean functional replays are available incontrol85/86.
Using it for long acceptance changes the pinned comparison and must not be silently
represented as originalHEAD. No further baseline rerun or speculative source edit
is justified by the same failures; no independent demonstrated repair remains.
Tail uncertainty and excludedzhttpcoverage remain accurately reported. Completion
is not claimed; this checkpoint prepares a reviewable result for the outstanding
baseline decision, without changingrepair2.md's requirements or substituting scope.



## Corrected baseline authorized — corrected101

The user approved using the corrected canonical baseline for long saga acceptance:
`6e9a1252` plus only control85's `queueCount` ordering guard. This supersedes the
pending baseline decision in the historical checkpoints below. Canonical
FlatBuffers, existing release configuration and original artifacts are retained;
no coordinator lifetime changes are backported. `ordered` labels this corrected
baseline; `candidate` labels the native ZiFB worktree. Original failed workloads
remain separate evidence. The matched full-workload measurements and exact
commands are recorded in `zdb/interop/evidence/repair2-20261010/corrected101/`.
No production change or rebuild is needed. Results and performance disposition
will be recorded after the batch completes.



## Corrected canonical long saga comparison — corrected101 completed

The approved guard-only canonical baseline completed the matched full workload:
six alternating pairs/backend,width32,1000×3iterations,18000samples/op/version.
All24latency and8separate counter processes exit0 without ERROR/FATAL; library
selection, seven worker masks, server placement, artifact/config hashes and cleanup
are verified. No production code change or rebuild. Original failures stay separate.

Native saga p50/p95/p99 changes are−9.67/−16.85/−4.44% for sqlite3 and
−29.91/−30.79/−30.23% for postgres. Every saga quantile improves in all six pairs.
Tracked requests fall50.450→43.380 and55.965→47.997 respectively (600samples/version;
partial Z heap telemetry and asynchronous-window limits). The corrected baseline
resolves the long-saga gate. Scope is logical completion, not durable commit.

SQLite ordinary update p99 remains+47.31%,with p50/p95+0.36/+0.99%; per-process
p99 ranges43.04–125.80µs canonical and48.77–127.82µs native. Five of six paired
p99 ratios favor canonical. No cause is established, no blanket tail pass is
claimed, and no speculative codec change follows. Existing large-width and
memory residuals remain recorded separately. The zhttp stop-building scope persists.
Evidence: corrected101/README.md,summary.json,input-hashes.json,analyze.py and raw
CSV/commands/placement records; report100 incorporates all operations. The pending
baseline preference in historical checkpoints is superseded by this approved run.



## SQLite update-tail mechanism — sqlite102

Four alternating syscall-traced original/current store phases exit0 clean, with
unchanged release artifacts. All12012runtime write transactions/process have
identical page/commit patterns; both versions have exactly15update checkpoints
and15WAL restarts at identical iteration positions. No additional copy,
allocation, store post or database write was demonstrated by source/counter/trace
review. No production edit or rebuild.

Those30positions match the30slowest updates in every one of12corrected101 timing
runs. With3000samples and the retained percentile definition,p99 is the31stlargest
sample: the single slowest ordinary update. The+47.31% paired p99 remains reported;
neighboring unfiltered p99.1/p99.9 change+0.18/+0.13%,maximum+0.31%. This identifies
a percentile boundary sensitive to one isolated delay,not the cause of every
delay. Conditional distributions are diagnostic and do not replace acceptance.
See sqlite102/README.md,summary.json,tail-summary.json,raw traces and original
input hashes. Original full saga gains and other residuals remain separately
recorded. No speculative optimization or blanket per-run tail guarantee follows.



## Current requirement audit and residual analysis — audit104/residual103

Read-only identity/log review reverified850current files,9clean matching memory
logs,26accepted dependent targets,5pinned original sources and23retained memory
benchmark artifacts. No rebuild/test/Valgrind rerun. The corrected long saga gate
is fulfilled; original baseline failures remain historical. SQLite update p99's
boundary explanation is retained without filtering acceptance samples.

Exact paired median-ratio intervals cross parity for small-memory update and
large-postgres write residuals; this is descriptive uncertainty,not equivalence.
Grouped sums of all11timed store-phase operations change+0.04/+0.61/+0.10% for
sqlite3 and+0.29/+0.74/+0.64% for postgres. Individual metrics remain authoritative.
SQLite width32insert p50 is consistently+2.13% (all6pairs+1.73to+2.76%),p95/p99
+1.81/+0.06%. This is the remaining concrete attribution target; saga gains and
aggregate parity do not silently clear it. Completion is not yet claimed.
Evidence: audit104/README.md,verification.json,verified-files.json,memory-logs.json;
residual103/README.md,summary.json,input-hashes.json,analyze.py. Current configuration,
size policy, type coverage and user zhttp exclusion remain unchanged.



## SQLite compiled binding cost — bind105

An isolated fixture calls each existing compiled bindInput<XFieldRefs> by its
exact typed signature/ELF offset and verified module base. Same nine-field row,
widths32/4096,6alternating pairs,width×10000bindings×7rounds,CPU8. All24processes0
with empty stderr; conversion-scratch hashes match across versions. Actual loader
selection and original/current/control artifacts verified. Two fixture generations
are retained after correcting a printf warning; no production rebuild/change.

Canonical/native batch means are145.58/150.97ns at width32 (paired+3.49%,about5ns)
and794.86/786.84ns at width4096 (−0.64%). This is binding only,through the compiled
reference-list variant; insert's inlined XFields variant, planning, callbacks,
SQLite stepping/commit and dispatch are excluded. It does not account for the
roughly540ns full-store insert discrepancy by itself. No speculative source fix
or completion claim. Evidence: bind105/README.md,summary.json,commands.json,
build.json,symbols.json,loader-verification.json,input-hashes.json and raw outputs.



## SQLite planning and statement execution — plan106/step107

Matched source-extracted parameter planning adds about6.5ns:17.52→24.04ns at
width32,17.53→24.11ns at4096. Native required size-policy checks remain in place.
All24component processes0/empty stderr; identical encoded scratch,production and
control artifacts unchanged. Source extraction is labeled,not misrepresented as
a callable original-module planner. No production change/rebuild.

External SQLite-step timing adds one diagnostic shim,with6alternating full store
pairs,width32,1000×3. All12processes0/no ERROR/FATAL,identical statement counts.
INSERT/BEGIN/COMMIT paired mean changes+0.32/−0.10/−0.12%; UPDATE−1.61%.
Instrumented full insert p50 still+1.67%. Timings are diagnostic only,with setup
and multiple write operations grouped by statement class. This narrows the
remaining540ns uninstrumented insert cost toward envelope/wrapper/callback/
dispatch work; it does not establish causality or clear the original+2.13%.
Evidence: plan106 and step107 README.md,summary.json,input-hashes.json,build and
command records/raw outputs. No speculative production edit or completion claim.



## SQLite success-path error-object repair — status108

Replaced the transaction wrapper's Event return and insert helper's Event flag
with boolean status; construct paramSizeError only on failure. Existing cap checks,
duplicate-row distinction, rollback-before-callback, exceptions and completion
paths remain intact. This removes unnecessary success-path object construction.

Isolated six-pair full-workload A/B (all12processes0,all16ops3000samples/process)
records insert p50/p95/p99−0.50/−0.63/−0.13%,update−1.06/+0.07/−8.18%,saga
−0.26/−2.32/−3.75%. Pairs are mixed; not a full540ns attribution or fresh canonical
acceptance. Applied the narrow source delta after this scoped comparison.
Incremental SQLite source build,unit6checks and integration56checks pass. Refreshed
SQLite Memcheck0errors,zero definite/indirect/possible lost bytes. An initial
memory harness invocation omitted connection arguments; preserved as setup failure,
not counted as acceptance. Only source/module hashes change among850audit identities.
Other eight memory programs and unchanged dependents retain their prior evidence.

Evidence: status108/README.md,verification.json,current-verified-files.json,
accepted-artifact-changes.json,accept-commands.json,final-memory logs,exact source
snapshots and isolated A/B raw results. Current production performance against
the corrected canonical baseline still needs refresh; older measurements refer
to the explicitly retained pre-status module generation. No broad completion claim.



## Current SQLite corrected-baseline refresh — sqlite109

The production library accepted in status108 has now completed the corrected
canonical comparison at widths 32 and 4096: 24 latency and 8 separate counter
processes, all exit 0 without application errors, all 16 operations retained.
Six alternating pairs/width yield 18000 latency samples/op/version/width; loader
selection, seven worker CPU masks, sample counts and unchanged artifacts verified.
Read-only checks reverify all 850 current file identities and nine accepted memory
logs (including status108's refreshed SQLite gate). No rebuild or suite rerun.

SQLite saga p50/p95/p99 changes −9.75/−20.82/−6.91% at width32 and
−9.37/−21.02/−5.61% at4096, all six pairs improve each quantile. Tracked requests
fall 50.557→43.428 and 50.540→43.228. Unchanged postgres retains corrected101's
−29.91/−30.79/−30.23% saga result. Approved baseline remains canonical 6e9a1252
plus only the scheduler ordering guard; original failures remain separate.

Small SQLite insert retains +1.24% p50 (paired +316 ns; all six pairs +0.67 to
+3.36%), p95/p99 −0.43/+0.26%. Width4096 insert −0.25/+0.05/−0.08%.
Small update-one +0.98/+0.09/+0.48%, mixed p50 pairs. Saga acceptance is fulfilled;
full repair completion remains unclaimed pending sustained ordinary-store residual
disposition. Separate-generation differences do not isolate the status108 change.
Evidence: sqlite109/README.md,summary.json,input-hashes.json,
current-evidence-verification.json,run.py,analyze.py and all raw records. Counter
telemetry is partial/asynchronous; saga completion is logical, not durable commit.
Postgres stopped; current configuration retained; user-excluded zhttp uncertified.



## SQLite transaction boundary isolation — txn110/txn111

Two external diagnostic controls use the accepted corrected canonical/current
production generations,each6alternating width32store-only pairs,1000×3. All24
processes exit0/no ERROR/FATAL; all11ops3000samples/process; artifact hashes match.
No production source/configuration change or rebuild, no correctness/memory rerun.

Aggregate insert body adds25.25ns; quantile control adds40.5ns at paired p50.
The quantile probe maps2setup+3003four-write cycles exactly to CSV operations,
excluding each round's warmup, and computes outside-transaction duration per
same-operation clocks before quantiling. Instrumented insert fullp50+2.30%;
outside transaction+556ns (+8.42%,all6pairspositive), transaction+155.5ns(+0.79%).
Quantile deltas are not additive. This narrows the investigation to dispatch,
envelope/callback/teardown/wakeup time; it does not isolate a particular cause.

Source comparison retains moved buffer/CommitFn,one store post,borrowed RecordView,
unchanged store dispatcher/ZmBlock/ZmFn/ZmRef/ZmSemaphore and IOBuf pool shape.
No demonstrated extra copy or heap post found there; no speculative edit applied.
Probe overhead includes clocks/classification and workload-sized external mmap
writes; diagnostic timings do not replace sqlite109's release+1.24%(316ns)insert
residual. Full repair remains open. Evidence: txn110/README.md,txn111/README.md,
summary.json,input-hashes.json,raw outputs/source/build and command records.



## SQLite scheduler/completion handoff isolation — handoff113

Six alternating corrected canonical/current external store-only pairs,width32,
1000×3:all12processes0/no ERROR/FATAL,all11ops3000samples/process. Each process
has21028main-thread enqueues,zero failed pushes; all12012runtime write enqueue
sequences match. Every write completion is paired with its actual same-semaphore
main wait. Separate loader smokes verify the shim forwards to the approved
scheduler control/current library. Original/current/control artifacts unchanged.

Instrumented insert p50 dispatch−30ns(−0.98%),post-to-wake−1ns(−0.03%),
commit-to-post+14.5ns,full+104.5ns(+0.41%),with mixed dispatch/wakeup/fullpairs.
Probe overhead changes the observed gap; it neither explains nor clears the
release+1.24%(316ns)insert residual. No extra enqueue/overflow/relay allocation
demonstrated and no speculative scheduler edit. handoff112's earlier diagnostic
generation is preserved separately;113adds atomic owner publication and explicit
semaphore identities. No production build/source/configuration or memory rerun.
Evidence: handoff113/README.md,summary.json,loader-verification.json,
input-hashes.json,raw source/build/command outputs. Full repair remains open.



## SQLite same-binary producer-context control — gap114

Two copied benchmark executables are built with retained release configuration,
linking unchanged canonical/native production libraries. Same binary/implementation
runs gap0 and minimum20microsecond producer interval before timed insert. Full
store workload retained; no production delay/fix. All24latency processes0/clean,
all11ops3000samples/process,4loader smokes/7worker masks/byte sets/hashes verified.

Gap0 insertp50+1.96%(496ns),gap20000+0.53%(135.5ns); paired change-in-differences
−331ns,5of6pairs shrink. Demonstrates sensitivity to producer timing/context,
not exact causal decomposition or proof that the original316ns is noise. No
padded workload replaces release acceptance. Update-one's gap20p50 grows+1.80%,
so all metrics remain reported. No speculative production edit follows.

Original sqlite109 data reanalysis: same-iteration codec+store insert
+0.54/−0.73/+0.14% atwidth32;−0.71/+0.09/−0.10% at4096. Sum ofall11timed
store-phaseops−0.45/−1.30/−0.03% and−4.10/−0.52/−0.45%. Sums exclude gaps and
are not uninterrupted end-to-end wall times. Constituent releaseinsert+1.24%
(316ns)remains authoritative.850current identities unchanged;postgres stopped.
Evidence: gap114/README.md,summary.json,release-grouped.json,input-hashes.json,
exactsource/build/process records. Performance residual disposition and final
requirement audit remain to be completed; no broad completion claim yet.



## Repair2 completion — audit115

The consolidated repair is complete within the authorized current-build scope.
The final requirement audit covers all5phases,RQ1–RQ4,retained data-store decisions,
copy/ownership/type/alignment/sharding/diagnostic/size-policy contracts,canonical
interop,owning/store/remaining-enabled suites and final memory gates. Read-only
verification pins850current source/build/artifact identities,23retained memory
performance artifacts,9matching clean memory logs and26accepted remaining targets,
including explicit public Binance execution. Only the2status108SQLite identities
changed sinceaudit104; their incremental source/unit/integration/memory refresh
passes. No new production build/test/Valgrind/configuration change for this audit.

RQ4 is accepted as an engineering assessment within measured workload scope,
with ordinary-operation residuals explicitly retained. SQLite small insertp50
remains+1.24%(316ns),p95/p99−0.43/+0.26%; it is not erased or called noise.
CurrentSQLite saga−9.75/−20.82/−6.91% atwidth32,−9.37/−21.02/−5.61% at4096;
PostgreSQL saga−29.91/−30.79/−30.23%. Memory saga also improves with about15–16
fewer tracked requests. Same-iteration original release codec+store insert sums
+0.54/−0.73/+0.14% and all11store-phase sums−0.45/−1.30/−0.03% atwidth32;
sums exclude gaps and do not replace constituent metrics. All original residual,
variability,maintenance and failed-baseline evidence is retained. No requirement
for every quantile to improve or exact causal attribution is invented.

The original corrected canonical baseline is6e9a1252 plus only the scheduler
queueCount ordering guard. Exact control source/DSO verified; unmodified original
failures stay historical. Diagnostic padding/interposition does not replace
release acceptance and is not applied to production. Oversized transmit splitting,
generic retained-payload copies,required owning boundaries and whole-vector updates
retain their documented costs. Logical completion is not durable commit; partial
heap telemetry and sampled workload limits remain explicit.

Evidence and all requirement dispositions: audit115/README.md,verification.json,
current-files.json,memory-logs.json,accepted-command-records.json,input-hashes.json.
This checkpoint supersedes older incomplete/pending status text for its current
source generation. zhttp remains excluded and uncertified by user instruction;
other compiler/flag/platform configurations are outside scope. PostgreSQL fixture
stopped. Work remains uncommitted; no install,clean,reset or deployment.

