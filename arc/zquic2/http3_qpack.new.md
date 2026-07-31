## Summary

Add production dynamic QPACK support to `Zhttp::H3` while preserving the
current static/literal hot path and the zero-capacity mode. The implementation
must support one receive dynamic table used while decoding peer field sections
and one transmit dynamic table used while encoding local field sections. The
receive side should be index/ring oriented; the transmit side needs insertion
order plus reverse lookup by field bytes.

Current code is not at the starting point described by the previous plan:
`zhttp/src/ZhttpQPack.hh` and `.cc` already contain partial dynamic QPACK
helpers and tests. Those helpers are useful as a staging area, but they are not
yet production-ready:

- `Zhttp::H3::Parser::parseFields_()` in `Zhttp.hh` still rejects any non-zero
  Required Insert Count or Base and only decodes static/literal field lines.
- `CxnParser` still treats peer QPACK encoder stream data as an error and
  drains peer decoder stream data.
- `Builder` still emits `Required Insert Count = 0` and `Delta Base = 0`,
  counts then writes the same field section, and has no dynamic insertion plan.
- `ZhttpQPack.cc` encodes QPACK encoder/decoder stream instructions using
  QUIC varints and synthetic instruction IDs. RFC 9204 uses prefix-coded QPACK
  instruction bytes. This must be fixed before integration.
- `QPack::fieldSectionBase()` currently omits the RFC negative-base `-1` term.
  For sign bit 1, `Base = ReqInsertCount - DeltaBase - 1`.
- The README says dynamic QPACK is integrated, but the code path does not yet
  match that statement.

The design below keeps the earlier constraints that matter:

- Use `ZtArray`-based ring-like contiguous storage for receive decoding.
- Use a non-local `ZmLHash` with `ZmNoLock` for transmit exact lookup so
  `ZmHashMgr` telemetry and overrides remain available.
- Default the transmit hash to `ZmHashParams{128}`.
- Assign explicit heap IDs for every QPACK string/array allocation site.
- Avoid node polymorphism and avoid `ZuObject`/`ZuRef` unless an ownership
  issue appears during implementation.
- Preserve direct stream writes in `Builder` where practical.
- Keep capacity zero fully supported and covered by tests.

Protocol references are RFC 9204:

- Absolute dynamic-table indices start at 0.
- Relative index 0 in encoder instructions means the most recent entry.
- Relative index 0 in field sections means absolute index `Base - 1`.
- Post-base index 0 means absolute index `Base`.
- Required Insert Count is encoded modulo `2 * floor(MaxTableCapacity / 32)`,
  not as a raw insert count, when dynamic capacity is non-zero.
- QPACK settings are `SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0x01` and
  `SETTINGS_QPACK_BLOCKED_STREAMS = 0x07`, both defaulting to zero.

High-level design:

- Move common RFC 9204 primitives into `ZhttpQPack.hh/.cc`: prefixed integer
  encode/decode, string literal encode/decode, field-section prefix
  encode/decode, static table helpers, and wire-correct encoder/decoder stream
  instruction encode/decode.
- Replace the single `DynamicTable` abstraction with direction-specific
  receive/transmit data structures.
- Do not keep compatibility wrappers; compatibility with existing tests is a
  non-goal. Cascade breaking changes to dependent code and tests.
- Inject receive dynamic table state into H3 parsers and transmit state plus
  encoder-stream output into H3 builders. Do not preserve old constructor or
  helper signatures solely for dependent tests; update dependent callers and
  tests to the new API.
- Sequence implementation by vertical feature slices: first table/index
  correctness, then peer encoder stream to parser decode, then transmit
  insertion without references, then dynamic references, then acknowledgements
  and optional blocked streams.

## Architecture Documentation

### New or Changed Components

`Zhttp::H3::QPack` remains the stateless codec namespace for RFC 9204
primitive operations and static table lookup. It should no longer own policy or
connection state. It should expose:

```c++
struct FieldSectionPrefix {
  uint64_t	requiredInsertCount = 0; // decoded absolute required count
  uint64_t	base = 0;		  // decoded absolute base
};

struct EncodedFieldSectionPrefix {
  uint64_t	encodedInsertCount = 0;
  uint64_t	deltaBase = 0;
  bool		baseNegative = false;
};
```

Rename or reshape the public prefix types as needed for correctness. The
implementation must distinguish wire-encoded insert count from decoded required
insert count, and dependent tests should be updated to the corrected API. Do
not keep a field named `requiredInsertCount` if it actually contains the
encoded modulo value.

Add receive dynamic table state:

```c++
using QPackRxString =
  ZtString<ZtStringHeapID<"Zhttp.H3.QPackRx.String">>;

struct QPackRxEntry {
  uint64_t	abs = 0;
  uint32_t	size = 0;	// name.length() + value.length() + 32
  QPackRxString	name;
  QPackRxString	value;
};

using QPackRxArray =
  ZtArray<QPackRxEntry, ZtArrayHeapID<"Zhttp.H3.QPackRx.Array">>;

struct QPackRxTable {
  QPackRxArray	entries;	// oldest-to-newest or newest-to-oldest, documented
  uint64_t	baseAbs = 0;	// absolute index of entries[0] if oldest-to-newest
  uint64_t	insertCount = 0;
  uint32_t	capacityBytes = 0;
  uint32_t	maxCapacityBytes = 0;
  uint32_t	usedBytes = 0;
};
```

Use absolute index 0 for the first insertion. If `entries` is oldest-to-newest,
lookup is:

```c++
if (abs < baseAbs || abs >= baseAbs + entries.length()) reject;
entry = entries[abs - baseAbs];
```

If implementation chooses newest-to-oldest for cheap front insertion, wrap the
same math behind helper functions and do not expose storage orientation to
parsers.

Add transmit dynamic table state:

```c++
static const char *QPackTxHashID() { return "Zhttp.H3.QPackTx"; }

using QPackTxString =
  ZtString<ZtStringHeapID<"Zhttp.H3.QPackTx.String">>;

struct QPackFieldKey {
  ZuCSpan	name;
  ZuCSpan	value;

  bool equals(const QPackFieldKey &) const;
  int cmp(const QPackFieldKey &) const;
  friend uint32_t ZuHashCode(const QPackFieldKey &);
};

struct QPackTxEntry {
  uint64_t	abs = 0;
  uint32_t	size = 0;
  uint64_t	refcnt = 0;	// unacknowledged field sections referencing it
  QPackTxString	name;
  QPackTxString	value;

  static QPackFieldKey FieldAxor(const QPackTxEntry &entry) {
    return {entry.name, entry.value};
  }
};

using QPackTxHash = ZmLHash<QPackTxEntry,
  ZmLHashKey<QPackTxEntry::FieldAxor,
    ZmLHashID<QPackTxHashID,
      ZmLHashLock<ZmNoLock>>>>;

using QPackTxOrder =
  ZtArray<uint64_t, ZtArrayHeapID<"Zhttp.H3.QPackTx.Order">>;
```

`ZmHashParams{128}` constructs enough bits for at least 128 hash slots, and
the non-local `ZmLHash` constructor with `ZmLHashID` registers with
`ZmHashMgr`. Do not use `ZmLHashLocal` for this table because local hashes are
not manager-registered and have no telemetry. Pointers returned by
`ZmLHash::find()` are table-internal and can be invalidated by delete/resize,
so use them only within a lookup/encode decision and copy out the absolute
index before mutating the hash.

`QPackTxTable` also needs ordered/indexed state outside the hash:

```c++
struct QPackTxTable {
  QPackTxHash	hash{ZmHashParams{128}};
  QPackTxOrder	order;
  uint64_t	insertCount = 0;
  uint64_t	knownReceivedCount = 0;
  uint32_t	capacityBytes = 0;
  uint32_t	maxCapacityBytes = 0;
  uint32_t	usedBytes = 0;
};
```

If absolute-index lookup is needed on the transmit side, add either an
`abs -> entry` array/ring or store order entries with enough copied key data to
delete from the hash during eviction. Do not depend on `ZmLHash` iteration
order for QPACK ordering.

### New or Changed Processes or Threads

No new thread is required. QPACK dynamic state should be connection-affine and
used on the same path that already owns H3 stream parsing/building. If a caller
uses separate stream objects, inject pointers or references to per-connection
state:

```c++
Parser(QPackRxTable *);
Builder(QPackTxTable *, QPackEncoderTx *);
CxnParser(QPackRxTable *, QPackTxTable *);
```

Null dynamic pointers may still be useful as a capacity-zero convenience, but
they are not required for compatibility with existing static-only tests. Prefer
explicit connection QPACK state if that produces a cleaner parser/builder API,
and update dependent tests accordingly.

### New or Changed Interfaces

Add or refine H3 configuration/state near the existing connection integration:

```c++
struct QPackConfig {
  uint32_t	localMaxCapacityBytes = 0;
  uint32_t	localBlockedStreams = 0;
  uint32_t	peerMaxCapacityBytes = 0;
  uint32_t	peerBlockedStreams = 0;
};
```

Keep or replace `Params::qpackTableCapacity()`, `qpackBlockedStreams()`,
`qpackIndex()`, and `qpackNeverIndex()` according to the cleaner final API,
but clearly separate:

- peer-advertised max table capacity: what our encoder may use;
- local-advertised max table capacity: what peer encoder may use;
- current dynamic table capacity: set by encoder-stream Set Dynamic Table
  Capacity instruction and bounded by peer max;
- hash size: local implementation detail, default `ZmHashParams{128}`.

The CRTP `Builder` needs hooks so applications can provide connection state.
These hooks may be required; avoiding updates to existing builder call sites is
not a goal:

```c++
QPackTxTable *qpackTx();
QPackEncoderTx *qpackEncoderTx();
const Params &h3Params();
```

The CRTP `Parser` needs:

```c++
QPackRxTable *qpackRx();
const Params &h3Params();
uint64_t streamID(); // only needed once decoder stream acknowledgements are emitted
```

### New or Changed Data Flows

Receive flow:

1. Peer control stream SETTINGS are parsed and validated.
2. Peer encoder stream instructions update `QPackRxTable`.
3. Request/response stream HEADERS decode field-section prefix.
4. If Required Insert Count is available, fields are decoded synchronously and
   callbacks fire as today.
5. If Required Insert Count is unavailable:
   - with `localBlockedStreams == 0`, fail the field section;
   - with non-zero blocked streams in a later phase, track the blocked stream
     explicitly.
6. If a dynamic field section with non-zero Required Insert Count is decoded,
   emit decoder stream Section Acknowledgement when decoder-stream Tx exists.

Transmit flow:

1. Peer SETTINGS establish `peerMaxCapacityBytes` and `peerBlockedStreams`.
2. Builder encodes a HEADERS field section using one field helper path.
3. For non-static fields allowed by policy, builder plans dynamic insertion.
4. Encoder stream emits Set Dynamic Table Capacity, Insert With Name Reference,
   Insert Literal, or Duplicate before field sections that can reference them.
5. With zero blocked streams, field sections only reference entries known to
   have been inserted before the current field section and not newer than the
   safe base selected for that section.
6. Peer decoder stream acknowledgements update known received count and
   outstanding reference bookkeeping.

### New or Changed Event-Driven or Timer Processing

No timer is required for first-pass dynamic QPACK. Event-driven changes are all
stream-input or stream-output events:

- peer encoder stream readable: parse one or more instructions and update
  receive table;
- peer decoder stream readable: parse acknowledgement/cancellation/increment
  instructions and update transmit table;
- local HEADERS write: optionally emit encoder-stream bytes first, then write
  HEADERS;
- stream reset/cancel: later, emit Stream Cancellation on the decoder stream
  if a blocked/dynamic field section was abandoned.

### New or Changed Network Programming

HTTP/3 unidirectional stream handling changes:

- Keep stream type `0x02` as QPACK encoder stream and `0x03` as QPACK decoder
  stream.
- Do not require opening local QPACK streams when peer max dynamic capacity is
  zero and no decoder instructions are needed. RFC 9204 permits avoiding an
  unused encoder stream.
- If QPACK streams are opened, do not close them. Treat peer closure or stop
  requests for critical QPACK streams as connection errors once stream lifecycle
  plumbing exposes that detail.
- Ensure encoder-stream bytes are submitted before HEADERS bytes that reference
  them. Do not assume QUIC stream scheduling alone gives semantic ordering
  across streams.

### New or Changed Data Stores

No persistent datastore is introduced. In-memory stores:

- `QPackRxTable`: bounded by local advertised max capacity.
- `QPackTxTable`: bounded by peer advertised max capacity plus hash/order
  overhead.
- blocked-stream bookkeeping: initially disabled by advertising zero blocked
  streams; later bounded by `SETTINGS_QPACK_BLOCKED_STREAMS`.

Heap IDs:

- `Zhttp.H3.QPackRx.String`: receive dynamic table owned names/values.
- `Zhttp.H3.QPackRx.Array`: receive table storage.
- `Zhttp.H3.QPackTx.String`: transmit dynamic table owned names/values.
- `Zhttp.H3.QPackTx.Order`: transmit insertion/eviction order.
- `Zhttp.H3.QPackTx`: `ZmLHash` ID for manager overrides and telemetry.
- Existing `Zhttp.H3.HeaderBytes`, `Zhttp.H3.Headers`, and
  `Zhttp.H3.FrameHdr` remain appropriate for transient encoding buffers.

`ZmLHashID` is not a `ZmHeap` heap ID. It names the hash for `ZmHashMgr`.
Precise heap attribution for `ZmLHash` slots would require a `ZmLHash`
facility change and is not required for this feature.

## Detailed Design and Implementation Plan

### Phase 1: Make QPACK Codec Primitives Wire-Correct

- Area of focus: RFC 9204 primitive correctness in `ZhttpQPack.hh/.cc`, without
  integrating dynamic state into `Zhttp.hh` yet.
- Replace encoder/decoder stream instruction encoding that currently uses
  `CodecBytes::putVar()` plus synthetic IDs with RFC prefix-coded instructions:
  - Set Dynamic Table Capacity: `001` with 5-bit prefix.
  - Insert With Name Reference: `1T` with 6-bit name index prefix, followed by
    a string-literal value.
  - Insert With Literal Name: `01H` with 5-bit name length prefix, followed by
    value as 7-bit string literal.
  - Duplicate: `000` with 5-bit relative index prefix.
  - Section Acknowledgement: `1` with 7-bit stream ID prefix.
  - Stream Cancellation: `01` with 6-bit stream ID prefix.
  - Insert Count Increment: `00` with 6-bit increment prefix; reject zero.
