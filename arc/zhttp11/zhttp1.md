# QPACK bounded-allocation and no-resize plan

## Objective

Make QPACK table storage deterministic and latency-safe:

- compile-time static-table lookup continues to use `ZuMatcher`;
- dynamic exact-field lookup continues to use a cache-friendly `ZmLHash`;
- all dynamic QPACK storage is bounded by trusted local configuration;
- peer SETTINGS can reduce what the local encoder may use but can never cause
  allocation;
- entry and section hashes are allocated and sized once, before use, and never
  resize on an active header path;
- the default zero-capacity configuration allocates no dynamic QPACK tables;
- Rx and Tx table state remains strictly shard-owned; and
- shutdown follows the existing asynchronous Rx-drain, Tx-drain, release
  sequence.

This plan fixes the current QPACK allocation and sizing model.  It does not add
HPACK dynamic encoding or QPACK dynamic name-only lookup; those are separate
compression-policy changes.  The storage APIs established here must permit a
future secondary name index to be pre-sized from the same local entry bound.

## Current code and defects

The relevant implementation is:

- `ZhttpStaticTable.hh`: compile-time static name and name/value matching;
- `ZhttpQPack.hh` and `ZhttpQPack.cc`: Rx/Tx dynamic tables;
- `ZhttpH3.hh`: field-section planning, encoding, and section tracking;
- `ZhttpH3Session.hh`: local SETTINGS and peer SETTINGS handoff;
- `ZhttpH3Engine.hh`: per-connection Rx/Tx QPACK ownership; and
- `ZhttpConfig.hh`: current H3 configuration is carried by `QUICConfig`.

The current defects are:

1. `QPackTxTable` constructs a 128-slot entry hash and a 64-slot section hash
   for every connection, even when dynamic QPACK is disabled.
2. Both hashes are dynamically resizable `ZmLHash` instances.  Crossing their
   initial load limit allocates a doubled table and rehashes every live entry
   synchronously on the Tx shard.
3. `QPackTxTable::setMaxCapacity()` reserves `capacity >> 5` ordered entries
   from the peer-advertised `SETTINGS_QPACK_MAX_TABLE_CAPACITY`.  A peer can
   therefore cause a large allocation independently of local policy.
4. The ordered Tx array is pre-sized from that peer value, while the exact
   lookup hash is not pre-sized from it at all.
5. The current `H3::Params::qpackTableCapacity()` name conflates the locally
   advertised decoder capacity with the local encoder's desired capacity.
6. `QPackRxTable::maxCapacityBytes_` is set directly only by tests; production
   connection setup does not initialize it from local SETTINGS.
7. The section hash has no explicit local bound.  Peer
   `SETTINGS_QPACK_BLOCKED_STREAMS` is not that bound: the current encoder only
   references known-received entries, so its sections are not blocked, and
   acknowledged-reference tracking can still span many concurrent streams.
8. `QPackTxHash` and `QPackTxSections` are connection-affine but use globally
   registered `ZmLHash` instances rather than local, no-lock hashes.

## Target model

### Separate trusted limits from peer permissions

Define unambiguous local H3/QPACK configuration:

- `qpackRxCapacity`: bytes advertised in
  `SETTINGS_QPACK_MAX_TABLE_CAPACITY`; it bounds and pre-sizes the local
  decoder table.
- `qpackTxCapacity`: locally permitted encoder-table bytes; it bounds and
  pre-sizes the local Tx order and exact-field hash.
- `qpackRxBlocked`: value advertised in
  `SETTINGS_QPACK_BLOCKED_STREAMS`.
- `qpackTxSections`: local maximum number of outstanding encoded field
  sections whose dynamic references may require acknowledgement.

Use these names consistently in `QUICConfig`, `H3::Params`, connection setup,
tests, and diagnostics.  Remove the ambiguous `qpackTableCapacity` and
`qpackBlockedStreams` names rather than retaining compatibility forwarders.

