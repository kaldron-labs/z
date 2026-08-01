# `ZmAlloc` to `ZmScratch` migration plan

## Objective

Replace `ZmAlloc` with `ZmScratch` where the temporary is a fixed-capacity
array whose live length is smaller than, or independent of, its capacity, or
where `ZmScratch` can own the lifetime of constructed elements.  Retain
`ZmAlloc` for raw backing storage when every slot is populated, the live length
does not need to be represented, and element destruction is either provably
unnecessary or deliberately controlled by the caller.

The audit found 72 migration candidates.  The inventory below names every
candidate; locations not listed as candidates are covered by the retained-use
audit.

## Migration rules

1. Allocate once from a stable capacity expression.  Both allocation macros
   evaluate the capacity more than once, so first cache expressions with side
   effects or non-trivial cost.
2. Let `ZmScratch` be the sole owner of live-element count and destruction:
   - use `push()` as uninitialized storage followed by placement construction;
   - use `push(value)`, `append()`, or `operator <<` for constructed values;
   - use `length(written)` after a C API or codec writes primitive elements;
   - use `null()`, `pop()`, or `length(shorter)` instead of explicit destructor
     loops when shortening the live range.
   Eliding a separately maintained authoritative length does not permit calling
   `length()` in every loop condition.  Cache it once, following Z style:
   `for (unsigned n = a.length(), i = 0; i < n; i++)`; do not write
   `for (unsigned i = 0; i < a.length(); i++)`.
   Use `unsigned` consistently for scratch capacities and live lengths; do not
   introduce `uint32_t` intermediates, range assertions, or underscore
   temporaries solely to bridge those types.
3. Pass non-owning views to helpers and external APIs: mutable capacity as
   `ZuSpan<T>`, live data as `ZuSpan<const T>`/`cspan()`.  A helper must not take
   a `ZtArray` base, `ZmScratch_`, or other owning concrete array type.
4. If a helper itself performs generic `push`, `append`, or `operator <<`, make
   the helper a template on the output array/stream type.  Do not introduce a
   dependency on `ZmScratch_`.
5. Do not call `length(capacity)` pre-emptively for non-primitive elements.
   That default-constructs them.  Construct only successful/live elements with
   `push()` and placement construction.
6. Preserve fixed-capacity overflow behavior for variable-length string output.
   Pair `ZmScratch` backing storage with `ZuStream`, which is the adapter
   intended to detect overflow while appending string data.  The dependents
   that explicitly rely on this behavior are the ZfCSV push/pull writers
   (`zf/src/ZfCSV.hh:664,684,729,735`) and the ZiCSV file push/pull writers
   (`zi/src/ZiCSV.hh:79,157,201,266`).  After a successful stream write, set the
   scratch length from the consumed portion and emit `cspan()`.  Do not add
   overflow state to `ZmScratch` or `ZuArray`, and do not substitute a
   sentinel-length scheme.
7. Remove obsolete `ZmAlloc.hh` includes and add `ZmScratch.hh` at the owning
   layer.  Keep `ZmAlloc` includes where retained uses remain.

## Candidate inventory

### `zm`: constructed arrays and partial snapshots (13 sites)

- `zm/src/ZmHeap.cc:185`, `zm/src/ZmHashMgr.cc:125`, and
  `zm/src/ZmThread.cc:161`: telemetry snapshots maintain a separate `length`,
  placement-construct each accepted record, pass an explicit span, and run a
  destructor loop.  Replace each with `ZmScratch`, construct through `push()`,
  pass `cspan()`, and rely on RAII destruction after the callback.
- `zm/src/ZmCache.hh:364` and `zm/src/ZmPolyCache.hh:350`: cache snapshots can
  stop before the allocated count and explicitly destroy each `NodeRef` after
  dispatch.  Push each successfully acquired reference into scratch storage;
  use `length()` for `ZmBlock`/iteration and let consumption shorten or clear
  the scratch array without `NodeRefFn::dtor()` calls.  Preserve the pointer
  specialization's no-op lifetime behavior through normal primitive traits.
