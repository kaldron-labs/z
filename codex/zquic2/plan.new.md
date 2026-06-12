# Zquic Heap and Copy Optimization Plan

This is an implementation plan for the allocation and buffer-copy review in
`quic_review.md`.  It is intentionally planning-only: it describes the target
changes, dependencies, tests, and acceptance gates, but does not implement them.

## Summary

The high-value work is on hot-path per-buffer, per-packet, and per-message
allocation/copy behavior in `Zquic` and the `Ztls` picotls boundary.  Session
setup allocations inside picotls or OpenSSL are not worth chasing unless they
become per-buffer/per-packet.  OpenSSL is only a cipher backend here; the QUIC
packet path should not depend on OpenSSL allocation behavior.

The target shape is:

| Area | Current behavior | Target behavior |
| --- | --- | --- |
| Packet buffers | `Endpoint::PacketAlloc` uses named `ZiIOBuf` allocation for datagrams and Tx packets | Keep packet buffers heap allocated with distinct Rx/Tx role heap IDs and counters |
| Packet protection | Creates picotls AEAD/cipher contexts per packet in traffic protection; Initial uses per-call OpenSSL `EVP_CIPHER_CTX_new` | Cache traffic AEAD and HP contexts per direction/epoch/connection using fixed-size named `ZmHeap` wrappers; remove per-packet context allocation |
| Tx encryption | Plaintext is assembled/copied into stack scratch, then encrypted into packet buffer | Write frame prefixes to packet buffer, pass retained stream buffers and scratch prefixes as `ptls_iovec_t`, encrypt into packet buffer with `ptls_aead_encrypt_v_s`; do not encrypt in place |
| Tx resend | Stream plaintext buffers are retained for resend | Keep plaintext buffers immutable through protection; packet buffers are destinations only |
| Rx decrypt | Decrypts packet payload in place | Keep in-place Rx decrypt; packet buffer already owns mutable ciphertext/plaintext |
| Stream Rx | Packet payload is copied into stream `ZiIOBuf`, then a separate `ZmPQueue` node references it | Replace data copy with retained packet-slice buffers where possible; consolidate queue node and buffer object into one intrusive allocation |
| Stream Tx | Tx buffer is retained, then separate queue node references it | Consolidate queue node and retained `ZiIOBuf` object where feasible |
| Crypto Rx | CRYPTO payload copies into stream buffers, then into `CryptoDelivery` byte-by-byte | Prefer direct contiguous spans or retained slices; use bounded local scratch for rare merge cases; remove byte-at-a-time heap growth |
| Stream table | Stream object and hash node are separate allocations | Make stream objects hash-intrusive or allocate stream/hash node as one object with a named stream-object heap |
| Refcounting | App-facing objects derive from `ZmPolymorph`; queue-only nodes mostly use `ZuObject` | Keep atomic `ZmObject`/`ZmPolymorph` where buffers or streams cross `Zi`/app/thread boundaries; use `ZuObject` only for engine-local unshared nodes |

Resolved design decisions:

| Decision | Resolution |
| --- | --- |
| QUIC Tx packet protection in-place encryption | Do not use it. Resend is stream-based from plaintext, so plaintext stream buffers must be retained and packet buffers must be distinct encryption destinations. This also permits optimized non-temporal cipher variants. |
| `ptls_aead_encrypt_v_s` availability | Use it. `../picotls/include/zpicotls.h:444` defines supplementary encryption, `:464` requires encryption contexts to implement `do_encrypt_v_s`, and `:2220` calls the callback. |
| Supplementary HP sample semantics | Safe for QUIC. The picotls comment at `../picotls/include/zpicotls.h:451` says `supp->input` may point into AEAD output and is read after ciphertext/tag output is written. |
| Non-temporal support | Available in the inspected picotls tree. `../picotls/lib/fusion.c:2314` installs non-temporal vector encryption and `:2315` installs `ptls_aead__do_encrypt_v_s`. |
| Picotls malloc wrappers | Do not use generic `ZmVHeap` for steady-state contexts. Use fixed-size named `ZmHeap` bucket wrappers around picotls context storage and call `setup_crypto`/dispose callbacks directly. |
| Packet-protection concurrency | Cache one Tx protection state on the Tx path and one Rx protection state on the Rx path per epoch/direction. Do not share an AEAD context across threads. |
| OpenSSL allocations | Ignore OpenSSL internal allocations for this project. Initial packet protection can be migrated to picotls wrapper/cached contexts when practical, but OpenSSL backend tuning is not the goal. |
| App receive API | Do not replace `ZiRxStream` with a synchronous callback. Current tests and template APIs expose retained `ZiRxStream`; copy-elision should be achieved with retained slice/composed `ZiIOBuf` objects. |
| `Endpoint::Cxn_` heap | Safe to give this a named heap. `ZiConnection` derives from `ZmPolymorph` and has a virtual destructor (`zi/src/ZiMultiplex.hh:452`, `:470`). |
| Intrusive container feasibility | Feasible. `ZmPQueue`, `ZmHash`, and `ZmList` all allow custom node bases; `ZmCache`/`ZmPolyCache` demonstrate stacking list/hash node intrusions. |