- Fix field-section prefix handling:
  - encode Required Insert Count using RFC modulo logic when dynamic capacity
    is non-zero;
  - decode encoded insert count back to absolute Required Insert Count using
    total inserts and max table capacity;
  - compute negative Base as `ReqInsertCount - DeltaBase - 1`;
  - reject sign bit 1 when `ReqInsertCount <= DeltaBase`;
  - allow zero Required Insert Count only with no dynamic references.
- Keep `QPack::decodeString()` and Huffman decoding path, but add tests for
  string literals in encoder-stream instructions because current tests mostly
  exercise synthetic varint framing.
- Consolidate static field helpers into `ZhttpQPack` if that gives the cleaner
  API. Churn in CRTP compile-time lookup tests is acceptable when the new shape
  is simpler and wire-correct.

Complexity and feasibility:

- Feasible and required. This is a correctness prerequisite. The current helper
  API behavior is known and does not match intended dynamic-stream use.
- No architectural blocker. The repo already has `putPref()` and
  `qpackDecodePrefInt_()` patterns.

Test additions:

- Known byte-level golden tests for every encoder and decoder stream
  instruction.
- Prefix tests for raw Required Insert Count 0, raw non-zero without wrap,
  wrapped encoded count, invalid encoded count, positive Base, negative Base,
  and invalid negative Base.
- Rewrite `ZhttpQPackTest` cases that assume raw insert count in the
  field-section prefix; preserving those assertions is a non-goal.

### Phase 2: Replace Direction-Agnostic DynamicTable With Receive Table

- Area of focus: receive dynamic table storage, capacity accounting, and index
  math.
- Add `QPackRxEntry` and `QPackRxTable` with explicit heap IDs.
- Store owned names and values using `QPackRxString`; never store `ZuCSpan` in
  the table because instruction spans are tied to transient rx buffers.
- Implement:
  - `setCapacity(uint32_t)`;
  - `insert(name, value)`;
  - `duplicate(relativeIndex)`;
  - `lookupAbs(abs)`;
  - `lookupRelative(base, index)`;
  - `lookupPostBase(base, index)`;
  - `insertCount()`;
  - `maxEntries()` derived from max capacity.
- Evict from the dropping point until `usedBytes + newEntrySize <= capacity`.
- Reject entries larger than capacity.
- Reject capacity above local advertised max.
- Preserve duplicate entries; duplicates are legal and must not be coalesced.
- Rewrite the old `DynamicTable` tests around `QPackRxTable`; do not keep the
  old table API or fixed `MaxEntries = 64` limit for test compatibility.
  Capacity already bounds entries, and max entries follows `capacity / 32`.

Complexity and feasibility:

- Moderate and feasible. `ZtArray` supports explicit heap IDs and direct
  storage. A sliding array is simple; a ring may be added later if shifting
  becomes material.
- The main constraint is span ownership. Existing `Header` uses spans, so table
  insert helpers must copy before the source buffer is released.

Test additions:

- Insert and evict by bytes.
- Entry larger than capacity is rejected without changing insert count.
- Duplicate copies a referenced entry even if insertion evicts the original.
- Absolute, relative, and post-base lookup use RFC absolute index 0.
- Invalid references after eviction fail.

### Phase 3: Parse Peer Encoder Stream Into Receive Table

- Area of focus: connect the QPACK encoder stream to receive dynamic state.
- Change `CxnParser::QPackEncoder` from "any bytes are invalid" to an ordered
  instruction parser.
- Because stream buffers can arrive partial, add a small incremental parser
  state for QPACK instructions rather than requiring a whole instruction in one
  rx chunk.
- Support and validate:
  - Set Dynamic Table Capacity;
  - Insert With Name Reference;
  - Insert With Literal Name;
  - Duplicate.
- Resolve name references:
  - static index when T=1;
  - dynamic relative index when T=0, relative to current receive table state.
- Decode Huffman strings into owned table storage before insertion.
- Map errors to QPACK encoder stream errors in the connection error model; the
  current `CxnState::Error` can be used initially if detailed H3 error
  propagation is not yet available.
- Keep capacity zero behavior: if local max capacity is zero, any insert or
  non-zero Set Capacity from the peer is an error.

Complexity and feasibility:

- Moderate. The underlying rx stream API already supports consuming named
  chunks; the new work is incremental prefix/string parsing and error mapping.
- No extra threading is needed.

Test additions:

- Peer encoder stream inserts literal entries across one buffer and split
  buffers.
- Insert with static name reference.
- Insert with dynamic name reference.
- Duplicate.
- Oversized Set Capacity and oversized entry produce error.
- Capacity zero rejects encoder instructions.

