# HPACK/QPACK dynamic transmit lookup plan

## Objective

Complete the missing transmit-side dynamic lookup combinations:

| Codec | Exact name/value | Name only |
|---|---:|---:|
| HPACK Tx | add | add |
| QPACK Tx | retained from `zhttp1.md` | add |

The resulting lookup order is:

```text
compile-time static exact match
  -> eligible dynamic exact match
  -> compile-time static name match
  -> eligible dynamic name match
  -> literal name
```

Static HPACK/QPACK tables continue to use `ZuMatcher`.  Dynamic tables use
bounded, pre-sized, connection-local hashes established by `zhttp1.md`; no
active header path may allocate, grow an array, resize a hash, gather a
segmented value, or take a lock.

`zhttp1.md` is an entry prerequisite.  This plan must not restore fixed-size
hashes, peer-authorized allocation, lazy hash growth, or ambiguous QPACK
capacity semantics while adding the new indices.

## Current implementation

### HPACK

`HPackEncoder::field()` currently:

1. uses `ZuMatcher` through `HPack::staticIndex()`;
2. otherwise uses only `HPack::staticNameIndex()`; and
3. emits a non-indexed or never-indexed literal.

It never searches the encoder's dynamic table, never emits an incremental
indexing literal, and never inserts a transmitted field into that table.
`HPackTable` is populated and exercised by the decoder, but the encoder-owned
instance has no string-to-index hash.

There is an additional integration defect: `H2Config::headerTableSize()` is
the local decoder limit advertised to the peer, but it is also used directly
to initialize the local encoder.  The encoder must instead be bounded by
trusted local Tx policy and the peer's `SETTINGS_HEADER_TABLE_SIZE`.

`H2::HeaderBlock::field()` invokes the encoder twice: once into `CountBytes`
and once into the actual framed output.  Dynamic-table mutation inside either
call would double-insert or make the two passes encode different bytes.

### QPACK

After `zhttp1.md`, `QPackTxTable` has a bounded exact name/value hash.  The H3
builder uses it for an eligible dynamic indexed field.  It does not have a
name-only index, so:

- a literal field whose name exists only in the dynamic table repeats the
  literal name; and
- an encoder-stream insertion whose name exists only in the dynamic table
  uses an Insert Without Name Reference instruction.

Decoding of relative and post-base dynamic name references already exists and
must remain unchanged.

## Design constraints

### Static and dynamic responsibilities

- `ZuMatcher` is used only for compile-time static names and values.
- Dynamic hashes index owned table entries; hash keys are borrowed views into
  that owned storage.
- Ordered table storage remains authoritative for RFC index calculation,
  eviction order, capacity accounting, and decoding.
- Hashes are accelerators.  Every result is range- and identity-checked
  against the authoritative ordered entry before use.
- Relocation or compaction of ordered entries rebuilds all borrowed-key hashes
  without resizing them.

### Newest-entry policy

Both protocols can contain repeated names and repeated name/value pairs.
Lookup selects the newest eligible entry because it has the smallest dynamic
relative index.

Eviction is oldest-first.  A hash entry therefore must not be deleted merely
because an older duplicate was evicted while a newer duplicate remains.
Deletion must verify that the hash currently points to the evicted absolute
entry.  Compaction rebuilds indices from the surviving newest entries.

Where protocol eligibility can exclude the newest matching QPACK entry, the
name index must still find the newest eligible older match.  Use an
absolute-index same-name chain in ordered entries:

- the name hash maps a name to its newest absolute entry;
- each entry stores the previous absolute entry with the same name;
- lookup walks newest-to-oldest without allocation;
- absolute lookup validates every hop against the live window; and
- reaching an evicted absolute index terminates the chain safely.

Do not scan the entire dynamic table.  The cost is proportional only to
duplicates of one name.

### No gathered values

The existing field API includes `(value1, separator, value2)` to avoid
constructing a contiguous temporary.  Exact dynamic lookup must preserve that
property.

Define a small borrowed field-view type representing:

- one name span;
- either one value span or two value spans plus one separator; and
- the exact combined value length.

Its equality and hash functions must produce the same result for logically
identical contiguous and segmented values.  Hash directly across the segments;
do not build a temporary `ZtString`, fixed array, or heap buffer.  Keep this
helper local to `zhttp` unless another concrete user justifies a foundation
API.

### Plan, emit, commit

Both encoders use an explicit immutable representation plan:

```text
inspect static/dynamic tables and policy
  -> choose representation and compute encoded length
  -> emit exactly that representation
  -> after output is accepted, commit any dynamic-table insertion once
```

A plan contains only borrowed input views plus fixed-size indices/flags.  It
does not own or gather header strings.

Counting and emission consume the same plan.  Neither mutates the dynamic
table.  Commit occurs exactly once and only for a successfully emitted
incremental insertion.  Failure before commit leaves the table unchanged.

