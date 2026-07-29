# zquic container and lookup remediation plan

## Goal

Remove the primitive-array, hard-cap, and linear-lookup defects found in the
`zquic` audit while preserving:

- QUIC wire behavior;
- Rx/Tx shard ownership;
- existing dependent source APIs and their error semantics;
- the existing clang debug configuration;
- intentional packet-buffer and zero-copy behavior; and
- no-allocation operation for the common hot-path case.

There is no HPACK-style string-to-index defect in `zquic`.  Use `ZuMatcher`
only if a fixed set of multiple strings is found to map to values.  Continue to
use `==` for one literal, and ordinary iteration for dynamic peer or external
library lists.  Use `ZuSwitch`, not a primitive lookup table, for static
integral dispatch.

## Mandatory execution rules

### Linear slices

Implement the slices strictly in order.  A slice starts only after its
predecessor meets every acceptance criterion, is reviewed, and is committed.
Do not develop later slices in parallel and do not leave temporary compatibility
code for a later slice to remove.

### Dependent API preservation

Before changing a declaration in any installed header listed by
`zquic/src/Makefile.am`, perform a read-only caller search across the repository.
This search does not authorize building a dependent layer.

Preserve:

- public type and function names;
- callable signatures and overload resolution;
- callback signatures;
- accessor return types;
- documented constants;
- success/failure and diagnostic behavior; and
- public value semantics.

An internal representation may change only when dependent code cannot observe
it.  Do not add an implicit compatibility shim merely to conceal a better
breaking design.  If the best implementation would change a dependent API,
layout relied upon by a dependent, callback contract, or public error behavior:

1. stop the slice before editing that contract;
2. identify the exact declaration and known callers;
3. explain the performance/correctness benefit and migration required; and
4. ask the user whether to authorize the breaking change.

Resume only after receiving that decision.  In particular, apply this gate to
the public members of `InitialSecret`/`TrafficSecret`, the `PathChallenge` and
`ResetToken` interfaces, and the `sendCryptoFlights` callback boundary.

### Z framework choices

Use the following consistently:

- `ZuBArray<N>` for bounded byte values;
- `ZuArray<T, N>` for bounded metadata with a compile-time policy capacity;
- `ZuBSpan`/`ZuSpan` at non-owning call boundaries;
- `ZtBuiltin<ZtArray<T, ZtArrayHeapID<"...">>, N>` for hot arrays whose common
  case is bounded but whose valid workload may grow;
- plain `ZtArray<T, ZtArrayHeapID<"...">>` for cold retained arrays;
- `ZtLocalArray` for run-time-sized hot scratch, with its intentional
  `ZmAlloc` stack-first/heap-fallback behavior;
- `ZtArray::push`, `length`, `clear`, and one `splice` for dynamic array
  mutation rather than repeated manual shifts;
- `ZuFmt`/`ZuPrint` for formatting;
- `ZuAssert`, `ZmAssert`, or `ZiAssert` according to the owning subsystem;
- `ZmHeapID`/`ZtArrayHeapID` on every new heap-capable path; and
- existing `ZmRef<ZiIOBuf>` role-specific packet/stream/crypto buffers without
  adding payload copies.

For integral constants that fit in `int`, use the repository enum convention
unless preserving an existing public constant requires retaining its exact
declaration.  Give every capacity a maintenance comment identifying it as a
wire-policy limit, a per-packet invariant, or a measured built-in tuning value.

Avoid default initialization before overwrite.  For non-primitive values
appended to Z arrays, use `push(value)` or placement construction into `push()`;
for primitive scratch, request uninitialized storage and fill it before read.

### Minimal build and test policy

The existing generated Makefiles are already clang debug:

- `CXX = clang++`
- `CXXFLAGS` contains `-g -DZDEBUG`

Before Slice 1, verify those values in `zquic/src/Makefile` and
`zquic/test/Makefile`.  If they are not present, stop and ask; do not rerun
`z.config`, `configure`, or `autoreconf`.

For an implementation slice:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8 <named-unit-test-targets>
./zquic/test/<NamedUnitTest>
```

Rules:

- Do not run top-level `make`, `make clean`, or `make test`.
- Do not rebuild `zu`, `zm`, `zt`, `ze`, `zf`, `zi`, or `ztls`; link against
  their existing clang-debug artifacts.
- Do not build or test `zhttp` or any other dependent layer.
- Between slices, build only `libZquic.la` and the named focused unit binaries.
- Do not run `make -C zquic/test test`, runtime loopback suites, handshake
  suites, socket suites, or full integration/interop tests between slices.
- Run the complete `zquic` test set only in the final slice.
- Do not reconfigure or rebuild with ASan, LSan, or another sanitizer.
- Use valgrind only to investigate an observed crash.  Run the already-built
  failing binary through libtool, for example:

```sh
./libtool --mode=execute valgrind --error-exitcode=1 \
  ./zquic/test/ZquicRecoveryTest
