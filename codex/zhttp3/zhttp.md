# zhttp guideline review

Review scope: `AGENTS.md`, `GUIDELINES.md`, and the maintainable `zhttp`
sources under `zhttp/src`, with tests/examples sampled where they clarify
intended behavior. Generated build files, objects, libraries and binaries were
not treated as source.

## Findings

1. **HTTP/1 request parser accepts unknown methods**

   `zhttp/src/Zhttp.hh:396` calls `Method::lookup()` but does not reject the
   `-1` result before calling `impl()->operation()` and moving to
   `State::Headers` at `zhttp/src/Zhttp.hh:403-404`. HTTP/3 does perform this
   check for `:method` at `zhttp/src/Zhttp.hh:1471-1474`, so the two parsers
   disagree. This is a correctness issue and an API hazard for callers that
   assume `Method::T` is always valid.

   Suggested fix: after lookup, reject `method < 0` via the existing `error()`
   path. Add an H1 parser test with an invalid request method.

2. **HTTP/1 header parsing rejects valid empty field values**

   `parseKV()` requires `bov(line)` and `eov(value)` to find non-LWS bytes
   (`zhttp/src/Zhttp.hh:218-223`). A syntactically valid header like
   `X-Empty:\r\n` or `X-Empty:   \r\n` is therefore treated as parse failure.
   This is stricter than HTTP field parsing requires, and it differs from the
   HTTP/3 path, which can deliver empty QPACK values.

   Suggested fix: allow an empty value span after trimming optional whitespace.
   Keep rejecting an empty key. Add H1 tests for empty and all-whitespace header
   values.

3. **`content-length` parsing is not strict**

   H1 and H3 both convert `content-length` with `ZuBox<uint64_t>{ZuCSpan{value}}`
   at `zhttp/src/Zhttp.hh:332` and `zhttp/src/Zhttp.hh:1555`. `ZuBox` accepts a
   string when the numeric scanner consumes a prefix; it does not prove the
   whole header value was numeric. Values like `5x` can therefore be interpreted
   as `5` rather than rejected.

   Suggested fix: use explicit scanning and require `n == value.length()`, as
   the chunk length parser already does partially with `scan()` at
   `zhttp/src/Zhttp.hh:485`. Add H1 and H3 parser tests for trailing garbage and
   overflow.

4. **H3 Huffman decode uses a fixed 64K stack buffer despite configurable
   header limits**

   `withString_()` allocates `uint8_t storage[DefltMaxHdr]` on the stack and
   decodes Huffman strings into it (`zhttp/src/Zhttp.hh:1437-1445`). This is
   both a `GUIDELINES.md` fixed-size array amber flag and a functional limit:
   `Params::maxHeaderListSize()` is configurable, but any single Huffman string
   decoded through this path is capped at `DefltMaxHdr` regardless of params.

   Suggested fix: use `ZtLocalArray(HeaderBytes, HPack::declen(ref.raw.length()))`
   or another Z array with an explicit heap ID, and validate against
   `h3Params_().maxHeaderListSize()`.

5. **H3 header name normalization imposes an undocumented 256-byte cap**

   `qpackHeaderRef_()` copies field names into `uint8_t key_[256]` and rejects
   `name.length() > 256` (`zhttp/src/Zhttp.hh:1510-1516`). The cap is not tied
   to RFC limits or `Params::maxHeaderListSize()`, and the separate buffer plus
   manual length handling is exactly the fixed-size-array pattern called out in
   `GUIDELINES.md`.

   Suggested fix: use `ZtLocalArray`/`HeaderBytes` scratch sized from the name
   length, with a built-in size if 256 is the expected fast path. Make any hard
   maximum a named constant with an explicit rationale.

6. **QPACK Tx table duplicates dynamic-entry storage across hash and order
   arrays**

   `QPackTxEntry` and `QPackTxOrderEntry` both own `name` and `value`
   (`zhttp/src/ZhttpQPackTypes.hh:207-227`), and insertion writes both copies at
   `zhttp/src/ZhttpQPack.cc:206-216`. This increases heap churn and memory
   footprint in a connection hot path. `GUIDELINES.md` specifically recommends
   consolidating container overhead, list/order nodes and reference counting in
   one allocated node for this kind of structure.

   Suggested fix: make the order structure store an intrusive pointer/node,
   hash handle, or absolute-id index into a single owned entry. Keep the
   `refcnt` with the unique entry.

7. **QPACK Tx insertion can advance `insertCount_` before insertion is durable**

   `QPackTxTable::insert()` increments `insertCount_` while populating a local
   `QPackTxEntry` (`zhttp/src/ZhttpQPack.cc:206-211`). If `hash.add(e)` fails,
   the function returns `false` with `insertCount_` already advanced and without
   a corresponding dynamic table entry. That corrupts later QPACK absolute index
   accounting.

   Suggested fix: compute `abs = insertCount_`, perform all fallible insertion
   work, then commit `insertCount_ = abs + 1` only after both the hash and order
   records are established. Add a failure-injection test if `ZmLHash`/`ZtArray`
   can be made to fail deterministically.

8. **QPACK encoder transport abstraction uses virtual dispatch**

   `QPackEncoderTx` is a virtual interface (`zhttp/src/ZhttpQPackTypes.hh:291-294`)
   used by the H3 builder through `qpackEncoderTx_()`/`writeAccepted()`
   (`zhttp/src/Zhttp.hh:2378-2388`). `GUIDELINES.md` asks for CRTP/static
   polymorphism in preference to virtual polymorphism, especially in
   performance-oriented networking paths.

   Suggested fix: prefer the existing detector-based static hook
   `qpackEncoderWriteAccepted(ZuBSpan)` as the primary interface and consider
   removing or demoting the virtual fallback if there are no external ABI needs.

9. **Minor comment/documentation drift**

   `zhttp/src/ZhttpLib.cc:7` says `Z secure websockets library`, but the symbol
   and module are HTTP (`zhttp/src/ZhttpLib.cc:11`). This is low risk, but it is
   stale module documentation.

   Suggested fix: update the comment to `Z HTTP library`.

## Positive alignment notes

- The core parser/builder design is CRTP-heavy and generally follows the
  repository preference for static polymorphism.
- The library mostly uses Z Framework containers and facilities (`ZuMatcher`,
  `ZuSwitch`, `ZuUnroll`, `ZtArray`, `ZtLocalArray`, `ZiIOBuf`) rather than STL.
- Recent QPACK decode paths already use `ZtLocalArray` scratch storage in
  `zhttp/src/ZhttpQPack.hh:92-98`; the fixed stack buffers above should follow
  that pattern.

## Test gaps to close

- H1 invalid request method.
- H1 empty header values.
- H1/H3 invalid `content-length` with trailing non-digits.
- H3 Huffman header value above 64K when params allow it.
- H3 header name above 256 bytes when total header list size is still allowed.
- QPACK Tx insert failure behavior, if allocator/hash failure can be injected.
