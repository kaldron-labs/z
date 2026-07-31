# QPACK Iteration Plan

## Goal and Guidelines

Design and implementation must align with `iterate_implementation.md` and `AGENTS.md`.

## Current State

`http3_qpack.new.md` has largely been implemented, but the current code is not
yet production-shaped. The recent changes added RFC-style QPACK instruction
encoding, receive/transmit tables, peer QPACK stream parsing, dynamic HEADERS
decode, and dynamic-capable builder hooks. The next pass should correct
state-safety and simplify repeated logic before adding broader functionality.

Primary code areas:

- `zhttp/src/ZhttpQPackTypes.hh`
- `zhttp/src/ZhttpQPack.hh`
- `zhttp/src/ZhttpQPack.cc`
- `zhttp/src/Zhttp.hh`
- `zhttp/test/ZhttpQPackTest.cc`
- `zhttp/test/ZhttpQPackDynamicTest.cc`

## Findings

### Performance Priorities

Fix these before broadening dynamic QPACK behavior:

1. Eliminate hot-path heap allocation in field-section build/decode.
2. Remove O(n) front-splice and hash-scan paths from incremental parsing and
   table eviction.
3. Preserve zero-copy spans only while their backing storage is stable; pass
   caller-owned scratch when decoded bytes must outlive a helper call.
4. Keep QPACK state connection-affine, but make the Rx and Tx sides explicitly
   separate so independent streams do not share buffers or cache lines.

### 1. QPACK stream parser state is shared across independent streams

`CxnParser` stores one `m_qpackBytes` buffer and uses it for both peer QPACK
encoder and decoder streams (`zhttp/src/Zhttp.hh:898`, `zhttp/src/Zhttp.hh:1116`,
`zhttp/src/Zhttp.hh:1130`). If the same parser instance processes multiple
unidirectional streams, a partial encoder instruction can contaminate decoder
stream parsing, or vice versa. QPACK streams are independent ordered streams and
need independent incremental decode state.

Plan:

- Add a small `QPackInsnParser` type in `ZhttpQPackTypes.hh` or
  `ZhttpQPack.hh`, parameterized by decoder function or direction.
- Give encoder-stream and decoder-stream handling separate buffers/state.
- Do not append one byte at a time and then `splice(0, n)` after every
  instruction. Keep a read offset into the buffered bytes, compact only when the
  offset crosses a threshold, and parse directly from the incoming `ZiRxStream`
  span when an instruction is complete in one span.
- Prefer storing that state on the per-stream parser object if `CxnParser` is
  per-stream. If it is connection-level, key parser state by stream ID.
- Keep buffers bounded and explicit; use a QPACK-specific heap ID if the buffer
  remains a `ZtArray`.
- Use `ZtScratch` for rare cross-buffer gather scratch, following the
  `ZiRxStream` and H3 frame-header patterns already in the repo.
- Add split-instruction tests that interleave encoder and decoder stream
  processing.

### 2. Builder mutates transmit QPACK state before the write is known to succeed

`Builder::Build::field()` appends encoder instructions and immediately calls
`tx->insert()` (`zhttp/src/Zhttp.hh:2325`). `writeHeaders_()` can still fail
later while encoding the prefix, writing the encoder stream, writing the HEADERS
frame header, or flushing (`zhttp/src/Zhttp.hh:2417`). That leaves local
transmit state advanced even if bytes were not emitted consistently.

Plan:

- Split builder work into a planning phase and a commit phase.
- Planning records decisions in a compact `QPackFieldPlan`/`QPackInsertPlan`
  array without mutating `QPackTxTable`.
- Commit writes encoder-stream bytes first, writes the HEADERS frame, then
  applies table insertions and section reference tracking only after the bytes
  are successfully accepted by the local stream abstraction.
- Preserve direct stream writes. The current `Build` object stores the complete
  HEADERS payload in `HeaderBytes body` and encoder instructions in
  `HeaderBytes encoder`; that shifts the hot path from streaming to heap-backed
  buffering. Count/plan with compact stack scratch, then write prefix and field
  lines directly to `TxBytes`.
- Use stack/local scratch first (`ZtScratch` if available in this codebase);
  fall back to heap-backed `ZtArray` with explicit heap IDs only for large field
  sections.
