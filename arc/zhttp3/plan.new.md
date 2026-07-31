## Summary

This plan remediates the Zhttp findings in `zhttp.md`: HTTP/1 parser correctness, strict `content-length` parsing shared by HTTP/1 and HTTP/3, removal of fixed-size HTTP/3/QPACK header scratch buffers, QPACK Tx dynamic table accounting/storage cleanup, demotion of the virtual encoder transport API, and the stale module comment.

The revised sequencing is vertical where possible. Phase 1 fixes externally visible parser behavior end-to-end for H1 and H3 before internal QPACK storage work. Phase 2 handles H3 header scratch allocation and header-list enforcement together, because both changes depend on `Params::maxHeaderListSize()` and `ZtLocalArray(HeaderBytes, ...)`. Phase 3 fixes QPACK Tx insertion durability before broader ownership changes, so later refactoring starts from correct insert-count semantics. Phase 4 consolidates QPACK Tx entry storage while preserving absolute-index lookup, duplicate detection, section refcounts, and eviction. Phase 5 removes or demotes the virtual encoder API after all behavior tests are stronger. Phase 6 performs the documentation-only cleanup.

Relevant external standards and comparable implementations:

- RFC 9110 defines `Content-Length` as a decimal non-negative integer and permits rejecting invalid duplicate/list forms rather than normalizing them. This plan intentionally rejects any value that does not scan as exactly one complete unsigned decimal integer. Source: https://www.rfc-editor.org/info/rfc9110/
- RFC 9112 describes HTTP/1 field-line extraction as trimming optional whitespace before/after the field value; the trimmed value may be empty. Source: https://httpwg.org/specs/rfc9112.html
- RFC 9204 defines QPACK insert count, FIFO dynamic-table insertion/eviction, and the rule that non-evictable referenced entries must not be evicted. That aligns with fixing durable insert accounting before changing Tx table storage. Source: https://www.rfc-editor.org/info/rfc9204/
- nghttp3 exposes QPACK encoder capacity and blocked-stream controls explicitly, reinforcing that Zhttp's `Params::qpackTableCapacity()`, `Params::qpackBlockedStreams()`, `QPackTxTable`, and section tracking must remain connection/stream-state driven. Source: https://nghttp2.org/nghttp3/qpack-howto.html

## Architecture Documentation

New or changed components:

- Add a small parser utility near existing parser helpers in `zhttp/src/Zhttp.hh`, for example `parseUInt64Full_(ZuBSpan, uint64_t &)`, implemented with `ZuBox<uint64_t>::scan()` and an exact consumed-length check.
- Rework `parseKV()` in `zhttp/src/Zhttp.hh` to require only a non-empty field name before `:` and to return an empty value span when OWS trimming leaves no bytes.
- Change `H1::Parser::parseOperation()` to reject `Method::lookup()` results below zero before calling `impl()->operation()`.
- Change H1 and H3 `content-length` handling to use the shared full-scan helper.
- Change H3 parser string decoding so Huffman strings use `ZtLocalArray(HeaderBytes, decodedLength)` or equivalent scratch with heap fallback instead of `uint8_t storage[DefltMaxHdr]`.
- Change H3 selected header-name normalization to use dynamic scratch sized from `name.length()` with a 256-byte built-in fast path, instead of `uint8_t key_[256]`.
- Change `QPackTxTable::insert()` so `insertCount_` advances only after all fallible table work is durable.
- Refactor QPACK Tx dynamic table storage to avoid duplicated owned `name`/`value` strings across hash and order records.
- Demote or remove `QPackEncoderTx` virtual dispatch after confirming in-tree and external API compatibility needs.

New or changed processes or threads:

- None. `QPackTxTable` remains connection-affine Tx state; callers continue to serialize access from the owning transmit path.

New or changed interfaces:

- Internal helper: `parseUInt64Full_(ZuBSpan, uint64_t &) -> bool`.
- Optional public/CRTP API preference: `qpackEncoderWriteAccepted(ZuBSpan)` remains the primary encoder-stream write hook.
- Optional legacy API: `qpackEncoderTx()` returning `QPackEncoderTx *` is either removed or kept as a deprecated fallback depending on ABI compatibility requirements.