No blocking open questions remain for implementation.  Contingencies are listed
near the end, but the plan below does not depend on unresolved research.

## Code Evidence

Current hot allocations and copies to change:

| Source | Current issue | Planned change |
| --- | --- | --- |
| `zquic/src/ZquicCrypto.cc:444` | `ptls_aead_new_direct` in `protect_()` per packet | Cached/wrapped AEAD context |
| `zquic/src/ZquicCrypto.cc:486` | `ptls_aead_new_direct` in `unprotect_()` per packet | Cached/wrapped decrypt context |
| `zquic/src/ZquicCrypto.cc:418` | `ptls_cipher_new` in traffic HP masking per packet | Cached/wrapped HP cipher context |
| `zquic/src/ZquicCrypto.cc:307`, `:439` | Header copied into packet buffer before AEAD | Keep only if packet builder cannot write header directly; target direct header write |
| `zquic/src/Zquic.hh:810`, `:1354` | `buildPayload_()` copies frame bytes into stack plaintext scratch | Replace with vector packet builder and AEAD vector input |
| `zquic/src/Zquic.hh:1943` | Stream Rx queues `new StreamRxPQueue::Node` around a buffer ref | Consolidate node and buffer/slice object |
| `zquic/src/Zquic.hh:1975` | Stream Tx queues `new TxDataPQueue::Node` around a buffer ref | Consolidate node and retained Tx buffer |
| `zquic/src/ZquicCrypto.cc:182` | Crypto Rx allocates `StreamBufAlloc` for CRYPTO bytes | Use retained slices/direct spans where possible; named consolidated heap when retained |
| `zquic/src/ZquicCrypto.cc:201` | CRYPTO delivery pushes bytes into `ZtArray` | Replace with direct span or local scratch merge |
| `zquic/src/ZquicEndpoint.cc:56` | Rx datagram buffer allocation | Keep; distinct Rx packet heap/counter |
| `zquic/src/ZquicEndpoint.cc:132` | `new Cxn_{this, ci}` has no distinct heap ID | Add `Zquic.Endpoint.Cxn` heap |
| `zquic/src/Zquic.hh:2089`, `:2146` | Stream objects are explicitly heap allocated | Add distinct stream-object heap and consolidate hash node |

Relevant APIs already inspected:

| API | Evidence | Meaning for the plan |
| --- | --- | --- |
| `Zi::IOBuf` external backing | `zi/src/ZiIOBuf.hh:58` stores data pointer/owner; protected ctors accept external data | A stream-slice object can retain a packet and point its `ZiIOBuf` data at a packet payload slice |
| `Zi::IOBufAlloc__` | `zi/src/ZiIOBuf.hh:315` composes a base object and inline bytes | Consolidated queue nodes can embed inline data without a second payload allocation |
| `ZmPQueueNode` | `zm/src/ZmPQueue.hh:151` selects custom node base; `:279` defines `Queue::Node` through `ZmNode` | A queue node can derive from a buffer object instead of containing only metadata |
| `ZmNode` derived mode | `zm/src/ZmNode.hh:93` derives from the item when `T` and `Base` match | Hash/queue node and object can be one allocation |
| `ZmHashNode` | `zm/src/ZmHash.hh:213` | Stream hash table can be made intrusive |
| `ZmHashShadow` | `zm/src/ZmHash.hh:237` | Allows shadow/intrusive variants when ownership is external |
| `ZmLambda` heap IDs | `zm/src/ZmFn.hh:115` | Any spilled callback allocation can receive a named heap ID; one-pointer captures should remain inline |
| Picotls context allocation | `../picotls/lib/picotls.c:6534`, `:6578` allocate context-size blocks with malloc | Wrapper must allocate storage itself, initialize base fields, call `setup_crypto`, and call dispose callbacks without `ptls_*_free` |

## Architecture

### Buffer Roles

Split generic packet/stream buffer aliases into role-specific aliases.  The
existing `ZquicBuf.hh` already introduces `PacketBufAlloc` and
`StreamBufAlloc`; refine it so each steady-state role has a distinct heap ID and
diagnostic counter:

```c++
using PacketRxBufAlloc =
  PacketBufAlloc<BufSize, ZiIOBuf_DefltMaxSize, "Zquic.Packet.Rx">;
using PacketTxBufAlloc =
  PacketBufAlloc<BufSize, ZiIOBuf_DefltMaxSize, "Zquic.Packet.Tx">;
using StreamTxBufAlloc =
  StreamBufAlloc<BufSize, ZiIOBuf_DefltMaxSize, "Zquic.Stream.TxBuf">;
using StreamRxCopyBufAlloc =
  StreamBufAlloc<BufSize, ZiIOBuf_DefltMaxSize, "Zquic.Stream.RxCopyBuf">;
using CryptoRxBufAlloc =
  StreamBufAlloc<BufSize, CryptoMaxBuffered, "Zquic.Crypto.RxBuf">;
```

The target should not default new Zquic buffers to `ZiIOBuf_HeapID` or a generic
`"ZtArray"` heap.  Temporary local arrays such as `RxSpans` should remain
`ZtLocalArray` where bounded by queue count; persistent arrays keep named
`ZtArrayHeapID<"Zquic.*">`.