- Avoid temporary `HeaderBytes insn` plus append-copy for each insertion.
  Encode insertion instructions into a caller-provided output sink or one
  pre-sized local encoder scratch buffer.
- Add tests that force failure after planning and verify `insertCount()`,
  `knownReceivedCount()`, hash contents, and `capacitySent` remain unchanged.

### 3. Dynamic field decoding logic is duplicated

`decodeLiteralDynamic()` in `ZhttpQPack.hh` and `Parser::parseFields_()` in
`Zhttp.hh` both implement the same QPACK field-line dispatch with separate
callbacks (`zhttp/src/ZhttpQPack.hh:97`, `zhttp/src/Zhttp.hh:1441`). This will
continue to diverge as static, dynamic, post-base, literal, Huffman, and
never-index cases evolve.

Plan:

- Factor field-section decode into one helper in `ZhttpQPack.hh/.cc`.
- The helper should decode a field line and call a supplied sink:
  `field(name, value, flags)` where flags carry static/dynamic source and
  never-index metadata.
- `QPack::decodeLiteral()` and `decodeLiteralDynamic()` become thin test/public
  wrappers around that helper.
- `Parser::parseFields_()` keeps semantic HTTP validation and CRTP callbacks,
  but delegates wire decode and header-list byte counting to the helper.
- Replace repeated `if` chains over field representation prefixes with a local
  `switch` on `first & mask` where that makes the representation table clearer.
- Add golden tests for all field representations through both the helper and the
  parser callback path.

### 4. Static table lookup is partial and encoded as chained conditionals

`QPack::staticIndex()`, `staticField()`, and `staticNameIndex()` are manually
maintained chains (`zhttp/src/ZhttpQPack.cc:265`, `zhttp/src/ZhttpQPack.cc:281`,
`zhttp/src/ZhttpQPack.cc:304`). They are incomplete by design, duplicate static
table knowledge already present in compile-time QPACK table machinery, and are
easy to make inconsistent.

Plan:

- Unify static table lookup behind the existing `QPackTbl`/`QPackKV` compile-time
  table if it contains the full table, or generate one central table used by
  both parser and builder.
- Keep hot exact lookup cheap: static exact index lookup can remain a short
  optimized path for the most common pseudo-fields, but it must be backed by a
  complete table fallback.
- Replace broad chained comparisons with `ZuSwitch`, a compact generated array,
  or a module-appropriate hash only if runtime lookup volume justifies it.
- Add tests for representative static entries outside the current small subset.

### 5. Fixed-size settings and policy arrays should be replaced or bounded better

`CxnParser` stores duplicate SETTINGS keys in `uint64_t m_settingsKeys[32]`
(`zhttp/src/Zhttp.hh:1007`, `zhttp/src/Zhttp.hh:1189` in the current file).
`Params` stores index and never-index names in fixed arrays of 32
`HeaderName` values (`zhttp/src/ZhttpQPackTypes.hh:109`). Both silently stop
recording after the fixed bound, which can miss duplicate SETTINGS keys and
silently ignore policy names.

Plan:

- Replace SETTINGS duplicate tracking with a small `ZuArray`/`ZtArray` or a
  `ZmLHash`/`ZmHash` set depending on existing HTTP/3 parser patterns.
- If using an array, reject once the bounded capacity is exceeded instead of
  silently not recording keys.
- Replace `Params` name lists with a small owned array type using explicit heap
  IDs, or make the fixed bound an explicit validation failure.
- For the common small policy case, prefer `ZuArray` or `ZtBuiltin`-style inline
  capacity over heap-backed `ZtArray`; heap allocation should only appear when
  an application deliberately supplies a larger policy list.
- Add tests for more than 32 SETTINGS parameters, duplicate after the 32nd
  parameter, and policy overflow.

### 6. Transmit eviction and absolute lookup scan the hash repeatedly

`QPackTxTable::lookupAbs()`, `evict()`, `trackSection()`, and `sectionAck()` all
scan the hash to find entries by absolute index (`zhttp/src/ZhttpQPack.cc:130`,
`zhttp/src/ZhttpQPack.cc:165`, `zhttp/src/ZhttpQPack.cc:193`,
`zhttp/src/ZhttpQPack.cc:209`). This preserves correctness for small tables but
is the wrong shape for a performance-oriented implementation.

Plan:

- Add an absolute-index ordered store for transmit entries, or store enough
  owned key data in `order` to delete directly from `QPackTxHash`.
- Keep `ZmLHash` as the exact name/value lookup and do not rely on hash
  iteration order for QPACK ordering.
- Make eviction skip or stop cleanly on referenced entries without dropping
  `order` entries that remain live.
- Add tests covering eviction with outstanding refs, capacity reduction, and
  lookup after several deletes.

### 7. Receive table eviction is O(n) due to front splicing

`QPackRxTable` stores oldest-to-newest entries and evicts with
`entries.splice(0, 1)` (`zhttp/src/ZhttpQPack.cc:45`, `zhttp/src/ZhttpQPack.cc:62`).
That is acceptable as a first pass but becomes a hot-path cost under churn.

Plan:

- Keep the current orientation for the immediate correctness pass.
- Add a narrow helper boundary for `oldest()`, `pushNewest()`, and
  `dropOldest()` so a ring implementation can replace front-splice without
  touching parser logic.
- Prefer a ring over repeated `ZtArray::splice(0, 1)` before enabling high
  churn dynamic QPACK by default. The receive table is bounded, but front
  deletion still turns sustained insert/evict traffic into repeated memmove.
- Revisit only after the transactional builder and decode unification are done.

### 8. QPACK Tx/Rx interfaces are not fully sharded or ownership-explicit

The current CRTP hooks return raw pointers to shared connection state
(`qpackRx()`, `qpackTx()`, `qpackEncoderTx()`, `qpackDecoderTx()`). That is
fine if the connection owner guarantees stream-affine access, but the code does
not document or enforce it. `QPackEncoderTx` is virtual
(`zhttp/src/ZhttpQPackTypes.hh:249`), which is simple but not ideal for a hot
path.

Plan:

- Document that QPACK tables are connection-affine and accessed only by the
  owning Rx/Tx path.
- Keep `ZmNoLock` only under that contract; if connection code can process
  QPACK decoder stream and HEADERS builders concurrently, introduce sharded
  Tx ownership or explicit synchronization at the connection boundary.
- Consider replacing `QPackEncoderTx` virtual dispatch with a CRTP hook or
  concrete callback once the call sites settle.

### 9. Huffman string decode uses local heap buffers with unsafe lifetimes

`QPack::decodeEncoderInstructionOne()` creates local `HeaderBytes` storage for
Huffman-decoded names and values, then returns spans inside
`QPackDecodedInstruction` (`zhttp/src/ZhttpQPack.cc:572`,
`zhttp/src/ZhttpQPack.cc:579`). If a string was Huffman encoded, the returned
span points into a destroyed local array. Even when the lifetime happens to be
safe for non-Huffman strings, the helper shape encourages hidden heap work.

Plan:

- Make instruction decode accept caller-owned scratch storage, or change the
  apply path to receive decoded strings through a functional callback while the
  scratch is still alive.
- Use `ZtScratch(HeaderBytes, expectedLen)` for Huffman decode scratch where
  the decoded size is bounded by the current input span.
- Keep raw non-Huffman strings as spans into the input buffer and only copy when
  inserting into `QPackRxTable`.
- Add Huffman-encoded encoder-stream instruction tests that fail under dangling
  storage and pass with caller-owned scratch.

### 10. Field-section decode allocates per field for rare Huffman cases

`QPack::decodeLiteral()` and `decodeLiteralDynamic()` declare `HeaderBytes`
`nameStorage` and `valueStorage` inside the field loop
(`zhttp/src/ZhttpQPack.hh:105`, `zhttp/src/ZhttpQPack.hh:164`). `HeaderBytes`
is a heap-backed `ZtArray` when it owns storage, so repeated Huffman fields can
allocate repeatedly in the parser hot path.

Plan:

- Move string decode scratch out of the per-field loop and reuse it for the
  whole field section.
- Prefer caller-owned `ZtScratch` scratch with a field-section-size bound.
- Structure the shared field-line decoder as:
  `decodeFields(input, rxTable, params, scratch, sink)`, where `sink` consumes
  spans synchronously.
- Add a test that decodes many Huffman literals and verifies the path does not
  allocate per field where heap telemetry hooks are available.

### 11. Header build currently copies payload bytes multiple times