```

Valgrind is diagnostic, not a routine acceptance step.

## Slice 1: bounded protocol and crypto byte values

Dependency: none.

### API gate

Read-only search all non-`zquic` source for `ResetToken`, `PathChallenge`,
`InitialSecret`, and `TrafficSecret`.  Preserve the complete existing
`ResetToken` and `PathChallenge` public interfaces.  If converting the public
crypto member arrays is judged to alter a dependent contract, stop and ask
before changing those members.

### Implementation

1. In `ZquicTypes.hh`, replace `ResetToken::m_data` with
   `ZuBArray<Length>`.  Keep `Length`, constructors, `set`, `generate`, `valid`,
   `length`, `data`, `bspan`, equality, and failure behavior unchanged.
   Construct the array with logical length `Length`; validity remains in
   `m_valid`, not in the array length.
2. Apply the same representation to `PathChallenge::m_data` while preserving
   the type name and every public method.  Do not rename `PathChallenge`.
3. In `Zquic.cc`, generate directly into `m_data.data()` and expose
   `ZuBSpan{m_data.data(), valid ? Length : 0}`.  Parse into a temporary
   `ZuBArray<Length>` only when needed to guarantee that invalid input leaves
   the object unchanged; then move/assign once.
4. Define one module-internal path payload value:
   `using PathData = ZuBArray<PathChallenge::Length>`.  Initialize it with
   logical length `PathChallenge::Length`.  Slice 2 will use this type in
   control/recovery metadata; do not expose a new public callback type.
5. In `ZquicCrypto.hh`, express fixed initial-secret, key, IV, HP, salt, nonce,
   mask, and tag storage as `ZuBArray<N>`, each constructed to its full logical
   length.  Replace `sizeof(array)` call sites with the named protocol constant
   or `.length()` as appropriate.
6. Define a namespace-scope `SecretBytes<Max>` value rather than nesting it in a
   template.  It owns `ZuBArray<Max>`, sets the array's logical length to the
   active length, rejects `length > Max`, exposes `data()` and `bspan()`, and
   clears both bytes and active length from `TrafficSecret::clear()`.
7. Use `SecretBytes` for the variable-length `TrafficSecret` secret, key, IV,
   and HP values, removing separately writable length fields only if the API
   gate permits it.  Keep algorithm pointers, tag length, and installed state
   explicit.
8. Preserve the current `TrafficSecret::clear()` contract.  Do not claim
   stronger secure-erasure guarantees or introduce a new erasure API in this
   cleanup; that would be a separate security/API decision.
9. Convert fixed local crypto scratch to `ZuBArray<N>`.  Use `ZtLocalArray`
   only where scratch length is determined at run time, with an underlying
   `ZtArray<uint8_t, ZtArrayHeapID<"Zquic.Crypto.Scratch">>`.
10. Pass `.data()` and `.length()` only at picotls/OpenSSL C boundaries.  Do not
    copy merely to obtain a contiguous pointer.

### Focused build and unit tests

Build:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8 \
  ZquicCIDTest ZquicCryptoTest ZquicPacketProtectionTest
```

Run only:

```sh
./zquic/test/ZquicCIDTest
./zquic/test/ZquicCryptoTest
./zquic/test/ZquicPacketProtectionTest
```

Add unit cases for invalid-length non-modification, all-zero valid values,
maximum algorithm lengths, failed secret installation, and clear/reinstall.

### Acceptance and handoff to Slice 2

- The API gate has a written result; no dependent signature or value contract
  changed without authorization.
- `ResetToken` and `PathChallenge` retain their exact public interface and
  validity semantics.
- No primitive fixed-byte array remains in the converted protocol/crypto scope
  except a documented C ABI or approved dependent-API exception.
- No new heap allocation occurs for fixed crypto material.
- All three named unit binaries pass using the existing clang-debug build.
- `git diff --check` passes and Slice 1 is committed.

Only then start Slice 2.

### Slice 1 execution record

- API gate: no non-`zquic` repository source uses `ResetToken`,
  `PathChallenge`, `InitialSecret`, or `TrafficSecret`; the existing
  `ResetToken` and `PathChallenge` public methods were preserved.
- Build: `make -C zquic/src -j8 libZquic.la`.
- Focused unit build:
  `make -C zquic/test -j8 ZquicCIDTest ZquicCryptoTest
  ZquicPacketProtectionTest`.
- Focused unit run: `ZquicCIDTest`, `ZquicCryptoTest`, and
  `ZquicPacketProtectionTest` all passed under the existing clang-debug
  configuration.

## Slice 2: propagate values and snapshot TLS epoch metadata

Dependency: accepted and committed Slice 1.

### API gate

Preserve public frame callbacks and TLS callback signatures.  Search dependent
layers for `sendCryptoFlights` before editing its declaration.  If removing its
`const size_t offsets[5]` boundary would change a dependent callback contract,
retain that boundary as a documented ingress adapter.  If a signature break is
materially better than the adapter, stop and ask.