- `zm/src/ZmBlock.hh:42,70`: result arrays contain generic, potentially
  non-trivial types.  Reserve callback destinations with `push()` and placement
  construction (or otherwise establish each result's lifetime), reduce over
  `cspan()`, and let scratch destruction finalize all results.  Avoid merely
  assigning into raw unconstructed storage.
- `zm/src/ZmCodec.hh:29,37`: encoded/decoded output is currently represented by
  a full-capacity `ZmAlloc` plus a separately truncated `ZuSpan`.  Write into
  `span()`, set the returned live length on the scratch buffer, and pass
  `cspan()` to the callback.
- `zm/bench/ZmCacheBench.cc:79`, `zm/bench/ZmHeapBench.cc:100`, and
  `zm/bench/ZmRingBench.cc:167,168`: `ZmThread` objects are placement-constructed
  and explicitly destroyed.  Construct them through scratch `push()` and use
  RAII destruction.  In `ZmCacheBench`, clear the scratch array after each
  joined batch before constructing the next batch.

### `zt`: variable-length formatting and non-POD self copies (13 sites)

- `zt/src/ZtQuote.hh:66,80,94`: Base32, Base64, and hex encoders pair raw
  capacity with a truncated span.  Use the codec return value as scratch
  length and print `cspan()`.
- `zt/src/ZtArray.hh:570,696,1196,1318`: alternate-character formatting first
  prints a variable-length `char` result into raw capacity.  Replace the
  temporary span with scratch length and feed its live span to UTF conversion.
- `zt/src/ZtArray.hh:428,1276`: self-splice/self-append copies generic `T`/`Char`
  elements into raw storage and explicitly calls `destroyElems`.  Copy-construct
  into scratch storage, consume its live span, and rely on scratch destruction.
- `zt/src/ZtString.hh:337,458,999,1103`: alternate-character formatting has the
  same capacity-plus-truncated-span pattern as `ZtArray`; migrate it in the same
  way.

### `zf`: codecs, scanners, writers, and parser rows (29 sites)

- `zf/src/ZfCLI.hh:1294,1301,1308,1315`,
  `zf/src/ZfJSON.hh:783,790,797,804`,
  `zf/src/ZfCSV.hh:213,220,227,234`, and
  `zf/src/ZfURI.hh:1086,1093,1100,1107`: all four serialization facets allocate
  maximum encoded byte counts and separately truncate spans for Base64,
  Base64URL, Base32, and hex.  Consolidate these repeated paths around a small
  templated codec helper that writes to a mutable span, sets scratch length,
  and supplies a live span to the facet-specific quoting/output step.
- `zf/src/ZfStruct.hh:952,1252,1267,1278,1411,1429,1451`: byte formatting,
  string scanning, vector-element scanning, and Base64 decoding all keep actual
  output length outside the allocation.  Use scratch live length for the
  decoded/scanned portion; append a CString terminator without including it in
  the logical string span where required.
- `zf/src/ZfCSV.hh:661,681,764,776`: fixed-row writers use `ZmAlloc`, `ZuStream`,
  pointer subtraction, and an independent overflow flag.  Make writer helpers
  operate on spans, retain `ZuStream` over the scratch capacity for string
  overflow detection, set scratch length from the consumed stream after a
  successful write, and emit `cspan()`.
- `zf/src/ZfCSV.hh:817`: parser rows allocate `Cell` storage and expose it by
  wrapping the storage in a non-owning `Row`.  Make `split` generic on its
  push-capable row output (or add a span-based parsing core), build live cells
  in `ZmScratch<Cell>`, and expose only a live `ZuSpan<Cell>` to loading code.
  The scratch owner, not the `Row` view, must destroy constructed union cells.
- `zf/src/ZfURI.hh:897`: the array-index prefix capacity is an upper bound and
  the actual formatted digit count is tracked by `ZuStream`.  Format into
  scratch and use its live span as the recursive prefix.

### `zi`: partial I/O and CSV buffers (8 sites)

- `zi/src/ZiFile.hh:270`: printable output has maximum capacity `len`, while
  `ZuPrint::print` returns the actual write length.  Set scratch length from the
  return value and write `cspan()`.
- `zi/src/ZiCSV.hh:102,154,299,312`: push and pull file writers share raw
  buffers with helpers that independently track read offsets, write lengths,
  and overflow.  Refactor helper boundaries to take spans for raw I/O and
  template parameters where the helper writes generically.  Use scratch live
  length for header reads; for row writes, retain `ZuStream` over scratch
  capacity, preserve its maximum-row-length error, and synchronize the
  successful stream length back to scratch.
- `zi/src/ZiCSV.hh:332`: the streaming file reader maintains `offset`, reads
  into the unused tail, then memmoves residue.  Use `length()` as the buffered
  byte count, read into the remaining-capacity span, and use `splice`/a single
  in-place move to retain unconsumed bytes.
- `zi/test/ZiCSVTest.cc:62` and `zi/test/ZiLogTest.cc:94`: file reads allocate
  from file size but use the returned short-read count.  Record the result as
  scratch length and construct the returned `ZtString` from `cspan()`.

### `zquic`: one explicitly guarded object (1 site)

- `zquic/src/ZquicLink.hh:8198`: `Frame` is placement-constructed in a
  one-element allocation and destroyed by a separate scope guard.  Use
  `ZmScratch(Frame, 1)`, placement-construct through `push()`, and remove the
  explicit guard.  Keep parsing helpers span/value based; they must not accept
  the scratch owner.

### `ztls`: bounded DER outputs and variable signature lengths (8 sites)

- `ztls/src/ZtlsPK.hh:272,407`: RSA and EC signing allocate the maximum
  signature size and track the actual `size_t k` separately.  Continue passing
  a mutable capacity span to the backend, then set scratch length from `k` and
  invoke the callback with `cspan()`.
- `ztls/src/ZtlsPK.hh:255,387,511`: RSA, EC, and Ed25519 `mkPK()` paths wrap raw
  DER storage in a non-owning `ZtArray`.  Write directly to a templated
  `ZmScratch<char>` output and pass its live span to the ASN.1 handler; do not
  retain a `ZtArray` base view.
- `ztls/src/ZtlsPK.hh:784,796,829`: `saveB64`, `savePEM`, and `saveFile` use the
  same raw-storage/non-owning-`ZtArray` pattern.  Make the save chain generic on
  its push-capable output, use scratch live spans between DER/Base64/PEM stages,
  and pass only spans to file and codec APIs.

## Audited uses to retain

These sites should remain `ZmAlloc`; they either exercise/implement the raw
allocator or satisfy the fully-populated POD / explicitly controlled-lifetime
criteria.

- `zm/src/ZmAlloc.hh`, `zm/test/ZmAllocTest.cc`, and the two backing allocations
  in `zt/src/ZtScratch.hh`: allocator implementation, tests, and the growable
  scratch container's internal backing store.
- `zm/src/ZmAssert.cc:34`: intentional one-object emergency buffer on an abort
  path; control never returns and an array live length provides no value.
- `zm/test/ZmRingFnTest.cc:31,53,69,88`: fully populated raw byte messages whose
  size is prescribed by the ring function ABI.
- `zdb/src/Zdb.hh:1227,1311`: retain the explicitly controlled undo-buffer
  lifetime, including the conditional destruction at `Zdb.hh:1233` and its
  delete-path counterpart.
- `zdb/src/ZdbMemStore.hh:1035`, `zdb_pq/src/ZdbPQ.hh:1583`, and
  `zdb_pq/src/ZdbPQ.cc:1003-1006,1064-1066,2199,2294`: offsets, libpq parameter
  arrays, and row tuples are populated to their known full size.  The primitive
  arrays require no destruction; the `Value` tuple sites explicitly document
  that destruction is intentionally elided.
- `zfb/src/Zfb.hh:178,188,197,218,228,237` and `zfb/src/Zfb.cc:52`:
  FlatBuffers offsets and the file-read byte buffer are fully populated POD
  storage with externally known length.
- `zf/src/ZfASN1.hh:1381,1391,1721,1732`: fixed-width UTF conversion staging is
  fully populated POD storage and all loops consume the already-known count.
- `zf/src/ZfCLI.hh:1109,1514` and `zf/src/ZfURI.hh:1332`: these prefix copies
  have exactly computed sizes and fully populate primitive character storage.
- `zi/src/ZiPlatform.cc:24`: `getpwuid_r` requires raw capacity, does not report
  a useful array length, and writes only primitive bytes.
- `zt/src/ZtString.hh:1037,1269`: self-append/self-splice character copies are
  fully populated primitive storage with an already-known length and no
  destruction requirement.
- `ztls/src/ZtlsPK.hh:80,81,109,115,206-213,345,351,357,607`: key-component
  export buffers and the checked full-file read are exact-size, fully populated
  byte storage.  Backend export functions require capacity spans but do not
  return a shorter logical length.

## Implementation order

1. Migrate the repeated primitive codec patterns (`ZmCodec`, `ZtQuote`,
   and the Zf facets), introducing only span-based or output-templated helpers.
2. Migrate scanners and I/O paths (`ZfStruct`, `ZiFile`, and tests), validating
   empty input, short output, and short reads.
3. Migrate non-trivial lifetime owners (`Zm*` telemetry/cache/block/bench,
   `ZtArray`, CSV `Cell`, and QUIC `Frame`) with destructor-count and early-exit
   tests where available.
4. Refactor fixed-capacity CSV writers/readers to pair `ZmScratch` storage with
   `ZuStream` overflow detection and synchronize live length after each write.
5. Migrate TLS DER/signature staging after the generic save chain accepts any
   push-capable output and all parsing boundaries consume spans.
6. Remove stale includes, rerun the full `ZmAlloc` inventory, and confirm that
   every remaining occurrence belongs to the retained list above.

## Verification

- Add/extend `ZmScratchTest` coverage for primitive `length(written)`, placement
  construction through `push()`, early shortening, and exactly-once destruction.
- Run focused suites for `zm`, `zt`, `zf`/ASN.1/CSV/JSON/CLI/URI, `zi`, `zquic`,
  `zdb`, `zdb_pq`, `zfb`, and `ztls` after their respective batches.
- Exercise CSV rows at `MaxRowLen - 1`, `MaxRowLen`, and `MaxRowLen + 1`, plus
  fragmented reads with residue retained between calls.
- Exercise codec and signature paths where actual output is shorter than
  capacity, including zero-length input.
- Run `make -j`, `make test`, `git diff --check`, and a final occurrence audit.
  The final audit should show no `ZmAlloc` use outside the retained categories
  unless a new site has a documented fully-populated-POD or controlled-lifetime
  justification.
