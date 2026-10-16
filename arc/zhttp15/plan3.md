# Immutable outbound compression and transmit-trailer removal plan

Implement this as a breaking cleanup.  Do not preserve the current message-time
QPACK planner, builder trailer aliases, or compatibility forwarding APIs.
Keep inbound H1/H2/H3 trailer parsing and `FieldSection::Trailers` unchanged.

The QPACK dynamic table is connection-local: an encoder table cannot literally
be shared by all connections.  "Built once per run" therefore has two parts:

- At `Client` / `Server` initialization, derive and materialize one immutable
  seed catalog from application-declared compile-time `Headers`, including
  fixed pairs and H3-eligible declared names.
- After each H3 peer's SETTINGS establishes its encoder capacity, install that
  catalog into the connection's Tx table and send the corresponding encoder
  instructions exactly once.  Message construction thereafter only reads
  acknowledged entries and never changes table entries, ownership, storage,
  or capacity.  Protocol counters such as the peer-acknowledged insert count
  remain connection-local Tx state and may advance from decoder-stream input.

HPACK has no encoder stream and the peer cannot populate its decoder table
before receiving a header block.  H2 therefore prepares, but does not transmit,
one immutable bootstrap before the first message on each connection.  The
bootstrap contains pre-encoded representations for declared fixed key/value
pairs and the corresponding immutable post-bootstrap encoder-table snapshot.
The first successfully committed message carries those prepared incremental-
indexing representations as its real fixed fields, then atomically publishes
the already-built snapshot locally.  Runtime-valued declarations and runtime-
named fields never enter the bootstrap.  Sending performs no entry insertion,
eviction, hashing, allocation, or adaptive capacity mutation.

If dynamic QPACK is disabled, has zero effective capacity, or cannot contain a
seed entry, continue with static-table and literal field encoding.  This is an
optional optimization and must not make startup or a connection fail merely
because a seed cannot be installed.

The non-negotiable H2/H3 hot-path invariant is **one pass: emission**.  Building
an initial field section must traverse the application Builder exactly once
and encode each resulting field exactly once.  There is no hot-path sizing
encode, dry run, validation pass, re-plan, or fallback re-encode.  H1 satisfies
this implicitly by writing its textual field lines once into the message's
scratch section; retain that property and cover it in the common contract
tests.

The tightened Builder cardinality contract is per message:

Here, "per message" means one construction of the initial field section after
the protocol has accepted the request/response start metadata.  A failed
transport which never starts that construction invokes no header provider; a
started construction never restarts or replays its traversal.  A redirect or
retry is a distinct transmission attempt which may traverse the same Builder
again.  The `replayable()` and `reproducible()` predicates do not exist: every
Builder must reproduce the same message on every attempt.

- For every `Key` in `Headers`, call `header<Key>(l)` exactly once if the
  concrete Builder implements that overload.  During that one provider call,
  the Builder may call `l(value)` zero or one times to emit the application-
  supplied value for that declared occurrence.
- A non-`void` value paired with `Key` in `Headers` is still emitted directly
  as the declared compile-time field.  An implemented `header<Key>(l)` is then
  the single opportunity to emit zero or one additional value for that key;
  it does not replace or suppress the declared fixed value.
- Call the non-template `header(l)` exactly once unconditionally.  The Builder
  base supplies the no-op fallback, so no compile-time detection or
  conditioning is needed.  During that one provider call, the Builder may call
  `l(key, value)` repeatedly, selecting the key and value independently on
  every callback.
- Compile-time overload detection applies only to `header<Key>(l)`.  Do not
  apply it to the non-template `header(l)` or remove its inherited no-op
  fallback.
- The number of provider calls is independent of how many times each provider
  calls back.  Empty providers still receive their one provider call.

## 1. Define the outbound header and QPACK contracts

1. Update the common `Zhttp::Builder` contract in `Zhttp.hh`:
   - Remove `Trailers` from the transmit Builder contract.
   - Document `header<Key>(l)` as an optional, synchronous provider called
     exactly once per message for each `Key` in `Headers`; its `l(value)` may
     be called zero or one times.
   - Document the non-template `header(l)` as a synchronous provider called
     exactly once per message unconditionally; its `l(key, value)` may be
     called zero or more times with different keys and values.  Retain the
     Builder base's no-op fallback and delete all compile-time conditioning on
     this call.
   - Specify compile-time detection only for the concrete
     `header<Key>(l)` overloads so a missing keyed overload receives no call.
   - Perform keyed-overload detection against the application Builder, not a
     protocol/facade adapter which necessarily declares forwarding functions.
     A forwarding adapter must conditionally enter each keyed application
     provider and must expose library-owned fields independently.
   - State the protocol-specific dynamic-table eligibility rules:
     - A non-`void` `(Key, Value)` pair in `Headers` is an exact seed candidate
       for both H2 and H3.
     - A `Key` paired with `void` is a name-only seed candidate for H3, but not
       H2.  Values later supplied by `header<Key>(l)` are never seed inputs.
     - Keys and values supplied by `header(l)` are never seed inputs.
   - State the mutation rule independently of C++ value type: `bodyHdrs` may
     target only a `Headers` entry whose declared `Value` is `void`.  A
     non-`void` compile-time value is immutable for the message and remains
     eligible for exact static/dynamic compression.