### Retained Slice Buffers

To eliminate packet-to-stream payload copies while preserving the `ZiRxStream`
contract, introduce a small retained slice object.  It is a `ZiIOBuf` whose data
pointer references bytes inside a retained packet buffer.  The object is the
queued/delivered stream buffer; the packet remains alive until the app consumes
the slice.

Sketch:

```c++
class StreamRxSliceBuf : public ZiIOBuf {
public:
  StreamRxSliceBuf(
    ZmRef<ZiIOBuf> packet, unsigned offset, unsigned length, void *owner) :
    ZiIOBuf{packet->data_() + offset, length, owner, length},
    m_packet{ZuMv(packet)} { }

private:
  ZmRef<ZiIOBuf> m_packet;
};
```

The real type should be composed with the queue node, not allocated as an extra
object beside the node.  Use this for packet-backed STREAM and CRYPTO receive
ranges when the packet buffer can be retained.  Keep a named copy-buffer fallback
only for cases requiring ownership decoupling or mutation that cannot be
expressed by `skip`/`length`.

### Intrusive Queue Buffers

The current `StreamRxPQueue` and `TxDataPQueue` nodes contain `RxData`/`TxData`
with a `ZmRef<ZiIOBuf>`.  That is the double-allocation pattern the review
flags.  Replace with queue types whose `Queue::Node` derives from the buffer or
slice object and stores range metadata in the same allocation.

For inline-copy fallback:

```c++
using RxInlineBase =
  Zi::IOBufAlloc__<IOQueue::Node, BufSize, ZiIOBuf_DefltMaxSize, ZuEmpty>;

using StreamRxInlinePQueue =
  ZmPQueue<RxData,
    ZmPQueueNode<RxInlineBase,
      ZmPQueueHeapID<"Zquic.Stream.RxInlineNode",
	ZmPQueueOverwrite<false,
	  ZmPQueueBits<2,
	    ZmPQueueLevels<2>>>>>>;
```

For packet-backed slices, add an equivalent heapless base that points at retained
packet bytes instead of carrying inline bytes.  If direct use of
`Zi::IOBufAlloc__` is too internal, expose a small public `Zi` alias for
heapless inline `IOBuf` storage first; do not work around this by adding a
second heap allocation.

### Packet Builder

Introduce a builder that separates packet output memory from plaintext source
spans.  It writes QUIC headers and small frame prefixes into packet/local scratch
and records plaintext vectors for AEAD.

Sketch:

```c++
struct PlainVec {
  ptls_iovec_t vec[8];
  unsigned count = 0;

  bool add(ZuCSpan span) {
    if (!span.length()) return true;
    if (count >= ZuArrayN(vec)) return false;
    vec[count++] = ptls_iovec_init(span.data(), span.length());
    return true;
  }
};

struct PacketBuild {
  ZmRef<ZiIOBuf> packet;
  uint8_t prefix[128];
  unsigned prefixLen = 0;
  PlainVec plaintext;
  unsigned pnOffset = 0;
  unsigned pnLength = 0;
  unsigned payloadOffset = 0;
};
```

Header bytes should be written directly to `packet->data_()`.  Small ACK,
CRYPTO, and STREAM frame prefixes can live in local scratch if they are AEAD
plaintext.  STREAM payload vectors point at retained Tx buffers.

### Packet Protection State

Move steady-state picotls context allocation behind a small `Ztls::Pico` wrapper
that uses fixed-size `ZmHeap` bucket classes.  Do not call `ptls_aead_free` or
`ptls_cipher_free` on wrapper-owned storage; instead call the context dispose
callback and return the wrapper block to its named heap.

Sketch:

```c++
template <unsigned Size, typename Heap>
struct PicoCtxBlock_ : public Heap {
  alignas(ZiIOBuf_Align) uint8_t bytes[Size];
};

template <unsigned Size, ZuString HeapID>
using PicoCtxBlock = PicoCtxBlock_<Size,
  ZmHeap_<ZuStringT<HeapID>, PicoCtxBlock_<Size, ZuEmpty>>>;

class AeadCtx {
public:
  bool init(ptls_aead_algorithm_t *algo, int isEnc,
    const void *key, const void *iv);
  void clear();
  ptls_aead_context_t *get() const { return m_ctx; }

private:
  void *m_block = nullptr;
  ptls_aead_context_t *m_ctx = nullptr;
};
```

Bucket selection is by `algo->context_size`/`cipher->context_size`, using the
smallest supported bucket:

| Context kind | Heap IDs |
| --- | --- |
| AEAD encrypt | `Ztls.Pico.AEAD.Enc.256`, `.512`, `.1024`, `.2048` |
| AEAD decrypt | `Ztls.Pico.AEAD.Dec.256`, `.512`, `.1024`, `.2048` |
| Header-protection cipher | `Ztls.Pico.Cipher.HP.128`, `.256`, `.512` |

If a selected algorithm exceeds the maximum bucket, initialization fails with a
diagnostic.  It must not silently fall back to malloc or `ZmVHeap` in the packet
path.

`PacketProtectionState` then owns:

```c++
struct PacketProtectionState {
  Ztls::Pico::AeadCtx aead;
  Ztls::Pico::CipherCtx hp;
  TrafficSecret secret;
  CryptoLevel::T level;
  bool tx;
};
```

`Crypto::updateTrafficKey_()` refreshes the relevant state when picotls installs
new keys.  Discarding a secret disposes the contexts.

### Vector Protection

Add vector variants while keeping scalar wrappers during migration:

```c++
static int protectShortV(
  uint8_t *out, unsigned len, PacketProtectionState &state,
  uint64_t pn, ZuCSpan header, const ptls_iovec_t *plain,
  unsigned plainCount, unsigned pnOffset, unsigned pnLength);
```

For Tx:

1. Write header and packet number into the packet buffer.
2. Prepare plaintext vectors from frame prefix scratch and retained stream or
   crypto buffers.
3. Call `ptls_aead_encrypt_v_s()` with output at `packet + payloadOffset`.
4. Set supplementary HP input to the ciphertext sample in the packet output.
5. Apply HP mask from `supp.output`.

For Rx:

1. Keep packet buffer in-place.
2. Use cached HP cipher to unmask.
3. Use cached AEAD decrypt context to decrypt in place.

This intentionally keeps Tx plaintext and ciphertext separate.

## Implementation Phases

### Phase 1: Allocation Taxonomy and Guardrails

Goal: make all current explicit Zquic allocations visible by role before
behavioral rewrites.

Depends on: no new APIs.

Files:

| File | Changes |
| --- | --- |
| `zquic/src/ZquicBuf.hh` | Add role-specific buffer aliases and counters; remove fallback `ZiIOBuf_HeapID` defaults from Zquic public aliases where call sites can specify role |
| `zquic/src/ZquicDiag.hh`, `zquic/src/ZquicDiag.cc` | Add counters for packet Rx/Tx buffers, stream Rx slice/copy buffers, stream Tx buffers, queue nodes, packet-protection context inits |
| `zquic/src/ZquicEndpoint.hh`, `.cc` | Split `PacketAlloc` into Rx/Tx aliases; add `Zquic.Endpoint.Cxn` heap for `Cxn_` |
| `zquic/src/Zquic.hh`, `zquic/src/ZquicPQueue.hh` | Name queue heaps distinctly and remove generic queue heap IDs |

Implementation notes:

- Keep behavior unchanged.
- Count allocation sites already in hot tests.
- Audit `ZmFn` captures in `Endpoint::openUDP()`. One-pointer captures should
  stay inline; any spilled callback allocation must use a named `ZmLambda` heap
  instead of a generic function heap.
- Add an `rg`-friendly comment or static helper for intentional remaining
  copies so future searches can distinguish protocol serialization from
  avoidable buffer copies.

Tests:

- `make -C zquic/test ZquicAPITest ZquicBufferTest ZquicEndpointTest`
- `./zquic/test/ZquicAPITest`
- `./zquic/test/ZquicBufferTest`
- `./zquic/test/ZquicEndpointTest`

Acceptance:

- No `new` allocation in `zquic/src` lacks a distinct role heap or an explicit
  reason why it is stack/app-owned.
- No Zquic-owned `ZtArray` defaults to `"ZtArray"`.
- Packet Rx and Tx buffer counters move independently in endpoint/runtime tests.

### Phase 2: Ztls Picotls Context Wrappers

Goal: remove per-packet picotls context malloc/free from traffic protection.

Depends on: Phase 1 diagnostics; inspected picotls ABI.

Files:

| File | Changes |
| --- | --- |
| `ztls/src/ZtlsPico.hh`, `.cc` | Add fixed-size `ZmHeap` bucket wrappers for `ptls_aead_context_t` and `ptls_cipher_context_t` |
| `ztls/src/Makefile.am` | Include any new wrapper source if split out |
| `zquic/src/ZquicCrypto.hh`, `.cc` | Replace per-call `ptls_aead_new_direct` and `ptls_cipher_new` with cached state wrappers |
| `zquic/test/ZquicPacketProtectionTest.cc` | Add context reuse and no-hot-malloc regression checks |

Implementation notes:

- Wrapper initialization mirrors `ptls_aead_new_direct()` and
  `ptls_cipher_new()` from `../picotls/lib/picotls.c:6534` and `:6578`.
- Wrapper disposal calls `ctx->dispose_crypto(ctx)` for AEAD and
  `ctx->do_dispose(ctx)` for cipher contexts, then frees the wrapper block.
- Add debug assertions that encryption contexts implement `do_encrypt_v_s`.
- Do not tune or wrap once-per-session picotls objects here.
- Do not involve OpenSSL allocators.
- Initial packet protection may remain as-is in this phase if tests prove the
  hot traffic path no longer allocates per packet. Add an explicit follow-up to
  migrate Initial to picotls wrapper/cache if Initial flood behavior matters.

Tests:

- `make -C zquic/test ZquicPacketProtectionTest ZquicHandshakeTest`
- `./zquic/test/ZquicPacketProtectionTest`
- `./zquic/test/ZquicHandshakeTest`

Acceptance:

- Traffic `protect*()` and `unprotect*()` do not call
  `ptls_aead_new_direct`, `ptls_aead_free`, `ptls_cipher_new`, or
  `ptls_cipher_free`.