### Implementation

1. Replace `ControlFrame::payload[8]` and `SentFrameRef::payload[8]` with the
   Slice 1 `PathData` value.  Initialize its logical length once in the
   containing value's PATH factory; do not default-fill and copy byte-by-byte.
2. Make `pathChallenge` and `pathResponse` factories validate
   `data.length() == PathChallenge::Length`, then construct `PathData{data}`.
   Preserve the current invalid-input result.
3. Replace `memcmp` and manual loops with `PathData` equality and `bspan`/`data`
   access.  Frame type continues to distinguish challenge from response.
4. Replace local path response/challenge raw arrays in client, server, and base
   link code with `PathData` or the public `PathChallenge` value.  Capture the
   complete small value by copy across shards.
5. Define, at namespace scope:

   - a named TLS epoch count using the established enum convention; and
   - `using CryptoOffsets = ZuArray<size_t, TLSEpochCount + 1>`.

   Construct `CryptoOffsets` with full logical length.
6. Keep the existing external/picotls callback signature if required by the API
   gate.  At that single ingress, validate the supplied pointer, copy exactly
   five `size_t` values into `CryptoOffsets`, and immediately call a typed
   internal helper.  No other internal function accepts `offsets[5]`.
7. Change client, server, and base-link internal helpers to
   `const CryptoOffsets &`; capture one `CryptoOffsets` value in the Tx post.
   Delete five scalar captures and local array reconstruction.
8. Keep `CryptoStream` ownership unchanged.  Use
   `ZuSpan<CryptoStream>` internally if it removes array-reference syntax
   without changing the owning member or dependent API.
9. Validate monotonicity and `offset <= len` once before posting.  Preserve
   `LinkTxDiag::failures` increments and current return behavior.

### Focused build and unit tests

Build:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8 ZquicCodecTest ZquicCryptoTest
```

Run only:

```sh
./zquic/test/ZquicCodecTest
./zquic/test/ZquicCryptoTest
```

Extend these unit binaries with direct factory and crypto-flight helper cases;
do not run handshake, runtime, socket, stream, or API integration binaries yet.

### Acceptance and handoff to Slice 3

- Public callback signatures are unchanged, or the user explicitly approved
  the break before implementation.
- Only the documented external ingress may still mention `offsets[5]`.
- No production path metadata uses `uint8_t payload[8]`, manual eight-byte copy
  loops, or `memcmp`.
- TLS epoch offsets are validated once, captured as one value, and consumed on
  the Tx owner.
- Empty, one-epoch, and multi-epoch unit cases pass; descending and
  out-of-bounds offsets retain existing failure diagnostics.
- Both named unit binaries pass, `git diff --check` passes, and Slice 2 is
  committed.

Only then start Slice 3.

### Slice 2 execution record

- API gate: no non-`zquic` caller of `sendCryptoFlights` was found.  Its
  picotls-style five-offset signature remains as a compatibility ingress; all
  link-internal helpers consume `CryptoOffsets`.
- Path metadata: `ControlFrame`, `SentFrameRef`, and Rx-to-Tx path posts own
  `PathData` values.  Invalid PATH factories retain the prior full-length,
  all-zero result.
- TLS metadata: picotls output is copied once into `CryptoOffsets`; client and
  server validate it before posting, capture one value, and account an invalid
  input on the Tx owner without changing the asynchronous return contract.
- Build: `make -C zquic/src -j8 libZquic.la`.
- Focused unit build:
  `make -C zquic/test -j8 ZquicCodecTest ZquicCryptoTest`.
- Focused unit run: `ZquicCodecTest` and `ZquicCryptoTest` passed under the
  existing clang-debug configuration.  Coverage includes valid and invalid
  PATH factories plus empty, single-epoch, multi-epoch, descending, and
  out-of-bounds crypto offsets.

## Slice 3: make recovery frame accumulation lossless

Dependency: accepted and committed Slice 2.

### API gate

Search dependent layers for `SentPkt`, `SentFrameRef`, and `PktTxUpdate`.
Preserve `SentPktQueue` public methods and their return semantics.  Do not
change ACK/loss callback ordering.  If a dependent consumes the raw
`PktTxUpdate` arrays directly and the better typed representation would break
it, stop and ask.

### Implementation

1. Define a namespace-scope aggregate ordered to minimize padding:

```c++
struct SentFrameUpdate {
  SentFrameRef ref;
  void *owner = nullptr;
};
```

2. Replace `SentPkt`'s parallel frame/owner arrays with
   `ZuArray<SentFrameUpdate, SentPkt::MaxFrames>`.  Preserve the per-packet
   `MaxFrames` policy, `addFrame`, `framesUsed`, `frame`, and `frameOwner`
   behavior.  Construct only the used elements.
3. Define separate ACKed and lost update types:

```c++
using SentFrameUpdates = ZtBuiltin<
  ZtArray<SentFrameUpdate,
    ZtArrayHeapID<"Zquic.Recovery.FrameUpdates">>,
  64>;