2. Introduce a small type-level traversal in the shared field/QPACK layer that
   visits `(Key, Value)` pairs in a `Headers` list and produces:
   - Exact candidates for every fixed compile-time value (`Value != void`).
   - H3 name-only candidates for every runtime-valued declaration
     (`Value == void`).
   Reuse the existing alternating key/value representation; do not add a
   parallel header declaration format.
3. Define deterministic seed eligibility:
   - Require each selected fixed value to be a single printable value suitable
     for an outbound field with stable, exact bytes.  A `Placeholder`, patched
     value, parser-only value list, or other value whose transmitted bytes are
     not fixed at initialization is not an exact candidate.  This is a
     compile-time seed classification only; the message caller does not inspect
     value types while rendering or patching.  Reject structurally invalid
     outbound declarations and otherwise leave mutable values on the runtime
     literal path.
   - Materialize every selected key and fixed value once into shared byte
     strings.  Represent an H3 name-only candidate as the QPACK dynamic entry
     `(Key, "")`; QPACK has no name-only entry representation, and the empty
     value exists only to make the declared name available for later dynamic
     name references.
   - Exclude forbidden/pseudo/never-index names and entries larger than the
     configured table capacity.  Seed ineligibility is not Client/Server
     initialization failure; the corresponding fields remain literal/static
     on the message path.
   - Skip an exact candidate when that exact key/value pair exists in the
     protocol's static table.  Skip an H3 name-only candidate when that name
     already has a QPACK static-table name index.
   - Deduplicate exact pairs and H3 name-only candidates while preserving
     declaration order.  If the same name has both an exact candidate and a
     name-only candidate, retain the exact entry and omit the redundant empty
     entry because the exact dynamic entry also supplies a name reference.
   - For every H2 exact candidate, prebuild both wire choices needed by the
     first message: its literal-with-incremental-indexing representation and
     its non-indexing/static fallback.  Restrict the prepared indexing form to
     static-name or literal-name encoding so it is independent of unpublished
     dynamic indices.
   - Never call an application Builder instance, `header<Key>(l)`, or
     `header(l)` while constructing the seed catalog.
4. Give the Client and Server explicit initialization-time seed sources:
   - Client derives its seed source from its application request
     `Request::Headers`.
   - Server exposes an application type alias listing the outbound response
     `Headers` sets it wants seeded, because response Builder types are chosen
     at request handling time and are not otherwise knowable at Server init.
     Default the alias to an empty type list.
   - Permit multiple header sets in the Server catalog and deduplicate their
     fixed pairs into one seed catalog.
5. Keep the catalog immutable after successful `Client::init` / `Server::init`
   and retain it in initialization-owned state shared read-only by H3 links.
   Use Z containers and an identified heap.  Connection table entries should
   retain views/indices into this catalog plus connection-local QPACK metadata;
   do not allocate, copy, or stringify seed names/values per connection or per
   message.
6. Build immutable H2 warm plans at the same initialization boundary:
   - Client has one plan derived from `Request::Headers`.
   - Server has one plan for each registered outbound response `Headers` set.
   - Each plan contains declaration-order fixed-field wire fragments,
     cumulative HPACK entry sizes, and immutable post-bootstrap table metadata.
     Plans may reference deduplicated catalog strings, but each retains its own
     `Headers` declaration order and resulting HPACK index order.
   - A response `Headers` set omitted from the Server registry uses only
     static/non-indexing encoding and cannot trigger hot-path plan construction
     or later table population.

### Acceptance gate

- The public comments unambiguously say that outbound trailers do not exist
  and both Builder header providers describe only initial headers.
- A compile-time fixture proves that fixed pairs become H2/H3 exact candidates,
  `void` pairs become H3-only name candidates, and no Builder callback is
  invoked.
- Client and Server initialization tests cover empty, duplicate, static-table,
  forbidden, never-index, and oversized seed candidates.
- No application response Builder is constructed or queried during Server
  initialization.
