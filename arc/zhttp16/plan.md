# Repair plan for `zhttp` / `zws` span findings

Work strictly in the order below.  A finding is complete only when all of its
acceptance criteria pass; do not begin the next finding while its gate is
open.  Propagate each breaking type change through immediate consumers instead
of adding overloads, compatibility aliases, forwarding shims, or casts.

Use source-level acceptance gates between findings.  Compile and execute tests
only at the explicit checkpoints after Findings 3, 5, 7, and 9 so that template
and dependent breakage is caught without repeatedly rebuilding the tree.

For every stage:

- Preserve `ZuCSpan` only for genuinely character-oriented inputs such as
  configuration, paths, diagnostics, URI parsing, and character-only scanners.
- Use `ZuBSpan`, `ZuSpan<uint8_t>`, `ZuBArray`, and `ZtBArray` for protocol and
  wire data.  Prefer direct span conversion when a dependency requires
  `ZuCSpan`; do not explicitly wrap or cast between equivalent span types.
- Update declarations, definitions, CRTP/detector signatures, dependent
  callbacks, tests, utilities, and interop programs together when an API
  changes.
- Add or adjust focused regression coverage before running the stage gate.
- Keep the existing clang+debug build configuration unchanged.  Do not run
  `z.config`, `configure`, `autoreconf`, or otherwise change build type or
  configuration flags.
- At a rebuild checkpoint, use `make -j8`; do not use bare `make`, `make -j`,
  or another parallelism value.
- At intermediate checkpoints, run only the focused tests named by that
  checkpoint.  Do not run `make test` or comprehensively retest other modules
  until the final Finding 9 gate.
- Run source-tree test binaries normally.  If a debugger or instrumentation is
  needed, use `libtool exec` rather than a binary under `.libs`.

## 1. Repair the shared HTTP field and compression layer

### Implementation

1. Change `Fields::forbidden`, `Fields::pseudo`, `Fields::nameOK`,
   `Fields::uint64`, `Fields::Semantics::field`, and `Semantics::pseudo_` in
   `ZhttpFields.hh` / `ZhttpFields.cc` to consume `ZuBSpan`.
2. Change `Compression::NameView` to retain `ZuBSpan`.
3. Change the prefix/string codec helpers in `ZhttpCompression.hh` to consume
   encoded input and expose decoded output as `ZuBSpan`; retain mutable decode
   destinations as `ZuSpan<uint8_t>`.
4. Propagate the new signatures through immediate H1, HPACK, QPACK, H2, and H3
   callers only as far as required to keep this foundational layer buildable.
   Use native byte locals and output variables, not conversion shims.
5. Update field/compression tests to exercise byte-array input directly,
   including invalid header bytes, integer overflow, truncated prefix/string
   input, raw strings, and Huffman-decoded strings.

### Acceptance gate

- `rg -n 'ZuCSpan' zhttp/src/ZhttpFields.hh zhttp/src/ZhttpFields.cc
  zhttp/src/ZhttpCompression.hh` reports only documented, truly
  character-only exceptions; no header or codec-buffer interface remains.
- No explicit `ZuCSpan{byteSpan}`, `ZuBSpan{charSpan}`, or char/byte
  `reinterpret_cast` was introduced at any migrated call site.
- Focused test changes for fields, compression, and parser behavior are present
  and reviewable; execution is deferred to the Finding 3 checkpoint.
- Immediate consumers have been updated by source inspection, with no known
  signature mismatch left for the deferred rebuild.

## 2. Repair HPACK

### Implementation

1. Change `H2::Field`, table lookup/insertion keys, static-table APIs,
   never-index policy APIs, decoder inputs/outputs, and encoder plan interfaces
   in `ZhttpHPack.hh` / `ZhttpHPack.cc` to `ZuBSpan`.
2. Replace character-backed HPACK-owned protocol storage (`HPackString`, entry
   name/value storage, and the never-index name set) with byte-backed Z
   containers.  Preserve heap IDs and allocation behavior.
3. Update comparison, hashing, static-table matching, printing, and Huffman
   paths to operate on byte views without temporary contiguous conversions.
4. Propagate the breaking API through H2 and its tests, removing explicit
   byte-to-character wrappers made obsolete by the new interfaces.