For a complete HPACK header block, commits must follow the exact order in which
the corresponding encoded fields become ordered on the connection.  H2 must
not allow scheduler reordering of header blocks to diverge from encoder-table
mutation order.

## Target HPACK behavior

### Capacity ownership

Normalize H2 configuration:

- retain a clearly named local Rx decoder capacity advertised in
  `SETTINGS_HEADER_TABLE_SIZE`;
- add a distinct trusted local Tx encoder ceiling;
- store the peer's current header-table-size setting on Tx; and
- use `effectiveTxCapacity = min(localTxCapacity, peerSetting)`.

The peer setting is a scalar permission and never authorizes allocation.
Pre-size HPACK Tx order, exact hash, and name hash once from the trusted local
Tx ceiling.  Apply a peer reduction by evicting as necessary and queueing the
RFC-required dynamic table size update at the beginning of the next header
block.  A later increase may raise effective capacity only to the preallocated
local ceiling and likewise emits the required update without allocating.

### Representation selection

For each non-sensitive field:

1. If the exact pair is in the static table, emit a static indexed field.
2. Otherwise, if the exact pair is in the dynamic table, emit the combined
   HPACK dynamic index (`62 + zero-based newest-relative-index`).
3. Otherwise select the static name index when present.
4. Otherwise select the newest dynamic name index when present.
5. Otherwise encode the literal name.
6. Emit the literal with incremental indexing and, after successful emission,
   insert the owned name/value into the Tx dynamic table.

For a never-index field:

- never use a dynamic exact indexed representation, since that would violate
  the local never-index policy;
- use static name, then dynamic name, then literal name;
- emit the never-indexed literal representation; and
- do not insert it.

If indexing is disabled by zero effective capacity or explicit policy, emit a
non-indexed literal and do not insert.

## Target QPACK name lookup

Add a second bounded Tx hash keyed by name.  It is pre-sized from the same
trusted local maximum-entry bound as the exact hash.

Use it in two distinct contexts:

1. **Field-section literal name reference.**  Select the newest same-name
   entry that is before the section base and known received under the current
   non-blocking policy.  Encode a relative dynamic name reference and add its
   absolute entry to section reference tracking.
2. **Encoder-stream insertion name reference.**  Select the newest live
   same-name entry at the current insert count.  Encoder-stream ordering makes
   a prior insertion available here; encode Insert With Name Reference with
   the dynamic flag and correct relative index.

Static name references remain preferred when present because their indices are
smaller and they require no dynamic reference tracking.

If no eligible dynamic name exists, retain the current literal-name encoding.
Never-index policy still prevents insertion, but it may use an eligible name
reference because only the value's indexing policy is sensitive.

## Delivery slices

### Slice 1 — Add failing lookup and representation tests

Extend `ZhttpHPackTest` and `ZhttpQPackDynamicTest` with exact wire assertions.

HPACK tests must cover:

- first occurrence encoded with incremental indexing;
- second identical occurrence encoded as a dynamic indexed field;
- same name with a new value encoded using a dynamic name reference;
- newest duplicate selection;
- eviction exposing or removing the correct duplicate mapping;
- static exact and static name precedence;
- never-index fields neither exact-reference nor insert;
- zero-capacity non-indexed fallback;
- peer capacity reduction/update ordering;
- contiguous and segmented values finding the same exact entry; and
- count and emission passes producing identical lengths without mutation.

QPACK tests must cover:

- literal field line with a relative dynamic name reference;
- Insert With Name Reference using a dynamic name;
- newest eligible same-name selection;
- newest unacknowledged entry falling back through the same-name chain to an
  older eligible entry;
- static name precedence;
- eviction and compaction preserving name lookup;
- denied section tracking suppressing the dynamic name reference; and
- contiguous and segmented values retaining existing no-gather behavior.

#### Acceptance criteria

- Every missing combination has a focused test which fails against the entry
  baseline for the expected missing behavior.
- Tests decode emitted bytes independently rather than merely inspecting
  encoder internals.
- Table counts prove that planning/counting do not mutate and commit inserts
  exactly once.
- Existing RFC decode vectors remain unchanged.

#### `GUIDELINES.md` alignment audit and repair

Audit tests for hard tabs, Z containers, bounded scratch storage, no hidden
gathers, and deterministic assertions.  Repair all findings before Slice 2.

### Slice 2 — Introduce allocation-free field views and plans

Add protocol-local borrowed field views and immutable representation-plan
types.

The common mechanics must:

- compare contiguous and segmented values without copying;
- hash segmented values identically to contiguous owned entries;
- carry static/dynamic index selection in fixed-size fields;
- compute encoded size without writing;
- emit from the same plan without a second lookup;
- make invalid or stale dynamic indices fail before output; and
- contain no owning string or variable-size container.