- H2 warm-plan tests prove that fixed indexing/fallback bytes and table
  metadata exist before any connection sends its first message.

## 2. Replace `HeaderPatches` with fixed indexed spans

1. Delete `HeaderPatches`, `HeaderPatches::Value`, its `Print` wrapper, tuple
   slots, `patch()` forwarding, and every Client/Server/H1/H2/H3 adapter member
   or test fixture built around them.  Delete `IsPlaceholder` and all
   `if constexpr (IsPlaceholder<...>)` / `suppressPads` filtering.  Do not add
   a replacement wrapper, tagged union, variant, hash, list, or other dynamic
   patch structure.
2. Keep `Placeholder` only as an ordinary printable value which reports its
   configured length and fills that many bytes.  Its renderer has no patching
   knowledge and is handled through the same value-emission path as strings,
   numbers, and other printable values.
3. Give each protocol-neutral request/response Builder caller a fixed span
   array:
   - Let `Keys = ZuTypeSlice<2, 0, Headers>` and allocate exactly `Keys::N`
     `ZuSpan<uint8_t>` slots as an intrinsic/fixed-size Z array member.
   - Slot `I` corresponds to header occurrence `I` in `Headers`; use the
     unrolled compile-time header index already driving emission rather than a
     run-time key lookup.
   - When `header<Key>(l)` emits its zero-or-one application value for a `void`
     declared occurrence `I`, store that mutable wire-value span directly in
     slot `I`, regardless of whether the value is a `Placeholder` or any other
     printable type.
   - A non-`void` compile-time `Value` emitted directly from `Headers` is fixed
     by definition: it does not occupy the application-value slot and
     `bodyHdrs` cannot mutate it.  Reject a `bodyHdrs` keyed callback targeting
     such an entry at compile time.  This preserves exact HPACK/QPACK seed and
     snapshot invariants.
     Any optional additional value emitted by `header<Key>(l)` for that same
     non-`void` key is likewise outside `bodyHdrs` and needs no span slot.
   - Repeated `header(l)` runtime fields remain legal but are not declared,
     indexed, or patchable.  Do not create storage for them.
4. Make emitted spans stable and protocol-correct:
   - H1 records the contiguous bytes after `"key: "` and before CRLF in the
     retained header output.
   - H2/H3 values emitted by `header<Key>(l)` for a `void` declaration always
     use a literal field representation with an uncompressed/non-Huffman
     contiguous value payload.  Make no attempt to compress or look up the
     runtime value: proceed directly to literal-value emission without testing
     for an exact static or dynamic-table match.  The field name may still use
     a static or frozen dynamic name reference.  Direct non-`void` compile-time
     values remain eligible for prepared exact indexing because they are
     immutable.
   - Render the entire initial header section into one stack-backed,
     spill-capable scratch buffer.  The inline size is only a fast-path
     optimization; `ZtScratch` grows on the heap when the section is larger.
     The Builder streams each value directly into that buffer, either as literal
     bytes or through the HPACK/QPACK encoder.  For strings and
     buffer-printable values with a known length, reserve the contiguous value
     payload and render directly into it.  For a printable whose length is not
     known without rendering, reserve length-prefix headroom, render it once
     in place, and finalize the prefix around those bytes.  Do not create a
     second value array, splice/append one array into another, copy the value,
     or render it again.
   - Record payload offsets while the spill-capable buffer can still grow,
     resolve them to final spans after emission, and keep that final backing
     storage stable until `bodyHdrs` completes and the message commits.  Span recording is part of
     the sole emission pass; do not run a count/print pass or rescan output to
     recover it.
5. Tighten the `Builder::bodyHdrs(l)` contract:
   - The caller invokes `bodyHdrs(l)` exactly once for a successfully produced
     fixed body, synchronously after the body length/result is known and after
     all patchable initial-header values have been emitted into retained
     storage, but before header bytes become externally visible.
   - The Builder may call `l.template operator()<Key>(mutator)` repeatedly,
     once for each declared header it wants to mutate.
   - For each callback, resolve `Key` to its compile-time `Headers` index, load
     that one span from the fixed array, and immediately call `mutator(span)`.
   - The Builder must request only a declared header value actually emitted by
     `header<Key>(l)` for a `void` entry in that message.  The key must identify
     one unique `Headers` occurrence; a non-`void` entry is not a valid mutation
     target.  Mutation is in-place and length-preserving; neither caller nor
     mutator may resize or replace the span.
   - If the Builder omits `bodyHdrs`, make no call and perform no patch work.