```

The built-in 64 is a tuning value matching the former common-case capacity,
not a limit.  Add a maintenance comment and retain heap fallback for valid
larger batches.
4. Replace `PktTxUpdate`'s parallel ACKed/lost arrays, owners, and counts with
   two `SentFrameUpdates` members.  Append each packet's complete used span with
   `push`/`append`; never stop because the built-in buffer is full.
5. Keep `ackedPNs` as bounded qlog sampling only, rename its capacity to make
   that purpose explicit, and retain `ackedPNsTruncated`.  It must not control
   protocol work.
6. Define a matching `ReapStreams`:

```c++
using ReapStreams = ZtBuiltin<
  ZtArray<Stream *, ZtArrayHeapID<"Zquic.Recovery.ReapStreams">>,
  64>;
```

Pass it by reference through ACK application; remove the pointer/count pair.
7. Update ACK, packet-threshold loss, time-threshold loss, reject, and any other
   `PktTxUpdate` producer to append all frame updates.  The existing recovery
   scan budget remains a CPU-yield budget only.
8. Iterate typed update values in `applyAckUpdateTx_`,
   `applyLossUpdateTx_`, retransmission, stream reaping, and qlog reduction.
   Qlog may snapshot bounded metadata inside `ZquicLOG`; it must not capture the
   dynamic protocol container or owner pointers.
9. Add `ZiAssert` checks that each packet contributes exactly
   `framesUsed()` updates and that qlog truncation never affects ACK/loss
   application.

### Focused build and unit tests

Build:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8 ZquicRecoveryTest
```

Run only:

```sh
./zquic/test/ZquicRecoveryTest
```

Add unit cases for:

- 65 and 256 frame updates;
- 256 packets with eight frames each;
- ACK of previously marked-lost packets;
- packet- and time-threshold loss above the built-in capacity;
- stream FIN/reset reaping; and
- allocation fallback without callback loss or duplication.

### Acceptance and handoff to Slice 4

- Every consumed ACKed/lost packet contributes all of its frame callbacks.
- Common batches through 64 updates use built-in storage; larger valid batches
  grow through the named Z heap.
- Recovery scan budgets do not truncate callbacks.
- ACK/loss ordering, congestion bytes, ACK-of-ACK, PMTUD, and reaping semantics
  are unchanged.
- Qlog captures contain bounded metadata only and no owner pointer.
- `ZquicRecoveryTest` passes, `git diff --check` passes, and Slice 3 is
  committed.

Only then start Slice 4.

### Slice 3 execution record

- API gate: the repository has no non-`zquic` caller of `SentPkt`,
  `SentFrameRef`, or `PktTxUpdate`; the documented queue methods and callback
  ordering remain unchanged.
- Recovery metadata: per-packet frames are `SentFrameUpdate` values, and ACKed
  and lost batches use `SentFrameUpdates` with 64 built-in elements plus the
  named `Zquic.Recovery.FrameUpdates` heap fallback.
- Stream reaping uses the matching `ReapStreams` value with the
  `Zquic.Recovery.ReapStreams` heap ID.  Qlog copies at most its former
  64-frame sample and never captures owner pointers or the dynamic container.
- Build: `make -C zquic/src -j8 libZquic.la`.
- Focused unit build and run: `ZquicRecoveryTest` passed under the existing
  clang-debug configuration.  Direct cases retain 65 lost updates and 2,048
  ACKed updates (256 packets with eight frames), including owners; qlog packet
  number truncation remains independent of protocol callback retention.

## Slice 4: separate ACK wire policy from retained storage

Dependency: accepted and committed Slice 3.

### API gate

Preserve `AckTracker` callable methods, `FrameCodec` signatures, and ACK frame
wire limits.  Reuse the existing public `Frame::AckRanges` alias without
changing its exposed maximum.  If a cleaner ACK API requires a signature or
wire-policy change, stop and ask.

### Implementation

1. Keep `Frame::MaxAckRanges == 64` as a named wire-encoding policy.  Keep
   `BuiltinAckRanges == 32` as the existing hot-path tuning value with
   `ZtBuiltin` heap fallback bounded by `ZtArrayHeapMax<MaxAckRanges>`.
2. Define a retained ACK-range container for `AckTracker` using
   `ZtBuiltin<ZtArray<AckRange, ZtArrayHeapMax<...,
   ZtArrayHeapID<"Zquic.Ack.RetainedRanges">>>, 32>`.  Give the retained policy
   limit its own name; do not reuse a storage capacity as policy.
3. Replace `m_sparse` and `m_sparseN` with the retained container.  Keep a
   module-local binary lower-bound helper over `ZuSpan<const AckRange>`; do not
   use STL or linear `find`.
4. Merge all overlapping/adjacent ranges first, then perform one
   `ZtArray::splice` for the erased run and insertion.  Do not repeatedly shift
   the tail for each merged range.