The peer supplies:

- `peerTxCapacity` from its `SETTINGS_QPACK_MAX_TABLE_CAPACITY`; and
- `peerBlocked` from its `SETTINGS_QPACK_BLOCKED_STREAMS`.

The encoder may use:

```text
effectiveTxCapacity = min(qpackTxCapacity, peerTxCapacity)
```

`peerBlocked` is retained for protocol correctness and any future blocking
policy, but it must not size the section table.  The current known-received-only
encoder continues to create zero blocked streams.

### Allocation bounds

Use the RFC entry-size minimum of 32 bytes:

```text
maxRxEntries = qpackRxCapacity >> 5
maxTxEntries = qpackTxCapacity >> 5
```

All conversions and power-of-two rounding must be overflow-checked before
allocation.  Local configuration validation must reject values which cannot be
represented by the selected Z containers or which exceed an explicit
repository-appropriate maximum.  Do not silently clamp a requested local
limit.

Peer values are validated for protocol representation and stored as scalars.
They do not call `ensure()`, construct a hash, resize a hash, or otherwise
allocate.

### Storage shape

- `QPackRxTable` owns one oldest-to-newest array, pre-sized to
  `maxRxEntries` on the Rx shard.
- `QPackTxTable` owns one oldest-to-newest array and one exact name/value
  `ZmLHash`, both pre-sized to `maxTxEntries` on the Tx shard.
- The exact hash uses `ZmLHashLocal<>` with `ZmNoLock`; connection-affine
  state must not be registered globally.
- The section map uses a separate `ZmLHashLocal<>`, pre-sized to
  `qpackTxSections`.
- Zero bounds leave the corresponding arrays/hashes unallocated.
- No active insertion, eviction, compaction, lookup, acknowledgement, or
  cancellation path may grow or reconstruct storage.
- Compaction may move ordered entries only within existing capacity and must
  rebuild borrowed-span hash keys in place without resizing.

The table owns all strings.  Hash keys remain borrowed spans into the ordered
entries.  Any operation which can relocate an ordered entry must rebuild the
hash before subsequent lookup.

### Section admission

Field-section planning must determine whether the stream ID can be tracked
before emitting any dynamic reference:

- an existing legal section entry may be updated only if QPACK permits that
  state transition;
- a new entry is admissible only below `qpackTxSections`;
- when tracking is unavailable, the complete field section falls back to
  static and literal representations and has a zero required insert count;
- `trackSection()` after emission becomes an assertion-backed commit of an
  already-admitted plan, not a potentially allocating or recoverable late
  failure; and
- the fallback must not partially mix untracked dynamic references into the
  section.

Do not use a post-emission reset or connection error to compensate for failed
local section admission.

## Delivery slices

### Slice 1 — Freeze the failure modes with focused tests

Extend `ZhttpQPackDynamicTest` before changing production behavior.

Add tests proving the current intended contract:

- zero local Rx/Tx/section limits produce no dynamic storage;
- a huge peer table-capacity setting changes only a scalar permission and
  performs no reservation;
- local capacity values around 31/32 bytes and around hash power-of-two
  boundaries produce the exact entry bounds;
- inserting, evicting, compacting, and reinserting up to the configured bound
  leaves `ZmLHash::resized() == 0`;
- temporary `ZuCSpan` name/value keys still find owned dynamic entries;
- section admission at, below, and above its configured bound is
  deterministic;
- a section denied tracking contains no dynamic reference;
- peer capacity reduction never reallocates and prevents subsequent use above
  the new effective limit; and
- repeated init/final cycles leave no table state or stale borrowed spans.

Tests should inspect stable capacity/resized counters, not wall-clock timing.
Add narrow read-only accessors where necessary; do not expose mutable
containers merely for testing.

#### Acceptance criteria

- Each defect listed above has a focused test which fails against the current
  implementation for the expected reason.