6. Simplify `MessageTx::fixed()` and `fixedH2_()` into one ordering contract for
   all protocols: produce/measure the retained body, emit the retained initial
   headers once while recording spans, invoke `bodyHdrs` once to perform all
   requested mutations, finish framing, validate retained budgets, then commit
   headers/body in wire order.  Remove the H1-only early-header/late-`patch()`
   branch and protocol-specific patch timing.
7. Remove `suppressPads` from Client/Server request/response adapters and empty
   message paths.  Continue suppressing framing-owned fields by declared key
   policy (`rejectContentLength`, transfer-encoding ownership, body policy),
   not by inspecting the emitted value's C++ type.

### Acceptance gate

- `rg -n 'HeaderPatches|IsPlaceholder|suppressPads' zhttp zws` returns no
  production or test match.
- The only patch storage is one intrinsic `ZuSpan<uint8_t>` array with exactly
  one slot per declared `Headers` entry; no patch operation allocates.
- Tests patch multiple declared keys by making one `bodyHdrs` call whose `l`
  callback is invoked repeatedly, and verify each mutator receives the exact
  retained wire-value span for its key.
- The same tests use both `Placeholder` and ordinary printable values and prove
  the caller follows an identical code path for both.
- H1, H2, and H3 fixed-body tests prove mutation occurs after sole emission and
  before visibility, preserves encoded length/framing, and never re-enters a
  header provider.
- H2/H3 tests prove patchable values are contiguous non-Huffman literals while
  still permitting static/frozen dynamic name references.
- With `ZhttpHeaders(("foo", "bar"), "baz")`, tests prove `foo: bar` may use
  exact static/dynamic compression and has no mutable slot, while every emitted
  `baz` value bypasses value-compression lookup and has a stored raw literal
  span which `bodyHdrs` may mutate.  A compile-time fixture rejects an attempted
  `bodyHdrs` mutation of `foo`.
- Empty/no-body and forbidden-content-length tests pass without value-type
  filtering.

## 3. Seed each H3 connection once

1. Add connection Tx state for `Unseeded`, `Seeded`, and `Disabled`.  It remains
   Tx-shard-owned alongside `QPackTxTable`.
2. Keep `QPackTxTable::init()` responsible only for allocating the configured
   connection-local storage.  Do not populate it before the peer's SETTINGS is
   known: the peer-advertised maximum capacity bounds encoder inserts.
3. Record the peer's QPACK maximum-table-capacity setting while parsing
   SETTINGS, defaulting it to zero when omitted.  When the complete peer
   SETTINGS frame has been validated and the local encoder stream is ready,
   post exactly one Tx-shard operation that:
   - Computes the effective capacity from local and peer limits.
   - Selects the longest declaration-ordered prefix of the immutable seed
     catalog whose cumulative QPACK entry size fits without eviction or
     resizing; entries after the first non-fitting entry are not installed.
   - Sends one Set Dynamic Table Capacity instruction, using the effective
     capacity, when the selected prefix is non-empty.
   - Sends the selected Insert instructions on the connection's encoder
     stream in catalog order.
   - Preflights all connection-local metadata/storage so committing cannot
     fail, then commits the same entries to the connection-local
     `QPackTxTable` only after the encoder-stream write succeeds.
   - Marks the connection `Seeded` after installing a non-empty prefix, or
     `Disabled` when the catalog is empty, effective capacity is zero, or no
     first entry fits.  Retries or duplicate SETTINGS must not seed it again.
4. Define failure behavior transactionally.  Build the complete seed encoder
   instruction bytes in per-connection cold-path storage before publishing
   table entries and submit them as one ordered write.  If writing fails,
   follow the existing H3 connection error path and do not publish the local
   entries.  A connection being closed after a transport-level partial write
   is sufficient; it must never continue with divergent encoder state.
5. Continue handling Insert Count Increment on the Tx shard.  A seeded entry
   becomes usable by message encoding only when its absolute index is covered
   by `knownReceivedCount()`.
6. Remove capacity changes and eviction after seeding.  Duplicate or invalid
   peer SETTINGS remain protocol errors; they do not trigger reseeding or
   message-time table reconstruction.

### Acceptance gate

- Each connection sends at most one capacity instruction and one ordered seed
  sequence, only after the complete peer SETTINGS frame and local encoder
  stream permit it.
- Omitted QPACK capacity resolves seeding to `Disabled`; no connection remains
  indefinitely `Unseeded` after valid peer SETTINGS completes.
- Zero capacity, an empty catalog, and disabled dynamic QPACK send no seed
  instructions and remain fully functional with literals/static entries.
- Tests prove that entries cannot be referenced before Insert Count Increment
  and can be referenced afterward.