5. Remove `trimSparseRanges_`'s repeated `count()` scan.  When retained policy
   requires retirement, remove the oldest range explicitly and increment a
   diagnostic counter.
6. Replace snapshot and `writeFrame` primitive scratch with
   `Frame::AckRanges`, constructing only the emitted elements.  Snapshot
   directly in codec wire order without a second reverse-copy array.
7. Change `AckSnapshot::ranges` to `Frame::AckRanges` only if its dependent API
   is internal; otherwise preserve the outward shape and isolate conversion at
   the Rx-to-Tx snapshot boundary.
8. Preserve Rx ownership of `AckTracker`; capture the complete bounded
   `AckSnapshot` by value for Tx.

### Focused build and unit tests

Build:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8 ZquicRecoveryTest ZquicCodecTest
```

Run only:

```sh
./zquic/test/ZquicRecoveryTest
./zquic/test/ZquicCodecTest
```

Add unit cases for range creation, bridging, adjacency, duplicate packets,
retirement, ACK-of-ACK, and exactly 64, 65, and more-than-65 retained ranges.

### Acceptance and handoff to Slice 5

- Wire encoding remains limited and ordered exactly as before.
- Retained-range retirement is an explicit policy event, never accidental
  storage exhaustion.
- Lookup begins with binary lower-bound; merging causes at most one tail move.
- Snapshot construction has no primitive scratch array or redundant reverse
  copy.
- Rx-to-Tx snapshots own their data and preserve shard ownership.
- Both named unit binaries pass, `git diff --check` passes, and Slice 4 is
  committed.

Only then start Slice 5.

### Slice 4 execution record

- API gate: no dependent layer uses `AckTracker` or the internal
  `AckSnapshot`; the existing raw `snapshot(AckRange *, unsigned)` and all
  codec signatures remain available.
- Retention: `AckTracker::RetainedRanges` has 32 built-in elements, the
  `Zquic.Ack.RetainedRanges` heap ID, and a separate 65-range retained policy.
  Lookup uses `ackRangeLowerBound`; merging and retirement use `splice`, and
  explicit retirements increment `retiredRanges()`.
- Wire snapshots: `Frame::MaxAckRanges` remains 64.  Typed snapshots are
  constructed directly in codec order, and `AckSnapshot` owns a
  `Frame::AckRanges` value across the Rx-to-Tx post.
- Build: `make -C zquic/src -j8 libZquic.la`.
- Focused unit build and run: `ZquicRecoveryTest` and `ZquicCodecTest` passed
  under the existing clang-debug configuration.  Tests cover bridging,
  adjacency, duplicates, 65 retained ranges, retirement beyond 65, the
  64-range wire cap, typed/raw snapshot ordering, and ACK-of-ACK behavior.

## Slice 5: replace `TxUnackdRanges`

Dependency: accepted and committed Slice 4.

### API gate

Preserve all `TxUnackdRanges` constructors and methods, `ZmPQResult` return
values, FIN semantics, and callers in stream recovery.  If replacing this
array with a tree becomes preferable, stop and ask because that changes
allocation/lifetime behavior beyond this slice.

### Implementation

1. Replace `m_ranges[128]` and `m_count` with:

```c++
using Ranges = ZtBuiltin<
  ZtArray<TxUnackdRange,
    ZtArrayHeapID<"Zquic.Stream.TxUnackdRanges">>,
  BuiltinRanges>;
```

2. Do not guess `BuiltinRanges`.  Before selecting it, record the high-water
   range count from the focused `ZquicPQueueTest` workloads and the new
   fragmentation cases.  Select the smallest named value covering at least
   95% of those operations, state the observed distribution in a maintenance
   comment, and retain heap fallback.  This tuning must not be a validity cap.
3. Implement one module-local lower-bound helper over `Ranges::cspan()`, keyed
   by `offset`.  Use it in `add`, `clear`, `spans`, and `find`; begin traversal
   at the predecessor only when it may overlap the requested start.
4. Check `offset + length` for overflow before lookup or mutation.
5. For `add`, compute the complete adjacent/overlapping run and merged FIN state
   without mutation, then replace that run with one `splice`.
6. For `clear`, identify the affected run, construct at most the left and right
   survivors, and replace the run with one `splice`.  Do not call a shifting
   `remove_` repeatedly.
7. Remove the 128-range `Invalid` result.  Return `Invalid` only for zero/invalid
   input or arithmetic failure under the existing API.  Let `ZtArray` use its
   named `ZmVHeap` path for valid growth.
8. Preserve cached aggregate length and assert after mutation that ranges are
   ordered, non-overlapping, non-adjacent, and sum to `m_length`.
9. Keep `Node`/`ZmRef<Node>` behavior unchanged; do not add a second container
   node allocation.

### Focused build and unit tests

Build:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8 ZquicPQueueTest
```

Run only:

```sh
./zquic/test/ZquicPQueueTest
```

Add table-driven unit cases covering built-in/heap transition, more than 128
disjoint ranges, exact/gap lookup, overlap, adjacency, split, partial/full
clear, FIN-only ranges, and integer overflow.

### Acceptance and handoff to Slice 6

- No valid fragmentation count is rejected by a fixed capacity.
- Every search starts at binary lower-bound rather than element zero.
- Each logical mutation performs at most one tail-moving `splice`.
- The chosen `BuiltinRanges` value and its focused-test distribution are
  documented; growth uses the named Z heap.
- Existing `TxUnackdRanges` API and result semantics are preserved.
- `ZquicPQueueTest` passes, `git diff --check` passes, and Slice 5 is committed.

Only then start Slice 6.

### Slice 5 execution record

- API gate: no non-`zquic` use of `TxUnackdRanges` was found.  Its constructor,
  methods, `Node` ownership, `ZmPQResult` values, public former-capacity
  constant, and FIN representation remain available.
- Storage and lookup: retained ranges use `ZtBuiltin` with eight inline
  elements and the `Zquic.Stream.TxUnackdRanges` heap ID.  Ordinary focused
  cases had a 95th percentile of two retained ranges; eight supplies one
  allocation-free growth class, while the adversarial case reached 256 and
  exercised heap fallback.  All four searches start with binary lower-bound.
- Mutation: add and clear validate endpoints, determine the complete affected
  run, and perform one `splice`.  The former 128-range rejection is gone;
  post-mutation `ZmAssert` checks ordering, separation, and cached length.
- Build: `make -C zquic/src -j8 libZquic.la`.
- Focused unit build and run: `ZquicPQueueTest` passed under the existing
  clang-debug configuration.  Coverage includes inline/heap transition, 256
  disjoint ranges, lookup gaps, bridge coalescing, span traversal, split and
  exact clears, data-plus-FIN and FIN-only ranges, and endpoint overflow.

## Slice 6: packet-builder and indexed metadata cleanup

Dependency: accepted and committed Slice 5.

### API gate

Preserve packet codec signatures, packet-space indexing semantics, and
`BufSize`.  Treat packet/I/O payload backing storage separately from metadata.
If changing an exposed aggregate's layout would affect a dependent layer, stop
and ask or retain it as a documented layout exception.

### Implementation

1. Convert `PlainVec::m_vec` to
   `ZuArray<ptls_iovec_t, PlainVec::Max>`, initialized with logical length zero
   and appended only for active vectors.  Preserve the checked per-packet
   capacity derived from `SentPkt::MaxFrames`.
2. Replace `PktBuild::m_scratch` with
   `ZuBArray<PktBuildScratchSize>` constructed to full logical length.
   Continue to expose only the committed prefix.
3. Remove `PktBuildZeroPad` only if packet assembly can append zeroed bytes
   directly into the final `PktTxBufAlloc` destination without an intermediate
   ciphertext/plaintext copy.  If vectored protection requires a stable zero
   source, retain it as a named `ZuBArray<BufSize>` with a comment explaining
   the lifetime and vector requirement.
4. Convert fixed packet-space metadata to
   `ZuArray<T, PktNumSpace::N>`.  Preserve direct enum indexing.
5. Replace anonymous `[2]`/`[4]` dimensions with existing direction/stream-type
   enums.  If no suitable enum exists, add a module-local `ZtEnum` only when
   runtime name mapping is needed; otherwise use the repository enum
   convention and a named count.
6. Convert remaining bounded recovery/link metadata arrays to `ZuArray`.
   Keep active lengths in the container rather than parallel count fields where
   that does not change a dependent API.
7. Do not convert `ZiIOBuf` packet, stream, or crypto payload ownership into
   stack/builtin arrays.  Preserve role-specific allocators and zero-copy
   receive/transmit behavior from `zquic/GUIDELINES.md`.
8. For each retained raw array, add one concise reason: external C ABI,
   required inline packet layout, or dependent API.  Do not use a generic
   "performance" exemption.

### Focused build and unit tests

Build:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8 \
  ZquicCodecTest ZquicBufferTest ZquicRecoveryTest
