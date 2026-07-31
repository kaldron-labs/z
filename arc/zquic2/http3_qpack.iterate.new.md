# QPACK Completion Plan

## Goal and Constraints

Complete the remaining work from `http3_qpack.iterate.md` so the QPACK changes
are production-shaped and aligned with `iterate_implementation.md` and
`AGENTS.md`.

Hard constraints:

- Do not use C++ concepts or `requires`; use existing `ZuIfT`/SFINAE patterns.
- Do not specify `ZmNoLock` where it is already the default.
- Do not add a separate runtime QPACK static lookup table; use `QPackTbl`,
  `QPackKV`, `ZuUnroll`, and `ZuSwitch` as needed.
- Raw `push()` intentionally returns uninitialized storage. Construct elements
  directly with placement new, e.g. `new (array.push()) T(...)`.
- Avoid temporary objects and copy-through buffers on the hot path.
- Keep indentation and local style aligned with `AGENTS.md`.

## Current Implemented Baseline

These items should be preserved, not reworked unless a later task requires a
small correction:

- QPACK encoder and decoder stream parsing are split into separate
  `QPackInsnParser` instances.
- QPACK instruction parsing has offset-based buffering and direct-span fast
  paths.
- `QPackDecodedInstruction` owns Huffman decoded instruction storage, avoiding
  dangling spans.
- Field-section decode is centralized in `QPack::decodeFieldSection()`.
- Static table helpers use the compile-time `QPackTbl` source of truth; indexed
  lookup uses `ZuSwitch`.
- `Params` policy name lists use `QPackNameList`; `SettingsKeys` is a named
  alias.
- Builder output is plan/emit/commit shaped and no longer buffers the complete
  field section in a heap-backed `HeaderBytes`.
- Tx exact lookup remains a `ZmLHash`, and routine absolute-index lookup uses
  order metadata rather than hash iteration.
- Outstanding Tx sections are keyed by stream ID in `ZmLHash`.
- `ZuTypeIndex` handles nested typelists and has regression coverage.

## Remaining Findings and Required Work

### 1. Make Builder Commit Failure-Atomic Against Stream Acceptance

Current state:

- Table mutation is deferred until after encoder bytes and HEADERS bytes are
  emitted.
- `QPackEncoderTx::write()` and stream `flush()` return `void`, so the code
  cannot prove bytes were accepted before committing Tx table state.

Plan:

- Introduce an acceptance-aware write path for QPACK encoder stream output and
  HEADERS flush without disrupting existing call sites.
- Preferred shape:
  - add a small boolean-returning adapter/helper used by the builder;
  - keep the old `void` callback usable by wrapping it as always accepted;
  - allow tests to inject failure at each commit boundary.
- Commit `QPackTxTable::setCapacity()`, `capacitySent`, inserts, and section
  reference tracking only after all required write/flush steps report success.
- Do not mutate Tx state after a partial accepted write unless the emitted bytes
  are guaranteed to be visible to the peer and the mutation exactly matches
  them.

Tests:

- Fail encoder capacity write: `capacity()`, `capacitySent`, `insertCount()`,
  `knownReceivedCount()`, hash, and refs unchanged.
- Fail encoder insert write after capacity planning: no insert committed; if
  capacity bytes were not accepted, capacity remains unchanged.
- Fail HEADERS frame header write: no Tx state mutation.
- Fail HEADERS payload emit or flush: no Tx state mutation.
- Successful dynamic build still commits exactly once.

### 2. Complete Field Representation Golden Coverage

Current state:

- Shared decode helper exists.
- Tests cover static fields, dynamic relative/post-base cases, instruction
  parsing, and builder query path.

Plan:

- Add golden decode tests for every QPACK field-line representation:
  - indexed static;
  - indexed dynamic relative;
  - indexed dynamic post-base;
  - literal with static name reference;
  - literal with dynamic name reference;
  - literal with post-base name reference;
  - literal with literal name;
  - Huffman name/value variants;
  - never-index variants.
- Exercise both public helper wrappers and `Parser::parseFields_()` callback
  integration where practical.
- Verify `QPackFieldFlags` values for static/dynamic/post-base/never-index.
- Keep semantic HTTP validation in parser tests separate from wire-format
  decode tests.

Tests:

- Helper-level golden tests in `ZhttpQPackDynamicTest`.
- Parser callback tests for pseudo-fields, regular headers, trailers, and
  invalid representation failures.

### 3. Finish Fixed-Bound Replacement Tests

Current state:

- `m_settingsKeys` is a `SettingsKeys` alias and no longer a fixed 32-element
  array.
- `Params` policy names no longer silently stop at 32 entries.