- Cross-shard tests verify that seed installation and every Tx-table mutation
  remain on the owning Tx shard.

## 4. Replace H3 message-time planning with emission-only encoding

1. Delete `Build<Bytes, Plan>`, `InsertPlan`, `InsertPlans`, `planInsert_()`,
   `canCommit()` use, message-time `setCapacity()`, message-time `insert()`,
   `deferCompression()`, and the plan/re-plan/emit callback cycle from
   `ZhttpH3.hh`.
2. Replace it with one emission function.  It must invoke `operation()`,
   `host()`, and `status()` once as applicable; invoke `header<Key>(l)` once
   for every `Key` in `Headers` whose concrete overload exists; and invoke the
   non-template `header(l)` once unconditionally.  These counts are per
   message, regardless of whether either provider calls its callback zero,
   one, or many times.  Encode every callback field exactly once.  Encoding
   decisions are final as each field is visited; no later condition may
   restart or replay the message.
   Validate names and values inline during that traversal; latch failure and
   discard the buffered section rather than starting a validation or fallback
   pass.
3. Resolve the HEADERS frame-length prefix without traversing the Builder:
   - Prefer a transmit-stream reservation/backpatch facility if it can prepend
     or finalize the QUIC varint length without moving/re-encoding fields.
   - Otherwise emit the entire section once into one scratch buffer with
     sufficient headroom, obtain its final byte length, finalize the field-
     section and frame prefixes in that headroom, and flush the already-
     encoded spans to the transmit stream.  Do not splice or append a second
     array into the section.
   - Do not run a count-only encoder.  Buffering encoded output is permitted;
     invoking providers or encoding fields a second time is not.
4. For each emitted field, choose encoding without mutation:
   - Prefer an exact QPACK static-table index.
   - Otherwise use an exact seeded dynamic entry only if it is acknowledged.
   - Otherwise use an acknowledged seeded name reference when available.
   - Otherwise emit a literal field line.
   Accumulate the field section's Required Insert Count and Base during this
   same emission pass, then prepend/finalize the field-section prefix after
   emission without revisiting any field.
5. Since seeded entries are never evicted, remove per-entry message reference
   collection, reference counts, and `QPackTxTable::trackSection()` calls.
   Retain minimal outstanding-section state, separate from the immutable
   table, only for validating peer Section Acknowledgement and Stream
   Cancellation instructions:
   - Emission registers the stream ID once only when the section used a dynamic
     reference.
   - Section Acknowledgement validates and removes that stream ID.
   - Stream Cancellation validates and removes that stream ID.
   - An acknowledgement/cancellation for an untracked stream, including a
     duplicate, follows the existing QPACK decoder-stream error path.
   - This state never controls entry lifetime, capacity, eviction, or whether
     a new message may use an already acknowledged entry.
   Preallocate this tracker at connection setup from the connection/stream
   concurrency limit; registration must not allocate or resize on the message
   path.
6. Make `QPackTxTable`'s post-seed interface express the invariant: message
   code receives const lookup access only.  Restrict capacity/insertion methods
   to the cold-path seeding owner rather than leaving them callable from the
   builder.
7. Remove obsolete QPACK build-failure values and logging which refer to
   planning, replanning, capacity commit, insert commit, or section tracking;
   retain precise failures for framing, encoding, and stream writes.

### Acceptance gate

- Instrumented H3 tests prove `header<Key>(l)` is called exactly once per
  message for each implemented declared-key overload and the non-template
  `header(l)` is called exactly once per message, including through the base
  no-op fallback.  Cover zero/one keyed-value emission and
  zero/one/multiple runtime emissions, including multiple distinct runtime
  keys.  The test must also prove there is no count-only traversal before
  emission.
- `rg` finds no message-path call to `QPackTxTable::setCapacity`, `insert`,
  `canCommit`, `trackSection`, or `deferCompression`.
- Code inspection finds exactly one call site which traverses/encodes an H3
  application field section: the emission path.
- Repeated requests leave capacity, entry count, insertion count, storage
  slots, and entry contents unchanged.
- Emission may update only preallocated outstanding-section metadata; it does
  not allocate, resize, insert, evict, or change dynamic-table metadata.
- Dynamic exact/name references appear only after acknowledgement; all other
  fields fall back deterministically without changing the table.

## 5. Make H2 header construction emission-only