- Existing static-table, dynamic decode, eviction, section-ack, and builder
  tests remain source-compatible until the production slice changes their API.
- The test fixture distinguishes local limits, peer permissions, effective
  capacity, allocated slots, and live-entry count.

#### `GUIDELINES.md` alignment audit and repair

Audit the new tests for Z container use, bounded stack storage, hard tabs,
concise names, and absence of timing-dependent assertions.  Repair every
deviation before handing off to Slice 2.

### Slice 2 — Normalize QPACK configuration and capacity semantics

In `ZhttpConfig.hh` and `ZhttpQPack.hh`:

1. Replace ambiguous QPACK configuration with
   `qpackRxCapacity`, `qpackTxCapacity`, `qpackRxBlocked`, and
   `qpackTxSections`.
2. Give all four values explicit conservative defaults.  Dynamic table
   capacities default to zero.  A zero Tx section limit disables dynamic
   references.
3. Validate local limits once during engine initialization.
4. Introduce a small value-type limits snapshot passed into each H3
   connection; do not let individual message builders choose larger
   connection-level storage.
5. Builders may apply stricter per-message indexing policy, but their
   capacity request is bounded by the immutable connection snapshot.
6. Parse both peer SETTINGS `0x01` and `0x07`.  Post their scalar values to
   the Tx shard without allocating on Rx.
7. Remove direct public mutation of `maxCapacityBytes_` from tests and replace
   it with the normalized initialization API.

Keep Rx and Tx meanings explicit: local Rx settings configure the decoder and
are sent to the peer; local Tx limits bound the encoder and are intersected
with peer settings.

#### Acceptance criteria

- No identifier named only `qpackTableCapacity` or `qpackBlockedStreams`
  remains.
- Local configuration alone determines allocation ceilings.
- Peer SETTINGS cannot increase either local ceiling.
- SETTINGS `0x01` and `0x07` are parsed, range-checked, and transferred to Tx
  as scalar snapshots.
- Invalid local configuration fails `init()` before an engine starts.
- Existing zero-capacity interoperability remains unchanged.

#### `GUIDELINES.md` alignment audit and repair

Audit configuration names against the repository's 28-byte naming limit,
verify overloaded getter/setter style, remove compatibility shims, and verify
that cross-shard SETTINGS handoff captures only fixed-size values.  Repair all
findings before Slice 3.

### Slice 3 — Pre-size and initialize the Rx table

Give `QPackRxTable` explicit Rx-shard lifecycle functions:

- initialize once from `qpackRxCapacity`;
- pre-size its ordered array to `maxRxEntries`;
- reject a peer encoder capacity above the advertised local maximum;
- keep insert/duplicate/evict/compact within the original allocation; and
- release storage only after Rx work is disabled and drained.

Initialization occurs during H3 connection setup before local SETTINGS permit
the peer to send dynamic instructions.  Do not lazily allocate on the encoder
instruction receive path.

#### Acceptance criteria

- Production connection setup, not test-only member access, initializes the Rx
  maximum.
- With zero capacity, peer `SetCapacity(0)` works and any insertion fails
  without allocating.
- At non-zero capacity, insertion/duplicate/eviction churn never increases
  array capacity.
- A peer capacity above the advertised limit produces the correct QPACK
  connection error without changing allocation.
- Rx state is touched only on the Rx shard.

#### `GUIDELINES.md` alignment audit and repair

Audit Rx ownership, initialization-before-advertisement ordering, in-place
array use, overflow handling, and teardown.  Repair all findings before
Slice 4.

### Slice 4 — Pre-size the Tx order and exact-field hash

Refactor `QPackTxTable` so construction is allocation-free.  Add explicit
Tx-shard initialization from the trusted local Tx limits:

1. Compute `maxTxEntries` safely.
2. Pre-size the ordered array once.
3. Construct a local/no-lock exact-field `ZmLHash` with enough slots for every
   possible live entry at its configured load factor.