New or changed data flows:

- H1 request parsing rejects invalid methods before invoking application callbacks.
- H1 and H3 `content-length` parsing now rejects empty, signed, overflowed, or trailing-garbage values before setting parser body length or invoking `contentLength()`.
- H3 Huffman-decoded header values flow through caller-sized scratch and are checked against `Params::maxHeaderListSize()`.
- QPACK Tx insertion stages data using a local absolute index, then commits hash/order/bytes/count as one logical update.

New or changed event-driven or timer processing:

- None.

New or changed network programming:

- No socket or reactor changes. The work affects protocol parsing, QPACK encoder-stream output hooks, and parser/builder callback behavior only.

New or changed data stores:

- `QPackTxTable` internal storage changes from duplicated hash/order-owned strings to a single owned entry representation plus order metadata sufficient for FIFO eviction and absolute-index lookup.

## Detailed Design and Implementation Plan

### Phase 1: Parser Correctness Across H1 and H3

- Fix the externally visible parser correctness issues first, with tests in the same phase.
- Add `parseUInt64Full_(ZuBSpan value, uint64_t &out)` near `parseKV()` in `zhttp/src/Zhttp.hh`.
- Implement the helper with `ZuBox<uint64_t> box; unsigned n = box.scan(ZuCSpan{value});` and require `n && n == value.length()`. Assign `out = box` only on success.
- Decide explicitly not to accept comma-separated duplicate `Content-Length` values in this remediation. RFC 9110 permits recipients to reject invalid/list forms, and accepting lists would require duplicate-value normalization not present in the current parser.
- In `H1::Parser::header_<ZuStringT<"content-length">>()`, replace `ZuBox<uint64_t>{ZuCSpan{value}}` with the helper. Reject parse failure and `contentLength > MaxBody` through the existing `State::Error` and `ZiLOG(Error, "Zhttp", "invalid content-length")` path.
- Make the same helper replacement in `H3::Parser::header_<ZuStringT<"content-length">>()`, setting `m_state = State::Error` on parse failure or `MaxBody` overflow.
- In `H1::Parser::parseOperation()`, check `Method::lookup()` immediately. If it is below zero, call the existing `error()` lambda and return before path parsing callbacks.
- Rework `parseKV()` so `eok(line)` remains the field-name validation, `normalize(key)` is unchanged, and value trimming works for empty/all-OWS values. After `line.offset(n + 1)`, compute leading OWS with `bov(line)` if present, but treat no non-LWS byte as an empty value rather than failure. Use `eov()` only when the value is non-empty.
- Preserve current obsolete line folding behavior controlled by `parseLine<true>()`; changing folding policy is a non-goal for this remediation.
- Add H1 parser tests in `zhttp/test/ZhttpParserTest.cc`:
  - invalid request method, expecting `ParserState::Error`, one `complete(Error)`, no `operation()` callback, and no header/body callbacks.
  - `X-Empty:\r\n` selected header value, expecting a callback with length 0.
  - `X-Empty:   \r\n` selected header value, expecting a callback with length 0.
  - invalid H1 `content-length` values: empty, `+5`, `-5`, `5x`, and an overflow string such as `18446744073709551616`, expecting `Error` and one completion callback.
- Add H3 parser/QPACK-fed tests in `zhttp/test/ZhttpQPackDynamicTest.cc` or a nearby H3 parser test:
  - feed decoded headers containing `content-length: 5x` and overflow.
  - expect parser error before body processing and no accepted `contentLength()` callback.

### Phase 2: H3 Header Scratch Storage and Header-List Limits