1. Add Tx-shard-owned `Cold` / `Warm` / `Reserved` / `Frozen` / `Disabled`
   HPACK state to each H2 connection.  When the peer's initial SETTINGS
   completes, before admitting any outbound message:
   - For every initialization-owned warm plan, select the longest prefix which
     fits the effective peer/local HPACK capacity.
   - Bind each plan to its prepared indexing/fallback fragments and an
     immutable post-bootstrap table snapshot containing exactly that prefix.
   - Prepare any required HPACK table-size-update bytes.
   - Store the bound plans in a connection array whose size/capacity was fixed
     from the initialization-time plan registry.  Binding references existing
     strings/fragments/metadata and performs no formatting, hashing, insertion,
     or heap allocation.
   Enter `Warm` only after every registered plan is bound and ready.  Enter
   `Disabled` instead when capacity is zero or no plan is registered.
2. Gate the first outbound header block until the peer's initial SETTINGS has
   completed and the connection is `Warm` or `Disabled`.  In `Warm`, select its
   bound plan by a compile-time plan index; this must be an O(1) lookup, not a
   scan or construction step.  Atomically reserve the bootstrap for that
   message and enter `Reserved` before invoking its Builder.  While `Reserved`,
   gate all other outbound header blocks on the connection so concurrent
   streams cannot encode against an unpublished or different HPACK state.  If
   the Builder's `Headers` set is unregistered, use ordinary static/literal
   non-indexing emission and freeze an empty table after successful commit.  A
   `Disabled` connection sends normally without reservation and remains
   `Disabled`.  Do not discover capacity, select a prefix, or construct a
   bootstrap while handling the first Builder.
3. Refactor `H2_::HeaderBlock` in `ZhttpH2Hub.hh` so one initial field section
   has one HPACK emission traversal.  Remove the `CountBytes`-then-`Bytes`
   pattern from `fieldPrint_()`, `field_()`, table-size-update output, and
   `endHeaders()`.
4. During the single Builder traversal:
   - Make the HPACK representation/indexing decision once per field using
     static-table lookup and const lookup against the currently published
     immutable snapshot.
   - Encode that field once, on the fly, into the one scratch-backed header
     block.
   - Reserve headroom for the already-prepared HPACK table-size update and
     finalize it at the beginning of the same buffer; do not encode or size it
     on the message path and do not append it from another array.
   - Validate and latch failure inline; discard the buffered block on failure
     without invoking a provider or encoding a field again.
5. Apply the prepared fixed-only bootstrap policy:
   - For the `Reserved` bootstrap message, write the selected bound plan's
     prebuilt incremental-indexing
     fragment for each
     directly emitted non-`void` `(Key, Value)` selected into the snapshot;
     write its prebuilt static/non-indexing fragment otherwise.  The fixed
     field remains in its normal declaration position in the message—never add
     a synthetic priming header.
   - Never incrementally index values from `header<Key>(l)`, even when `Key`
     has a `void` declaration; encode them using static/dynamic lookup or a
     literal without indexing.
   - Never incrementally index fields from `header(l)`.
   - In `Frozen` or `Disabled`, no field may cause insertion or eviction.  Use
     existing static/dynamic exact or name lookup, otherwise a literal without
     indexing.
   - Remove adaptive `HPackEncoder::plan()` / per-field `commit()` from message
     construction.  Runtime fields use a direct non-mutating lookup-and-emit
     path and create no owned plan/value copy.
   - Do not expose the prepared snapshot for lookup while assembling the first
     block; unpublished entries cannot be referenced before their prepared
     incremental-indexing fields appear on the wire.
6. After emission completes, split the already-encoded HPACK bytes at the peer
   maximum frame size and prepend HEADERS/CONTINUATION frame headers with their
   now-known lengths.  Move/chain the encoded bytes into frames; do not run an
   HPACK count encoder and do not regenerate a field to determine frame
   boundaries.
7. Publish HPACK state transactionally without mutating it:
   - After a direct first header block is accepted for ordered transmission,
     atomically switch the connection from its empty table view to the prepared
     snapshot and from `Reserved` to `Frozen`, then release gated streams in
     their existing Tx order.
   - For retained/deferred messages, keep the snapshot unpublished until
     message commit and keep other streams gated.  A discarded/failed retained
     message returns to `Warm`, preserves the same bytes/snapshot for the next
     attempt, and releases the next gated stream to reserve it.
   - On direct encode/queue failure, discard the buffered block, return to
     `Warm`, and release the next gated stream; never rebuild, patch, insert
     into, or partially publish the snapshot.
   - This publication is a pointer/state transition, not dynamic-table
     insertion or eviction caused by sending.
   - Peer SETTINGS_HEADER_TABLE_SIZE changes are control-plane changes: apply
     them on the owning Tx shard after all previously encoded header blocks.
     Gate new header blocks, prepare a truncated immutable snapshot and the
     mandated table-size-update bytes, atomically replace the local lookup view
     with that snapshot on the control path, then release the gate.  The next
     message writes the prepared update before its fields and performs const
     lookup against the already-truncated view; it does not publish, evict, or
     resize anything on its send path.  Capacity increases never add entries or
     reopen `Warm`.
   - If a capacity change arrives while `Reserved`, order it after resolution
     of that bootstrap block, then prepare/gate the next-message transition;
     never rebuild the reserved block against a new capacity.