5. Extend HPACK coverage for static and dynamic lookup, eviction, exact/name
   indexing, never-index policy, raw/Huffman literals, truncation, and binary
   octets in values.

### Acceptance gate

- `rg -n 'ZuCSpan|ZtString' zhttp/src/ZhttpHPack.hh
  zhttp/src/ZhttpHPack.cc` finds no protocol-span or protocol-storage use; any
  remaining result has an inline justification and requires `char` semantics.
- HPACK encode/decode and table APIs accept byte spans directly, and their
  implementations contain no char/byte casts or conversion-only copies.
- Focused HPACK, H2 frame, and H2 message test changes are present and
  reviewable; execution is deferred to the Finding 3 checkpoint.
- Source inspection finds no unresolved HPACK declaration/definition or direct
  caller mismatch.

## 3. Repair QPACK

### Implementation

1. Change `H3::Header`, `QPackFieldKey`, parameter name policy, dynamic table
   APIs, field-section APIs, instruction decoders, and encode helpers in
   `ZhttpQPack.hh` / `ZhttpQPack.cc` to `ZuBSpan`.
2. Replace `HeaderName`, `QPackRxString`, `QPackTxString`, table entry fields,
   and name-policy set keys with byte-backed Z containers while preserving
   existing heap IDs and ownership/lifetime behavior.
3. Keep encoded instruction and field-section buffers byte-oriented end to
   end; eliminate explicit `ZuCSpan` construction in incremental parsing.
4. Propagate the API through H3 and all QPACK tests without compatibility
   overloads.
5. Cover static/dynamic references, name-only/exact lookup, duplicate and
   eviction behavior, blocked sections, instruction truncation, raw/Huffman
   fields, and binary value octets.

### Acceptance gate

- `rg -n 'ZuCSpan|ZtString' zhttp/src/ZhttpQPack.hh
  zhttp/src/ZhttpQPack.cc` finds no wire/header span or protocol-owned string;
  any surviving occurrence is explicitly justified as character-only.
- Encoder/decoder stream instructions and field sections travel through byte
  spans with no explicit equivalent-span wrappers or char/byte casts.
- Source inspection finds no unresolved QPACK declaration/definition or direct
  caller mismatch.
- **Rebuild checkpoint:** run `make -j8` in the unchanged clang+debug build.
- After that succeeds, run only `ZhttpFieldsTest`, `ZhttpCompressionTest`,
  `ZhttpParserTest`, `ZhttpHPackTest`, `ZhttpH2FrameTest`,
  `ZhttpH2MessageTest`, `ZhttpQPackTest`, `ZhttpQPackDynamicTest`, and
  `ZhttpH3PushTest`; all must pass before Finding 4 begins.

## 4. Repair HTTP/2 framing and header construction

### Implementation

1. Change `H2::decodeHeader` to accept `ZuBSpan`; read its bytes directly and
   remove the initial `reinterpret_cast` in `ZhttpH2.cc`.
2. Change `PrefaceParser::value()` to return a byte span and keep preface
   comparison byte-oriented.
3. Remove explicit `ZuCSpan` construction from `FrameHeaderParser` and the H2
   hub frame-header call sites.
4. Change H2 field/header builder arguments, cached header views, status views,
   and runtime header names to byte spans.  Use byte backing for generated
   protocol fields where it avoids a char-to-byte boundary.
5. Update H2 app/test fixtures to use the repaired dependent-facing signatures
   and retain coverage for all frame types, malformed headers, preface
   fragmentation, static/runtime headers, and extended CONNECT.

### Acceptance gate

- `decodeHeader`, `PrefaceParser`, H2 field construction, and H2 hub field APIs
  expose only byte spans for wire/header data.
- `rg -n 'ZuCSpan' zhttp/src/ZhttpH2.hh zhttp/src/ZhttpH2.cc
  zhttp/src/ZhttpH2Hub.hh` reports only genuine character-oriented exceptions
  such as diagnostic messages or external dependency configuration.
- No conversion surrounds a call to `decodeHeader`, and the decoder performs
  no char-to-byte cast.
- Focused H2 frame, message, hub, and stream test changes are present and
  reviewable; execution is deferred to the Finding 5 checkpoint.
- Source inspection finds no unresolved H2 declaration/definition, CRTP, or
  direct caller mismatch.