`Build::field()` encodes into `body`; `writeHeaders_()` later copies `prefix`
and `body` into `TxBytes` (`zhttp/src/Zhttp.hh:2313`,
`zhttp/src/Zhttp.hh:2442`). Query paths also build a temporary `HeaderBytes`
value before encoding (`zhttp/src/Zhttp.hh:2390`). Encoder stream insertions
are encoded into a temporary `HeaderBytes insn`, copied into `encoder`, then
written.

Plan:

- Keep field-section length calculation separate from byte emission, but make
  byte emission direct to the final stream output.
- Add field encoders that accept segmented values, e.g. `path`, separator, and
  `query`, so `:path` with query does not require concatenation.
- For encoder-stream instructions, write into a local scratch once per HEADERS
  block or directly to `QPackEncoderTx` during commit.
- Use `ZtScratch` for the field prefix and any bounded temporary byte output;
  reserve heap-backed `HeaderBytes` for genuinely large or buffered sections.

### 12. Hashing and equality recompute work and miss obvious cached keys

`QPackFieldKey::hash()` hashes the name three times and the value once
(`zhttp/src/ZhttpQPackTypes.hh:180`). Tx ref tracking and eviction then scan the
hash by absolute index. This is small today but sits directly on the dynamic
compression path.

Plan:

- Compute the name hash once inside `QPackFieldKey::hash()`.
- Store cached hash or key metadata in `QPackTxEntry` if `ZmLHash` supports
  avoiding recomputation through the accessor path.
- Add an absolute-index store so ref tracking does not iterate the hash.
- Keep `ZmLHashID<QPackTxHashID>` and `ZmNoLock` for manager visibility and
  connection-affine performance.

### 13. Capacity setup and SETTINGS handling conflate local and peer policy

`writeHeaders_()` initializes Tx max capacity from `h3Params().qpackTableCapacity()`
when `QPackTxTable::maxCapacity()` is zero (`zhttp/src/Zhttp.hh:2403`). Peer
SETTINGS also update Tx max capacity (`zhttp/src/Zhttp.hh:839`). Falling back
to local params risks using capacity that the peer did not advertise, which can
increase latency through failed interop and invalid dynamic references.

Plan:

- Keep peer-advertised Tx capacity separate from local desired capacity.
- Emit Set Dynamic Table Capacity only after peer SETTINGS are known and the
  chosen capacity is `min(localDesiredTxCapacity, peerMaxCapacity)`.
- Keep local receive capacity in `QPackRxTable` and peer transmit capacity in
  `QPackTxTable`; do not infer one from the other.
- Add tests that no encoder-stream bytes are emitted before peer SETTINGS and
  none are emitted when peer capacity remains zero.

### 14. Acknowledgement tracking can grow and scan linearly

`QPackTxTable::sections` is a heap-backed array of outstanding sections, and
`sectionAck()` scans it by stream ID (`zhttp/src/ZhttpQPackTypes.hh:215`,
`zhttp/src/ZhttpQPack.cc:209`). Under many concurrent streams, ack latency
becomes linear and ref release work repeats hash scans.

Plan:

- Store outstanding sections in a `ZmLHash` keyed by stream ID if concurrent
  dynamic sections are expected to exceed a tiny inline threshold.
- For small counts, use inline/local storage first and promote only when needed.
- Store direct absolute-index references compatible with the Tx absolute-index
  store from Finding 6.
- Add stress tests with many outstanding dynamic sections and out-of-order
  Section Acks.

### 15. Error logging is too coarse for performance diagnosis

Most failure paths in the builder log only `"failed to write H3 headers"`
(`zhttp/src/Zhttp.hh:2417`, `zhttp/src/Zhttp.hh:2425`,
`zhttp/src/Zhttp.hh:2437`). Dynamic QPACK needs low-volume diagnostics that can
distinguish prefix encode failure, encoder-stream write failure, capacity
policy failure, and table commit failure.

Plan:

- Use existing `QPackLog`/`ZiLOG` categories for precise dynamic QPACK
  diagnostics, but keep logs out of the hot success path.
- Record counters or reason codes on parser/builder state for telemetry instead
  of formatting strings on every failure edge.
- Add negative tests that assert the internal error reason where such state is
  exposed.

## Implementation Sequence

### Phase A: Correct state isolation and failure atomicity