8. Use one stack-backed, spill-capable scratch buffer for each header section.
   Header emission must not allocate per field, splice/append intermediate
   arrays, or copy the entire encoded block through a second contiguous
   temporary.

### Acceptance gate

- Instrumented H2 tests prove every implemented Builder provider is invoked
  once per message and every callback field is HPACK-encoded once.
- `H2_::HeaderBlock` contains no `CountBytes` sizing encode for an outbound
  field section, field, printable value, or table-size update.
- `rg` finds no message-path `HPackEncoder::plan`, per-field table `commit`,
  `HPackTxTable::insert`, or capacity/eviction call; only prepared snapshot
  publication and const lookup remain.
- HPACK static indexing, prepared bootstrap insertion, frozen dynamic lookup,
  never-index literals, table-size updates, and multi-frame
  HEADERS/CONTINUATION output pass through the same single emission path.
- Direct and retained/deferred messages produce identical wire fields and
  commit HPACK state only at their respective success/commit boundary.
- A late field failure neither re-enters the Builder nor commits partial HPACK
  state.
- Concurrent-stream tests prove exactly one first message owns `Reserved`, no
  later stream encodes against unpublished state, and commit/failure releases
  gated streams in Tx order.
- Given `ZhttpHeaders(("foo", "bar"), "baz")`, the first committed message may
  carry the prebuilt incremental-indexing form of `foo: bar` when it lacks an
  HPACK static exact entry, then publish the prebuilt table snapshot.  It never
  inserts any runtime value emitted for `baz`.  Later messages do not change
  the table through field emission.
- Profiling/allocation instrumentation proves first-message fixed-field output
  performs no HPACK planning, string formatting, hashing, insertion, eviction,
  prefix selection, table allocation, or fixed-field encoding beyond an O(1)
  bound-plan lookup and writing prepared bytes; its only unavoidable extra
  cost is transmitting those bytes.

## 6. Remove outbound trailers from all message builders

1. Apply the cardinality contract from section 1 to H1 and H2 as well as H3:
   emit every fixed `(Key, Value)` pair, conditionally call the concrete
   application's `header<Key>(l)` once for every declared key, and call the
   non-template `header(l)` once per message unconditionally.  The inherited
   base fallback is the no-op implementation.  Library-owned fields such as
   Server `alt-svc` must be emitted independently of the application callback.
2. Remove `BuilderTrailers`, the common Builder `Trailers` alias, and the
   `Trailers` template parameter from H1/H2/H3 `Builder_`, `Request`, and
   `Response` transmit types.  Propagate the breaking signatures through
   `ZhttpClient.hh`, `ZhttpServer.hh`, WebSocket adapters, utilities, interop
   programs, and tests.
3. H1:
   - Remove `trailers()` and trailer emission from chunked `finish()`.
   - Preserve the terminating zero chunk and final CRLF.
4. H2:
   - Remove trailer-dependent `endStream` decisions and trailer HEADERS output
     from `finish()`.
   - End the DATA/message stream directly according to body/stream mode.
5. H3:
   - Remove trailer HEADERS output from `finish()` and end/flush the message
     directly.
6. Keep all parser states, semantic validation, callbacks, and tests needed to
   accept trailers from H1/H2/H3 peers.  Parsing support is deliberately
   asymmetric with message construction.
7. Remove transmit fixtures which declare `Trailers`; replace their assertions
   with initial-header/body completion assertions.  Retain or add parser-only
   fixtures that feed peer trailer wire data and assert
   `FieldSection::Trailers` callbacks.

### Acceptance gate

- `rg -n 'BuilderTrailers|typename Trailers|Trailers_' zhttp/src zws/src`
  finds no transmit-builder trailer API or template plumbing.
- No H1/H2/H3 builder emits a trailing field section.
- H1, H2, and H3 contract tests all prove the same per-message optional
  provider cardinality, including fixed fields plus additional keyed values.
- Parser tests independently cover receiving valid and invalid trailers for
  every supported protocol.
- H1 chunked, H2 streaming/fixed, and H3 streaming/fixed completion tests
  verify correct end-of-message framing without outbound trailers.

## 7. Simplify QPACK state and tests