4. Store peer maximum and effective capacity separately.
5. Make peer-capacity updates allocation-free and non-growing.
6. Make `setCapacity()` reject values above the effective maximum.
7. Make `insert()` prove space from the byte and entry bounds before touching
   either container.
8. Keep hash insertion/deletion and ordered insertion/eviction atomic with
   respect to failure.
9. Rebuild hash keys after ordered compaction without changing hash bits or
   allocation.
10. Assert in debug builds that `resized() == 0` and that live counts never
    exceed configured slots.

Do not change `ZmLHash` globally.  Correct its use locally by constructing it
at terminal capacity.

#### Acceptance criteria

- A default H3 connection allocates no QPACK Tx order or exact hash.
- Peer maximum values, including `uint32_t(-1)`, perform no allocation.
- Configurations above the former 128-entry threshold complete churn tests
  with zero hash resizes.
- Hash size and ordered-array capacity remain constant from Tx initialization
  through finalization.
- Exact lookup, duplicate insertion, eviction, referenced-entry protection,
  and compaction remain correct.
- No allocation occurs in `find`, `insert`, `dropOldest`, `compactOrder`, or
  `rebuildHash`.

#### `GUIDELINES.md` alignment audit and repair

Audit Tx-only ownership, local/no-lock hash policy, borrowed-span lifetime,
failure atomicity, and absence of hidden copies or per-entry allocation.
Repair all findings before Slice 5.

### Slice 5 — Bound section tracking and make fallback pre-emission

Replace the always-allocated 64-slot section hash with an allocation-free
default and explicit Tx initialization:

1. Pre-size a local/no-lock section hash from `qpackTxSections`.
2. Add a planning query which determines whether the current stream can be
   tracked without mutation or allocation.
3. If it cannot, disable dynamic references for the entire field section
   before count/prefix encoding.
4. Commit the admitted section only after successful encoder-stream and
   HEADERS writes.
5. Keep ack/cancellation deletion allocation-free.
6. Define and test the legal policy for multiple outstanding field sections
   on one stream; do not silently overwrite or reject after emission.
7. Keep `peerBlocked` separate.  Do not use it as the section-map allocation
   bound.

#### Acceptance criteria

- Section tracking is unallocated when its local bound is zero.
- Reaching the section bound causes literal/static fallback, not hash resize,
  late commit failure, stream reset, or connection failure.
- The section hash reports zero resizes through admission/ack/cancellation
  churn.
- Required insert count is zero whenever section tracking was denied.
- Reference counts return exactly to zero after out-of-order acknowledgements
  and cancellations.
- One stream cannot corrupt another stream's tracked references.

#### `GUIDELINES.md` alignment audit and repair

Audit the planner/commit split, ensure there is no compensating error path for
bad admission, verify no active-path allocation, and check that the Tx shard
owns all section state.  Repair every issue before Slice 6.

### Slice 6 — Integrate asynchronous connection lifecycle

Thread the normalized limits through both H3 client and server engines.

Connection setup must:

1. validate and snapshot local QPACK limits;
2. initialize Rx storage on Rx;
3. post Tx initialization to the Tx shard;
4. continue H3 readiness only from the Tx initialization continuation; and
5. advertise local SETTINGS only after the corresponding Rx storage is ready.

Peer SETTINGS handling remains:

```text
Rx parse
  -> snapshot fixed-size peer values
  -> txRun
  -> update scalar peer permission
```

Connection teardown must:

1. prevent new H3/QPACK work;
2. drain Rx and finalize Rx-owned QPACK state;
3. post the Tx continuation;
4. drain Tx, finalize Tx hashes/arrays, and reject late instructions; then
5. continue normal connection release.

Do not block, poll, infer completion from `run()`, or finalize Tx-owned storage
from Rx/destructors.

#### Acceptance criteria

- Client and server use the same normalized initialization path.
- No request/header builder can run before Tx QPACK initialization completes.
- Peer SETTINGS received before, during, or after readiness are ordered on Tx
  without races or allocation.