- Replace the two fixed scratch buffers in the H3 parser while preserving zero-copy behavior for non-Huffman strings.
- Change `withString_()` in `zhttp/src/Zhttp.hh` from a static helper to a helper that can see H3 params, or pass the relevant max size into it. Candidate signature: `template <typename L> bool withString_(QPackStringRef ref, L l)`.
- For non-Huffman strings, check `ref.raw.length() <= h3Params_().maxHeaderListSize()` before calling the callback and pass `ref.raw` directly.
- For Huffman strings, compute `uint64_t decodedMax = HPack::declen(ref.raw.length())`, reject if it exceeds `h3Params_().maxHeaderListSize()`, allocate `auto storage = ZtLocalArray(HeaderBytes, decodedMax)`, call `HPack::decode(ZuSpan<uint8_t>{storage.data(), unsigned(decodedMax)}, ...)`, reject decode failure, set `storage.length(unsigned(n))` from the returned byte count, and pass `ZuCSpan` over `storage`.
- Use the existing QPACK decode pattern in `zhttp/src/ZhttpQPack.hh`, which already uses `ZtLocalArray(HeaderBytes, HPack::declen(in.length()))`.
- Replace `uint8_t key_[256]` in `qpackHeaderRef_()` with header-name scratch sized from `name.length()`. If preserving a 256-byte fast path matters, introduce a small named local `HeaderNameBytes`/`HeaderNameScratch` array type with `ZtBuiltin`/`ZtLocalArray`-style stack-first storage; otherwise use `auto key = ZtLocalArray(HeaderBytes, name.length())` and set its logical length before copying.
- Normalize the scratch span in place and pass `header_(key, ZuBSpan{value})`.
- Do not impose a standalone 256-byte name cap. Reject only when the field section exceeds `Params::maxHeaderListSize()` or when allocation fails.
- Track aggregate H3 field-section bytes consistently. `QPack::decodeFieldSection()` already counts decoded names and values against `params.maxHeaderListSize()`. The parser-side `withString_()` and name scratch checks should be defensive for direct parser string decoding paths and should not double-count incorrectly.
- Add H3 tests:
  - Huffman-encoded header value with decoded length above 64 KiB and below a raised `Params::maxHeaderListSize()`, expecting success.
  - Huffman-encoded header value above `maxHeaderListSize()`, expecting parser error.
  - selected H3 header name longer than 256 bytes but below `maxHeaderListSize()`, expecting success and normalized lowercase name handling.
  - selected H3 header name/value combination that exceeds `maxHeaderListSize()`, expecting error.

### Phase 3: Durable QPACK Tx Insert Accounting

- Fix insert-count correctness before touching table ownership. RFC 9204 makes Insert Count the absolute history of dynamic table insertions, so `insertCount_` must not advance unless the entry is durably present.
- In `QPackTxTable::insert(Header h, uint64_t *abs)`:
  - Leave duplicate detection unchanged: if `find(h.name, h.value)` succeeds, return the existing absolute index and do not increment `insertCount_`.
  - Compute `uint64_t nextAbs = insertCount_;` locally.
  - Fill the candidate `QPackTxEntry` with `e.abs = nextAbs`, not `insertCount_++`.
  - Complete all fallible operations first: capacity/eviction, hash insertion, order push/allocation, string copies, and order metadata fill.
  - Commit `usedBytes_ += n`, `insertCount_ = nextAbs + 1`, and `*abs = nextAbs` only after hash and order records are present.
- Because current `ZmLHash::add()` returns a pointer and `ZtArray::push()` placement allocation can fail structurally only by returning unusable storage or throwing/asserting depending on the container, inspect exact failure behavior during implementation. If `order.push()` can fail after `hash.add(e)`, either reserve/push the order record before `hash.add(e)` or roll back `hash.del(QPackFieldKey{h.name, h.value})`.
- Prefer ordering operations so rollback is unnecessary: prepare/copy order metadata first if possible, add hash second, then commit counters.
- Add or update dynamic-table tests in `zhttp/test/ZhttpQPackDynamicTest.cc`:
  - duplicate insert returns the original absolute index and leaves `insertCount()` unchanged.
  - failed insert due to capacity leaves `insertCount()`, `used()`, `find()`, and `lookupAbs()` unchanged.
  - if deterministic allocation/hash failure injection is practical locally, add a failure-injection test proving `insertCount()` does not advance. If not practical, document in the test or implementation comment why operation ordering makes this path structurally safe.