1. Remove Tx-table machinery used only for churn: eviction-on-message,
   per-entry section reference counts, reference arrays, `maxSections`, order
   compaction, and blocked-section admission.  Retain only immutable seeded
   entry metadata, protocol counters required for acknowledged lookup, and the
   separate minimal outstanding-section tracker required for decoder-stream
   validation.
2. Revisit `qpackTxSections` / blocked-stream configuration.  Preserve peer
   SETTINGS parsing for interoperability, but do not use the value to admit
   table mutation: messages only reference entries already acknowledged by the
   decoder and therefore never create blocked sections.  Size the minimal
   acknowledgement tracker from the existing connection/stream concurrency
   limit, not from dynamic-table churn policy.
3. Update `ZhttpQPackDynamicTest`:
   - Keep Rx dynamic-table, decoder, malformed-instruction, SETTINGS, and
     interoperability coverage.
   - Replace Tx churn/replanning/eviction tests with seed selection,
     single-installation, acknowledgement gating, immutable lookup, and
     literal fallback tests.
4. Update HPACK tests to cover initialization-built fixed-only warm plans,
   first-message snapshot publication, failed and retained first messages, the
   `Frozen` state, and prebuilt peer-directed capacity reduction without
   reopening warmup.
5. Update transport, H3 hub, message, client, and server tests to assert the
   single-pass Builder contract and table immutability across multiple
   messages/concurrent streams.
6. Add regression Builders whose providers consume counters or one-shot
   guards.  One H3 message must succeed with each implemented provider observed
   exactly once, including a runtime provider which emits several different
   key/value pairs during that single call; a Builder omitting the runtime
   overload must enter the inherited no-op fallback exactly once.

### Acceptance gate

- Tx dynamic-table allocation is bounded at Client/Server initialization and
  connection setup; no message increases its allocation or entry count.
- There is no per-entry reference tracking, reference-count churn, insert
  planning, or capacity negotiation in the message builder; the separate
  outstanding-section tracker contains stream IDs only.
- The encoder still interoperates when the peer advertises zero capacity,
  delays Insert Count Increment, or never acknowledges seeded entries.
- H2 exposes only fixed compile-time pairs from the first committed message to
  peer HPACK insertion; all insertion representations and the resulting local
  snapshot are prebuilt before that message is admitted.  Runtime values and
  every later message are non-mutating.

## 8. Verification and documentation

1. Update API comments and any README/design text describing Builder trailers,
   H2 sizing emission, H3 callback replay, adaptive QPACK insertion,
   blocked-section tracking, or message-time dynamic-table updates.
2. Run `make -j8` from the repository root.
3. Run the focused tests first:
   - `ZhttpQPackDynamicTest`
   - `ZhttpTransportContractTest`
   - `ZhttpH3HubTest`
   - `ZhttpHPackTest`
   - `ZhttpH2HubTest`
   - `ZhttpH2MessageTest`
   - `ZhttpMessageHubTest`
   - `ZhttpClientCancelTest`
   - `ZhttpServerIdleTest`
   - `ZhttpParserTest`
4. Run `make test` after the focused tests pass.
5. Review the final diff for hidden allocations/copies on H3 message paths,
   stale trailer transmit APIs, and any callback still reached more than once
   per message.

### Final acceptance

- Dynamic QPACK seed selection happens once at Client/Server initialization;
  each H3 connection installs its immutable seed at most once after SETTINGS.
- H3 message construction performs one emission pass only, invokes each
  implemented declared-key provider once per key and the non-template runtime
  provider once per message unconditionally, and does not mutate or allocate
  dynamic-table state.
- H2 message construction likewise performs one HPACK emission pass only; it
  does not size-encode or regenerate fields before framing them.  Only fixed
  pairs in the first committed message may populate the peer's HPACK table,
  using prebuilt bytes and an atomically published prebuilt local snapshot;
  sending never incrementally mutates the local table, and subsequent message
  fields never change it.
- H1 implements the same provider cardinality and remains implicitly
  emission-only through direct textual output.
- `HeaderPatches` and all placeholder-specific dispatch are gone.  Fixed-body
  mutation uses one compile-time-sized span array, one `bodyHdrs` provider call
  with repeated keyed callbacks, and no allocation or value-type branching.
- Fixed compile-time pairs from application `Headers` are eligible for H2 and
  H3 dynamic tables when absent from the corresponding static table.  H3 may
  additionally seed a declared runtime-valued key as `(Key, "")` when the name
  has no QPACK static name index.  Runtime callback values and runtime-named
  fields never seed either table.
- H1/H2/H3 message builders cannot declare or emit trailers, while all three
  parsers continue to accept peer trailers.
- The full build and test suite pass without warnings.