Plan:

- Add parser-level tests that build a SETTINGS frame with more than 32
  parameters and a duplicate key after the historical 32-key boundary.
- Verify duplicate detection still fails after arbitrary preceding unique keys.
- Keep `SettingsKeys` storage explicit with the existing heap ID unless a local
  inline-storage pattern already exists and is simpler.

Tests:

- More than 32 unique SETTINGS are accepted if otherwise valid.
- Duplicate SETTINGS key after 32 unique keys is rejected.
- Policy lists with more than 32 names continue to honor all entries.

### 4. Remove Remaining Front-Splice Hot Paths

Current state:

- Rx and Tx tables have helper boundaries.
- Both still use `order.splice(0, 1)` or `entries.splice(0, 1)` for oldest
  removal.

Plan:

- Replace `QPackRxTable::entries` front-splice eviction with a ring or
  head-offset representation.
- Replace `QPackTxTable::order` front-splice eviction with a ring or head-offset
  representation.
- Preserve absolute-index math and keep lookups O(1) by absolute index.
- Keep storage compact and connection-affine; avoid adding locks.
- Make compaction explicit and rare if using a head-offset array instead of a
  ring.

Tests:

- Rx churn test: repeated insert/evict keeps `baseAbs`, `insertCount`, `used`,
  and lookup semantics correct.
- Tx churn test: repeated insert/evict preserves exact lookup, absolute lookup,
  and used/capacity accounting.
- Referenced-entry eviction test continues to pass.

### 5. Strengthen Tx Section Tracking

Current state:

- Outstanding sections are stored in `QPackTxSections`.
- Ref release uses `findAbs()` and no longer scans the hash.

Plan:

- Add stress coverage for many outstanding sections, out-of-order acknowledgments,
  and cancellation.
- Decide whether duplicate tracking for the same stream ID should replace,
  reject, or merge. Implement and document that behavior.
- Ensure failure to add a section cannot leave refcounts partially incremented.
- If `sections.add()` fails, leave all refcounts unchanged and report failure
  to callers; consider changing `trackSection()` to return `bool`.

Tests:

- Many sections tracked and acknowledged out of order.
- Duplicate stream ID behavior is deterministic.
- Section add failure path leaves refcounts unchanged if failure injection is
  practical.

### 6. Finish Allocation Discipline

Current state:

- Field-section decode reuses `ZtScratch` scratch outside the loop.
- Builder uses local scratch for prefix and encoder instructions.
- Instruction decode owns Huffman storage in the returned instruction object.

Plan:

- Add heap telemetry or allocator-count tests where existing `ZmHeapMgr` /
  `ZmVHeap` hooks make this practical.
- If telemetry is not locally usable, add structural tests and code comments
  identifying the intended non-allocating paths and the remaining owned-storage
  fallbacks.
- Consider a scratch/callback form for instruction decode to avoid
  `QPackDecodedInstruction` heap-backed storage in rare Huffman paths when the
  caller can consume synchronously.
- Keep raw non-Huffman spans zero-copy into the input buffer.

Tests:

- Decode many Huffman literals and verify scratch reuse or no per-field heap
  allocation where measurable.
- Decode many non-Huffman literals and verify returned spans remain input-backed
  for helper-synchronous consumption.

### 7. Replace Virtual QPACK Encoder Tx Where It Matters

Current state:

- `QPackEncoderTx` remains virtual.
- QPACK table state is documented as connection-affine.

Plan:

- Audit current call sites to decide whether virtual dispatch is on a hot
  enough path to replace now.
- If replacing, prefer a CRTP/detected hook or concrete callback stored in the
  builder path, matching nearby `Zhttp` CRTP style.
- Preserve test capture ergonomics.
- Do not broaden ownership or introduce synchronization unless the connection
  execution model requires it.

Tests:

- Existing builder capture tests continue to pass.
- Any new callback/hook path supports failure injection from item 1.

### 8. Add Internal Failure Reasons

Current state:

- Builder logs are more specific than before.
- There is no internal reason code/counter exposed for negative tests.

Plan:

- Add a compact enum for builder/QPACK emit failure reason, stored on local
  builder state or exposed through a test-only/callback-visible path.
- Keep formatting/logging off the success path.
- Set reasons for:
  - planning failure;
  - prefix encode failure;
  - capacity policy failure;
  - encoder capacity write failure;
  - encoder insert write failure;
  - HEADERS frame header failure;
  - HEADERS payload emit failure;
  - flush failure;
  - capacity commit failure;
  - insert commit failure;
  - section tracking failure.
- Keep public API impact minimal.

Tests:

- Negative builder tests assert exact reason codes.
- Existing success-path behavior remains unchanged.

### 9. Stabilize HTTP/3 Interop Verification

Current state:

- `ZhttpQPackTest`, `ZhttpQPackDynamicTest`, `ZhttpFallbackTest`, `ZuTLTest`,
  and full `make -j` have passed.
- `Zhttp3InteropTest` is currently flaky in HTTP/3 timeout cases involving
  curl/Caddy/local UDP interaction.

Plan:

- Reproduce `Zhttp3InteropTest` failures in isolation with repeated runs.
- Determine whether failure is environmental timing, unrelated QUIC behavior,
  or caused by these QPACK changes.
- Capture relevant logs around the failing subtest without adding noisy
  success-path logs.
- If QPACK-related, fix before marking this work complete.
- If environmental, document the instability and keep deterministic QPACK tests
  as the gate for QPACK-specific changes.

Tests:

- Run `Zhttp3InteropTest` repeatedly enough to classify stability.
- Keep `ZhttpFallbackTest` as a separate fallback gate.

## Revised Implementation Sequence

### Phase 1: Close Correctness/Test Gaps

1. Add SETTINGS duplicate-after-32 and policy >32 tests.
2. Add full field representation golden tests at helper level.
3. Add parser callback coverage for representative static, dynamic, literal,
   and trailer paths.
4. Add Tx section stress/out-of-order ack tests.

### Phase 2: Failure Atomicity

1. Add acceptance-aware write/flush abstraction for builder tests.
2. Change builder commit flow to commit only after accepted writes.
3. Add builder failure reason enum/state.
4. Add failure injection tests for every commit boundary.

### Phase 3: Table Churn Performance

1. Replace Rx front-splice with ring/head-offset storage.
2. Replace Tx order front-splice with ring/head-offset storage.
3. Keep absolute lookup and exact lookup semantics intact.
4. Add Rx/Tx churn tests.

### Phase 4: Allocation and Interface Cleanup

1. Add allocation telemetry tests where practical.
2. Tighten instruction decode scratch/callback shape if it removes meaningful
   heap work without complicating ownership.
3. Replace virtual encoder Tx only if the new acceptance-aware path gives a
   clean CRTP/callback route.

### Phase 5: Verification and Interop

1. Run QPACK and parser-focused tests.
2. Run repeated HTTP/3 interop tests and classify any remaining timeout.
3. Run full `make -j`.
4. Do a final pattern scan for prohibited constructs and stale runtime static
   table artifacts.

## Required Verification Commands

Run after each risky phase:

```sh
./zhttp/test/ZhttpQPackTest
./zhttp/test/ZhttpQPackDynamicTest
./zhttp/test/ZhttpFallbackTest
./zu/test/ZuTLTest
```

Run before final completion:

```sh
./zhttp/test/Zhttp3InteropTest
make -j
rg -n "qpackStaticTbl_|QPackStaticEntry|ZHTTP_QPACK_STATIC_TUPLES|requires\\s*\\(|concept\\s+|ZmNoLock|m_qpackBytes|HeaderBytes body|HeaderBytes encoder|HeaderBytes insn" \
  AGENTS.md \
  zhttp/src/Zhttp.hh \
  zhttp/src/ZhttpQPack.hh \
  zhttp/src/ZhttpQPackTypes.hh \
  zhttp/src/ZhttpQPack.cc \
  zhttp/test/ZhttpQPackDynamicTest.cc \
  zhttp/test/ZhttpQPackTest.cc
```

For interop stability:

```sh
for i in 1 2 3 4 5; do ./zhttp/test/Zhttp3InteropTest || break; done
```

## Completion Criteria

This work is complete only when:

- All original 15 findings are either implemented or explicitly documented as
  intentionally deferred with a technical reason.
- Builder Tx state cannot advance on any simulated unaccepted write/flush path.
- Field decode has one implementation path and full representation coverage.
- Fixed historical 32-entry bounds are covered by tests.
- Rx/Tx oldest-entry eviction no longer relies on hot front-splice.
- Tx section tracking has stress coverage and deterministic duplicate behavior.
- Allocation-sensitive paths have telemetry-backed tests or clear structural
  proof where telemetry is unavailable.
- QPACK/static-table code has no separate runtime static lookup array.
- Touched code contains no C++ concepts/`requires` and no explicit `ZmNoLock`.
- `ZhttpQPackTest`, `ZhttpQPackDynamicTest`, `ZhttpFallbackTest`, `ZuTLTest`,
  `Zhttp3InteropTest`, and `make -j` pass, or any interop instability is
  conclusively classified as external to this QPACK work.
