# zhttp remediation plan

This plan addresses every finding in `zhttp.md`. Work should be done in small
patches so parser behavior changes, QPACK table changes and API cleanup can be
reviewed independently.

## Phase 1: Parser correctness

1. **Reject unknown HTTP/1 request methods**

   - In `H1::Parser::parseOperation()`, check `Method::lookup()` immediately.
   - If the result is `< 0`, use the existing `error()` path and return before
     calling `impl()->operation()`.
   - Add a `ZhttpParserTest` request parser case with an invalid method and
     verify `State::Error` plus one `complete(Error)` callback.

2. **Allow empty HTTP/1 header values**

   - Rework `parseKV()` so it only requires a non-empty key before `:`.
   - After the colon, trim leading and trailing LWS manually. If no non-LWS
     byte remains, pass an empty value span to the callback.
   - Keep normalizing the key in place.
   - Add tests for `X-Empty:\r\n` and `X-Empty:   \r\n`.

3. **Make `content-length` parsing strict in H1 and H3**

   - Add one small shared helper near the parser utilities, for example
     `parseUInt64Full_(ZuBSpan, uint64_t &)`, using `ZuBox<uint64_t>::scan()`.
   - Require the scan count to equal the complete value length.
   - Use the helper in both H1 and H3 `content-length` handling.
   - Reject empty, signed, trailing-garbage and overflow values.
   - Add H1 tests in `ZhttpParserTest.cc`.
   - Add H3 tests in the existing H3 parser or QPACK parser test that feed
     `content-length: 5x` through decoded headers.

## Phase 2: H3 header scratch storage and limits

4. **Replace fixed Huffman decode stack buffer**

   - Change `withString_()` so it accepts scratch storage or params context.
   - Use `ZtLocalArray(HeaderBytes, HPack::declen(ref.raw.length()))` instead
     of `uint8_t storage[DefltMaxHdr]`.
   - Check decoded length against `h3Params_().maxHeaderListSize()` before
     passing the span to callbacks.
   - Keep non-Huffman strings zero-copy.
   - Add a test with a Huffman value larger than 64K when params allow it.
   - Add a negative test where decoded length exceeds `maxHeaderListSize()`.

5. **Replace the fixed 256-byte header-name buffer**

   - Remove `uint8_t key_[256]` from `qpackHeaderRef_()`.
   - Use `ZtLocalArray(HeaderBytes, 256)` or a dedicated header-name scratch
     array with a 256-byte built-in size and heap fallback.
   - Size it from `name.length()` and reject only when the total header list
     limit is exceeded.
   - Normalize the scratch span in place and pass it to `header_()`.
   - Add a test with a selected H3 header name longer than 256 bytes while
     staying below `maxHeaderListSize()`.

## Phase 3: QPACK Tx table accounting and storage

6. **Fix durable insert accounting first**

   - In `QPackTxTable::insert()`, do not increment `insertCount_` until all
     fallible work has succeeded.
   - Compute `uint64_t abs = insertCount_` locally.
   - Fill temporary/order data with `abs`.
   - Commit `insertCount_ = abs + 1` only after hash and order records are both
     present and `usedBytes_` is ready to update.
   - If order allocation can fail after `hash.add()`, add rollback for the hash
     entry or reorder operations so commit is atomic from the table's point of
     view.
   - Add a unit test if failure can be injected. If not, document why this path
     remains structurally protected after the code change.

7. **Consolidate QPACK Tx dynamic-entry ownership**

   - Inspect `ZmLHash` capabilities for intrusive/list node support or stable
     pointers.
   - Replace the duplicated `QPackTxOrderEntry::{name,value}` storage with an
     order node that refers to the single owning `QPackTxEntry`, or merge hash
     node, order node and `refcnt` into one allocated entry.
   - Preserve absolute-index lookup, eviction order, `refcnt`, `sectionAck()`
     and `streamCancellation()` semantics.
   - Keep heap IDs explicit for any new arrays or allocated nodes.
   - Add dynamic-table tests covering insert, duplicate insert, indexed lookup,
     blocked-section refcounting, eviction with referenced entries and order
     compaction.

## Phase 4: Static QPACK encoder interface

8. **Demote or remove virtual `QPackEncoderTx`**

   - Search all in-tree uses of `QPackEncoderTx`; current known use is the
     dynamic QPACK test capture type.
   - Prefer the already-supported static hook:
     `bool qpackEncoderWriteAccepted(ZuBSpan)`.
   - Update tests/examples to implement the static hook directly where
     possible.
   - If external ABI compatibility is not required, remove `QPackEncoderTx`,
     `HasQPackEncoderTx_`, and `qpackEncoderTx_()`.
   - If compatibility is required, keep it only as a legacy fallback and note
     that new code should use the detector hook.

## Phase 5: Documentation cleanup

9. **Fix stale module comment**

   - Change `zhttp/src/ZhttpLib.cc:7` from `Z secure websockets library` to
     `Z HTTP library`.

## Verification

Run these after the patches:

1. `make -j`
2. `./zhttp/test/ZhttpParserTest`
3. `./zhttp/test/ZhttpQPackTest`
4. `./zhttp/test/ZhttpQPackDynamicTest`
5. `./zhttp/test/Zhttp3Test`
6. `./zhttp/test/Zhttp3StateTest`
7. `./zhttp/test/Zhttp3LoopTest`
8. `./zhttp/test/ZhttpFallbackTest`

Run `./zhttp/test/Zhttp3InteropTest` separately if Caddy/curl/QUIC test
dependencies are available in the environment.

## Suggested patch order

1. Parser correctness and parser tests.
2. H3 scratch-buffer and header-limit changes.
3. QPACK Tx durable insert accounting.
4. QPACK Tx storage consolidation.
5. Static encoder hook cleanup.
6. Comment cleanup.

This order fixes user-visible correctness first, then removes fixed-size
buffers, then handles the higher-risk QPACK storage work after the behavior
tests are stronger.