## 5. Repair HTTP/3 framing, QPACK streams, and header construction

### Implementation

1. Change contiguous QUIC-varint helpers, push-promise parsing, SETTINGS and
   control-frame parsing, field-section parsing, and payload dispatch in
   `ZhttpH3.hh` to `ZuBSpan`.
2. Remove the `reinterpret_cast` from the varint helper and the explicit
   `ZuCSpan` wrappers at byte-buffer call sites.
3. Change H3 header builder/planner fields and QPACK encoder/decoder stream
   boundaries to byte spans.
4. Make the generated status view byte-oriented, changing its backing array if
   appropriate rather than casting it.
5. Update H3 tests and interop fixtures for fragmented control frames, SETTINGS
   validation, QPACK stream instructions, headers/trailers, push promises, and
   payload dispatch.

### Acceptance gate

- `rg -n 'ZuCSpan' zhttp/src/ZhttpH3.hh` reports only justified
  character-only uses; no frame, payload, field, QPACK-stream, or varint input
  remains character-typed.
- `rg -n 'reinterpret_cast<.*(char|uint8_t)' zhttp/src/ZhttpH3.hh` finds no
  char/byte representation conversion.
- Source inspection finds no unresolved H3 declaration/definition, CRTP, or
  direct caller mismatch.
- **Rebuild checkpoint:** run `make -j8` in the unchanged clang+debug build.
- After that succeeds, run only `ZhttpH2FrameTest`, `ZhttpH2MessageTest`,
  `ZhttpH2HubTest`, `ZhttpH2StreamTest`, `ZhttpH3HubTest`, `ZhttpH3PushTest`,
  `ZhttpQPackTest`, and `ZhttpQPackDynamicTest`; all must pass before Finding 6
  begins.  Do not run the complete test suite.

## 6. Repair WebSocket handshake and dependent-facing header APIs

### Implementation

1. Change the exported handshake key, accept, token, and subprotocol helpers in
   `ZwsHandshake.hh` / `ZwsHandshake.cc` to `ZuBSpan`.
2. Change H1 and Extended request/response/parser state views for key, host,
   target, offered protocols, and selected protocol to byte spans.  Convert
   owned handshake protocol data to byte-backed storage where it is retained.
3. Change the `App::accept` detector, fallback, dispatch, and implementations
   to expose `ZuBSpan` for `host`, `target`, and `offered`; update selected
   protocol input/output consistently.
4. Propagate the API through `ZwsH1Hub.hh`, `ZwsHub.hh`, utilities, benchmarks,
   interop clients, and tests without adapters or overloads.
5. Retain byte-oriented tests for nonce/accept validation, token whitespace and
   case rules, invalid subprotocols, selected-protocol validation, and both H1
   and extended handshakes.

### Acceptance gate

- `rg -n 'ZuCSpan' zws/src/ZwsHandshake.hh zws/src/ZwsHandshake.cc
  zws/src/ZwsH1.hh zws/src/ZwsExtended.hh zws/src/ZwsH1Hub.hh
  zws/src/ZwsHub.hh` reports only explicitly justified character-only uses; no
  handshake/header/subprotocol or dependent callback remains character-typed.
- A compile-time fixture verifies an application whose `accept` callback takes
  `ZuBSpan` for all three views is detected and invoked.
- Focused handshake, H1 hub, protocol, and source-boundary test changes are
  present and reviewable; execution is deferred to the Finding 7 checkpoint.
- Source inspection finds no unresolved WebSocket declaration/definition,
  detector, CRTP, dependent callback, or direct caller mismatch.

## 7. Repair the WebSocket close-reason API

### Implementation

1. Change public `close()` and internal `close_()` in `ZwsCodec.hh` to take
   `ZuBSpan`.
2. Snapshot the reason into the destination-owned `ZuBArray` without manual
   element-type conversion; retain the cross-shard value capture and maximum
   control-frame limit.
3. Treat close-reason bytes as application-defined and assemble the close
   payload without validation or char intermediates.  The codec does not
   validate UTF-8 for text messages or close reasons; applications may
   disconnect when their own text policy rejects a message.
4. Update all dependent callers and tests; do not retain a `ZuCSpan` overload.
5. Add coverage for empty and maximum-length reasons, arbitrary non-ASCII and
   non-UTF-8 bytes, overlong reasons, invalid close codes, and exact transmitted
   and received payload bytes.