- Stop during setup and stop with active sections both complete through
  continuations without leaks or stale callbacks.
- Reconnect creates fresh bounded tables and does not retain peer limits.
- Existing H3 FIN/reset/STOP, stream isolation, and engine lifecycle tests
  remain green.

#### `GUIDELINES.md` alignment audit and repair

Perform a shard-by-shard ownership audit and trace setup and teardown
continuations linearly.  Repair any cross-shard direct access, blocking call,
owner-retaining callback, or destructor-driven cleanup before Slice 7.

### Slice 7 — Regression, allocation, and interoperability closure

Minimize rebuilding:

1. Keep the current configured toolchain and build mode; do not reconfigure.
2. Make all source edits before the first rebuild where practical.
3. Rebuild `zhttp/src` once with `make -C zhttp/src -j3`.
4. Rebuild only affected QPACK/H3 tests, then run:
   - `ZhttpQPackTest`;
   - `ZhttpQPackDynamicTest`;
   - `ZhttpH3EngineTest`;
   - `ZhttpTransportContractTest`; and
   - `Zhttp3InteropTest`.
5. Run the complete `zhttp/test` regression target once after focused tests
   pass.

Add deterministic allocation evidence around direct table operations:

- zero-capacity init/final;
- locally bounded large peer SETTINGS;
- entry churn beyond the old 128-entry boundary;
- section churn beyond the old 64-entry boundary; and
- repeated connection setup/teardown.

The evidence must show stable container capacity and zero `ZmLHash` resizes.
Do not use throughput as a proxy for absence of latency spikes.

Use the repository regression-debugging order if a regression appears:
inspect the code diff first, then gdb, then Valgrind.  Do not introduce a new
ASAN build unless the completed normal regression run exposes memory
corruption that the earlier steps cannot diagnose.

#### Acceptance criteria

- All focused and full `zhttp` regressions pass in the current build.
- Dynamic QPACK interoperability still passes when explicitly enabled.
- Default zero-capacity H3 behavior remains interoperable and allocation-free
  for dynamic QPACK state.
- No peer-provided capacity causes allocation above trusted local limits.
- No entry or section hash resize occurs after connection readiness.
- Repeated lifecycle runs are leak-free under Valgrind if lifecycle evidence
  requires it.
- `git diff --check -- zhttp` is clean.

#### Final `GUIDELINES.md` alignment audit and repair

Before handoff:

- re-read the performance, container, sharding, continuation, lifetime, and
  teardown sections of `GUIDELINES.md`;
- verify hard tabs and local layout;
- verify all names remain within the repository limit;
- confirm local configuration is the sole allocation authority;
- confirm peer SETTINGS cause scalar state changes only;
- confirm active QPACK paths contain no heap allocation, array growth, hash
  resize, lock, blocking operation, or avoidable copy;
- confirm Rx and Tx state are initialized/finalized on their owning shards;
- confirm teardown is disable -> Rx drain -> Tx drain -> release; and
- repair every discrepancy before declaring the plan complete.

## Completion definition

The work is complete only when a reviewer can verify all of the following
directly from code and tests:

1. `ZuMatcher` serves only compile-time static tables.
2. Dynamic exact-field lookup uses a pre-sized, local/no-lock hash.
3. Local configuration fixes all allocation ceilings before connection
   readiness.
4. Peer SETTINGS never allocate or grow storage.
5. Rx order, Tx order, entry hash, and section hash retain constant capacity
   during active use.
6. Entry and section hashes report zero resizes.
7. Capacity exhaustion falls back or fails before wire emission, never through
   compensating teardown.
8. Default zero-capacity connections allocate no dynamic QPACK state.
9. Client/server setup and teardown obey shard ownership and continuation
   ordering.
10. Focused, full-regression, lifecycle, and interoperability evidence is
    green.