### Phase 4: Decode Dynamic Field Sections Through Parser Callbacks

- Area of focus: request/response HEADERS decoding end-to-end.
- Update `Parser::parseFields_()` to use `QPack` prefix decode and an optional
  `QPackRxTable`.
- Support all RFC field representations:
  - indexed static and dynamic;
  - indexed post-base;
  - literal with static/dynamic name reference;
  - literal with post-base name reference;
  - literal with literal name.
- Preserve the semantic CRTP callback behavior:
  - `:method`, `:path`, `:scheme`, `:authority`, and `:status` validation
    remains synchronous;
  - normal headers still normalize and dispatch immediately;
  - no heap allocation for method/path/status/header values unless the string
    was inserted into the dynamic table earlier.
- Validate pseudo-header ordering and initial/trailer constraints as today.
- With `localBlockedStreams == 0`, reject a field section whose Required Insert
  Count exceeds the receive table insert count. Do not buffer raw HEADERS in
  this phase.
- If dynamic decoding succeeds and decoder-stream Tx exists, plan Section
  Acknowledgement emission for non-zero Required Insert Count. It may be wired
  in Phase 8 if stream Tx is not available yet.

Complexity and feasibility:

- Moderate-high because this touches user-facing parser behavior.
- Feasible because `decodeLiteralDynamic()` already sketches the field-line
  decode cases, but it must be made prefix-correct and merged with
  `Parser::qpackHeaderRef_()` to preserve callbacks and validation.

Test additions:

- Dynamic indexed request pseudo-fields trigger `operation()`.
- Dynamic indexed response status triggers `status()`.
- Dynamic literal-name reference dispatches typed headers.
- Post-base indexed and name-reference cases.
- Required Insert Count unavailable is rejected with zero blocked streams.
- Static-only field sections remain valid protocol input, but existing parser
  tests may be rewritten around the new parser/QPACK-state API.

### Phase 5: Add Transmit Table Exact Lookup and Insertion Planning

- Area of focus: transmit-side state and builder planning without emitting
  dynamic references yet.
- Add `QPackTxTable` with:
  - exact name/value hash lookup by `QPackFieldKey`;
  - insertion order by absolute index;
  - byte capacity and eviction;
  - known received count;
  - optional refcount/outstanding stream bookkeeping.
- Use non-local `ZmLHash` with `ZmNoLock`, `ZmLHashID<QPackTxHashID>`, and
  default constructor `QPackTxHash{ZmHashParams{128}}`.
- Implement value-key comparison and hashing by bytes so temporary header spans
  can lookup entries owned by `QPackTxEntry`.
- Do not rely on hash order for eviction or relative-index conversion.
- Implement a builder encoding context:

```c++
enum class QPackBuildMode { Count, Write };

struct QPackFieldPlan {
  ZuCSpan	name;
  ZuCSpan	value;
  uint64_t	existingAbs = uint64_t(-1);
  bool		staticEncoded = false;
  bool		insert = false;
  bool		neverIndex = false;
};
```

- The count pass may discover and record planned inserts, but must not mutate
  `QPackTxTable`.
- The write pass must emit the same field bytes and commit inserts exactly
  once.
- If a local plan requires owning header bytes beyond callback lifetime, use a
  small `ZtScratch` scratch plan first and only allocate heap for unusually
  large field sections.

Complexity and feasibility:

- Moderate-high. The current builder is intentionally simple and double-runs
  its encoder lambda. Planning is necessary to avoid duplicate dynamic
  insertion.
- `ZmLHash` behavior aligns with the intended use as long as table pointers are
  not retained across mutations.

Test additions:

- Repeated non-static field is planned for insertion once despite count/write.
- Static exact fields are not inserted.
- Never-index fields are never inserted and are emitted with the N bit.
- Hash lookup by temporary spans finds owned entries by value.
- Hash default size/ID path can be inspected with `ZmHashMgr` telemetry if a
  lightweight test hook exists.

### Phase 6: Emit Encoder Stream Insertions Without Dynamic References

- Area of focus: safe transmit insertion, still decoding as static/literal
  field sections.
- Add a `QPackEncoderTx` interface or CRTP hook capable of writing bytes to the
  local QPACK encoder stream.
- After peer SETTINGS allow non-zero capacity, emit Set Dynamic Table Capacity
  once before the first insert, with capacity no greater than peer max.
- For fields not encoded through static QPACK and allowed by policy, emit:
  - Insert With Name Reference when static/dynamic name reference exists;
  - Insert With Literal Name otherwise;
  - Duplicate only when useful to refresh an older entry before it becomes
    non-evictable or expensive to reference.
- Do not emit any encoder stream bytes when peer max table capacity is zero.
- Do not emit dynamic field-section references in this phase. HEADERS remain
  static/literal, but future field sections can benefit after Phase 7.

Complexity and feasibility:

- Moderate. It requires local QPACK encoder stream availability and flush
  ordering, but avoids blocked-stream correctness risks.
- This vertical slice verifies encoder stream generation without requiring the
  peer to accept dynamic references in HEADERS yet.

Test additions:

- Peer capacity zero skips Set Capacity and inserts.
- Peer capacity non-zero emits Set Capacity then insert instruction for a
  variable field.
- Sensitive fields are not inserted.
- Encoder stream bytes are emitted before the associated HEADERS write path
  returns.

### Phase 7: Enable Zero-Blocked Dynamic References For Older Entries

- Area of focus: compression benefit without blocked field-section buffering.
- Allow builder to reference only entries inserted before the current field
  section and safe under zero blocked streams.
- Initial policy:
  - never reference entries inserted by the same field section;
  - choose `Base = insertCount` at field-section start;
  - set Required Insert Count to the largest referenced absolute index + 1;
  - encode relative index as `Base - abs - 1`;
  - do not use post-base references in transmit until non-zero blocked stream
    support is intentionally added.
- Dynamic exact name/value lookup order:
  1. static exact key/value;
  2. static name reference with literal value;
  3. dynamic exact key/value if safe;
  4. dynamic name reference if safe and useful;
  5. literal name/value.
- For all `Headers` not encoded via static QPACK and allowed by policy, plan an
  insert into the dynamic table, including pseudo-fields such as uncommon
  methods and non-`/` paths, `:authority`, variable `content-length`, typed
  headers, and trailers.
- Maintain pseudo-header ordering: all pseudo-fields before regular headers.
- Preserve direct stream writes by writing encoder-stream instructions first,
  writing the HEADERS frame header after count/planning, then streaming field
  bytes directly to the request/response stream.

Complexity and feasibility:

- High but feasible. The hard part is ensuring inserted-before and
  known-received guarantees are conservative enough when `peerBlockedStreams`
  is zero.
- This phase should not implement blocked buffering.

Test additions:

- Second request/response with same non-static field uses dynamic indexed
  representation.
- Name-only dynamic reference emits literal value with dynamic name reference.
- Newly inserted entry in same field section is not referenced.
- Required Insert Count and Base bytes match expected RFC encoding.
- Count/write pass emits identical bytes and only one insertion.

### Phase 8: Parse Peer Decoder Stream and Use Acknowledgement State

- Area of focus: transmit-side safety and eviction.
- Change `CxnParser::QPackDecoder` from drain-only to instruction parser.
- Support:
  - Section Acknowledgement;
  - Stream Cancellation;
  - Insert Count Increment.
- Track per-stream outstanding dynamic references only for field sections where
  Required Insert Count is non-zero.
- Reject invalid acknowledgements:
  - Section Acknowledgement for a stream with no unacknowledged dynamic field
    section;
  - Insert Count Increment of zero;
  - Insert Count Increment beyond sent insert count.
- Use known received count and per-entry outstanding references to determine
  evictability before table capacity reductions or new inserts.

Complexity and feasibility:

- Moderate-high. It requires stream IDs from builder/parser integration and
  table eviction rules tied to outstanding references.
- Feasible after Phase 7 because dynamic references already record the exact
  absolute indices used by each field section.

Test additions:

- Insert Count Increment advances known received count.
- Invalid increment is rejected.
- Section Ack releases outstanding references.
- Stream Cancellation releases outstanding references.
- Entries with outstanding references are not evicted.

### Phase 9: Optional Non-Zero Blocked Streams

- Area of focus: allow references that may arrive before inserts are processed.
- Only implement after the zero-blocked path is correct and interoperable.
- Add explicit blocked-stream tracking on receive:
  - stream ID;
  - buffered field section payload or rx-frame continuation state;
  - Required Insert Count;
  - memory accounting bounded by `localBlockedStreams` and max field size.
- Add unblocking when peer encoder stream advances insert count.
- Add cancellation path when request stream resets or app abandons it.
- On transmit, allow references to newly inserted entries in the same field
  section only when `peerBlockedStreams > 0` and outstanding state can be
  tracked.

Complexity and feasibility:

- High. This is intentionally out of the first production pass unless interop
  or compression requirements demand it.

## Code References to Impacted Code

- `zhttp/src/ZhttpQPack.hh:45` - `QPackDecodedInstruction` needs fields that
  represent RFC prefix-coded instructions, not synthetic varint records.
- `zhttp/src/ZhttpQPack.hh:52` - `FieldSectionPrefix` needs to distinguish
  encoded insert count, decoded required insert count, and decoded base.
- `zhttp/src/ZhttpQPack.hh:237` - Replace or split `DynamicTable` into
  `QPackRxTable` and `QPackTxTable`; remove fixed production `MaxEntries`.
- `zhttp/src/ZhttpQPack.hh:328` - Merge `decodeLiteralDynamic()` behavior into
  `Parser::parseFields_()` so CRTP callbacks and pseudo-header validation are
  preserved.