### Acceptance gate

- All close reason entry points, stored snapshots, codec paths, and callbacks
  use byte spans/arrays consistently with ping and pong.
- `rg -n 'close_?\([^)]*ZuCSpan|const char \*src' zws/src` returns no match.
- Source inspection finds no unresolved close API or direct caller mismatch.
- **Rebuild checkpoint:** run `make -j8` in the unchanged clang+debug build.
- After that succeeds, run only `ZwsProtocolTest`, `ZwsH1HubTest`, and the
  WebSocket source-boundary test, including the new close cases; all must pass
  before Finding 8 begins.  Do not run the complete test suite.

## 8. Remove unnecessary explicit char/byte conversions

### Implementation

1. Audit every `reinterpret_cast` between `char` and `uint8_t` and every
   explicit `ZuCSpan{...}` / `ZuBSpan{...}` conversion in `zhttp` and `zws`.
2. For protocol-owned buffers, correct the backing type so no cast is needed.
   Where a dependency genuinely requires characters, pass the existing span
   directly or create a named local character view without a pointer cast.
3. Repair the identified production sites in `Zhttp.hh`, `ZhttpFields.hh`,
   `ZhttpAltSvc.cc`, `ZhttpDiscovery.cc`, `ZhttpURL.cc`, `ZhttpClient.hh`,
   `zhttp/util/zhttp.cc`, and `zws/src/zws.cc`.
4. Remove the analogous conversion-only wrappers from unit, integration, and
   interop tests so examples reinforce direct interoperability.
5. Review each surviving cast or explicit cross-element span construction and
   document why it represents a real type/representation boundary.

### Acceptance gate

- Repository-wide searches for char/byte `reinterpret_cast` and explicit
  equivalent-span wrappers under `zhttp` and `zws` produce only a reviewed
  allowlist of real representation boundaries.  The allowlist contains no
  protocol-buffer conversion and records file, line, and rationale.
- No temporary buffer or element-by-element copy exists solely to cross
  `ZuCSpan` / `ZuBSpan`.
- The remaining Finding 9 authority/target sites are the only known incorrect
  span uses still outstanding.
- Focused conversion-cleanup test changes are present and reviewable; their
  execution is deferred to the final Finding 9 checkpoint.
- Source inspection confirms that conversion cleanup introduced no unresolved
  declaration/definition or direct caller mismatch.

## 9. Repair client/request authority and target views

### Implementation

1. Change `ClientMessage::ReqOps` cached authority and target parameters,
   members, and builder constructors in `ZhttpClient.hh` to `ZuBSpan`.
2. Change the public client/link authority getter to return `ZuBSpan` and
   propagate the breaking signature through dependent code.
3. Change the `zhttpd` request adapter fields for parsed target, authority,
   protocol, path/query, and retained header values to byte spans wherever
   they remain non-owning protocol views.
4. Remove all now-redundant `ZuCSpan{target...}` and `ZuCSpan{value}`
   constructions in utilities, integration tests, and interop programs.
5. Add coverage for byte-span request construction and routing, authority
   access, origin comparison, path/query splitting, runtime headers, retry,
   fallback, and request lifetime.

### Acceptance gate

- Client/request authority, target, protocol, path/query, and retained header
  protocol views are `ZuBSpan`; intentionally owned application strings remain
  character types only when they require string semantics.
- `rg -n 'ZuCSpan' zhttp/src/ZhttpClient.hh zhttp/util/zhttpd.hh
  zhttp/util/zhttpd.cc` reports only reviewed configuration, filesystem,
  diagnostic, or character-scanner uses.
- No explicit character-span construction remains around parsed URL, target,
  authority, or header byte views.
- **Final rebuild checkpoint:** run `make -j8` in the unchanged clang+debug
  build; it must pass.
- Run the focused client, fallback, lifecycle, multi-request, parser, URL, and
  `zhttpd` tests first so failures remain attributable to Finding 9.
- Only after those focused tests pass, run the single comprehensive final
  `make test`; do not reconfigure the tree first.
- A final repository-wide audit confirms every item in `findings.md` is
  repaired and every surviving `ZuCSpan` in `zhttp` / `zws` has genuine
  character semantics.