Keep HPACK and QPACK representation enums separate where their wire formats
differ.  Factor only the field-view/hash mechanics that are actually
identical.

#### Acceptance criteria

- Contiguous and segmented equality/hash tests cover empty and boundary
  segments.
- Plan creation performs no allocation and does not mutate either table.
- Count and emit consume one plan and produce exactly equal lengths.
- Plans cannot outlive their callback-scoped source spans by API design.

#### `GUIDELINES.md` alignment audit and repair

Audit borrowed lifetime, side-effect-free planning, naming length, template
use, and absence of STL or generic abstraction overhead.  Repair all findings
before Slice 3.

### Slice 3 — Build bounded HPACK Tx exact and name indices

Separate HPACK decoder storage from encoder storage where necessary; do not
burden the Rx decoder with Tx-only hashes.

The HPACK Tx table must contain:

- oldest-to-newest owned entries;
- a pre-sized local/no-lock exact name/value hash;
- a pre-sized local/no-lock name hash;
- absolute insertion sequence numbers or equivalent stable identities;
- newest-same-name linkage; and
- byte capacity and live-window accounting.

Initialize all Tx storage once from the trusted local Tx ceiling.  Zero
capacity allocates nothing.  Insert, evict, compact, exact lookup, name lookup,
and rebuild never resize or grow.

Update H2 SETTINGS handling so `SETTINGS_HEADER_TABLE_SIZE` is snapshotted on
Rx and posted to Tx with frame-size and Extended CONNECT settings.  Do not
read Rx-owned peer settings from the encoder.

#### Acceptance criteria

- Exact and name hashes report zero resizes through churn beyond their former
  implicit/default sizes.
- Exact lookup returns the newest live duplicate.
- Name lookup returns the newest live same-name entry.
- Oldest-first eviction updates mappings without hiding newer duplicates.
- Compaction preserves all mappings and borrowed key validity.
- Peer settings cause no allocation above the local ceiling.
- HPACK decoder RFC vectors remain green.

#### `GUIDELINES.md` alignment audit and repair

Audit Tx ownership, local/no-lock hashes, pre-sizing, failure atomicity,
borrowed hash keys, and SETTINGS cross-shard handoff.  Repair all findings
before Slice 4.

### Slice 4 — Convert HPACK encoding to plan/emit/commit

Replace direct `HPackEncoder::field(Bytes &, ...)` lookup/mutation with:

- `plan(fieldView, policy)`;
- `size(plan)`;
- `emit(bytes, plan)`; and
- `commit(plan)`.

Refactor `H2::HeaderBlock` so each field is looked up once, counted and emitted
from one plan, then committed exactly once after framed bytes are accepted.
Remove the encoder's misleading `const` path and assert Tx ownership in its
mutating entry points.

Handle pending table-size changes at the beginning of the next header block,
before any field representation.  Coalesce multiple peer setting changes as
required by RFC 7541 while preserving the minimum reduction that must be
signalled.

Verify that H2 queues complete header blocks in the same connection order in
which plans are committed.  If current scheduling can reorder blocks, add one
Tx-owned header-block serialization queue; do not add locks or cross-shard
table access.

#### Acceptance criteria

- First literal insertion and subsequent exact/name references match expected
  HPACK bytes.
- Counting performs no insertion; successful emission inserts once.
- Failed allocation/framing before acceptance leaves the Tx table unchanged.
- Table-size updates precede the first field of the affected header block.
- Concurrent logical streams cannot make encoder state diverge from wire
  header-block order.
- Header encoding adds no per-field heap allocation or payload/name copy.

#### `GUIDELINES.md` alignment audit and repair

Audit the plan/emit/commit boundary, Tx serialization, callback lifetimes,
failure ordering, and active-path allocation.  Repair all findings before
Slice 5.

### Slice 5 — Add the bounded QPACK name index

Extend the `zhttp1.md` QPACK Tx storage with:

- a pre-sized local/no-lock name hash;
- one previous-same-name absolute link per ordered entry; and
- lookup variants for field-section eligibility and encoder-stream
  eligibility.

Update insertion, failed-insert rollback, eviction, capacity reduction,
compaction, and hash rebuild as one atomic storage change.  Then update H3
field planning and encoder insertion planning to select the new name
representations.

Field-section planning must include a dynamic name reference in its required
insert count and reference list.  If the section cannot be tracked under
`zhttp1.md`, rebuild the complete plan without any dynamic exact or name
reference before emitting its prefix.

#### Acceptance criteria

- Name hash capacity is fixed at connection initialization and reports zero
  resizes.
- Dynamic field-line and encoder-stream name references have correct relative
  indices.
- An ineligible newest name match finds an eligible older duplicate without
  scanning unrelated entries.