1. Split QPACK instruction parser state by stream/direction.
2. Replace append-byte/front-splice instruction buffering with offset-based
   parsing and direct-span fast paths.
3. Fix Huffman instruction decode lifetime with caller-owned scratch/callbacks.
4. Add tests for partial encoder and decoder instructions on separate streams.
5. Refactor builder into plan/commit phases.
6. Add failure-path tests proving no premature `QPackTxTable` mutation.

### Phase B: Unify decode and static helpers

1. Introduce one field-line decode helper shared by public literal decode and
   CRTP parser integration.
2. Reuse caller-owned decode scratch across all fields in a field section.
3. Move static table lookup into one source of truth.
4. Replace repeated parser/helper dispatch blocks with shared helpers.
5. Expand field representation golden tests.

### Phase C: Replace silent fixed bounds

1. Fix SETTINGS duplicate tracking overflow behavior.
2. Fix `Params` index/never-index overflow behavior or replace arrays.
3. Separate peer-advertised QPACK capacity from local desired capacity.
4. Add explicit tests for bound handling.

### Phase D: Optimize table indexing boundaries

1. Add direct absolute-index support or order-owned keys to `QPackTxTable`.
2. Clean up eviction/refcount handling around outstanding sections.
3. Add direct or hashed stream-ID lookup for outstanding Tx sections.
4. Add helper boundaries around receive-table front eviction.
5. Add churn tests for both Rx and Tx tables.

### Phase E: Restore streaming output and allocation discipline

1. Convert builder emission to direct `TxBytes` writes after planning.
2. Add segmented value encoders for composed fields such as `:path`.
3. Replace temporary per-instruction `HeaderBytes` with sink-based encoding or
   one bounded local scratch buffer.
4. Verify heap allocation sites through `ZmHeapMgr`/`ZmVHeap` telemetry where
   available.

## Test Plan

Run at minimum:

- `./zhttp/test/ZhttpQPackTest`
- `./zhttp/test/ZhttpQPackDynamicTest`
- `./zhttp/test/Zhttp3InteropTest`
- `./zhttp/test/ZhttpFallbackTest`
- `make -j`

Add focused tests before broad interop:

- Split QPACK encoder instruction across buffers.
- Split QPACK decoder instruction across buffers.
- Interleaved peer encoder and decoder streams do not share partial bytes.
- Huffman-coded QPACK instruction names/values survive decode and are copied
  only when inserted into a dynamic table.
- Many Huffman-coded field literals reuse decode scratch instead of allocating
  per field.
- Builder planning failure leaves Tx table unchanged.
- Builder direct-emission path produces identical bytes to the old buffered
  path for static, literal, dynamic indexed, and insertion cases.
- `:path` with query is encoded without heap concatenation.
- Repeated field sections use dynamic references only after known-received
  state permits them.
- No encoder-stream bytes are emitted before peer SETTINGS advertise capacity.
- Duplicate SETTINGS after the fixed historical bound is rejected.
- Dynamic parser callbacks match literal parser callbacks for pseudo-fields,
  regular headers, and trailers.
- Tx table eviction and Section Ack handling stay sublinear under many
  outstanding sections if the hashed/indexed path is implemented.

## Acceptance Criteria

- QPACK encoder and decoder stream parsers have independent incremental state.
- QPACK instruction parsing avoids byte-by-byte append and front-splice on the
  hot path.
- Builder dynamic insertions are committed exactly once and only after local
  bytes are accepted.
- Builder emission is streaming-first and does not buffer whole field sections
  except for deliberately bounded scratch or large fallback cases.
- Huffman decode uses caller-owned scratch or synchronous callbacks; no returned
  span points into dead local storage.
- Dynamic field-line decode logic has one implementation path.
- Static table helpers have one source of truth.
- Fixed-size arrays no longer silently ignore settings or policy entries.
- `QPackTxTable` does not scan the hash for routine absolute-index operations.
- QPACK dynamic capacity is never inferred from local params when peer SETTINGS
  have not advertised it.
- Hot-path temporary storage uses `ZtScratch`, `ZuArray`, spans, or direct
  callbacks before heap-backed `ZtArray`.
- Existing zero-capacity static/literal behavior remains valid.
- Dynamic QPACK tests cover parser, builder, encoder stream, decoder stream,
  eviction, and failure paths.