### Phase 4: QPACK Tx Dynamic-Entry Ownership Consolidation

- Consolidate duplicated dynamic-entry storage after Phase 3 proves durable insert semantics.
- Current issue: `QPackTxEntry` and `QPackTxOrderEntry` both own `QPackTxString name` and `QPackTxString value`, causing duplicate heap-owned field storage per dynamic table entry.
- First inspect `ZmLHash` and `ZmHash` node APIs during implementation for a clean intrusive path. `ZmLHash::add()` returns a stable pointer to hash-owned storage, and `del()` removes by key/data. If this is sufficient, use it directly; otherwise prefer a low-risk order metadata refactor over introducing a custom container.
- Preferred implementation option:
  - Keep `QPackTxEntry` as the single owner of `abs`, `size`, `refcnt`, `name`, and `value`.
  - Change `QPackTxOrderEntry` to hold `uint64_t abs` plus either a stable pointer/handle to the `QPackTxEntry` or the minimum copied key material needed to find and delete the hash entry.
  - If a stable pointer is used, ensure it is never dereferenced after `hash.del()`. Eviction must read needed data before deletion.
  - Preserve `findAbs()` and `lookupAbs()` by resolving order metadata to the single hash-owned entry.
- Alternative implementation option if `ZmLHash` pointer lifetime or deletion behavior makes pointer-backed order unsafe:
  - Keep copied order lookup keys temporarily but remove duplicated payload where possible by storing only `abs`, `size`, and compact key references/spans whose lifetime is tied to the hash-owned entry.
  - If safe spans cannot be guaranteed after hash relocation/deletion, defer full consolidation and document the blocker; still keep Phase 3 accounting fixes.
- Preserve existing semantics:
  - exact `find(name, value)` lookup by temporary spans.
  - absolute-index lookup via `findAbs()` and `lookupAbs()`.
  - duplicate insert returns the existing absolute index.
  - `trackSection()`, `sectionAck()`, and `streamCancellation()` refcount protected entries.
  - `setCapacity()`, `evict()`, `dropOldest()`, and `compactOrder()` preserve FIFO order and never evict referenced entries.
  - explicit heap IDs remain on owned dynamic strings, order storage, refs, and section hash data.
- Add or extend QPACK dynamic tests:
  - insert, duplicate insert, exact lookup, and absolute lookup.
  - churn enough entries to force eviction and `compactOrder()`.
  - referenced-entry eviction failure followed by successful eviction after `sectionAck()`.
  - multiple streams with out-of-order `sectionAck()` and `streamCancellation()`.
  - lookup by temporary spans after storage consolidation, to prove no dangling caller-span dependency.

### Phase 5: Static QPACK Encoder Transport Interface

- Demote or remove virtual `QPackEncoderTx` after parser/QPACK behavior changes are stable.
- Search in-tree usages. Current known use is `CaptureEncoderTx` in `zhttp/test/ZhttpQPackDynamicTest.cc`, while `BuilderState` already implements `qpackEncoderWriteAccepted(ZuBSpan)`.
- Prefer the existing static detector hook:
  - `HasQPackEncoderWriteAccepted_`.
  - `impl()->qpackEncoderWriteAccepted(ZuBSpan)`.
- Update tests/examples to implement `qpackEncoderWriteAccepted()` directly and store captured bytes in the concrete state rather than through a virtual `QPackEncoderTx`.
- If external ABI/API compatibility is not required, remove:
  - `QPackEncoderTx` in `zhttp/src/ZhttpQPackTypes.hh`.
  - `HasQPackEncoderTx_` and `qpackEncoderTx_()` in `zhttp/src/Zhttp.hh`.
  - the fallback branch in `qpackEncoderWriteAccepted_()`.