- Key updates refresh cached state and secret discard disposes cached state.
- Packet protection vectors still match existing RFC/test vectors.

### Phase 3: Vector Packet Protection

Goal: make the encryption API capable of consuming plaintext vectors and writing
ciphertext into packet buffers, preserving retained plaintext for resend.

Depends on: Phase 2 cached contexts.

Files:

| File | Changes |
| --- | --- |
| `zquic/src/ZquicCrypto.hh`, `.cc` | Add `protectLongV()` and `protectShortV()` APIs using `ptls_aead_encrypt_v_s()` |
| `zquic/src/ZquicPacket.hh`, `.cc` | Add helper accessors if packet header/pn offsets need cleaner builder integration |
| `zquic/test/ZquicPacketProtectionTest.cc` | Add scalar-vs-vector parity tests including supplementary HP |
| `zquic/test/ZquicHandshakeTest.cc` | Add short-packet vector protection round-trip |

Implementation notes:

- Preserve scalar wrappers by adapting a single `ZuCSpan` into one iovec.
- Use `ptls_aead_supplementary_encryption_t` for HP sample encryption so the
  non-temporal picotls path can optimize AEAD plus HP.
- Validate zero-length vector behavior because picotls permits zero-length
  vectors; keep packet budget checks in Zquic.
- Ensure plaintext vectors never overlap the output buffer. Header/AAD points to
  output, but plaintext source spans point to stream buffers or local prefix
  scratch.

Tests:

- `make -C zquic/test ZquicPacketProtectionTest ZquicHandshakeTest`
- `./zquic/test/ZquicPacketProtectionTest`
- `./zquic/test/ZquicHandshakeTest`

Acceptance:

- Vector protection produces byte-identical packets to scalar protection for
  existing test cases.
- Header protection sample is taken from ciphertext output after AEAD writes.
- Non-temporal-capable picotls contexts pass the same parity tests.

### Phase 4: Packet Builder and Tx Copy Removal

Goal: eliminate stack plaintext payload copies in packet send paths.

Depends on: Phase 3 vector protection.

Files:

| File | Changes |
| --- | --- |
| `zquic/src/ZquicPacketBuilder.hh` | New builder for header output, frame-prefix scratch, and plaintext vectors |
| `zquic/src/Zquic.hh` | Replace `buildPayload_()` and `send*Packet_()` payload scratch copies with builder flow |
| `zquic/src/ZquicSched.hh` | Split STREAM prefix generation from payload bytes if needed |
| `zquic/src/ZquicFrame.hh`, `.cc` | Add prefix-only writers for STREAM/CRYPTO frames where full-frame writers currently copy payload |
| `zquic/test/ZquicStreamTest.cc` | Extend packetizer tests for vector/source retention |
| `zquic/test/ZquicRuntimeTest.cc` | Runtime handshake and app-data smoke after builder migration |

Implementation notes:

- ACK and small control frames may still be serialized into local scratch because
  they are protocol bytes, not buffer data copies.
- STREAM frame payloads should be vectors referencing retained Tx buffers.
- CRYPTO Tx can initially use a retained crypto output buffer as a vector; later
  phases improve TLS output allocation.
- Padding for Initial/short-packet sample requirements should be represented as
  a zero-filled local/static pad vector or written directly to packet plaintext
  scratch, not by copying stream payload.
- `recordTxPacket_()` must receive frame metadata without requiring a full copied
  frame span.

Tests:

- `make -C zquic/test ZquicStreamTest ZquicRuntimeTest ZquicH3InteropTest`
- `./zquic/test/ZquicStreamTest`
- `./zquic/test/ZquicRuntimeTest`
- `./zquic/test/ZquicH3InteropTest`

Acceptance:

- `sendInitialPacket_()`, `sendHandshakePacket_()`, and `sendShortPacket_()` no
  longer copy full payload frames into stack `uint8_t payload[BufSize]`.
- Stream Tx buffers remain valid and unchanged after packet protection.
- Loss/retransmit tests resend from plaintext stream state, not packet
  ciphertext.

### Phase 5: Stream Rx Slice and Queue Consolidation

Goal: eliminate packet-to-stream payload copies for receive and consolidate
queue node plus stream buffer object.

Depends on: Phase 1 buffer roles; `ZiIOBuf` external backing support.

Files:

| File | Changes |
| --- | --- |
| `zquic/src/ZquicBuf.hh` | Add retained packet-slice buffer base and inline-copy fallback base |
| `zquic/src/ZquicPQueue.hh` | Add consolidated Rx queue node types and helpers |
| `zquic/src/Zquic.hh` | Change `Stream::queueRx_()` and `Stream::process()` to use consolidated nodes/slices |
| `zquic/test/ZquicBufferTest.cc` | Add slice lifetime, skip/length, and no-copy counter tests |
| `zquic/test/ZquicStreamTest.cc` | Update receive tests to expect reduced copy counts |
| `zquic/test/ZquicFlowTest.cc` | Preserve flow-control duplicate/gap behavior |

Implementation notes:

- Preserve `ZiRxStream` app-facing behavior: the app still receives
  `ZmRef<ZiIOBuf>` via the existing stream.