```

Run only:

```sh
./zquic/test/ZquicCodecTest
./zquic/test/ZquicBufferTest
./zquic/test/ZquicRecoveryTest
```

Add unit cases for maximum frame vectors, maximum ACK encoding, QUIC Initial
minimum padding, protection-sample padding, and vector-capacity failure.

### Acceptance and handoff to Slice 7

- Packet construction retains source-to-final-destination protection with no
  new payload staging copy.
- Packet-space/direction/stream-class dimensions are named and compile-time
  checked.
- Bounded metadata uses Z arrays; retained raw arrays have a specific reviewed
  ABI/layout/API reason.
- `BufSize` remains packet-buffer capacity, not a path payload limit.
- All three named unit binaries pass, `git diff --check` passes, and Slice 6 is
  committed.

Only then start Slice 7.

### Slice 6 execution record

- API gate: no dependent use required a packet-codec signature or exposed
  aggregate-layout change.  Public diagnostic arrays remain raw with an
  explicit dependent-API reason; picotls cipher, extension, and epoch-offset
  arrays remain raw for its C ABI.
- Packet assembly: `PlainVec` owns active `ptls_iovec_t` values in
  `ZuArray`; `PktBuild` owns full-length uninitialized scratch in `ZuBArray`.
  Vectored protection still references one stable process-lifetime zero
  `ZuBArray`, so packet payload goes directly to the final `ZiIOBuf`-backed
  destination without a staging copy.
- Indexed metadata: packet-space path, crypto, ACK, recovery, and Link state
  use full-length `ZuArray` values.  Stream-direction state uses
  `StreamType::N`; the four wire stream-ID classes use the named
  `StreamClassCount`.  Frame-reference and sent-control arrays now retain
  active length in their containers rather than parallel counts.
- Initialization: `fixedArray` explicitly value-fills primitive fixed arrays;
  the focused recovery test caught and prevented reliance on default
  initialization of primitive `ZuArray` elements.
- Build: `make -C zquic/src -j8 libZquic.la`.
- Focused unit build and run: `ZquicCodecTest`, `ZquicBufferTest`, and
  `ZquicRecoveryTest` passed under the existing clang-debug configuration.
  Coverage includes maximum vector capacity/failure, Initial minimum padding,
  protection-sample padding, maximum ACK encoding, and retained recovery
  batches.

## Slice 7: formatting lookup cleanup and final source audit

Dependency: accepted and committed Slice 6.

### API gate

Preserve qlog JSON schema, field names, event names, and public logger APIs.
All qlog-only work must remain inside `ZquicLOG`, capture by value, and contain
no payload, path-token bytes, or secret material.

### Implementation

1. Replace `qlogJSONVersion_`'s primitive hex table and nibble loop with the
   established `ZuFmt`/`ZuPrint` hexadecimal formatting path.
2. Configure the formatter to emit exactly eight lowercase hexadecimal digits,
   including leading zeroes, directly into the qlog output stream between JSON
   quotes.  Do not allocate a temporary `std::string`.
3. Search `zquic/src` for:

   - raw array declarations;
   - numeric capacity subscripts;
   - parallel arrays/counts;
   - pointer-decayed array parameters;
   - manual byte-copy/`memcmp` loops over protocol values;
   - linear lookup from element zero; and
   - static character/integral lookup tables.

4. Classify every remaining result as converted, external C ABI, required
   packet layout, explicit protocol policy, or dependent API.  Add a local
   maintenance comment for each intentional exception.
5. If a fixed multi-string mapping is found, implement it with `ZuMatcher`.  If
   a static integral table is found, implement it with `ZuSwitch`.  Do not
   change dynamic peer/library traversal.
6. Keep qlog enum/string mapping on the logger thread through `ZtEnumMap`,
   `ZfStruct`, and `ZfJSON`; do not move formatting onto Rx/Tx.

### Focused build and unit tests

Build:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8 ZquicLogTest
```

Run only:

```sh
./zquic/test/ZquicLogTest
```

Add unit cases for versions `0`, `1`, a value containing `a` through `f`, and
`UINT32_MAX`; verify exact JSON text.

### Acceptance and handoff to Slice 8

- Version output is exactly eight lowercase hexadecimal digits and valid JSON.
- No qlog-only work escaped `ZquicLOG`; captures remain bounded values.
- The source audit has an explicit disposition for every remaining raw array
  and lookup candidate.
- No unexplained primitive runtime-state cap, parallel-array invariant, or
  linear fixed lookup remains.
- `ZquicLogTest` passes, `git diff --check` passes, and Slice 7 is committed.

Only then start Slice 8.

### Slice 7 execution record

- Formatting: `qlogJSONVersion_` now writes a `ZuBoxed` value through
  `ZuFmt::Right<8, '0'>` in lowercase hexadecimal directly to the qlog stream.
  Exact JSON checks cover `0`, `1`, `0x1abcdef0`, and `UINT32_MAX`.
- Converted audit findings: the qlog hexadecimal character table and nibble
  lookup are gone; the fixed HKDF label prefix is a `ZuCSpan`; runtime arrays,
  scratch buffers, and parallel array/count state were converted in the
  preceding slices.
- External C ABI exceptions: the null-terminated picotls cipher-suite array,
  sentinel picotls extension array, and five-element epoch-offset callback
  parameter remain raw and have local comments.  The dependent
  `sendCryptoFlights` array-reference API remains unchanged and binds
  `ZuArray` directly.
- Required layout exceptions: `CryptoRxPQueue::Buf::data_` remains inline for
  the `ZiIOBuf` pool layout, and the diagnostic packet-space arrays remain raw
  public aggregate fields; both carry local maintenance comments.