- `zhttp/src/ZhttpQPack.hh:403` - Split `DynamicState` into receive blocked
  state and transmit acknowledgement/reference state.
- `zhttp/src/ZhttpQPack.cc:180` - Fix field-section prefix encode/decode,
  including modulo Required Insert Count and negative Base math.
- `zhttp/src/ZhttpQPack.cc:228` - Keep field-line static/literal helpers but
  extend with dynamic name/value encoding and N-bit handling.
- `zhttp/src/ZhttpQPack.cc:279` - Replace encoder stream instruction encoding
  with RFC 9204 prefix-coded instructions.
- `zhttp/src/ZhttpQPack.cc:358` - Replace decoder stream instruction parsing
  with RFC 9204 prefix-coded parsing and incremental parser support.
- `zhttp/src/Zhttp.hh:944` - SETTINGS parsing must reject duplicate settings
  parameters and store QPACK settings distinctly from max field-section size.
- `zhttp/src/Zhttp.hh:1048` - Peer QPACK encoder stream must parse
  instructions into `QPackRxTable` instead of erroring on data.
- `zhttp/src/Zhttp.hh:1052` - Peer QPACK decoder stream must parse
  acknowledgements once transmit references are emitted.
- `zhttp/src/Zhttp.hh:1320` - `Parser::parseFields_()` must decode dynamic
  field sections through `QPackRxTable`.
- `zhttp/src/Zhttp.hh:1830` - `encodeFieldPrefix()` must be replaced with a
  prefix encoder using Required Insert Count/Base chosen by the builder plan.
- `zhttp/src/Zhttp.hh:1836` - `encodeKnownField()` and variable field helpers
  should route through one field encoder that tries static exact, static name,
  dynamic exact, dynamic name, then literal.
- `zhttp/src/Zhttp.hh:2150` - `writeHeaders_()` needs planning so count and
  write passes do not mutate QPACK state twice.
- `zhttp/src/Zhttp.hh:2170` - Request pseudo-fields need dynamic insert policy
  for uncommon method/path/authority while preserving pseudo-header order.
- `zhttp/src/Zhttp.hh:2196` - Response status/content-length/header encoding
  must use the same dynamic-capable helper path.
- `zhttp/src/Zhttp.hh:2220` - Trailers should use the same field-section
  encoder and dynamic insertion policy as regular headers.
- `zhttp/src/ZhttpLib.hh:37` - Use existing `QPackLog` for dynamic QPACK
  diagnostics.
- `zhttp/src/Makefile.am:13` - Already installs/builds `ZhttpQPack.hh/.cc`;
  update only if new files are split out.
- `zhttp/test/Makefile.am:22` - `ZhttpQPackDynamicTest` is already in the test
  target; rewrite or split it as needed and add parser/builder integration
  tests.
- `zhttp/README.md:23` - Update after implementation if scope remains
  conservative; current text overstates dynamic integration.
- `zquic/test/ZquicH3Lite.cc:283` - H3Lite SETTINGS already writes QPACK
  settings and can be reused for interop fixtures, but it is not production
  `Zhttp` integration.

## Detailed Test Plan

Unit tests in `zhttp/test/ZhttpQPackTest.cc`, or a renamed replacement:

- Static table exact and name lookup remain wire-correct.
- Field-section prefix raw zero, non-zero, wrap, invalid wrap, positive base,
  negative base, and invalid negative base.
- RFC byte-level field line encodings:
  - indexed static;
  - literal static-name reference;
  - literal literal-name;
  - indexed dynamic;
  - indexed post-base;
  - literal dynamic name reference;
  - literal post-base name reference.
- Huffman string decode failure still rejects malformed fields.

Unit tests in `zhttp/test/ZhttpQPackDynamicTest.cc`, or a renamed replacement:

- Receive table capacity, eviction, duplicate, absolute/relative/post-base
  lookup, and owned string lifetime.
- Transmit hash exact lookup by temporary `ZuCSpan` key.
- Transmit eviction deletes hash entries and preserves order.
- Set Capacity, Insert With Name Reference, Insert With Literal Name, Duplicate,
  Section Ack, Stream Cancellation, and Insert Count Increment all use RFC
  bytes and reject malformed/truncated inputs.
- Never-index policy blocks dynamic insertion for `authorization`, `cookie`,
  `set-cookie`, and configured names.

Parser integration tests:

- Zero-dynamic protocol behavior remains valid, but parser tests may need to
  change for the new QPACK-state API.
- Non-zero Required Insert Count with null/no table is rejected.
- Peer encoder stream insert followed by HEADERS dynamic indexed field invokes
  the same callbacks as a literal field.
- Dynamic pseudo-fields produce `operation()`/`status()` callbacks.
- Dynamic trailers are accepted only after body completion.
- Evicted references fail.