- For in-order and out-of-order STREAM frames, queue a retained packet slice
  where the original packet buffer can safely outlive parsing.
- For overlap clipping, mutate queue metadata and `ZiIOBuf::skip`/`length`
  rather than copying.
- Keep `copySpanToStream()` only as an explicit fallback with a counter named
  for unavoidable copies.
- If the slice object is app-visible as `ZiIOBuf`, it remains
  `ZmPolymorph`/atomic. Queue-only metadata nodes can remain `ZuObject`.

Tests:

- `make -C zquic/test ZquicBufferTest ZquicStreamTest ZquicFlowTest ZquicLoopTest`
- `./zquic/test/ZquicBufferTest`
- `./zquic/test/ZquicStreamTest`
- `./zquic/test/ZquicFlowTest`
- `./zquic/test/ZquicLoopTest`

Acceptance:

- Normal STREAM receive does not increment packet-to-stream copy counters.
- Duplicate and overlapping STREAM frames still avoid duplicate delivery.
- The retained packet buffer remains alive until the delivered `ZiIOBuf` slice is
  released.
- There is one allocation per retained stream range, not one queue node plus one
  buffer.

### Phase 6: Crypto Stream Buffer and TLS Output Cleanup

Goal: reduce CRYPTO receive copies and ensure TLS output buffers are named
`ZmHeap`/`ZiIOBuf` allocations passed through picotls.

Depends on: Phase 5 slice buffers.

Files:

| File | Changes |
| --- | --- |
| `zquic/src/ZquicCrypto.hh`, `.cc` | Replace `CryptoDelivery` byte array with direct span/local scratch merge; use consolidated CRYPTO Rx slice queue |
| `zquic/src/ZquicBuf.hh` | Add `CryptoRxSlice`/`CryptoRxInline` aliases if distinct from stream aliases |
| `ztls/src/ZtlsPico.hh`, `.cc` | Add helper for picotls tx buffers if picotls growth must be redirected into named heap buffers |
| `zquic/test/ZquicHandshakeTest.cc` | Add fragmented CRYPTO reassembly tests |
| `zquic/test/ZquicCryptoTest.cc` | Add TLS output buffer growth/no-copy tests where feasible |

Implementation notes:

- If a CRYPTO frame arrives at the current offset as one contiguous slice, pass
  its `ZuCSpan` directly to `Crypto::handleTLSMessage()`.
- If multiple queued slices are needed for a TLS message, merge into
  `ZtLocalArray`/`ZmLocal` scratch up to a bounded threshold.
- If the message exceeds local scratch, use a named heap buffer such as
  `Zquic.Crypto.MergeBuf`; this is a handshake/fragmentation fallback, not the
  steady-state packet path.
- `ptls_buffer_init_tx()` should receive a sufficiently sized `ZiIOBuf` backing
  buffer. If picotls grows anyway, either fail/retry with a larger named buffer
  or route growth through a `Ztls` named heap wrapper. Do not allow silent
  picotls malloc in a per-message output path.

Tests:

- `make -C zquic/test ZquicCryptoTest ZquicHandshakeTest ZquicRuntimeTest`
- `./zquic/test/ZquicCryptoTest`
- `./zquic/test/ZquicHandshakeTest`
- `./zquic/test/ZquicRuntimeTest`

Acceptance:

- `CryptoStream::process()` no longer appends bytes one at a time to
  `ZtArray`.
- Common in-order CRYPTO delivery passes a span without heap allocation.
- Fragmented CRYPTO still reassembles correctly.
- TLS output buffers use named Zquic/Ztls heap IDs or caller-provided `ZiIOBuf`
  storage.

### Phase 7: Stream Tx Queue Consolidation

Goal: remove the retained Tx buffer plus separate queue-node allocation pattern.

Depends on: Phase 4 packet builder and Phase 5 intrusive queue pattern.

Files:

| File | Changes |
| --- | --- |
| `zquic/src/ZquicBuf.hh` | Add Tx retained-buffer node base |
| `zquic/src/ZquicPQueue.hh` | Add consolidated `TxDataPQueue` variant |
| `zquic/src/Zquic.hh` | Change `Stream::sent_()`, `consumeTxRange()`, and Tx range iteration to use consolidated nodes |
| `zquic/test/ZquicStreamTest.cc` | Preserve Tx retention, split, FIN, and packetizer behavior |
| `zquic/test/ZquicCongestionTest.cc` | Ensure loss/requeue frame refs still point to retained plaintext |

Implementation notes:

- The Tx object remains the owner of plaintext bytes for resend.
- Packet builder consumes `ZuSpan`/`ptls_iovec_t` views into Tx nodes.
- Splitting a Tx range should mutate node metadata or requeue the same object
  where possible; it should not allocate a second node unless the range must be
  represented as two live ranges.
- FIN-only frames remain metadata and should not allocate buffers.

Tests:

- `make -C zquic/test ZquicStreamTest ZquicCongestionTest ZquicRuntimeTest`
- `./zquic/test/ZquicStreamTest`
- `./zquic/test/ZquicCongestionTest`
- `./zquic/test/ZquicRuntimeTest`

Acceptance:

- Each flushed Tx buffer creates one retained object that is also its queue node.
- Packetization and resend use slices of retained plaintext without copying.
- Existing FIN and split-range tests pass.

### Phase 8: Stream Object and Hash Consolidation

Goal: reduce per-stream allocation count and assign distinct object heaps.

Depends on: Phase 1 allocation taxonomy; confirmed `ZmHashNode`/`ZmHashShadow`
support.

Files:

| File | Changes |
| --- | --- |
| `zquic/src/Zquic.hh` | Make `Stream` heap allocation explicit with a role heap; make the stream table intrusive or node-derived |
| `zquic/test/ZquicAPITest.cc` | Preserve stream lookup and API shape |
| `zquic/test/ZquicStreamTest.cc` | Preserve local/peer stream open/accept behavior |

Implementation notes:

- `Stream` is app-visible and currently derives from `ZmPolymorph`; keep atomic
  refcounting unless a later complete thread audit proves all streams are
  shard-local.
- Prefer `ZmObject`/`ZmPolymorph` over `ZuObject` for app-visible streams.
- Use plain object bases, not `*Polymorph`, unless type erasure is required.
  The current template/app API uses polymorphic destruction, so
  `ZmPolymorph` remains acceptable.
- Convert `Streams_` from `ZmHash<ZmRef<Stream_>>` to an intrusive form where
  the hash node derives from the stream object, or introduce a stream-node type
  that is the only allocated stream object.

Sketch:

```c++
template <typename Stream_>
struct StreamHashFn {
  static int64_t key(const Stream_ &s) { return s.id(); }
};

template <typename Stream_>
using Streams_ = ZmHash<Stream_,
  ZmHashKey<StreamHashFn<Stream_>::key,
    ZmHashNode<Stream_,
      ZmHashHeapID<"Zquic.Stream.ObjectHash">>>>;
```

The exact `ZmHashKey` accessor syntax should follow the local `ZmHash` API, but
the core rule is fixed: do not store a `ZmRef<Stream_>` value in a separately
allocated hash node when the stream object can carry the node intrusion.

Tests:

- `make -C zquic/test ZquicAPITest ZquicStreamTest ZquicLoopTest`
- `./zquic/test/ZquicAPITest`
- `./zquic/test/ZquicStreamTest`
- `./zquic/test/ZquicLoopTest`

Acceptance:

- Stream creation performs one stream/hash allocation, not stream object plus
  hash node.
- Stream lookup, accept, local open, and queued-open behavior remains unchanged.
- Stream object heap ID is distinct from stream buffer and queue node heap IDs.

### Phase 9: Endpoint and Callback Cleanup

Goal: finish non-hot explicit allocation instrumentation and remove accidental
generic heaps.

Depends on: previous phases for buffer aliases and diagnostics.

Files:

| File | Changes |
| --- | --- |
| `zquic/src/ZquicEndpoint.cc` | Add named heap to `Endpoint::Cxn_`; use Rx/Tx packet buffer aliases at call sites |
| `zquic/src/ZquicEndpoint.hh` | Separate callback heap decisions if any `ZmFn` capture spills |
| `zquic/test/ZquicEndpointTest.cc` | Verify endpoint counters and open/close lifecycle |

Implementation notes:

- `Cxn_` is once per endpoint, so it is not hot, but it is an explicit object
  allocation and should still have a distinct heap ID.
- `ZiConnection` has a virtual destructor, so deleting through the `Zi` base is
  compatible with a heap-bearing derived object.
- Do not add heap indirection to callbacks unless actual spill allocation is
  observed. A one-pointer capture should remain inline.

Tests:

- `make -C zquic/test ZquicEndpointTest ZquicSockTest`
- `./zquic/test/ZquicEndpointTest`
- `./zquic/test/ZquicSockTest`

Acceptance:

- `new Cxn_` uses `Zquic.Endpoint.Cxn`.
- Rx datagrams use `Zquic.Packet.Rx`; transmitted packets use
  `Zquic.Packet.Tx`.
- No callback allocation appears in hot send/receive unless named and justified.

### Phase 10: Final Copy Audit and Performance Gates

Goal: verify that remaining copies are intentional protocol serialization or
bounded scratch merges.

Depends on: all prior phases.

Files:

| File | Changes |
| --- | --- |
| `quic_review.md` | Update statuses after implementation, including eliminated/consolidated allocations and copies |
| `zquic/test/*` | Add regression counters as needed |
| `scripts/` or local test helper | Optional allocation/copy scan script using `rg` |

Implementation notes:

- Re-run `rg -n "memcpy|memmove|new |ptls_aead_new_direct|ptls_cipher_new|ZtArrayHeapID<\\\"ZtArray\\\"" zquic/src ztls/src`.
- Classify remaining copies:
  - fixed-size key/secret/header serialization;
  - protocol frame prefix serialization;
  - unavoidable bounded CRYPTO merge fallback;
  - fallback stream copy with counter.
- Remove dead scalar packet-protection code only after vector paths cover Initial,
  Handshake, and 1-RTT tests.

Tests:

- `make -C zquic/test test`
- Targeted reruns of any test touched during the final audit.

Acceptance:

- No per-packet traffic protection context allocation remains.
- No per-STREAM-payload Tx copy into packet plaintext scratch remains.
- Normal STREAM Rx payload delivery has no packet-to-stream data copy.
- Remaining hot allocations have named heap IDs and diagnostics.
- `quic_review.md` matches the implemented state.

## Test Matrix

| Test | Purpose |
| --- | --- |
| `ZquicPacketProtectionTest` | Scalar/vector AEAD parity, cached context reuse, HP supplementary behavior |
| `ZquicHandshakeTest` | TLS/CRYPTO behavior, fragmented CRYPTO, traffic key updates |
| `ZquicRuntimeTest` | End-to-end handshake, protected packet Tx/Rx counters |
| `ZquicH3InteropTest` | App-data stream path after builder/vector migration |
| `ZquicStreamTest` | Rx slice delivery, Tx retention, packetizer/resend behavior |
| `ZquicFlowTest` | Flow-control accounting with no-copy Rx spans |
| `ZquicPQueueTest` | Queue gap/overlap semantics after intrusive node changes |
| `ZquicBufferTest` | Buffer role heaps, retained slices, lifetime/copy counters |
| `ZquicEndpointTest` | Packet buffer role allocation and endpoint `Cxn_` lifecycle |
| `ZquicCongestionTest` | Sent-packet tracking and loss requeue with retained plaintext |
| `ZquicLoopTest` | Integrated stream/link loop behavior |

Preferred final command:

```sh
make -C zquic/test test
```

Run narrower test sets after each phase as listed above to avoid debugging a
large integrated failure after many ownership changes.

## Acceptance Criteria

- All explicit Zquic object/buffer allocations have distinct named heap IDs.
- No hot-path allocation falls back to generic heap IDs such as `"ZtArray"`,
  `"ZiIOBuf"`, or anonymous queue/list heap IDs.
- Per-packet traffic protection does not call picotls allocation/free functions.
- Ztls/picotls buffer/context wrappers use named `ZmHeap` buckets, not generic
  `ZmVHeap`, for steady-state packet-path storage.
- Tx packet protection writes ciphertext into packet buffers from plaintext
  vectors and never mutates retained stream plaintext buffers.
- Rx packet protection may decrypt in place.
- Normal STREAM Rx does not copy payload bytes from packet buffer into a new
  stream buffer.
- Queue-node-plus-buffer double allocations are consolidated for stream Rx,
  stream Tx, and CRYPTO Rx retained ranges.
- Stream table allocation is consolidated so a stream object carries its hash
  node or is allocated as a combined stream/hash node object.
- App-visible/reference-counted objects use `ZmObject`/`ZmPolymorph`; unshared
  engine-local nodes use `ZuObject`.
- Existing public test behavior and app-facing `ZiRxStream` contracts remain
  intact.

## Non-Goals

- Do not tune OpenSSL internal allocation behavior.
- Do not chase once-per-program or once-per-session picotls allocations unless
  they appear in a per-buffer/per-packet path.
- Do not replace the app-facing receive model with a new synchronous callback
  API.
- Do not introduce generic pooling that hides allocation roles behind one heap
  ID.
- Do not use in-place Tx encryption for QUIC packet protection; retained
  plaintext is required for stream-based resend.
- Do not rewrite unrelated congestion, ACK, or transport-parameter logic except
  where tests need access to allocation/copy counters.

## Options and Open Questions

No blocking open questions remain.

Resolved options:

| Topic | Resolution |
| --- | --- |
| Picotls supplementary encryption | Use `ptls_aead_encrypt_v_s()` with `ptls_aead_supplementary_encryption_t`; add parity tests |
| Picotls context storage | Use fixed-size named `ZmHeap` buckets; fail initialization if no bucket fits |
| Packet-protection state sharing | One cached state per connection/direction/epoch on its owning path; no shared AEAD context |
| Stream receive copy-elision | Preserve `ZiRxStream`; deliver retained packet-backed `ZiIOBuf` slices |
| Stream object refcount | Keep `ZmPolymorph` unless a full later audit proves stream objects are thread-local |
| Endpoint connection allocation | Add named heap to `Endpoint::Cxn_`; `ZiConnection` supports polymorphic destruction |
| Stream hash-node consolidation | Implement intrusive/derived hash node; existing `ZmHashNode`/`ZmNode` support the pattern |

Contingencies:

| Contingency | Response |
| --- | --- |
| Future picotls algorithm context size exceeds available bucket | Add a new named fixed bucket and test it. Do not fall back to malloc in packet protection. |
| `Zi::IOBufAlloc__` is considered too internal for queue composition | Add a small public heapless inline-storage alias in `ZiIOBuf.hh` and use that. |
| A retained packet slice keeps a large datagram alive too long under app backpressure | Add policy thresholds: copy small/long-lived slices into `Zquic.Stream.RxCopyBuf` with counters, while keeping no-copy as the normal path. |
| Vector count exceeds fixed builder array | Flush/build another packet or merge only small prefixes into local scratch; do not copy stream payloads into a monolithic plaintext buffer. |
| A callback capture spills unexpectedly | Convert that callback to a named `ZmLambda` heap or a small explicit callback object; keep it out of packet hot paths. |