- Lookup and traversal disposition: ACK membership and stream-range lookup use
  binary lower-bound.  The retained ACK index APIs traverse the bounded
  composite wire-order view and are locally documented.  Negotiated ALPN,
  version-negotiation, picotls suite/extension, qlog event-list, and protocol
  range traversal are dynamic peer/library or active-length traversal, not
  fixed lookup.  Packet/varint/header-protection byte loops are wire encoding;
  authentication/reset-token comparisons retain documented full constant-time
  traversal.  No fixed multi-string mapping or static integral table remains,
  so no new `ZuMatcher` or `ZuSwitch` is required.
- Build: `make -C zquic/src -j8 libZquic.la`.
- Focused unit build and run: `ZquicLogTest` passed under the existing
  clang-debug configuration.

## Slice 8: final zquic-only integration and delivery

Dependency: accepted and committed Slice 7.

This is the only slice that runs the complete module test set.  It changes no
design unless a failure requires returning to the owning earlier slice.

### Build and test

Do not clean or reconfigure.  Build only `zquic`:

```sh
make -C zquic/src -j8 libZquic.la
make -C zquic/test -j8
make -C zquic/test test
```

This final run includes the module's API, socket, handshake, runtime, stream,
loop, timer, qlog, and other integration-style binaries.  Do not build or test
`zhttp`, examples, or any other dependent layer.  Do not rebuild the Z
dependency layers.

If an observed crash needs diagnosis, run only the failing already-built binary
under valgrind through `./libtool --mode=execute`; do not create an
ASan/LSan build.

### Final acceptance

- Every Slice 1-7 commit remains present in dependency order and each commit
  builds from its predecessor.
- No dependent API was changed without explicit user authorization.
- No ACKed or lost packet can be consumed without every required frame callback
  being retained and applied.
- Valid stream fragmentation is not rejected by a storage cap.
- ACK and stream-range lookup start with binary search and use workload-aware Z
  storage.
- Fixed protocol values and bounded metadata use Z containers or have a
  reviewed ABI/layout/API exception.
- Cross-shard fixed metadata is captured as a complete owned value.
- ACK retention limits are explicit wire/runtime policy rather than accidental
  storage behavior.
- The complete `zquic` test target passes under the existing clang-debug
  configuration.
- The delivery note records exact commands, tests, platform/toolchain, any
  valgrind investigation, chosen built-in capacities and evidence, and every
  retained raw-array exception.

### Slice 8 delivery record

- Platform/toolchain: Linux `x86_64`, clang 22.1.8, existing debug
  configuration (`-g -DZDEBUG`); no clean or reconfiguration was performed.
- Final commands were `make -C zquic/src -j8 libZquic.la`,
  `make -C zquic/test -j8`, and `make -C zquic/test test`.  After repairing
  findings, the same three commands were rerun as applicable; no dependency
  layer or dependent module was built or tested.
- The first complete test build found two stale internal contracts:
  `sendCryptoFlights_` accepted only a raw C array after Link storage became
  `ZuArray`, and `ZquicStreamTest` still wrote the removed parallel
  `AckSnapshot::nRanges`.  The internal helper now accepts either indexed
  container while the public raw callback signature is unchanged; the test
  constructs the active `AckRanges` value directly.
- The first complete run exposed an abort in `ZquicRuntimeTest`.  The required
  `./libtool --mode=execute valgrind --track-origins=yes
  --error-exitcode=99 zquic/test/ZquicRuntimeTest` traced it to an
  inline-backed `ZtBuiltin` ACK snapshot copy freeing stack storage.
  `AckSnapshot` now deep-copies into its already-constructed destination
  buffer.  The repaired runtime binary passed normally.  A follow-up valgrind
  run no longer reported the ACK snapshot corruption but changed shutdown
  timing enough to expose a separate endpoint-disconnect lifetime race; no
  ASan/LSan build was made and that unrelated shutdown redesign was not folded
  into this storage slice.
- Final result: all 20 zquic test binaries passed, 159 tests total.  The test
  set includes API, socket, codec, crypto, packet protection, handshake,
  recovery, stream state, PMTUD, version, CID, stream, flow, buffer,
  congestion, packet queue, loop, runtime, timer, and qlog coverage.
- Built-in tuning retained by evidence: crypto staging uses 8 KiB inline with
  heap fallback up to the explicit 64 KiB message policy; recovery updates and
  reap lists use 64 inline elements, with focused coverage at 65 and 2,048
  updates; ACK wire and retained ranges use 32 inline elements with separate
  64-wire and 65-retained policies; stream retransmit ranges use eight inline
  elements, with focused 256-range heap-fallback coverage.
- Reviewed raw-array exceptions remain exactly those recorded in Slice 7:
  picotls cipher/extension/epoch-offset C ABI, public diagnostic aggregate
  layout, dependent raw callback ingress, and the inline `ZiIOBuf` pool
  payload.  All carry local maintenance comments.