- If compatibility is required, keep `QPackEncoderTx` as a legacy fallback but add a short comment that new code should use the detector hook. The fallback should remain after the static hook, as it is today.
- Add/update tests proving builder behavior when:
  - `qpackEncoderWriteAccepted()` accepts bytes.
  - `qpackEncoderWriteAccepted()` rejects a write and the builder reports the existing QPACK build failure path.

### Phase 6: Documentation Cleanup

- Change `zhttp/src/ZhttpLib.cc:7` from `Z secure websockets library` to `Z HTTP library`.
- Keep this as the final standalone patch because it is documentation-only and should not obscure behavior changes.

## Code References to Impacted Code

- `zhttp/src/Zhttp.hh:213` - `parseKV()` rejects empty header values today; rework trimming and empty-value handling.
- `zhttp/src/Zhttp.hh:331` - H1 `content-length` currently uses permissive `ZuBox` construction; replace with full-scan helper.
- `zhttp/src/Zhttp.hh:386` - H1 `parseOperation()` currently does not reject unknown request methods.
- `zhttp/src/Zhttp.hh:888` - `HasQPackEncoderTx_` virtual fallback detector; remove or demote in Phase 5.
- `zhttp/src/Zhttp.hh:1437` - H3 `withString_()` fixed 64 KiB stack buffer; replace with `ZtLocalArray`.
- `zhttp/src/Zhttp.hh:1510` - H3 `qpackHeaderRef_()` fixed 256-byte header-name buffer; replace with local array scratch.
- `zhttp/src/Zhttp.hh:1554` - H3 `content-length` currently uses permissive `ZuBox` construction; replace with full-scan helper.
- `zhttp/src/Zhttp.hh:2378` - builder `qpackEncoderTx_()` virtual fallback; remove or retain as legacy fallback.
- `zhttp/src/ZhttpQPack.hh:106` - existing `ZtLocalArray(HeaderBytes, HPack::declen(...))` pattern to reuse for H3 parser Huffman scratch.
- `zhttp/src/ZhttpQPackTypes.hh:207` - `QPackTxEntry` owns dynamic field strings and refcount.
- `zhttp/src/ZhttpQPackTypes.hh:223` - `QPackTxOrderEntry` duplicates dynamic field strings today.
- `zhttp/src/ZhttpQPackTypes.hh:291` - `QPackEncoderTx` virtual interface.
- `zhttp/src/ZhttpQPack.cc:195` - `QPackTxTable::insert()` increments `insertCount_` before insertion is durable.
- `zhttp/src/ZhttpQPack.cc:222` - Tx eviction path depends on order/hash consistency and referenced-entry refcounts.
- `zhttp/src/ZhttpQPack.cc:262` - Tx section tracking increments dynamic-entry refcounts.
- `zhttp/test/ZhttpParserTest.cc:1` - add H1 invalid method, empty header, and strict `content-length` parser tests.
- `zhttp/test/ZhttpQPackDynamicTest.cc:113` - current virtual `CaptureEncoderTx` test helper; convert to static hook in Phase 5 if ABI permits.
- `zhttp/test/ZhttpQPackDynamicTest.cc:661` - existing QPACK Tx table tests to extend for accounting/storage consolidation.
- `zhttp/src/ZhttpLib.cc:7` - stale module comment.
- `zu/src/ZuBox.hh:536` - `ZuBox::scan()` returns consumed length; use this API for full numeric validation.
- `zt/src/ZtLocalArray.hh:26` - stack scratch with heap fallback; use this instead of fixed arrays.
- `zm/src/ZmLHash.hh:691` - `ZmLHash::add()` returns a pointer to table storage; scrutinize this during Tx storage consolidation.
- `zm/src/ZmLHash.hh:946` - `ZmLHash::del()` removes by key/data; eviction logic must preserve enough key data to delete safely.

## Detailed Test Plan