- Eviction/compaction never leaves a stale same-name link or borrowed key.
- Section reference counts include name references and return to zero after
  ack/cancellation.
- Literal fallback remains correct when no eligible name or tracking slot
  exists.

#### `GUIDELINES.md` alignment audit and repair

Audit bounded duplicate-chain traversal, exact/name hash consistency,
reference tracking, failure atomicity, and absence of post-emission
compensation.  Repair all findings before Slice 6.

### Slice 6 — Policy, limits, and malformed-state closure

Exercise interactions that are easy to miss:

- sensitive names which also have exact dynamic entries;
- capacity zero and capacity smaller than one entry;
- capacity reductions evicting entries referenced by upcoming plans;
- duplicate pairs and duplicate names across wrap/compaction;
- maximum HPACK combined index and QPACK relative-index integer boundaries;
- empty values and split values with an empty side;
- dynamic entries whose names also exist statically;
- failed hash insertion or output allocation leaving all indices unchanged;
- QPACK known-received count lag; and
- connection reset with outstanding HPACK/QPACK state.

Do not add a linear full-table fallback.  If a hash or same-name chain
invariant fails, reject the local plan before output and surface a focused
internal failure.

#### Acceptance criteria

- Never-index policy cannot be bypassed by exact dynamic lookup.
- All integer/index boundary encodings round-trip through independent
  decoders.
- No malformed or stale lookup result is emitted.
- Capacity and failure paths are deterministic and allocation-bounded.
- Existing HPACK/QPACK error handling and connection isolation remain intact.

#### `GUIDELINES.md` alignment audit and repair

Audit overflow checks, switch-based representation dispatch, flat control
flow, assertions, and absence of compatibility code or silent recovery.
Repair all findings before Slice 7.

### Slice 7 — Minimal build and verification closure

Keep the current configured compiler and build mode; do not reconfigure.
Minimize rebuilds:

1. Complete grouped source edits first.
2. Rebuild `zhttp/src` once with `make -C zhttp/src -j3`.
3. Rebuild only affected tests initially.
4. Run focused tests:
   - `ZhttpHPackTest`;
   - `ZhttpH2FrameTest`;
   - `ZhttpH2SessionTest`;
   - `ZhttpH2EngineTest`;
   - `ZhttpQPackTest`;
   - `ZhttpQPackDynamicTest`;
   - `ZhttpH3EngineTest`; and
   - `Zhttp3InteropTest`.
5. Run `make -C zhttp/test test` once after focused tests pass.

Add allocation/capacity evidence showing:

- no HPACK/QPACK field-plan allocation;
- no contiguous gather for segmented values;
- no hash resize in exact, name, or section indices;
- no ordered-array growth after connection readiness; and
- no dynamic-state allocation at zero configured capacity.

Inspect any regression from the code diff first, then use gdb, then Valgrind.
Do not create a separate ASAN build unless the normal completed regression
suite exposes otherwise undiagnosable memory corruption.

#### Acceptance criteria

- All focused and full regressions pass in the current build.
- HPACK interop demonstrates first-use insertion followed by exact and
  name-only dynamic references.
- QPACK interop demonstrates dynamic name references without introducing
  blocked-stream regressions.
- H2/H3 shared-connection and stream-isolation tests remain green.
- Allocation/capacity evidence proves zero active-path hash resizing,
  container growth, gathering, and per-field heap allocation.
- `git diff --check -- zhttp` is clean.

#### Final `GUIDELINES.md` alignment audit and repair

Before handoff:

- re-read the performance, container, sharding, continuation, lifetime, and
  teardown guidance;
- verify `ZuMatcher` remains compile-time-only;
- verify all dynamic hashes are locally bounded, pre-sized, no-lock, and
  connection-affine;
- verify HPACK/QPACK encoder state is Tx-owned and ordered with wire output;
- verify count/emit are side-effect-free and commit occurs once;
- verify segmented values are never gathered;
- verify no active path allocates, resizes, blocks, locks, or copies header
  strings unnecessarily;
- verify teardown prevents use, drains Rx, drains Tx, then releases all table
  storage; and
- repair every discrepancy before declaring completion.

## Completion definition

The plan is complete only when:

1. HPACK Tx performs bounded exact dynamic lookup.
2. HPACK Tx performs bounded name-only dynamic lookup.
3. QPACK Tx performs bounded name-only dynamic lookup for both field lines and
   encoder insertions.
4. Static matches still use `ZuMatcher` and retain precedence.
5. Dynamic duplicate selection is newest and protocol-eligible.
6. HPACK planning/counting/emission cannot double-mutate its table.
7. QPACK dynamic name references participate correctly in section tracking.
8. Contiguous and segmented values share exact lookup without gathering.
9. No dynamic hash or ordered table grows after connection readiness.
10. Full regression, lifecycle, allocation, and interoperability evidence is
    green.