Builder integration tests:

- Capacity zero emits the same static/literal field sections as today.
- Peer capacity non-zero emits Set Capacity and insert instructions for
  allowlisted non-static fields.
- Count/write pass does not duplicate inserts.
- Repeated field on a later field section emits dynamic indexed reference.
- Newly inserted fields are not referenced in the same field section when
  blocked streams are zero.
- Pseudo-header order remains valid.

Connection/interoperability tests:

- `curl` to `Zhttp` H3 server with non-zero advertised local dynamic capacity,
  using a synthetic peer if curl does not emit dynamic QPACK reliably.
- `zhttpclient` to Caddy or another HTTP/3 server with peer dynamic capacity
  advertised; validate both zero-capacity and non-zero-capacity modes.
- A synthetic peer should be kept because common servers may choose not to use
  dynamic QPACK in small tests.
- Negative tests for duplicate SETTINGS, oversize QPACK capacity, unexpected
  encoder stream data in zero-capacity mode, and invalid decoder stream
  acknowledgement.

Run commands:

- `make -j` after configuration.
- `./zhttp/test/ZhttpQPackTest`
- `./zhttp/test/ZhttpQPackDynamicTest`
- `./zhttp/test/Zhttp3InteropTest`
- `./zhttp/test/ZhttpFallbackTest`

If those binaries are renamed or split as part of the redesign, run the
replacement binaries instead. Keeping the old test entry points is not an
acceptance criterion.

## Acceptance Criteria

- Capacity zero remains a supported protocol mode with static/literal field
  sections, but dependent tests and helper APIs may change.
- All QPACK encoder/decoder stream instructions are RFC 9204 wire-compatible.
- Field-section prefix encoding/decoding handles modulo Required Insert Count
  and correct Base math.
- Peer encoder stream instructions populate receive dynamic table state.
- Dynamic field references decode through existing CRTP parser callbacks.
- Builder can insert non-static, policy-allowed fields into transmit dynamic
  state without duplicate count/write mutations.
- Builder can use dynamic references for entries inserted before the current
  field section under zero blocked streams.
- Peer decoder stream acknowledgements are parsed and invalid updates rejected.
- Dynamic table memory is bounded by advertised capacities.
- All dynamic allocation sites have explicit heap IDs.
- No polymorphic table entries are introduced.
- `ZmLHash` use remains manager-overridable and does not retain unstable
  pointers across mutation.
- README and the updated test suite accurately describe implemented dynamic
  QPACK behavior.

## Non-goals

- Do not implement receive-side blocked field-section buffering in the first
  production pass.
- Do not use post-base references for locally encoded field sections until
  non-zero blocked streams are deliberately implemented.
- Do not add a third transmit hash table for name-only lookup unless profiling
  or code clarity justifies it. Prefer an ordered hint/list or linear scan over
  recent entries first.
- Do not use polymorphic entries or `ZuObject`/`ZuRef` for table entries unless
  asynchronous ownership proves plain owned strings and table-local references
  unsafe.
- Do not make dynamic QPACK mandatory.
- Do not change HTTP/1 builder/parser behavior.
- Do not preserve dependent HTTP/3/QPACK tests, helper signatures, or
  constructor shapes solely for compatibility; update them to the new design.
- Do not broaden into server push, DATAGRAM, WebTransport, QUIC v2, 0-RTT, or
  unrelated QUIC transport behavior.

## Options and Open Questions

- **Blocking streams:** Defer. Zero blocked streams is the pragmatic first
  production target. Non-zero blocked stream support is feasible but materially
  increases memory accounting, stream buffering, and cancellation complexity.
- **Transmit name-only lookup:** Start without a second hash. Dynamic exact
  lookup gives most of the immediate benefit, and static name references cover
  common HTTP names. Add a small recent-entry scan or auxiliary name index only
  after tests show value.
- **Storage orientation:** Receive table can be oldest-to-newest for simple
  absolute lookup or newest-to-oldest for cheap QPACK-relative lookup. Hide the
  choice behind helper methods. The plan recommends oldest-to-newest plus
  `baseAbs` unless insertion cost becomes measurable.
- **Hash slot heap attribution:** `ZmLHashID` gives telemetry/override ID, not
  a `ZmHeap` heap ID. Adding heap attribution for hash slots would be a
  general `ZmLHash` enhancement and is not required for dynamic QPACK.
- **Detailed H3 error propagation:** The code currently collapses many H3
  failures into `CxnState::Error`. Dynamic QPACK should classify failures as
  QPACK decompression, encoder stream, or decoder stream errors internally.
  If the transport close path cannot carry those yet, record the detailed
  reason in state/logging first and wire transport error codes in a later
  HTTP/3 error propagation phase.

No legacy open question blocks the implementation. The main deliberate scope
choice is to ship zero-blocked dynamic QPACK first and treat non-zero blocked
streams as an optional follow-up.