- Build all touched tests with `make -j` after implementation.
- Run `./zhttp/test/ZhttpParserTest` for H1 parser changes.
- Run `./zhttp/test/ZhttpQPackTest` for QPACK codec/static behavior.
- Run `./zhttp/test/ZhttpQPackDynamicTest` for dynamic table, H3 parser QPACK-fed strict `content-length`, scratch-buffer, and encoder hook tests.
- Run `./zhttp/test/Zhttp3Test` for HTTP/3 parser/builder integration.
- Run `./zhttp/test/Zhttp3StateTest` for state-machine regressions.
- Run `./zhttp/test/Zhttp3LoopTest` for loopback behavior.
- Run `./zhttp/test/ZhttpFallbackTest` for fallback protocol behavior.
- Run `./zhttp/test/Zhttp3InteropTest` separately only if Caddy/curl/QUIC dependencies are available.
- For parser tests, assert both final state and callback counts. Error cases should produce exactly one `complete(Error)` callback and should not deliver body data after the invalid input is detected.
- For QPACK Tx tests, assert table invariants after every operation: `insertCount()`, `used() <= capacity()` when expected, `knownReceivedCount()`, `find()`, `lookupAbs()`, and duplicate/refcount behavior.

## Acceptance Criteria

- Unknown H1 request methods are rejected before `operation()` is called.
- H1 accepts empty and all-OWS field values and passes an empty value span to selected header callbacks.
- H1 and H3 reject empty, signed, trailing-garbage, and overflowing `content-length` values.
- H3 parser no longer has fixed 64 KiB Huffman string or 256-byte header-name limits unrelated to `Params::maxHeaderListSize()`.
- H3 non-Huffman strings remain zero-copy.
- H3 Huffman and header-name scratch storage uses Z Framework arrays with stack-first heap fallback and explicit heap IDs through `HeaderBytes`.
- QPACK Tx `insertCount_` advances only when insertion is durably represented in the dynamic table.
- QPACK Tx dynamic entries no longer duplicate owned `name`/`value` strings across hash and order records, unless implementation-time API constraints are documented with a narrower accepted alternative.
- QPACK Tx duplicate insertion, absolute lookup, FIFO eviction, referenced-entry protection, section acknowledgements, and stream cancellations continue to work.
- QPACK encoder-stream output prefers static CRTP/detector hooks; virtual dispatch is removed unless compatibility requires a documented legacy fallback.
- All listed Zhttp tests pass in the configured build environment.

## Non-goals

- Do not implement the remediation while writing this plan.
- Do not change socket, scheduler, QUIC transport, or reactor behavior.
- Do not redesign all HTTP header parsing or remove obsolete folded-line support in this remediation.
- Do not add support for comma-separated duplicate `Content-Length` normalization; rejecting invalid/list forms is acceptable for this scope.
- Do not change public parser callback names or core CRTP parser/builder shape except for the optional `QPackEncoderTx` cleanup.
- Do not introduce STL containers where Z Framework containers already fit.
- Do not add new mandatory external dependencies.

## Options and Open Questions

- `QPackEncoderTx` compatibility: If zhttp has external consumers depending on `qpackEncoderTx()` or subclassing `QPackEncoderTx`, keep it as a legacy fallback for now. If there is no ABI/API compatibility requirement, remove it in Phase 5.
- QPACK Tx storage consolidation depth: The ideal shape is one hash-owned dynamic entry and order metadata pointing at it. If `ZmLHash` pointer lifetime or deletion APIs make that unsafe without larger container work, use the narrower safe alternative and document the remaining duplication/blocker.
- Allocation failure injection: If `ZtArray`/`ZmLHash` failure cannot be made deterministic in unit tests without invasive test hooks, rely on operation ordering plus invariant tests and document why `insertCount_` cannot advance before durable insertion.
- H3 header-list accounting location: `QPack::decodeFieldSection()` already tracks aggregate decoded name/value bytes. Parser-side `withString_()` checks should remain defensive and per-string; avoid accidental double rejection for valid aggregate sizes.
- Test placement for H3 strict `content-length`: Prefer `ZhttpQPackDynamicTest.cc` if it already has the shortest H3 parser frame construction helpers; otherwise add to the existing H3 parser test with less boilerplate.
